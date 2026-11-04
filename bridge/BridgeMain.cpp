// fdm_bridge32 / fdm_bridge64: hosts ONE VST2 plugin for FreeDAWm in its own process.
//
//   fdm_bridge32 <session name> <host pid>
//
// The 32-bit build lets the 64-bit FreeDAWm use 32-bit plugins; the 64-bit build runs
// 64-bit plugins sandboxed. FreeDAWm talks to it through shared memory (BridgeProtocol.h).
// Threads: the main thread answers control requests, pumps window messages and idles the
// editor; an audio thread processes blocks on request.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "../app/Source/vst2/Vst2Abi.h"
#include "BridgeProtocol.h"
#include "Ipc.h"

#ifdef _WIN32
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <ole2.h>
#else
 #include <dlfcn.h>
 #include <signal.h>
 #include <sys/prctl.h>
#endif

using namespace fdm::bridge;
namespace v = fdm::vst2;

namespace {

Session session;
Shared* S = nullptr;
v::Plugin* plugin = nullptr;
void* library = nullptr;
std::int32_t shellIdToLoad = 0;
std::string pluginDirectory;
std::atomic<bool> running{true};
std::atomic<bool> processing{false};       // true while the audio thread is inside the plugin
thread_local bool inAudioThread = false;
double sampleRate = 48000.0;
int blockSize = 512;
bool editorOpen = false;
std::vector<char> chunk;                   // chunk staging for cmdChunkGet/Read and Write/Set
v::TimeInfo timeInfo {};

// ---- library loading --------------------------------------------------------------
#ifdef _WIN32
std::wstring wide(const std::string& s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n - 1 : 0), L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}
void* loadLibrary(const std::string& path) { return reinterpret_cast<void*>(LoadLibraryW(wide(path).c_str())); }
void* findSymbol(void* lib, const char* name) { return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(lib), name)); }
#else
void* loadLibrary(const std::string& path) { return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL); }
void* findSymbol(void* lib, const char* name) { return dlsym(lib, name); }
#endif

v::iptr call(std::int32_t op, std::int32_t index = 0, v::iptr value = 0, void* ptr = nullptr, float opt = 0.0f) {
    return plugin != nullptr ? plugin->dispatch(plugin, op, index, value, ptr, opt) : 0;
}

void copyString(char* dst, size_t cap, const char* src) {
    std::strncpy(dst, src, cap - 1);
    dst[cap - 1] = 0;
}

std::string query(std::int32_t op, std::int32_t index = 0) {
    char buf[1024] = {};
    call(op, index, 0, buf);
    buf[sizeof(buf) - 1] = 0;
    return buf;
}

// ---- callbacks from the plugin -----------------------------------------------------
v::iptr FDM_VST2_CALL hostCallback(v::Plugin*, std::int32_t opcode, std::int32_t index, v::iptr value, void* ptr, float opt) {
    switch (opcode) {
        case v::host::version:          return 2400;
        case v::host::currentId:        return shellIdToLoad;
        case v::host::getTime:          return reinterpret_cast<v::iptr>(&timeInfo);
        case v::host::getSampleRate:    return static_cast<v::iptr>(sampleRate);
        case v::host::getBlockSize:     return blockSize;
        case v::host::getProcessLevel:  return inAudioThread ? 2 : 1;
        case v::host::getAutomationState: return 1;
        case v::host::getLanguage:      return 1;
        case v::host::getVendorVersion: return 100;
        case v::host::getDirectory:     return reinterpret_cast<v::iptr>(pluginDirectory.c_str());
        case v::host::getVendor:
        case v::host::getProduct:
            if (ptr != nullptr) std::strcpy(static_cast<char*>(ptr), "FreeDAWm");
            return 1;
        case v::host::canDo: {
            if (ptr == nullptr) return 0;
            static const char* yes[] = {"sendVstEvents", "sendVstMidiEvent", "sendVstTimeInfo", "receiveVstEvents",
                                        "receiveVstMidiEvent", "sizeWindow", "supplyIdle", "shellCategory", "startStopProcess"};
            for (auto* y : yes) if (std::strcmp(static_cast<const char*>(ptr), y) == 0) return 1;
            return 0;
        }
        case v::host::automate: {
            if (S == nullptr) return 0;
            const std::uint32_t w = S->autoWrite.load(std::memory_order_relaxed);
            if (w - S->autoRead.load(std::memory_order_acquire) < static_cast<std::uint32_t>(kAutomationRing)) {
                S->automation[w % kAutomationRing] = {index, opt};
                S->autoWrite.store(w + 1, std::memory_order_release);
            }
            if (index >= 0 && index < kMaxParams) S->params[index].store(opt, std::memory_order_relaxed);
            return 0;
        }
        case v::host::sizeWindow:
            if (S != nullptr) { S->sizeW = index; S->sizeH = static_cast<std::int32_t>(value); S->sizeSeq.fetch_add(1); }
            return 1;
        case v::host::ioChanged:
        case v::host::updateDisplay:
            if (S != nullptr && plugin != nullptr) { S->latency.store(plugin->latency); S->displayChanged.store(1); }
            return 1;
        case v::host::beginEdit:
        case v::host::endEdit:
            return 1;
        default:
            return 0;
    }
}

