#pragma once

#include "Common.h"

/** Wraps a hosted VST3 plugin so the engine can use it as an effect. */
class Vst3Effect : public fdm::IEffect {
public:
    explicit Vst3Effect(std::unique_ptr<juce::AudioPluginInstance> plugin);
    ~Vst3Effect() override;

    void prepare(double sampleRate, int maxBlockSize) override;
    void process(float* left, float* right, int numFrames) override;
    void reset() override;
    int  latencySamples() const override;
    const char* name() const override { return name_.toRawUTF8(); }

    juce::AudioPluginInstance& plugin() { return *plugin_; }

private:
    std::unique_ptr<juce::AudioPluginInstance> plugin_;
    juce::String   name_;
    juce::MidiBuffer midi_;
    int  maxBlock_ = 512;
    bool prepared_ = false;
};

/** A floating window showing a plugin's own editor (or a generic one). */
class PluginWindow : public juce::DocumentWindow {
public:
    PluginWindow(juce::AudioPluginInstance& plugin, std::function<void()> onCloseRequested);
    void closeButtonPressed() override { if (onClose_) onClose_(); }

private:
    std::function<void()> onClose_;
};

/** Finds VST3 and VST2 plugins and creates effects from them.

    By default only the `plugins` folder shipped next to the application is
    scanned, so the effects bundled with FreeDAWm are always found. Extra folders
    and the system VST3 folders can be switched on from the Plugins menu. */
class PluginHost {
public:
    explicit PluginHost(juce::PropertiesFile* settings);
    ~PluginHost();

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList          knownPlugins;

    /** The folders that hold effects shipped with FreeDAWm (the ones that exist). */
    juce::Array<juce::File> bundledFolders() const;
    juce::File              primaryBundledFolder() const;
    juce::FileSearchPath    searchPaths(const juce::String& formatName = "VST3") const;

    bool useSystemFolders() const { return useSystemFolders_; }
    /** VST2 support (FreeDAWm's own clean-room host). On by default. */
    bool vst2Enabled() const { return vst2Enabled_; }
    void setVst2Enabled(bool on) { vst2Enabled_ = on; saveSettings(); }
    void setUseSystemFolders(bool on) { useSystemFolders_ = on; saveSettings(); }
    void addFolder(const juce::File& folder);
    void removeFolder(const juce::String& path) { extraFolders_.removeString(path); saveSettings(); }
    const juce::StringArray& extraFolders() const { return extraFolders_; }

    /** Scans in the background with a progress window; `onDone(numPluginsFound)` runs on the message thread. */
    void scanAsync(std::function<void(int)> onDone);

    /** Effects (not instruments) that have been found, sorted by name. */
    juce::Array<juce::PluginDescription> availableEffects() const;
    /** Instrument plugins that have been found, sorted by name. */
    juce::Array<juce::PluginDescription> availableInstruments() const;

    /** Creates an instrument plugin with a stereo main output (inputs and extra outputs off where possible). */
    std::unique_ptr<juce::AudioPluginInstance> createInstrument(const juce::PluginDescription& description, double sampleRate,
                                                                int blockSize, juce::String& error);

    std::unique_ptr<fdm::IEffect> createEffect(const juce::PluginDescription& description, double sampleRate,
                                               int blockSize, juce::String& error);

    void saveSettings();
    juce::AudioPluginFormat* formatFor(const juce::String& name) const;

private:
    class ScanJob;

    juce::File deadMansPedalFile() const;

    juce::PropertiesFile* settings_ = nullptr;
    juce::StringArray     extraFolders_;
    bool                  useSystemFolders_ = false;
    bool                  vst2Enabled_ = true;
    std::unique_ptr<ScanJob> scanJob_;
};
