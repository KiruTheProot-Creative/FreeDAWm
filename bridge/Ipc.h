#pragma once

// Minimal cross-process primitives for the plugin bridge (Windows and Linux), no JUCE.

#include <cstddef>
#include <cstdint>
#include <string>

#include "BridgeProtocol.h"

namespace fdm::bridge {

/** A named block of shared memory. The host creates it, the bridge opens it. */
class SharedMemory {
public:
    SharedMemory() = default;
    ~SharedMemory();
    SharedMemory(const SharedMemory&) = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;

    bool create(const std::string& name, std::size_t size);
    bool open(const std::string& name, std::size_t size);
    void close();
    void* data() const { return data_; }

private:
    void* data_ = nullptr;
    std::size_t size_ = 0;
    std::string name_;
    bool owner_ = false;
#ifdef _WIN32
    void* mapping_ = nullptr;
#endif
};

/** A semaphore shared between the two processes.
    Linux: a futex on a word inside the shared memory (works between 32- and 64-bit processes).
    Windows: a named kernel semaphore. */
class Signal {
public:
    Signal() = default;
    ~Signal();
    Signal(const Signal&) = delete;
    Signal& operator=(const Signal&) = delete;

    bool create(const std::string& name, SignalWord& word);
    bool open(const std::string& name, SignalWord& word);
    void post();
    /** Returns true if signalled, false on timeout. */
    bool wait(int timeoutMs);

private:
    SignalWord* word_ = nullptr;
#ifdef _WIN32
    void* handle_ = nullptr;
#endif
};

/** The four signals of a session, named after its shared memory. */
struct Session {
    SharedMemory memory;
    Signal controlRequest, controlReply, audioRequest, audioReply;
    Shared* shared() const { return static_cast<Shared*>(memory.data()); }

    bool create(const std::string& name);       // host
    bool open(const std::string& name);         // bridge
};

/** True while process `pid` exists. */
bool processIsAlive(std::int64_t pid);
std::int64_t currentProcessId();

}  // namespace fdm::bridge