// ---- plugin lifetime -----------------------------------------------------------------
v::Plugin* instantiate(const std::string& path, std::int32_t shellId, std::string& error) {
    if (library == nullptr) library = loadLibrary(path);
    if (library == nullptr) { error = "The bridge couldn't load the library (wrong architecture or missing dependency)."; return nullptr; }
    v::EntryFn entry = nullptr;
    for (const char* name : {"VSTPluginMain", "main"})
        if (auto* f = findSymbol(library, name)) { entry = reinterpret_cast<v::EntryFn>(f); break; }
    if (entry == nullptr) { error = "This file isn't a VST2 plugin."; return nullptr; }
    const auto slash = path.find_last_of("/\\");
    pluginDirectory = slash == std::string::npos ? std::string() : path.substr(0, slash);
    shellIdToLoad = shellId;
    v::Plugin* p = entry(hostCallback);
    shellIdToLoad = 0;
    if (p == nullptr || p->magic != v::kMagic) { error = "The plugin refused to start."; return nullptr; }
    return p;
}

void refreshParams() {
    if (plugin == nullptr) return;
    const int n = std::min(plugin->numParams, kMaxParams);
    for (int i = 0; i < n; ++i) S->params[i].store(plugin->getParameter(plugin, i), std::memory_order_relaxed);
}

void stopAudio() {   // control thread: make sure the audio thread is not inside the plugin
    while (processing.load()) std::this_thread::yield();
}

// ---- control requests ------------------------------------------------------------------
void setData(const std::string& s) {
    auto& c = S->control;
    const size_t n = std::min(s.size(), static_cast<size_t>(kDataBytes - 1));
    std::memcpy(c.data, s.data(), n);
    c.data[n] = 0;
    c.dataSize = static_cast<std::int32_t>(n);
}

std::string dataString() {
    auto& c = S->control;
    const auto n = static_cast<size_t>(std::clamp(c.dataSize, 0, kDataBytes - 1));
    return std::string(c.data, n);
}

