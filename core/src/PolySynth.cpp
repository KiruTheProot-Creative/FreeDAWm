#include "fdm/PolySynth.h"

namespace fdm {

PolySynth::PolySynth()
    : osc1Wave_  (params_.add("osc1.wave",  "Osc 1 Wave",   0.0f, 3.0f, 0.0f, false, true)),
      osc2Wave_  (params_.add("osc2.wave",  "Osc 2 Wave",   0.0f, 3.0f, 1.0f, false, true)),
      osc2Semi_  (params_.add("osc2.semi",  "Osc 2 Semi",  -24.0f, 24.0f, 0.0f, false, true)),
      osc2Fine_  (params_.add("osc2.fine",  "Osc 2 Fine",  -50.0f, 50.0f, 7.0f)),
      osc2Level_ (params_.add("osc2.level", "Osc 2 Level",  0.0f, 1.0f, 0.6f)),
      pulseWidth_(params_.add("pulse",      "Pulse Width",  0.05f, 0.95f, 0.5f)),
      cutoff_    (params_.add("cutoff",     "Cutoff",       20.0f, 20000.0f, 1800.0f, true)),
      resonance_ (params_.add("resonance",  "Resonance",    0.0f, 1.0f, 0.2f)),
      envAmount_ (params_.add("env.amount", "Filter Env",  -1.0f, 1.0f, 0.5f)),
      keyTrack_  (params_.add("keytrack",   "Key Track",    0.0f, 1.0f, 0.3f)),
      ampA_      (params_.add("amp.a",      "Amp Attack",   0.001f, 5.0f, 0.005f, true)),
      ampD_      (params_.add("amp.d",      "Amp Decay",    0.001f, 5.0f, 0.25f, true)),
      ampS_      (params_.add("amp.s",      "Amp Sustain",  0.0f, 1.0f, 0.6f)),
      ampR_      (params_.add("amp.r",      "Amp Release",  0.005f, 8.0f, 0.25f, true)),
      fltA_      (params_.add("flt.a",      "Flt Attack",   0.001f, 5.0f, 0.002f, true)),
      fltD_      (params_.add("flt.d",      "Flt Decay",    0.001f, 5.0f, 0.3f, true)),
      fltS_      (params_.add("flt.s",      "Flt Sustain",  0.0f, 1.0f, 0.1f)),
      fltR_      (params_.add("flt.r",      "Flt Release",  0.005f, 8.0f, 0.3f, true)),
      volume_    (params_.add("volume",     "Volume",      -36.0f, 6.0f, 0.0f)) {}

void PolySynth::prepare(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    counter_    = 0;
    phaseSeed_  = 1;      // so identical sessions render identically
    for (auto& v : voices_) {
        v.filter.setSampleRate(sampleRate);
        v.amp.setSampleRate(sampleRate);
        v.flt.setSampleRate(sampleRate);
        v.amp.reset();
        v.flt.reset();
        v.filter.reset();
        v.active = false;
        v.gate = false;
    }
}

int PolySynth::activeVoiceCount() const {
    int n = 0;
    for (auto& v : voices_) n += v.active ? 1 : 0;
    return n;
}

int PolySynth::allocateVoice() {
    // 1. A free voice.
    for (int i = 0; i < kNumVoices; ++i)
        if (!voices_[static_cast<size_t>(i)].active) return i;

    // 2. The quietest voice that is already releasing.
    int best = -1;
    float bestLevel = 2.0f;
    for (int i = 0; i < kNumVoices; ++i) {
        const auto& v = voices_[static_cast<size_t>(i)];
        if (!v.gate && v.amp.level() < bestLevel) { bestLevel = v.amp.level(); best = i; }
    }
    if (best >= 0) return best;

    // 3. The oldest held voice.
    uint64_t oldest = UINT64_MAX;
    for (int i = 0; i < kNumVoices; ++i) {
        const auto& v = voices_[static_cast<size_t>(i)];
        if (v.age < oldest) { oldest = v.age; best = i; }
    }
    return best;
}

void PolySynth::noteOn(int note, float velocity01) {
    // Same note already held: retrigger that voice instead of stacking.
    int idx = -1;
    for (int i = 0; i < kNumVoices; ++i) {
        const auto& v = voices_[static_cast<size_t>(i)];
        if (v.active && v.note == note) { idx = i; break; }
    }
    if (idx < 0) idx = allocateVoice();

    Voice& v = voices_[static_cast<size_t>(idx)];
    const bool fresh = !v.active;
    v.active   = true;
    v.gate     = true;
    v.note     = note;
    v.age      = ++counter_;
    v.velocity = std::clamp(velocity01, 0.0f, 1.0f);
    v.baseHz   = midiToHz(static_cast<float>(note));
    v.controlCounter = 0;

    if (fresh) {
        // Deterministic pseudo-random start phase so stacked notes don't phase-lock.
        phaseSeed_ = phaseSeed_ * 1664525u + 1013904223u;
        const float p1 = static_cast<float>(phaseSeed_ >> 8) * (1.0f / 16777216.0f);
        phaseSeed_ = phaseSeed_ * 1664525u + 1013904223u;
        const float p2 = static_cast<float>(phaseSeed_ >> 8) * (1.0f / 16777216.0f);
        v.o1.reset(p1);
        v.o2.reset(p2);
        v.filter.reset();
        v.amp.reset();
        v.flt.reset();
    }
    v.amp.gateOn();
    v.flt.gateOn();
}

void PolySynth::noteOff(int note) {
    for (auto& v : voices_) {
        if (v.active && v.gate && v.note == note) {
            v.gate = false;
            v.amp.gateOff();
            v.flt.gateOff();
        }
    }
}

void PolySynth::allNotesOff() {
    for (auto& v : voices_) {
        if (v.active && v.gate) {
            v.gate = false;
            v.amp.gateOff();
            v.flt.gateOff();
        }
    }
}

void PolySynth::render(float* left, float* right, int numFrames) {
    const auto  wave1  = static_cast<dsp::Wave>(static_cast<int>(osc1Wave_.get() + 0.5f));
    const auto  wave2  = static_cast<dsp::Wave>(static_cast<int>(osc2Wave_.get() + 0.5f));
    const float ratio2 = std::exp2((osc2Semi_.get() + osc2Fine_.get() * 0.01f) / 12.0f);
    const float level2 = osc2Level_.get();
    const float pw     = pulseWidth_.get();
    const float cutoff = cutoff_.get();
    const float res    = resonance_.get();
    const float envAmt = envAmount_.get();
    const float keyTr  = keyTrack_.get();
    // The oscillator mix is normalised to +-1, so one voice peaks at `outGain` and a chord has headroom.
    const float oscNorm = 1.0f / (1.0f + level2);
    const float outGain = dbToGain(volume_.get()) * 0.3f * oscNorm;
    const auto  sr = static_cast<float>(sampleRate_);

    for (auto& v : voices_) {
        if (!v.active) continue;

        v.amp.set(ampA_.get(), ampD_.get(), ampS_.get(), ampR_.get());
        v.flt.set(fltA_.get(), fltD_.get(), fltS_.get(), fltR_.get());

        for (int i = 0; i < numFrames; ++i) {
            // Filter coefficients at control rate (every 16 samples) - cheap and click-free.
            if ((v.controlCounter++ & 15) == 0) {
                const float octaves = envAmt * 5.0f * v.flt.level()
                                    + keyTr * (static_cast<float>(v.note) - 60.0f) / 12.0f;
                v.filter.set(cutoff * std::exp2(octaves), res);
            }

            float s = v.o1.next(wave1, v.baseHz, sr, pw);
            if (level2 > 0.0f) s += level2 * v.o2.next(wave2, v.baseHz * ratio2, sr, pw);
            v.flt.next();
            s = v.filter.process(s, dsp::Svf::Mode::LowPass);

            const float o = s * v.amp.next() * v.velocity * outGain;
            left[i]  += o;
            right[i] += o;

            if (!v.amp.active()) { v.active = false; v.gate = false; break; }
        }
    }
}

}  // namespace fdm
