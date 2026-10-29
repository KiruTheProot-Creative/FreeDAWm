#pragma once

// Small header-only DSP building blocks shared by the built-in instruments.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "fdm/Types.h"

#if defined(__SSE2__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

namespace fdm::dsp {

/** Flushes denormals to zero for the lifetime of the object (x86; no-op elsewhere). */
class ScopedFlushDenormals {
public:
#if defined(__SSE2__) || defined(_M_X64)
    ScopedFlushDenormals() : prev_(_mm_getcsr()) { _mm_setcsr(prev_ | 0x8040u); }
    ~ScopedFlushDenormals() { _mm_setcsr(prev_); }
private:
    unsigned int prev_;
#else
    ScopedFlushDenormals() = default;
#endif
};

/** Deterministic white noise (xorshift32). */
class Noise {
public:
    void seed(uint32_t s) { state_ = s ? s : 1u; }
    float next() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return static_cast<float>(static_cast<int32_t>(state_)) * (1.0f / 2147483648.0f);
    }
private:
    uint32_t state_ = 0x12345678u;
};

/** Linear attack, exponential decay/release. Release reaches -60 dB after `release` seconds. */
class Adsr {
public:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    void setSampleRate(double sr) { sr_ = static_cast<float>(sr); }

    void set(float attack, float decay, float sustain, float release) {
        attackInc_   = 1.0f / (std::max(attack, 0.0005f) * sr_);
        decayCoef_   = 1.0f - std::exp(-6.9f / (std::max(decay, 0.001f) * sr_));
        releaseCoef_ = 1.0f - std::exp(-6.9f / (std::max(release, 0.001f) * sr_));
        sustain_     = std::clamp(sustain, 0.0f, 1.0f);
    }

    void gateOn()  { stage_ = Stage::Attack; }   // restarts from the current level: no click on retrigger
    void gateOff() { if (stage_ != Stage::Idle) stage_ = Stage::Release; }
    void reset()   { stage_ = Stage::Idle; level_ = 0.0f; }

    bool  active() const { return stage_ != Stage::Idle; }
    Stage stage()  const { return stage_; }
    float level()  const { return level_; }

    float next() {
        switch (stage_) {
            case Stage::Idle: break;
            case Stage::Attack:
                level_ += attackInc_;
                if (level_ >= 1.0f) { level_ = 1.0f; stage_ = Stage::Decay; }
                break;
            case Stage::Decay:
                level_ += (sustain_ - level_) * decayCoef_;
                if (std::fabs(level_ - sustain_) < 1.0e-4f) { level_ = sustain_; stage_ = Stage::Sustain; }
                break;
            case Stage::Sustain:
                level_ = sustain_;
                break;
            case Stage::Release:
                level_ -= level_ * releaseCoef_;
                if (level_ < 1.0e-5f) { level_ = 0.0f; stage_ = Stage::Idle; }
                break;
        }
        return level_;
    }

private:
    Stage stage_ = Stage::Idle;
    float sr_ = 48000.0f, level_ = 0.0f, attackInc_ = 0.01f, decayCoef_ = 0.001f,
          releaseCoef_ = 0.001f, sustain_ = 0.7f;
};

/** Zero-delay-feedback state variable filter (Zavalishin / TPT). Stable under fast modulation. */
class Svf {
public:
    enum class Mode { LowPass, BandPass, HighPass };

    void setSampleRate(double sr) { sr_ = static_cast<float>(sr); }
    void reset() { ic1_ = ic2_ = 0.0f; }

    /** resonance 0..1 maps to Q of 0.7 .. ~20. */
    void set(float cutoffHz, float resonance01) {
        cutoffHz = std::clamp(cutoffHz, 10.0f, 0.45f * sr_);
        const float g = std::tan(kPi * cutoffHz / sr_);
        const float q = 0.707f * std::pow(28.0f, std::clamp(resonance01, 0.0f, 1.0f));
        k_  = 1.0f / q;
        a1_ = 1.0f / (1.0f + g * (g + k_));
        a2_ = g * a1_;
        a3_ = g * a2_;
    }

    float process(float x, Mode mode = Mode::LowPass) {
        const float v3 = x - ic2_;
        const float v1 = a1_ * ic1_ + a2_ * v3;
        const float v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
        ic1_ = 2.0f * v1 - ic1_;
        ic2_ = 2.0f * v2 - ic2_;
        switch (mode) {
            case Mode::LowPass:  return v2;
            case Mode::BandPass: return v1 * k_;   // normalised so the peak gain is ~1
            case Mode::HighPass: return x - k_ * v1 - v2;
        }
        return v2;
    }

private:
    float sr_ = 48000.0f, k_ = 1.4f, a1_ = 0, a2_ = 0, a3_ = 0, ic1_ = 0, ic2_ = 0;
};

enum class Wave : int { Saw = 0, Square = 1, Triangle = 2, Sine = 3 };

inline float polyBlep(float t, float dt) {
    if (t < dt)         { t /= dt;               return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt)  { t = (t - 1.0f) / dt;   return t * t + t + t + 1.0f; }
    return 0.0f;
}

/** Band-limited (polyBLEP) oscillator. */
class Oscillator {
public:
    void reset(float phase01 = 0.0f) { phase_ = phase01; }

    float next(Wave wave, float freqHz, float sampleRate, float pulseWidth = 0.5f) {
        const float dt = std::min(freqHz / sampleRate, 0.45f);
        float v = 0.0f;
        switch (wave) {
            case Wave::Saw:
                v = 2.0f * phase_ - 1.0f - polyBlep(phase_, dt);
                break;
            case Wave::Square: {
                v = phase_ < pulseWidth ? 1.0f : -1.0f;
                v += polyBlep(phase_, dt);
                float t2 = phase_ + 1.0f - pulseWidth;
                t2 -= std::floor(t2);
                v -= polyBlep(t2, dt);
                break;
            }
            case Wave::Triangle:
                v = 4.0f * std::fabs(phase_ - 0.5f) - 1.0f;
                break;
            case Wave::Sine:
                v = std::sin(kTwoPi * phase_);
                break;
        }
        phase_ += dt;
        if (phase_ >= 1.0f) phase_ -= 1.0f;
        return v;
    }

private:
    float phase_ = 0.0f;
};

/** Safety limiter for the master bus: instant attack, smooth release, hard ceiling. */
class Limiter {
public:
    void setSampleRate(double sr) { releaseCoef_ = 1.0f - std::exp(-1.0f / (0.05f * static_cast<float>(sr))); }
    void reset() { gain_ = 1.0f; }

    void process(float* l, float* r, int n, float ceiling = 0.98f) {
        for (int i = 0; i < n; ++i) {
            const float peak = std::max(std::fabs(l[i]), std::fabs(r[i]));
            const float target = peak > ceiling ? ceiling / peak : 1.0f;
            if (target < gain_) gain_ = target;
            else                gain_ += (target - gain_) * releaseCoef_;
            l[i] = std::clamp(l[i] * gain_, -1.0f, 1.0f);
            r[i] = std::clamp(r[i] * gain_, -1.0f, 1.0f);
        }
    }

    float currentGain() const { return gain_; }

private:
    float gain_ = 1.0f, releaseCoef_ = 0.0005f;
};

}  // namespace fdm::dsp
