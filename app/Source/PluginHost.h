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
    PluginWindow(Vst3Effect& effect, std::function<void()> onCloseRequested);
    void closeButtonPressed() override { if (onClose_) onClose_(); }

private:
    std::function<void()> onClose_;
};

/** Finds VST3 plugins and creates effects from them.

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
    juce::FileSearchPath    searchPaths() const;

    bool useSystemFolders() const { return useSystemFolders_; }
    void setUseSystemFolders(bool on) { useSystemFolders_ = on; saveSettings(); }
    void addFolder(const juce::File& folder);
    const juce::StringArray& extraFolders() const { return extraFolders_; }

    /** Scans in the background with a progress window; `onDone(numPluginsFound)` runs on the message thread. */
    void scanAsync(std::function<void(int)> onDone);

    /** Effects (not instruments) that have been found, sorted by name. */
    juce::Array<juce::PluginDescription> availableEffects() const;

    std::unique_ptr<fdm::IEffect> createEffect(const juce::PluginDescription& description, double sampleRate,
                                               int blockSize, juce::String& error);

    void saveSettings();

private:
    class ScanJob;

    juce::File deadMansPedalFile() const;

    juce::PropertiesFile* settings_ = nullptr;
    juce::StringArray     extraFolders_;
    bool                  useSystemFolders_ = false;
    std::unique_ptr<ScanJob> scanJob_;
};
