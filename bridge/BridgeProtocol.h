#pragma once

// ===========================================================================
// Shared-memory protocol between FreeDAWm and fdm_bridge32 / fdm_bridge64.
//
// The bridge is a small helper process that loads one VST2 plugin. FreeDAWm (64-bit)
// talks to it through one shared memory block. The 32-bit bridge loads 32-bit
// plugins; the 64-bit one sandboxes 64-bit plugins so a crash can't take the app down.
//
// Every struct here must have the SAME layout in 32-bit and 64-bit builds: only
// fixed-size types, every 8-byte field explicitly 8-byte aligned (i386 would align
// doubles to 4 otherwise), no pointers. Both sides check the layout at startup.
// ===========================================================================

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace fdm::bridge {

constexpr std::uint32_t kMagic   = 0x46444252;   // "FDBR"
constexpr std::uint32_t kVersion = 1;

constexpr int kMaxChannels   = 16;
constexpr int kMaxBlock      = 4096;
constexpr int kMaxEvents     = 1024;
constexpr int kMaxParams     = 8192;
constexpr int kDataBytes     = 1 << 20;            // strings, chunks (sent in pieces), shell lists
constexpr int kAutomationRing = 1024;

/** A counter that is waited on and posted across processes (Linux: futex word; Windows: unused, named semaphores). */
struct alignas(8) SignalWord {
    std::atomic<std::int32_t> value{0};
    std::int32_t pad = 0;
};

enum Command : std::int32_t {
    cmdNone = 0,
    cmdOpen,            // data = file path (UTF-8), i0 = shell id  -> data = PluginInfo
    cmdClose,           // close the plugin and exit
    cmdShellList,       // data = file path                          -> data = "id\tname\n" lines
    cmdPrepare,         // d0 = sample rate, i0 = block size
    cmdRelease,
    cmdParamName,       // i0 = index -> data = string
    cmdParamUnit,
    cmdParamText,
    cmdSetParam,        // i0 = index, d0 = value
    cmdGetProgram,      // -> result
    cmdSetProgram,      // i0
    cmdProgramName,     // i0 -> data
    cmdSetProgramName,  // data
    cmdChunkGet,        // -> result = total size (the bridge keeps a copy)
    cmdChunkRead,       // v0 = offset -> data, dataSize
    cmdChunkWrite,      // v0 = offset, data, dataSize (bridge accumulates)
    cmdChunkSet,        // v0 = total size: applies the accumulated chunk
    cmdCanDo,           // data = string -> result
    cmdTail,            // -> result (samples)
    cmdEditorRect,      // -> i0 = width, i1 = height
    cmdEditorOpen,      // v0 = native parent window handle -> i0, i1 = size
    cmdEditorClose,
    cmdPing
};

/** What cmdOpen returns. */
struct PluginInfo {
    std::int32_t numParams, numPrograms, numInputs, numOutputs;
    std::int32_t flags, uniqueId, version, latency;
    std::int32_t category, vendorVersion, acceptsMidi, producesMidi;
    char name[256];
    char vendor[256];
    char product[256];
};

struct alignas(8) Control {
    SignalWord request, reply;
    std::int32_t cmd = 0, result = 0, i0 = 0, i1 = 0;
    alignas(8) std::int64_t v0 = 0;
    alignas(8) double d0 = 0.0;
    std::int32_t dataSize = 0, pad = 0;
    char data[kDataBytes];
};

struct MidiEvent {
    std::int32_t frame;
    std::uint8_t bytes[4];
};

struct alignas(8) Audio {
    SignalWord request, reply;
    std::int32_t frames = 0, numEvents = 0, numIn = 0, numOut = 0;
    // transport for host::getTime
    alignas(8) double samplePos = 0, sampleRate = 48000, ppq = 0, tempo = 120, barStart = 0, loopStart = 0, loopEnd = 0, seconds = 0;
    std::int32_t sigNum = 4, sigDen = 4, timeFlags = 0, pad = 0;
    MidiEvent events[kMaxEvents];
    float in[kMaxChannels][kMaxBlock];
    float out[kMaxChannels][kMaxBlock];
};

struct AutomationEvent { std::int32_t index; float value; };

struct alignas(8) Shared {
    std::uint32_t magic, version;
    std::uint32_t layoutSize, controlOffset, audioOffset, paramsOffset;
    std::atomic<std::int32_t> bridgeReady{0};
    std::atomic<std::int32_t> latency{0};
    std::atomic<std::int32_t> displayChanged{0};
    std::atomic<std::int32_t> sizeSeq{0};
    std::int32_t sizeW = 0, sizeH = 0;
    std::atomic<std::uint32_t> autoWrite{0}, autoRead{0};
    AutomationEvent automation[kAutomationRing];
    std::atomic<float> params[kMaxParams];        // current parameter values, refreshed by the bridge
    Control control;
    Audio audio;
};

static_assert(sizeof(std::atomic<std::int32_t>) == 4 && sizeof(std::atomic<float>) == 4, "lock-free 32-bit atomics needed");
static_assert(std::atomic<std::int32_t>::is_always_lock_free && std::atomic<float>::is_always_lock_free, "lock-free atomics needed");

inline void writeLayout(Shared& s) {
    s.magic = kMagic;
    s.version = kVersion;
    s.layoutSize = static_cast<std::uint32_t>(sizeof(Shared));
    s.controlOffset = static_cast<std::uint32_t>(offsetof(Shared, control));
    s.audioOffset = static_cast<std::uint32_t>(offsetof(Shared, audio));
    s.paramsOffset = static_cast<std::uint32_t>(offsetof(Shared, params));
}

inline bool layoutMatches(const Shared& s) {
    return s.magic == kMagic && s.version == kVersion && s.layoutSize == sizeof(Shared)
           && s.controlOffset == offsetof(Shared, control) && s.audioOffset == offsetof(Shared, audio)
           && s.paramsOffset == offsetof(Shared, params);
}

}  // namespace fdm::bridge
