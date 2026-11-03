#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>

#include "fdm/Clip.h"
#include "fdm/DrumRack.h"
#include "fdm/IEffect.h"
#include "fdm/IInstrument.h"
#include "fdm/PolySynth.h"
#include "fdm/Types.h"

namespace fdm {

enum class InstrumentType : int { DrumRack = 0, PolySynth = 1 };

constexpr int kMaxPlacements = 128;   // arrangement clips per channel
constexpr int kPlacementUnit = kTicksPerStep;   // placements start and end on 16th-note boundaries

/** A clip slot placed on the arrangement timeline. */
struct Placement {
    int startTick  = 0;
    int lengthTicks = kTicksPerBar;
    int slot       = 0;      // which clip slot's notes it plays
};

constexpr int kNoQueue   = -1;   // Channel::queuedSlot: nothing queued
constexpr int kQueueStop = -2;   // Channel::queuedSlot: stop the playing clip at the next launch point

/** One slot in a channel's effect chain. The engine owns the effect; use
    Engine::swapEffect() to change it safely while audio is running. */
struct EffectSlot {
    std::atomic<IEffect*> effect{nullptr};
    std::atomic<bool>     bypass{false};
};

/** The mixer part of a channel: fader, balance, mute, meters, and an FX chain.
    Instrument channels, the two return channels and the master all have one. */
struct Strip {
    Strip() { setName("Strip"); }

    void setName(const char* n) { std::snprintf(name, sizeof(name), "%s", n); }

    char name[32] = {};
    std::atomic<float> volumeDb{0.0f};
    std::atomic<float> pan{0.0f};          // -1 (left) .. +1 (right); a balance control for stereo material
    std::atomic<bool>  mute{false};
    std::array<EffectSlot, kMaxFxSlots> fx;

    // Meters. The audio thread keeps the max since the UI last looked; the UI reads with take*().
    std::atomic<float> peakL{0.0f}, peakR{0.0f};
    float takePeakL() { return peakL.exchange(0.0f, std::memory_order_relaxed); }
    float takePeakR() { return peakR.exchange(0.0f, std::memory_order_relaxed); }

    void updatePeak(float l, float r) {                       // audio thread
        float cur = peakL.load(std::memory_order_relaxed);
        while (l > cur && !peakL.compare_exchange_weak(cur, l, std::memory_order_relaxed)) {}
        cur = peakR.load(std::memory_order_relaxed);
        while (r > cur && !peakR.compare_exchange_weak(cur, r, std::memory_order_relaxed)) {}
    }

    float lastGain = -1.0f;   // audio thread only: previous block's gain, for click-free ramps
};

/** An instrument channel: instrument + clip slots + mixer strip. */
struct Channel : Strip {
    Channel() {
        for (auto& s : sends) s.store(0.0f, std::memory_order_relaxed);
        for (auto& p : pending) p = {};
        for (auto& w : placements) w.store(0, std::memory_order_relaxed);
    }

    // ---- arrangement placements (UI thread writes; one packed atomic word each) ----
    //   bit 63 active | bits 40-42 slot | bits 20-39 length (units) | bits 0-19 start (units)
    std::array<std::atomic<uint64_t>, kMaxPlacements> placements;