void handleControl() {
    auto& c = S->control;
    c.result = 0;
    switch (c.cmd) {
        case cmdOpen: {
            std::string error;
            const auto path = dataString();
            plugin = instantiate(path, c.i0, error);
            if (plugin == nullptr) { c.result = -1; setData(error); break; }
            call(v::op::open);
            PluginInfo info {};
            info.numParams = plugin->numParams;
            info.numPrograms = plugin->numPrograms;
            info.numInputs = plugin->numInputs;
            info.numOutputs = plugin->numOutputs;
            info.flags = plugin->flags;
            info.uniqueId = plugin->uniqueId;
            info.version = plugin->pluginVersion;
            info.latency = plugin->latency;
            info.category = static_cast<std::int32_t>(call(v::op::getCategory));
            info.vendorVersion = static_cast<std::int32_t>(call(v::op::getVendorVersion));
            info.acceptsMidi = call(v::op::canDo, 0, 0, const_cast<char*>("receiveVstMidiEvent")) > 0
                               || call(v::op::canDo, 0, 0, const_cast<char*>("receiveVstEvents")) > 0;
            info.producesMidi = call(v::op::canDo, 0, 0, const_cast<char*>("sendVstMidiEvent")) > 0;
            copyString(info.name, sizeof(info.name), query(v::op::getEffectName).c_str());
            copyString(info.vendor, sizeof(info.vendor), query(v::op::getVendor).c_str());
            copyString(info.product, sizeof(info.product), query(v::op::getProduct).c_str());
            std::memcpy(c.data, &info, sizeof(info));
            c.dataSize = sizeof(info);
            S->latency.store(plugin->latency);
            refreshParams();
            break;
        }
        case cmdShellList: {
            std::string error;
            v::Plugin* shell = instantiate(dataString(), 0, error);
            if (shell == nullptr) { c.result = -1; setData(error); break; }
            plugin = shell;
            call(v::op::open);
            std::string list;
            if (call(v::op::getCategory) == v::category::shell)
                for (int guard = 0; guard < 1000; ++guard) {
                    char name[1024] = {};
                    const auto id = static_cast<std::int32_t>(call(v::op::shellNextPlugin, 0, 0, name));
                    if (id == 0) break;
                    name[sizeof(name) - 1] = 0;
                    list += std::to_string(id) + "\t" + name + "\n";
                }
            call(v::op::close);
            plugin = nullptr;
            setData(list);
            break;
        }
        case cmdPrepare:
            stopAudio();
            sampleRate = c.d0;
            blockSize = std::clamp(c.i0, 16, kMaxBlock);
            call(v::op::setSampleRate, 0, 0, nullptr, static_cast<float>(sampleRate));
            call(v::op::setBlockSize, 0, blockSize);
            call(v::op::setActive, 0, 1);
            call(v::op::startProcessing);
            S->latency.store(plugin != nullptr ? plugin->latency : 0);
            break;
        case cmdRelease:
            stopAudio();
            call(v::op::stopProcessing);
            call(v::op::setActive, 0, 0);
            break;
        case cmdParamName: setData(query(v::op::getParamName, c.i0)); break;
        case cmdParamUnit: setData(query(v::op::getParamUnit, c.i0)); break;
        case cmdParamText: setData(query(v::op::getParamText, c.i0)); break;
        case cmdSetParam:
            if (plugin != nullptr && c.i0 >= 0 && c.i0 < plugin->numParams) {
                plugin->setParameter(plugin, c.i0, static_cast<float>(c.d0));
                if (c.i0 < kMaxParams) S->params[c.i0].store(plugin->getParameter(plugin, c.i0));
            }
            break;
        case cmdGetProgram: c.result = static_cast<std::int32_t>(call(v::op::getProgram)); break;
        case cmdSetProgram: call(v::op::setProgram, 0, c.i0); refreshParams(); break;
        case cmdProgramName: {
            char buf[1024] = {};
            if (call(v::op::getProgramNameAt, c.i0, -1, buf) > 0) { buf[sizeof(buf) - 1] = 0; setData(buf); }
            else if (c.i0 == static_cast<std::int32_t>(call(v::op::getProgram))) setData(query(v::op::getProgramName));
            else setData("");
            break;
        }
        case cmdSetProgramName: {
            char buf[25] = {};
            copyString(buf, sizeof(buf), dataString().c_str());
            call(v::op::setProgramName, 0, 0, buf);
            break;
        }
        case cmdChunkGet: {
            void* data = nullptr;
            const auto size = call(v::op::getChunk, 0, 0, &data);
            chunk.assign(static_cast<const char*>(data), static_cast<const char*>(data) + (data != nullptr && size > 0 ? size : 0));
            c.result = static_cast<std::int32_t>(chunk.size());
            break;
        }
        case cmdChunkRead: {
            const auto offset = static_cast<size_t>(std::max<std::int64_t>(0, c.v0));
            const size_t n = offset < chunk.size() ? std::min(chunk.size() - offset, static_cast<size_t>(kDataBytes)) : 0;
            if (n > 0) std::memcpy(c.data, chunk.data() + offset, n);
            c.dataSize = static_cast<std::int32_t>(n);
            break;
        }
        case cmdChunkWrite: {
            const auto offset = static_cast<size_t>(std::max<std::int64_t>(0, c.v0));
            const auto n = static_cast<size_t>(std::clamp(c.dataSize, 0, kDataBytes));
            if (chunk.size() < offset + n) chunk.resize(offset + n);
            std::memcpy(chunk.data() + offset, c.data, n);
            break;
        }
        case cmdChunkSet:
            chunk.resize(static_cast<size_t>(std::max<std::int64_t>(0, c.v0)));
            stopAudio();
            call(v::op::setChunk, 0, static_cast<v::iptr>(chunk.size()), chunk.data());
            chunk.clear();
            refreshParams();
            break;
        case cmdCanDo: c.result = static_cast<std::int32_t>(call(v::op::canDo, 0, 0, const_cast<char*>(dataString().c_str()))); break;
        case cmdTail: c.result = static_cast<std::int32_t>(call(v::op::getTailSamples)); break;
        case cmdEditorRect:
        case cmdEditorOpen: {
            if (c.cmd == cmdEditorOpen && !editorOpen) {
                call(v::op::editorOpen, 0, 0, reinterpret_cast<void*>(static_cast<std::intptr_t>(c.v0)));
                editorOpen = true;
            }
            v::Rect* r = nullptr;
            call(v::op::editorGetRect, 0, 0, &r);
            c.i0 = r != nullptr ? r->right - r->left : 0;
            c.i1 = r != nullptr ? r->bottom - r->top : 0;
            break;
        }
        case cmdEditorClose:
            if (editorOpen) { call(v::op::editorClose); editorOpen = false; }
            break;
        case cmdClose:
            running = false;
            stopAudio();
            if (editorOpen) { call(v::op::editorClose); editorOpen = false; }
            if (plugin != nullptr) { call(v::op::stopProcessing); call(v::op::setActive, 0, 0); call(v::op::close); plugin = nullptr; }
            break;
        case cmdPing: c.result = 1; break;
        default: c.result = -2; break;
    }
}

