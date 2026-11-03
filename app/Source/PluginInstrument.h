#pragma once

#include "Common.h"

/** Lets the engine play a hosted instrument plugin (VST3 or VST2) like a built-in instrument.

    The engine calls noteOn/noteOff on the audio thread, each after setBlockOffset() with the
    sample position in the coming block, then render() once per block. Notes become MIDI events
    at those positions, so timing is sample-accurate while the plugin sees normal block sizes. */
class PluginInstrument final : public fdm::IInstrument {
public:
    explicit PluginInstrument(std::unique_ptr<juce::AudioPluginInstance> plugin)
        : plugin_(std::move(plugin)), name_(plugin_->getName().toStdString()) {
        midi_.ensureSize(8192);
    }

    ~PluginInstrument() override {
        if (prepared_) plugin_->releaseResources();
    }

    juce::AudioPluginInstance& plugin() { return *plugin_; }

    void prepare(double sampleRate, int maxBlockSize) override {
        if (prepared_) plugin_->releaseResources();
        maxBlock_ = std::max(16, maxBlockSize);
        plugin_->setRateAndBufferSizeDetails(sampleRate, maxBlock_);
        plugin_->prepareToPlay(sampleRate, maxBlock_);
        const int channels = std::max({2, plugin_->getTotalNumInputChannels(), plugin_->getTotalNumOutputChannels()});
        buffer_.setSize(channels, maxBlock_);
        midi_.clear();
        prepared_ = true;
    }

    bool wantsWholeBlocks() const override { return true; }
    void setBlockOffset(int frame) override { offset_ = frame; }

    void noteOn(int note, float velocity01) override {
        midi_.addEvent(juce::MidiMessage::noteOn(1, note, juce::jlimit(0.01f, 1.0f, velocity01)), offset_);
    }
    void noteOff(int note) override { midi_.addEvent(juce::MidiMessage::noteOff(1, note), offset_); }
    void allNotesOff() override {
        midi_.addEvent(juce::MidiMessage::allNotesOff(1), offset_);
        midi_.addEvent(juce::MidiMessage::allSoundOff(1), offset_);
    }

    void render(float* left, float* right, int numFrames) override {
        if (!prepared_) { midi_.clear(); return; }
        juce::ScopedNoDenormals noDenormals;
        // Engine blocks can be longer than the plugin was prepared for: hand it slices.
        for (int done = 0; done < numFrames;) {
            const int n = std::min(maxBlock_, numFrames - done);
            juce::AudioBuffer<float> view(buffer_.getArrayOfWritePointers(), buffer_.getNumChannels(), n);
            view.clear();
            slice_.clear();
            slice_.addEvents(midi_, done, n, -done);
            plugin_->processBlock(view, slice_);
            const int outs = std::max(1, plugin_->getTotalNumOutputChannels());
            juce::FloatVectorOperations::add(left + done, view.getReadPointer(0), n);
            juce::FloatVectorOperations::add(right + done, view.getReadPointer(std::min(1, outs - 1)), n);
            done += n;
        }
        midi_.clear();
        offset_ = 0;
    }

    fdm::ParamList& params() override { return noParams_; }
    const char* name() const override { return name_.c_str(); }

private:
    std::unique_ptr<juce::AudioPluginInstance> plugin_;
    std::string name_;
    juce::AudioBuffer<float> buffer_;
    juce::MidiBuffer midi_, slice_;
    fdm::ParamList noParams_;
    int maxBlock_ = 512, offset_ = 0;
    bool prepared_ = false;
};
