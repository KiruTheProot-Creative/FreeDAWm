#pragma once

#include <map>

#include "Common.h"
#include "PluginHost.h"
#include "EnginePlayHead.h"
#include "PluginInstrument.h"
#include "Prefs.h"

/** Everything the UI needs: the engine, audio/MIDI I/O, plugin hosting, the
    current selection, and project files. UI-thread only unless noted. */
class AppState : private juce::AudioIODeviceCallback,
                 private juce::MidiInputCallback,
                 private juce::ChangeListener,
                 private juce::Timer {
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
    /** Slot for a new effect at the end of the chain (fills a gap only when the end is full); -1 if none. */
    int          nextFreeFxSlot(fdm::Strip& strip);
    int          effectCount(fdm::Strip& strip);
    void         showEffectEditor(fdm::Strip& strip, int slot);

    // ---- instrument plugins on MIDI tracks --------------------------------------------
    /** Loads an instrument plugin onto a MIDI track (replacing its built-in or previous instrument).
        Returns an error message, or an empty string on success. */
    juce::String      loadInstrument(int channel, const juce::PluginDescription& description, const juce::MemoryBlock* savedState = nullptr);
    void              clearInstrument(int channel);              // back to the built-in placeholder
    PluginInstrument* instrumentAt(int channel);
    void              showInstrumentEditor(int channel);
    /** Loads the instrument on the selected MIDI track, or on a new MIDI track if an audio track/bus is selected. */
    juce::String      loadInstrumentFromUi(const juce::PluginDescription& description, bool forceNewTrack = false);

    // ---- live notes (computer keyboard, on-screen keys) -------------------------
    void previewNoteOn(int note, int velocity = 100);
    void previewNoteOff(int note);

    // ---- projects -----------------------------------------------------------
    void         newProject();
    juce::Result saveProject(const juce::File& file);
    juce::Result loadProject(const juce::File& file);
    juce::Result exportWav(const juce::File& file, int scene, int bars, fdm::WavFormat format);

    /** Export using the options from Preferences > Export (or the export dialog). */
    struct ExportOptions {
        fdm::WavFormat format = fdm::WavFormat::Pcm24;
        double sampleRate = 0.0;          // 0 = the audio device's rate
        float  tailSeconds = 2.0f;
        bool   normalize = false;
        float  normalizeDb = -0.3f;
        bool   dither = true;
    };
    ExportOptions exportOptionsFromPrefs() const;
    juce::Result exportTimeline(const juce::File& file, const ExportOptions& options);
    juce::String projectName() const;
    const juce::File& projectFile() const { return projectFile_; }

    // ---- audio tracks -------------------------------------------------------------------
    /** Loads a sound file into the engine's sample pool (or finds it if already loaded). Returns the index or -1. */
    int importSample(const juce::File& file, juce::String* error = nullptr);
    /** Places a sound file on an audio track. Returns the clip index or -1 (and shows why). */
    int addAudioClip(int channel, const juce::File& file, int64_t startTick);
    /** Length of a pooled sample in ticks at the current tempo. */
    int sampleLengthTicks(int sampleIndex);

    // ---- sample explorer ------------------------------------------------------------
    juce::StringArray sampleFolders();
    void addSampleFolder(const juce::File& folder);
    void removeSampleFolder(const juce::String& path);
    void previewSample(const juce::File& file);      // plays through the master output, outside the mixer
    void stopSamplePreview();
    bool isPreviewingSample() const { return previewTransport_.isPlaying(); }

    // ---- audio helpers for Preferences ---------------------------------------------------
    void playTestTone() { testToneFrames_.store(static_cast<int>(engine.sampleRate())); }
    void setMidiInputEnabled(const juce::String& identifier, bool on);
    bool isMidiInputEnabled(const juce::String& identifier) const { return deviceManager.isMidiInputDeviceEnabled(identifier); }
    /** Opens the plugin's editor after it was loaded from the UI, when the preference asks for it. */
    void effectLoadedFromUi(fdm::Strip& strip, int slot) { if (prefs->getBool("openEditorOnLoad")) showEffectEditor(strip, slot); }

    // ---- audio --------------------------------------------------------------
    double cpuUsage() const { return deviceManager.getCpuUsage(); }
    juce::String audioStatus() const { return audioError_; }

    // ---- members ------------------------------------------------------------
    fdm::Engine                 engine;
    juce::ApplicationProperties properties;
    std::unique_ptr<Prefs>      prefs;
    EnginePlayHead              playHead{engine};      // tempo/position for every hosted plugin
    std::unique_ptr<PluginHost> pluginHost;
    juce::AudioDeviceManager    deviceManager;

    // Destroyed before the engine, so no editor outlives the plugin it shows.
    std::map<fdm::IEffect*, std::unique_ptr<PluginWindow>> editors;

private:
    std::map<int, std::unique_ptr<PluginInstrument>> instruments_;
    std::map<int, std::unique_ptr<PluginWindow>>     instrumentEditors_;      // destroyed before the instruments
public:

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
    void clearAllInstruments();
    void closeEditorsFor(fdm::Strip& strip);
    void resetMixerAndTransport();
    void notifyStructureChanged();
    juce::Result writeProject(const juce::File& file);
    void makeBackup(const juce::File& file);
    void applyPrefs();
    void changeListenerCallback(juce::ChangeBroadcaster*) override { applyPrefs(); }
    void timerCallback() override;          // autosave

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
    std::atomic<int> testToneFrames_{0};
    double testTonePhase_ = 0.0;
    int autosaveMinutes_ = -1;

    juce::AudioFormatManager     formats_;
    juce::TimeSliceThread        previewThread_{"Sample preview"};
    juce::AudioTransportSource   previewTransport_;
    std::unique_ptr<juce::AudioFormatReaderSource> previewSource_;
    juce::AudioBuffer<float>     previewBuffer_;
};
