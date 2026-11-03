#pragma once

// ===========================================================================
// FreeDAWm's own description of the VST 2.x binary interface.
//
// Written from scratch for interoperability: it declares only the memory
// layout, function signatures and numeric codes a host needs to talk to an
// existing VST2 plugin, using FreeDAWm's own names. It contains no code or
// text from Steinberg's SDK. "VST" is a trademark of Steinberg Media
// Technologies GmbH; FreeDAWm is not affiliated with or endorsed by Steinberg.
// ===========================================================================

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
 #define FDM_VST2_CALL __cdecl
#else
 #define FDM_VST2_CALL
#endif

namespace fdm::vst2 {

using i32 = std::int32_t;
using iptr = std::intptr_t;

struct Plugin;

/** Host -> plugin: the "dispatcher" every plugin exposes. */
using DispatchFn = iptr (FDM_VST2_CALL*)(Plugin*, i32 opcode, i32 index, iptr value, void* ptr, float opt);
/** Plugin -> host. */
using HostFn = iptr (FDM_VST2_CALL*)(Plugin*, i32 opcode, i32 index, iptr value, void* ptr, float opt);
using ProcessFn = void (FDM_VST2_CALL*)(Plugin*, float** inputs, float** outputs, i32 frames);
using ProcessDoubleFn = void (FDM_VST2_CALL*)(Plugin*, double** inputs, double** outputs, i32 frames);
using SetParamFn = void (FDM_VST2_CALL*)(Plugin*, i32 index, float value);
using GetParamFn = float (FDM_VST2_CALL*)(Plugin*, i32 index);
/** The library's exported entry point ("VSTPluginMain", or "main_macho" / "main" in old builds). */
using EntryFn = Plugin* (FDM_VST2_CALL*)(HostFn host);

constexpr i32 kMagic = 0x56737450;      // the four bytes 'V' 's' 't' 'P'

/** The plugin's main structure, as laid out in memory. */
struct Plugin {
    i32 magic;
    DispatchFn dispatch;
    ProcessFn processAccumulating;      // old, adds to the outputs
    SetParamFn setParameter;
    GetParamFn getParameter;
    i32 numPrograms;
    i32 numParams;
    i32 numInputs;
    i32 numOutputs;
    i32 flags;
    iptr hostReserved1;                 // FreeDAWm keeps its instance pointer here
    iptr hostReserved2;
    i32 latency;                        // samples
    i32 unusedA;
    i32 unusedB;
    float unusedRatio;
    void* pluginObject;
    void* pluginUser;
    i32 uniqueId;
    i32 pluginVersion;
    ProcessFn processReplacing;
    ProcessDoubleFn processDoubleReplacing;
    char padding[56];
};

namespace flag {
constexpr i32 hasEditor         = 1 << 0;
constexpr i32 canReplace        = 1 << 4;
constexpr i32 chunks            = 1 << 5;
constexpr i32 isInstrument      = 1 << 8;
constexpr i32 silentWhenStopped = 1 << 9;
constexpr i32 canDouble         = 1 << 12;
}  // namespace flag

/** Host -> plugin opcodes. */
namespace op {
constexpr i32 open               = 0;
constexpr i32 close              = 1;
constexpr i32 setProgram         = 2;
constexpr i32 getProgram         = 3;
constexpr i32 setProgramName     = 4;
constexpr i32 getProgramName     = 5;
constexpr i32 getParamUnit       = 6;
constexpr i32 getParamText       = 7;
constexpr i32 getParamName       = 8;
constexpr i32 setSampleRate      = 10;
constexpr i32 setBlockSize       = 11;
constexpr i32 setActive          = 12;
constexpr i32 editorGetRect      = 13;
constexpr i32 editorOpen         = 14;
constexpr i32 editorClose        = 15;
constexpr i32 editorIdle         = 19;
constexpr i32 getChunk           = 23;
constexpr i32 setChunk           = 24;
constexpr i32 sendEvents         = 25;
constexpr i32 getProgramNameAt   = 29;
constexpr i32 getCategory        = 35;
constexpr i32 getEffectName      = 45;
constexpr i32 getVendor          = 47;
constexpr i32 getProduct         = 48;
constexpr i32 getVendorVersion   = 49;
constexpr i32 canDo              = 51;
constexpr i32 getTailSamples     = 52;
constexpr i32 getInterfaceVersion= 58;
constexpr i32 shellNextPlugin    = 70;
constexpr i32 startProcessing    = 71;
constexpr i32 stopProcessing     = 72;
}  // namespace op

/** Plugin -> host opcodes. */
namespace host {
constexpr i32 automate           = 0;
constexpr i32 version            = 1;
constexpr i32 currentId          = 2;
constexpr i32 idle               = 3;
constexpr i32 getTime            = 7;
constexpr i32 sendEventsToHost   = 8;
constexpr i32 ioChanged          = 13;
constexpr i32 sizeWindow         = 15;
constexpr i32 getSampleRate      = 16;
constexpr i32 getBlockSize       = 17;
constexpr i32 getInputLatency    = 18;
constexpr i32 getOutputLatency   = 19;
constexpr i32 getProcessLevel    = 23;
constexpr i32 getAutomationState = 24;
constexpr i32 getVendor          = 32;
constexpr i32 getProduct         = 33;
constexpr i32 getVendorVersion   = 34;
constexpr i32 canDo              = 37;
constexpr i32 getLanguage        = 38;
constexpr i32 getDirectory       = 41;
constexpr i32 updateDisplay      = 42;
constexpr i32 beginEdit          = 43;
constexpr i32 endEdit            = 44;
}  // namespace host

/** Plugin categories reported by op::getCategory. */
namespace category {
constexpr i32 unknown = 0, effect = 1, instrument = 2, analysis = 3, mastering = 4, spatial = 5,
              roomFx = 6, restoration = 8, offline = 9, shell = 10, generator = 11;
}

/** Editor rectangle (op::editorGetRect returns a pointer to one). */
struct Rect { std::int16_t top, left, bottom, right; };

/** Transport info (host::getTime returns a pointer to one). */
struct TimeInfo {
    double samplePos;
    double sampleRate;
    double nanoSeconds;
    double ppqPos;
    double tempo;
    double barStartPos;
    double loopStartPos;
    double loopEndPos;
    i32 timeSigNumerator;
    i32 timeSigDenominator;
    i32 smpteOffset;
    i32 smpteFrameRate;
    i32 samplesToNextClock;
    i32 flags;
};

namespace time {
constexpr i32 changed = 1, playing = 2, looping = 4, recording = 8;
constexpr i32 nanosValid = 1 << 8, ppqValid = 1 << 9, tempoValid = 1 << 10, barsValid = 1 << 11,
              loopValid = 1 << 12, timeSigValid = 1 << 13;
}

/** A MIDI event in an event list. */
struct MidiEvent {
    i32 type;                // 1 = MIDI
    i32 byteSize;            // sizeof(MidiEvent)
    i32 frameOffset;         // position in the current block
    i32 flags;
    i32 noteLength;
    i32 noteOffset;
    char data[4];
    char detune;
    char noteOffVelocity;
    char reservedA;
    char reservedB;
};

/** Event list header; `events` really holds `count` pointers. */
struct Events {
    i32 count;
    iptr reserved;
    MidiEvent* events[2];
};

static_assert(sizeof(MidiEvent) == 32, "VST2 MIDI event layout");
static_assert(sizeof(Rect) == 8, "VST2 rect layout");

}  // namespace fdm::vst2
