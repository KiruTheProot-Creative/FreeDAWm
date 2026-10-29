#pragma once

#include "fdm/Param.h"

namespace fdm {

/** Something that turns note events into audio. */
class IInstrument {
public:
    virtual ~IInstrument() = default;

    virtual void prepare(double sampleRate, int maxBlockSize) = 0;

    virtual void noteOn(int note, float velocity01) = 0;
    virtual void noteOff(int note) = 0;
    virtual void allNotesOff() = 0;

    /** Render `numFrames` of audio, ADDING it into the two buffers. */
    virtual void render(float* left, float* right, int numFrames) = 0;

    virtual ParamList& params() = 0;
    virtual const char* name() const = 0;
};

}  // namespace fdm
