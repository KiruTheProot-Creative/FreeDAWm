#include "Ipc.h"

#include <chrono>
#include <cstring>
#include <new>

#ifdef _WIN32
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#else
 #include <cerrno>
 #include <climits>
 #include <ctime>
 #include <fcntl.h>
 #include <linux/futex.h>
 #include <signal.h>
 #include <sys/mman.h>
 #include <sys/syscall.h>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace fdm::bridge {

#ifdef _WIN32
namespace {
std::wstring wide(const std::string& s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n - 1 : 0), L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}
std::wstring objectName(const std::string& s) { return L"Local\\" + wide(s); }
}  // namespace
#endif

// ---------------------------------------------------------------------------
// SharedMemory
// ---------------------------------------------------------------------------
SharedMemory::~SharedMemory() { close(); }

bool SharedMemory::create(const std::string& name, std::size_t size) {
    close();
    name_ = name;
    size_ = size;
    owner_ = true;
#ifdef _WIN32
    const auto high = static_cast<DWORD>(static_cast<unsigned long long>(size) >> 32);
    const auto low = static_cast<DWORD>(size & 0xffffffffu);
    mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, high, low, objectName(name).c_str());
    if (mapping_ == nullptr) return false;
    data_ = MapViewOfFile(static_cast<HANDLE>(mapping_), FILE_MAP_ALL_ACCESS, 0, 0, size);
#else
    const std::string path = "/" + name;
    shm_unlink(path.c_str());
    const int fd = shm_open(path.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) return false;
    if (ftruncate(fd, static_cast<off_t>(size)) != 0) { ::close(fd); shm_unlink(path.c_str()); return false; }
    void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ::close(fd);
    data_ = p == MAP_FAILED ? nullptr : p;
#endif
    if (data_ == nullptr) { close(); return false; }
    std::memset(data_, 0, size);
    return true;
}

bool SharedMemory::open(const std::string& name, std::size_t size) {
    close();
    name_ = name;
    size_ = size;
    owner_ = false;
#ifdef _WIN32
    mapping_ = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, objectName(name).c_str());
    if (mapping_ == nullptr) return false;
    data_ = MapViewOfFile(static_cast<HANDLE>(mapping_), FILE_MAP_ALL_ACCESS, 0, 0, size);
#else
    const std::string path = "/" + name;
    const int fd = shm_open(path.c_str(), O_RDWR, 0600);
    if (fd < 0) return false;
    struct stat st {};
    if (fstat(fd, &st) != 0 || static_cast<std::size_t>(st.st_size) < size) { ::close(fd); return false; }
    void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ::close(fd);
    data_ = p == MAP_FAILED ? nullptr : p;
#endif
    return data_ != nullptr;
}

void SharedMemory::close() {
#ifdef _WIN32
    if (data_ != nullptr) UnmapViewOfFile(data_);
    if (mapping_ != nullptr) CloseHandle(static_cast<HANDLE>(mapping_));
    mapping_ = nullptr;
#else
    if (data_ != nullptr) munmap(data_, size_);
    if (owner_ && !name_.empty()) shm_unlink(("/" + name_).c_str());
#endif
    data_ = nullptr;
    owner_ = false;
}

// ---------------------------------------------------------------------------
// Signal
// ---------------------------------------------------------------------------
Signal::~Signal() {
#ifdef _WIN32
    if (handle_ != nullptr) CloseHandle(static_cast<HANDLE>(handle_));
#endif
}

bool Signal::create(const std::string& name, SignalWord& word) {
    word_ = &word;
    word.value.store(0);
#ifdef _WIN32
    handle_ = CreateSemaphoreW(nullptr, 0, 0x7fffffff, objectName(name).c_str());
    return handle_ != nullptr;
#else
    (void) name;
    return true;
#endif
}

bool Signal::open(const std::string& name, SignalWord& word) {
    word_ = &word;
#ifdef _WIN32
    handle_ = OpenSemaphoreW(SEMAPHORE_ALL_ACCESS, FALSE, objectName(name).c_str());
    return handle_ != nullptr;
#else
    (void) name;
    return true;
#endif
}

void Signal::post() {
#ifdef _WIN32
    ReleaseSemaphore(static_cast<HANDLE>(handle_), 1, nullptr);
#else
    word_->value.fetch_add(1, std::memory_order_release);
    syscall(SYS_futex, reinterpret_cast<std::int32_t*>(&word_->value), FUTEX_WAKE, 1, nullptr, nullptr, 0);
#endif
}

bool Signal::wait(int timeoutMs) {
#ifdef _WIN32
    return WaitForSingleObject(static_cast<HANDLE>(handle_), timeoutMs < 0 ? INFINITE : static_cast<DWORD>(timeoutMs)) == WAIT_OBJECT_0;
#else
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::milliseconds(timeoutMs < 0 ? 0 : timeoutMs);
    for (;;) {
        std::int32_t v = word_->value.load(std::memory_order_acquire);
        while (v > 0)
            if (word_->value.compare_exchange_weak(v, v - 1, std::memory_order_acq_rel)) return true;
        timespec ts {};
        timespec* tsp = nullptr;
        if (timeoutMs >= 0) {
            const auto left = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - clock::now()).count();
            if (left <= 0) return false;
            ts.tv_sec = static_cast<time_t>(left / 1000000000LL);
            ts.tv_nsec = static_cast<long>(left % 1000000000LL);
            tsp = &ts;
        }
        syscall(SYS_futex, reinterpret_cast<std::int32_t*>(&word_->value), FUTEX_WAIT, 0, tsp, nullptr, 0);
    }
#endif
}

// ---------------------------------------------------------------------------
// Session
// ---------------------------------------------------------------------------
bool Session::create(const std::string& name) {
    if (!memory.create(name, sizeof(Shared))) return false;
    auto* s = new (memory.data()) Shared();          // the block is zeroed; construct the atomics in place
    writeLayout(*s);
    return controlRequest.create(name + "_cq", s->control.request) && controlReply.create(name + "_cr", s->control.reply)
           && audioRequest.create(name + "_aq", s->audio.request) && audioReply.create(name + "_ar", s->audio.reply);
}

bool Session::open(const std::string& name) {
    if (!memory.open(name, sizeof(Shared))) return false;
    auto* s = shared();
    if (!layoutMatches(*s)) return false;
    return controlRequest.open(name + "_cq", s->control.request) && controlReply.open(name + "_cr", s->control.reply)
           && audioRequest.open(name + "_aq", s->audio.request) && audioReply.open(name + "_ar", s->audio.reply);
}

// ---------------------------------------------------------------------------
bool processIsAlive(std::int64_t pid) {
#ifdef _WIN32
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (h == nullptr) return false;
    const bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return alive;
#else
    return kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
#endif
}

std::int64_t currentProcessId() {
#ifdef _WIN32
    return static_cast<std::int64_t>(GetCurrentProcessId());
#else
    return static_cast<std::int64_t>(getpid());
#endif
}

}  // namespace fdm::bridge
