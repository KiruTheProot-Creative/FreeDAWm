#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/** Runs a VST2 plugin inside fdm_bridge32 / fdm_bridge64 (a separate process) and makes
    it look like any other AudioPluginInstance. Used for 32-bit plugins, and for 64-bit
    plugins when "Run VST2 plugins in a separate process" is on. */
namespace bridged {

enum class Arch { unknown, x86, x64, other };

/** Reads the file header (PE on Windows, ELF on Linux) to see what CPU the plugin is built for. */
Arch architectureOf(const juce::File& binary);

/** The helper executable for an architecture, next to the app (or in the folder set below). */
juce::File bridgeExecutable(Arch arch);
void setBridgeFolder(const juce::File& folder);

/** Scans a file through the bridge (also handles shell plugins). Returns false and an error if it failed. */
bool describe(const juce::File& file, Arch arch, juce::OwnedArray<juce::PluginDescription>& results, juce::String& error);

std::unique_ptr<juce::AudioPluginInstance> create(const juce::PluginDescription& desc, Arch arch, double sampleRate, int blockSize,
                                                  juce::String& error);

}  // namespace bridged
