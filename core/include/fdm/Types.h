#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace fdm {

// ---------------------------------------------------------------------------
// Timing. 4/4 only for now. 960 ticks per quarter note gives ~0.5 ms
// resolution at 120 BPM, which is finer than anyone can hear.
// ---------------------------------------------------------------------------
constexpr int kPPQ          = 960;
constexpr int kTicksPerBeat = kPPQ;
constexpr int kTicksPerBar  = kPPQ * 4;
constexpr int kTicksPerStep = kPPQ / 4;   // one 16th note

// ---------------------------------------------------------------------------
// Fixed capacities. Everything the audio thread touches is preallocated, so
// adding a channel or a note never allocates memory on the audio thread.
// ---------------------------------------------------------------------------
constexpr int kMaxChannels     = 12;   // instrument channels
constexpr int kNumReturns      = 2;    // return channels (A, B)
constexpr int kMaxScenes       = 8;    // clip slots per channel
constexpr int kMaxNotesPerClip = 512;
constexpr int kMaxClipBars     = 16;
constexpr int kMaxFxSlots      = 4;    // effect slots per channel / return / master
constexpr int kMaxBlockSize    = 4096;

constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

inline float dbToGain(float db) { return db <= -90.0f ? 0.0f : std::pow(10.0f, db * 0.05f); }
inline float gainToDb(float g)  { return g <= 1.0e-5f ? -100.0f : 20.0f * std::log10(g); }
inline float midiToHz(float note) { return 440.0f * std::exp2((note - 69.0f) / 12.0f); }

}  // namespace fdm
