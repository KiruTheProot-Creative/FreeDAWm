#pragma once

#include "Common.h"

/** Tells hosted plugins (effects and instruments, VST3 and VST2) where the song is: tempo,
    position, bar, time signature, play state and loop. Plugins call this from the audio thread
    while the engine processes them, so it reads the engine's per-block transport directly. */
class EnginePlayHead final : public juce::AudioPlayHead {
public:
    explicit EnginePlayHead(const fdm::Engine& engine) : engine_(engine) {}

    juce::Optional<PositionInfo> getPosition() const override {
        const auto& t = engine_.blockTransport();
        PositionInfo info;
        info.setBpm(t.bpm);
        info.setTimeSignature(TimeSignature{t.timeSigNumerator, t.timeSigDenominator});
        info.setPpqPosition(t.ppq);
        const double beatsPerBar = 4.0 * t.timeSigNumerator / t.timeSigDenominator;
        const auto bar = static_cast<int64_t>(std::floor(t.ppq / beatsPerBar));
        info.setBarCount(bar);
        info.setPpqPositionOfLastBarStart(static_cast<double>(bar) * beatsPerBar);
        info.setTimeInSamples(t.samplePos);
        info.setTimeInSeconds(t.sampleRate > 0.0 ? static_cast<double>(t.samplePos) / t.sampleRate : 0.0);
        info.setIsPlaying(t.playing);
        info.setIsRecording(false);
        info.setIsLooping(t.looping);
        if (t.looping) info.setLoopPoints(LoopPoints{t.loopStartPpq, t.loopEndPpq});
        return info;
    }

private:
    const fdm::Engine& engine_;
};