// ---- audio ---------------------------------------------------------------------------
void audioThread() {
    inAudioThread = true;
    std::vector<std::vector<float>> extraIn, extraOut;
    std::vector<float*> ins, outs;
    std::vector<char> eventBytes(sizeof(v::Events) + kMaxEvents * sizeof(void*));
    std::vector<v::MidiEvent> events(kMaxEvents);

    while (running) {
        if (!session.audioRequest.wait(100)) continue;
        auto& a = S->audio;
        processing = true;
        if (plugin != nullptr && running) {
            const int frames = std::clamp(a.frames, 0, kMaxBlock);
            // transport for host::getTime
            timeInfo.samplePos = a.samplePos;
            timeInfo.sampleRate = a.sampleRate;
            timeInfo.ppqPos = a.ppq;
            timeInfo.tempo = a.tempo;
            timeInfo.barStartPos = a.barStart;
            timeInfo.loopStartPos = a.loopStart;
            timeInfo.loopEndPos = a.loopEnd;
            timeInfo.nanoSeconds = a.seconds * 1.0e9;
            timeInfo.timeSigNumerator = a.sigNum;
            timeInfo.timeSigDenominator = a.sigDen;
            timeInfo.flags = a.timeFlags;

            // MIDI
            const int count = std::clamp(a.numEvents, 0, kMaxEvents);
            if (count > 0) {
                auto* list = reinterpret_cast<v::Events*>(eventBytes.data());
                auto** slots = reinterpret_cast<v::MidiEvent**>(eventBytes.data() + offsetof(v::Events, events));
                for (int i = 0; i < count; ++i) {
                    auto& e = events[static_cast<size_t>(i)];
                    std::memset(&e, 0, sizeof(e));
                    e.type = 1;
                    e.byteSize = sizeof(v::MidiEvent);
                    e.frameOffset = a.events[i].frame;
                    std::memcpy(e.data, a.events[i].bytes, 3);
                    slots[i] = &e;
                }
                list->count = count;
                list->reserved = 0;
                call(v::op::sendEvents, 0, 0, list);
            }

            // channel pointers: shared memory for the first 16, scratch beyond
            const int ni = std::max(0, plugin->numInputs), no = std::max(0, plugin->numOutputs);
            if (static_cast<int>(extraIn.size()) < ni) extraIn.resize(static_cast<size_t>(ni));
            if (static_cast<int>(extraOut.size()) < no) extraOut.resize(static_cast<size_t>(no));
            ins.assign(static_cast<size_t>(std::max(ni, 1)), nullptr);
            outs.assign(static_cast<size_t>(std::max(no, 1)), nullptr);
            for (int i = 0; i < ni; ++i) {
                if (i < kMaxChannels) { ins[static_cast<size_t>(i)] = a.in[i]; continue; }
                extraIn[static_cast<size_t>(i)].assign(static_cast<size_t>(kMaxBlock), 0.0f);
                ins[static_cast<size_t>(i)] = extraIn[static_cast<size_t>(i)].data();
            }
            for (int i = 0; i < no; ++i) {
                if (i < kMaxChannels) { std::fill_n(a.out[i], frames, 0.0f); outs[static_cast<size_t>(i)] = a.out[i]; continue; }
                extraOut[static_cast<size_t>(i)].assign(static_cast<size_t>(kMaxBlock), 0.0f);
                outs[static_cast<size_t>(i)] = extraOut[static_cast<size_t>(i)].data();
            }
            if ((plugin->flags & v::flag::canReplace) != 0 && plugin->processReplacing != nullptr)
                plugin->processReplacing(plugin, ins.data(), outs.data(), frames);
            else if (plugin->processAccumulating != nullptr)
                plugin->processAccumulating(plugin, ins.data(), outs.data(), frames);
            refreshParams();
        }
        processing = false;
        session.audioReply.post();
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "FreeDAWm plugin bridge (%d-bit). Started by FreeDAWm, not by hand.\n", static_cast<int>(sizeof(void*) * 8));
        return 2;
    }
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    OleInitialize(nullptr);                         // plugin editors often need COM / OLE on the UI thread
#else
    prctl(PR_SET_PDEATHSIG, SIGKILL);               // die with FreeDAWm
#endif
    const std::string name = argv[1];
    const std::int64_t hostPid = std::atoll(argv[2]);
    if (!session.open(name)) return 3;
    S = session.shared();

    std::thread audio(audioThread);
    S->bridgeReady.store(1, std::memory_order_release);

    using clock = std::chrono::steady_clock;
    auto lastIdle = clock::now(), lastHostCheck = clock::now();
    while (running) {
        if (session.controlRequest.wait(editorOpen ? 5 : 20)) {
            handleControl();
            session.controlReply.post();
        }
#ifdef _WIN32
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
#endif
        const auto now = clock::now();
        if (editorOpen && now - lastIdle > std::chrono::milliseconds(30)) { call(v::op::editorIdle); lastIdle = now; }
        if (now - lastHostCheck > std::chrono::milliseconds(500)) {
            lastHostCheck = now;
            if (!processIsAlive(hostPid)) running = false;   // FreeDAWm went away
        }
    }
    running = false;
    session.audioRequest.post();
    audio.join();
    // Like the in-process host, the library is never unloaded: some plugins keep threads alive after closing.
    std::fflush(stdout);
    std::_Exit(0);
}