    static uint64_t packPlacement(const Placement& p) {
        const uint64_t start = static_cast<uint64_t>(std::max(0, p.startTick) / kPlacementUnit) & 0xFFFFF;
        const uint64_t len   = static_cast<uint64_t>(std::max(kPlacementUnit, p.lengthTicks) / kPlacementUnit) & 0xFFFFF;
        const uint64_t slot  = static_cast<uint64_t>(std::clamp(p.slot, 0, kMaxScenes - 1)) & 0x7;
        return (1ull << 63) | (slot << 40) | (len << 20) | start;
    }
    static Placement unpackPlacement(uint64_t w) {
        Placement p;
        p.startTick   = static_cast<int>(w & 0xFFFFF) * kPlacementUnit;
        p.lengthTicks = static_cast<int>((w >> 20) & 0xFFFFF) * kPlacementUnit;
        p.slot        = static_cast<int>((w >> 40) & 0x7);
        return p;
    }
    int addPlacement(const Placement& p) {
        for (int i = 0; i < kMaxPlacements; ++i)
            if ((placements[static_cast<size_t>(i)].load() >> 63) == 0) {
                placements[static_cast<size_t>(i)].store(packPlacement(p), std::memory_order_release);
                return i;
            }
        return -1;
    }
    bool getPlacement(int i, Placement& out) const {
        if (i < 0 || i >= kMaxPlacements) return false;
        const uint64_t w = placements[static_cast<size_t>(i)].load(std::memory_order_acquire);
        if ((w >> 63) == 0) return false;
        out = unpackPlacement(w);
        return true;
    }
    void setPlacement(int i, const Placement& p) { placements[static_cast<size_t>(i)].store(packPlacement(p), std::memory_order_release); }
    void removePlacement(int i) { placements[static_cast<size_t>(i)].store(0, std::memory_order_release); }
    void clearPlacements() { for (auto& w : placements) w.store(0, std::memory_order_release); }
    int  countPlacements() const {
        int n = 0;
        for (auto& w : placements) n += (w.load() >> 63) ? 1 : 0;
        return n;
    }

    std::atomic<bool> active{false};
    std::atomic<int>  instrumentType{static_cast<int>(InstrumentType::DrumRack)};
    std::atomic<bool> solo{false};
    std::array<std::atomic<float>, kNumReturns> sends;     // linear 0..1, post-fader
    std::atomic<int>  colorIndex{0};

    std::array<Clip, kMaxScenes> clips;

    // Launch state. playingSlot/playPosTicks are written by the audio thread, read by the UI.
    std::atomic<int> playingSlot{-1};
    std::atomic<int> queuedSlot{kNoQueue};
    std::atomic<int> playPosTicks{0};

    // Both built-in instruments always exist, so switching a channel's
    // instrument is just flipping instrumentType - nothing is allocated.
    DrumRack  drums;
    PolySynth synth;

    /** Optional replacement instrument (e.g. a hosted VST3 instrument or a test double).
        Must be set while the audio thread is not running. Not owned. */
    std::atomic<IInstrument*> external{nullptr};

    IInstrument& instrument() {
        if (auto* e = external.load(std::memory_order_acquire)) return *e;
        return instrumentType.load(std::memory_order_relaxed) == static_cast<int>(InstrumentType::DrumRack)
                   ? static_cast<IInstrument&>(drums)
                   : static_cast<IInstrument&>(synth);
    }

    // ---- audio-thread-only sequencing state --------------------------------
    struct PendingOff { int note = 0; int64_t offTick = 0; };
    std::array<PendingOff, 64> pending;
    int     numPending   = 0;
    int     arrIndex     = -1;       // audio thread: placement currently playing (arrangement mode)
    int64_t clipStartTick = 0;

    void addPending(int note, int64_t offTick, IInstrument& inst) {
        if (numPending == static_cast<int>(pending.size())) {      // full: cut the oldest short
            inst.noteOff(pending[0].note);
            for (int i = 1; i < numPending; ++i) pending[static_cast<size_t>(i - 1)] = pending[static_cast<size_t>(i)];
            --numPending;
        }
        pending[static_cast<size_t>(numPending++)] = {note, offTick};
    }

    void releaseDue(int64_t tick, IInstrument& inst) {
        for (int i = 0; i < numPending;) {
            if (pending[static_cast<size_t>(i)].offTick <= tick) {
                inst.noteOff(pending[static_cast<size_t>(i)].note);
                pending[static_cast<size_t>(i)] = pending[static_cast<size_t>(--numPending)];
            } else {
                ++i;
            }
        }
    }

    void releaseNote(int note, IInstrument& inst) {
        for (int i = 0; i < numPending;) {
            if (pending[static_cast<size_t>(i)].note == note) {
                inst.noteOff(note);
                pending[static_cast<size_t>(i)] = pending[static_cast<size_t>(--numPending)];
            } else {
                ++i;
            }
        }
    }

    void releaseAll(IInstrument& inst) {
        for (int i = 0; i < numPending; ++i) inst.noteOff(pending[static_cast<size_t>(i)].note);
        numPending = 0;
    }
};

}  // namespace fdm
