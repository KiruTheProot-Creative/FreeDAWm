#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "BridgedPlugin.h"

/** Hosts VST 2.x plugins through FreeDAWm's own clean-room interface (vst2/Vst2Abi.h),
    without Steinberg's VST2 SDK. 32-bit plugins run in the fdm_bridge32 helper process (BridgedPlugin).

    Plugs into juce::AudioPluginFormatManager like any other format, so the scanner,
    the known-plugin list and createPluginInstance work unchanged. Format name: "VST2". */
class Vst2Format : public juce::AudioPluginFormat {
public:
    Vst2Format() = default;

    static const char* formatName() { return "VST2"; }

    juce::String getName() const override { return formatName(); }
    void findAllTypesForFile(juce::OwnedArray<juce::PluginDescription>& results, const juce::String& fileOrIdentifier) override;
    bool fileMightContainThisPluginType(const juce::String& fileOrIdentifier) override;
    juce::String getNameOfPluginFromIdentifier(const juce::String& fileOrIdentifier) override;
    bool pluginNeedsRescanning(const juce::PluginDescription&) override;
    bool doesPluginStillExist(const juce::PluginDescription&) override;
    bool canScanForPlugins() const override { return true; }
    bool isTrivialToScan() const override { return false; }
    juce::StringArray searchPathsForPlugins(const juce::FileSearchPath&, bool recursive, bool allowAsync = false) override;
    juce::FileSearchPath getDefaultLocationsToSearch() override;
    bool requiresUnblockedMessageThreadDuringCreation(const juce::PluginDescription&) const override { return false; }

    /** Run every VST2 plugin in its own bridge process (crash protection). 32-bit plugins always are. */
    static void setRunInSeparateProcess(bool on);
    static bool runsInSeparateProcess();
    /** Which bridge a plugin file needs, or Arch::unknown to load it inside FreeDAWm. */
    static bridged::Arch bridgeFor(const juce::File& file);

    /** Creates an instance synchronously (on the message thread). */
    static std::unique_ptr<juce::AudioPluginInstance> create(const juce::PluginDescription&, double sampleRate, int blockSize,
                                                             juce::String& error);

protected:
    void createPluginInstance(const juce::PluginDescription&, double initialSampleRate, int initialBufferSize,
                              PluginCreationCallback) override;
};
