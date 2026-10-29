#pragma once

#include <array>
#include <memory>

#include "fdm/IInstrument.h"
#include "fdm/dsp/Dsp.h"

namespace fdm {

/** Eight fully synthesized drum pads, mapped to General MIDI drum notes so
    external MIDI keyboards and pad controllers work out of the box. */
class DrumRack : public IInstrument {
public:
    static constexpr int kNumPads = 8;

    DrumRack();
    ~DrumRack() override;

    void prepare(double sampleRate, int maxBlockSize) override;
    void noteOn(int note, float velocity01) override;
    void noteOff(int) override {}              // drums are one-shots
    void allNotesOff() override {}
    void render(float* left, float* right, int numFrames) override;

    ParamList& params() override { return params_; }
    const char* name() const override { return "Drum Rack"; }

    /** The MIDI note a pad answers to (GM: 36 kick, 37 rim, 38 snare, 39 clap, 41 tom, 42 closed hat, 46 open hat, 56 cowbell). */
    static int         padNote(int pad);
    static const char* padName(int pad);
    /** -1 if the note is not mapped to a pad. */
    static int         padForNote(int note);

private:
    struct Voice;

    ParamList params_;
    Param* volume_ = nullptr;
    struct PadParams { Param *volume, *tune, *decay, *pan; };
    std::array<PadParams, kNumPads> pad_{};
    std::array<std::unique_ptr<Voice>, kNumPads> voices_;
    double sampleRate_ = 48000.0;
};

}  // namespace fdm
