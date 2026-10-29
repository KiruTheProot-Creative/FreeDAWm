#pragma once

#include <map>

#include "Common.h"
#include "PluginHost.h"

/** Everything the UI needs: the engine, audio/MIDI I/O, plugin hosting, the
    current selection, and project files. UI-thread only unless noted. */
class AppState : private juce::AudioIODeviceCallback,
                 private juce::MidiInputCallback {
public:
    AppState();
    ~AppState() override;

    // ---- selection ----------------------------------------------------------
    struct Selection {
        enum class Kind { Channel, Return, Master };
        Kind kind  = Kind::Channel;
        int  index = 0;        // channel or return index
        int  scene = 0;        // selected clip slot (Channel only)
        bool operator==(const Selection& o) const { return kind == o.kind && index == o.index && scene == o.scene; }
    };

    class Listener {
    public:
        virtual ~Listener() = default;
        virtual void selectionChanged() {}
        virtual void structureChanged() {}      // channels added/removed, project loaded, FX changed
    };
    void addListener(Listener* l) { listeners_.add(l); }
    void removeListener(Listener* l) { listeners_.remove(l); }

    const Selection& selection() const { return selection_; }
    void select(const Selection& s);
    void selectChannel(int channel, int scene);
    fdm::Strip*   selectedStrip();
    fdm::Channel* selectedChannel();
    fdm::Clip*    selectedClip();                  // nullptr when the slot is empty

    // ---- channels -----------------------------------------------------------
    int  addChannel(fdm::InstrumentType type);     // returns the new index or -1
    void removeChannel(int index);

    // ---- effects ------------------------------------------------------------
    /** Loads a plugin into an FX slot. Returns an error message, or an empty string on success. */
    juce::String loadEffect(fdm::Strip& strip, int slot, const juce::PluginDescription& description,
                            const juce::MemoryBlock* savedState = nullptr);
    void         clearEffect(fdm::Strip& strip, int slot);
    Vst3Effect*  effectAt(fdm::Strip& strip, int slot);
    void         showEffectEditor(fdm::Strip& strip, int slot);

    // ---- live notes (computer keyboard, on-screen keys) -------------------------
    void previewNoteOn(int note, int velocity = 100);
    void previewNoteOff(int note);

    // ---- projects -----------------------------------------------------------
    void         newProject();
    juce::Result saveProject(const juce::File& file);
    juce::Result loadProject(const juce::File& file);
    juce::Result exportWav(const juce::File& file, int scene, int bars, fdm::WavFormat format);
    juce::String projectName() const;
    const juce::File& projectFile() const { return projectFile_; }

    // ---- audio --------------------------------------------------------------
    double cpuUsage() const { return deviceManager.getCpuUsage(); }
    juce::String audioStatus() const { return audioError_; }

    // ---- members ------------------------------------------------------------
    fdm::Engine                 engine;
    juce::ApplicationProperties properties;
    std::unique_ptr<PluginHost> pluginHost;
    juce::AudioDeviceManager    deviceManager;

    // Destroyed before the engine, so no editor outlives the plugin it shows.
    std::map<fdm::IEffect*, std::unique_ptr<PluginWindow>> editors;

    void saveSettings();
    void structureHasChanged() { notifyStructureChanged(); }     // e.g. after a clip is created or deleted

private:
    class ScopedAudioPause {
    public:
        explicit ScopedAudioPause(AppState& s) : state_(s), wasRunning_(s.audioAttached_) { if (wasRunning_) s.detachAudio(); }
        ~ScopedAudioPause() { if (wasRunning_) state_.attachAudio(); }
    private:
        AppState& state_;
        bool wasRunning_;
    };

    void attachAudio();
    void detachAudio();
    void clearAllEffects();
    void closeEditorsFor(fdm::Strip& strip);
    void resetMixerAndTransport();
    void notifyStructureChanged();

    // juce::AudioIODeviceCallback
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceIOCallbackWithContext(const float* const* input, int numInputs, float* const* output,
                                          int numOutputs, int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;

    // juce::MidiInputCallback
    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;

    Selection selection_;
    std::atomic<int> armedChannel_{-1};      // read by the MIDI thread
    juce::ListenerList<Listener> listeners_;
    juce::File projectFile_;
    juce::String audioError_;
    bool audioAttached_ = false;
    int  blockSize_ = 512;
    std::vector<float> monoScratch_;
};
