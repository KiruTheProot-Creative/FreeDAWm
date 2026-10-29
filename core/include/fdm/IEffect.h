#pragma once

namespace fdm {

/** An audio effect that lives in a channel's FX slot.

    The engine only knows this interface. The desktop app wraps hosted VST3
    plugins in an implementation of it, so the core never needs to know what a
    VST3 is and can be tested without any plugin SDK.

    All methods except the constructor/destructor are called from the audio
    thread (prepare() is called with the audio stopped). */
class IEffect {
public:
    virtual ~IEffect() = default;

    /** Called before processing starts, and again if the sample rate or block size changes. */
    virtual void prepare(double sampleRate, int maxBlockSize) = 0;

    /** Process a stereo block in place. */
    virtual void process(float* left, float* right, int numFrames) = 0;

    /** Clear delay lines, filter state, reverb tails. */
    virtual void reset() {}

    /** Latency this effect adds, in samples. (Not compensated yet.) */
    virtual int latencySamples() const { return 0; }

    virtual const char* name() const = 0;
};

}  // namespace fdm
