#pragma once

#include <array>

#include "fdm/IInstrument.h"
#include "fdm/dsp/Dsp.h"

namespace fdm {

/** 8-voice subtractive synth: two band-limited oscillators, a resonant
    low-pass filter, and separate amp and filter ADSR envelopes. */
class PolySynth : public IInstrument {
public:
    static constexpr int kNumVoices = 8;

    PolySynth();

    void prepare(double sampleRate, int maxBlockSize) override;
    void noteOn(int note, float velocity01) override;
    void noteOff(int note) override;
    void allNotesOff() override;
    void render(float* left, float* right, int numFrames) override;

    ParamList& params() override { return params_; }
    const char* name() const override { return "Poly Synth"; }

    int activeVoiceCount() const;

private:
    struct Voice {
        bool     active = false;
        bool     gate   = false;
        int      note   = -1;
        uint64_t age    = 0;
        float    velocity = 0.0f;
        float    baseHz   = 440.0f;
        int      controlCounter = 0;
        dsp::Oscillator o1, o2;
        dsp::Svf        filter;
        dsp::Adsr       amp, flt;
    };

    int allocateVoice();

    ParamList params_;
    Param& osc1Wave_;
    Param& osc2Wave_;
    Param& osc2Semi_;
    Param& osc2Fine_;
    Param& osc2Level_;
    Param& pulseWidth_;
    Param& cutoff_;
    Param& resonance_;
    Param& envAmount_;
    Param& keyTrack_;
    Param& ampA_, &ampD_, &ampS_, &ampR_;
    Param& fltA_, &fltD_, &fltS_, &fltR_;
    Param& volume_;

    std::array<Voice, kNumVoices> voices_;
    double   sampleRate_ = 48000.0;
    uint64_t counter_    = 0;
    uint32_t phaseSeed_  = 1;
};

}  // namespace fdm
