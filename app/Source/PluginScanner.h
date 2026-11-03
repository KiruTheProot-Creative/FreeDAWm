#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/** Scans plugin files in a separate process, so a plugin that crashes or hangs
    while being scanned can't take FreeDAWm down with it.

    The app re-launches its own executable with
        --fdm-scan-plugin <format> <file> <output.xml>
    and that worker loads the plugin, writes what it found as XML, and exits. */
namespace pluginscan {

constexpr const char* kWorkerFlag = "--fdm-scan-plugin";

struct Result {
    bool ok = false;                               // the worker finished and wrote a result
    bool timedOut = false;
    bool aborted = false;                          // shouldAbort() asked us to stop
    juce::String error;                            // why it failed, for the user
    juce::OwnedArray<juce::PluginDescription> found;
};

/** True if this command line asks for a scan worker. */
bool isWorkerCommandLine(const juce::StringArray& args);

/** Runs the worker side. Returns the process exit code. Needs JUCE initialised (message thread). */
int runWorker(const juce::StringArray& args);

/** Scans one file in a child process running `executable`. */
Result scanInChildProcess(const juce::File& executable, const juce::String& formatName, const juce::String& file,
                          int timeoutMs, const std::function<bool()>& shouldAbort);

/** Makes a crash in this process exit quietly instead of showing a system crash dialog (Windows). */
void suppressCrashDialogs();

}  // namespace pluginscan
