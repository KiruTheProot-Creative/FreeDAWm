#include "PluginScanner.h"

#include "vst2/Vst2Format.h"

#if JUCE_WINDOWS
extern "C" __declspec(dllimport) unsigned int __stdcall SetErrorMode(unsigned int);
#endif

namespace pluginscan {

bool isWorkerCommandLine(const juce::StringArray& args) { return args.contains(kWorkerFlag); }

void suppressCrashDialogs() {
   #if JUCE_WINDOWS
    SetErrorMode(0x0001 /*SEM_FAILCRITICALERRORS*/ | 0x0002 /*SEM_NOGPFAULTERRORBOX*/ | 0x8000 /*SEM_NOOPENFILEERRORBOX*/);
   #endif
}

int runWorker(const juce::StringArray& args) {
    suppressCrashDialogs();
    const int at = args.indexOf(kWorkerFlag);
    if (at < 0 || at + 3 >= args.size()) return 2;
    const auto formatName = args[at + 1].unquoted();
    const auto file = args[at + 2].unquoted();
    const juce::File output(args[at + 3].unquoted());

    juce::AudioPluginFormatManager formats;
    juce::addDefaultFormatsToManager(formats);
    formats.addFormat(std::make_unique<Vst2Format>());

    juce::AudioPluginFormat* format = nullptr;
    for (int i = 0; i < formats.getNumFormats(); ++i)
        if (formats.getFormat(i)->getName() == formatName) format = formats.getFormat(i);
    if (format == nullptr) return 3;

    juce::OwnedArray<juce::PluginDescription> found;
    format->findAllTypesForFile(found, file);                      // if the plugin crashes, it crashes here, alone

    juce::XmlElement root("FDM_SCAN");
    root.setAttribute("file", file);
    for (auto* d : found) root.addChildElement(d->createXml().release());
    const auto temp = output.getSiblingFile(output.getFileName() + ".part");
    if (!root.writeTo(temp) || !temp.moveFileTo(output)) return 4;  // the result appears in one step
    return 0;
}

Result scanInChildProcess(const juce::File& executable, const juce::String& formatName, const juce::String& file,
                          int timeoutMs, const std::function<bool()>& shouldAbort) {
    Result r;
    const auto output = juce::File::createTempFile(".fdmscan.xml");
    output.deleteFile();

    juce::ChildProcess child;
    if (!child.start(juce::StringArray{executable.getFullPathName(), kWorkerFlag, formatName, file, output.getFullPathName()},
                     0)) {                                       // no pipes: a hung child must never block us
        r.error = "Couldn't start the scanner process.";
        return r;
    }

    const auto started = juce::Time::getMillisecondCounter();
    while (child.isRunning()) {
        if (shouldAbort && shouldAbort()) { child.kill(); r.aborted = true; r.error = "Cancelled."; output.deleteFile(); return r; }
        if (juce::Time::getMillisecondCounter() - started > static_cast<juce::uint32>(timeoutMs)) {
            child.kill();
            r.timedOut = true;
            r.error = "It didn't finish within " + juce::String(timeoutMs / 1000) + " seconds.";
            output.deleteFile();
            return r;
        }
        juce::Thread::sleep(20);
    }

    if (!output.existsAsFile()) {
        const auto code = child.getExitCode();
        r.error = code != 0 ? "It crashed the scanner (exit code " + juce::String(static_cast<int>(code)) + ")." : juce::String("It crashed the scanner.");
        return r;
    }
    if (auto xml = juce::XmlDocument::parse(output)) {
        for (auto* e : xml->getChildIterator()) {
            auto d = std::make_unique<juce::PluginDescription>();
            if (d->loadFromXml(*e)) r.found.add(d.release());
        }
        r.ok = true;
    } else {
        r.error = "The scanner's result was unreadable.";
    }
    output.deleteFile();
    return r;
}

}  // namespace pluginscan
