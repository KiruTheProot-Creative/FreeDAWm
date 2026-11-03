// Loads FreeDAWm's test VST2 plugins through the clean-room host and checks everything end to end.
#include <juce_audio_processors/juce_audio_processors.h>

#include "../Source/PluginInstrument.h"
#include "../Source/PluginScanner.h"
#include "../Source/vst2/Vst2Format.h"

namespace {
int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { ++failures; std::printf("    FAILED line %d: %s\n", __LINE__, #cond); } } while (0)
#define EXPECT_NEAR(a, b, eps) EXPECT(std::abs(static_cast<double>(a) - static_cast<double>(b)) <= (eps))

float runOnce(juce::AudioPluginInstance& p, float input, juce::MidiBuffer* midi = nullptr) {
    juce::AudioBuffer<float> buf(2, 256);
    for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::fill(buf.getWritePointer(c), input, 256);
    juce::MidiBuffer empty;
    p.processBlock(buf, midi != nullptr ? *midi : empty);
    return buf.getSample(1, 100);
}
}  // namespace

int main(int argc, char** argv) {
    juce::StringArray args;
    for (int i = 0; i < argc; ++i) args.add(argv[i]);
    if (pluginscan::isWorkerCommandLine(args)) {                   // we are our own scan worker
        juce::ScopedJuceInitialiser_GUI juce;
        return pluginscan::runWorker(args);
    }
    if (argc < 6) { std::printf("usage: %s <plugin> <shell> <crashing plugin> <hanging plugin> <synth>\n", argv[0]); return 2; }
    juce::ScopedJuceInitialiser_GUI juce;
    Vst2Format format;

    std::printf("[ .. ] scan a plain plugin\n");
    juce::OwnedArray<juce::PluginDescription> found;
    EXPECT(format.fileMightContainThisPluginType(argv[1]));
    format.findAllTypesForFile(found, argv[1]);
    EXPECT(found.size() == 1);
    if (found.isEmpty()) { std::printf("cannot continue\n"); return 1; }
    const auto desc = *found[0];
    EXPECT(desc.name == "FDM Test Gain");
    EXPECT(desc.manufacturerName == "FreeDAWm Tests");
    EXPECT(desc.pluginFormatName == "VST2");
    EXPECT(!desc.isInstrument);
    EXPECT(desc.numInputChannels == 2 && desc.numOutputChannels == 2);

    std::printf("[ .. ] create through AudioPluginFormatManager and process\n");
    juce::AudioPluginFormatManager manager;
    manager.addFormat(std::make_unique<Vst2Format>());
    juce::String error;
    auto plugin = manager.createPluginInstance(desc, 48000.0, 256, error);
    EXPECT(plugin != nullptr);
    if (plugin == nullptr) { std::printf("    %s\n", error.toRawUTF8()); return 1; }
    plugin->prepareToPlay(48000.0, 256);
    EXPECT(plugin->getParameters().size() == 3);
    EXPECT(plugin->getHostedParameter(0)->getName(32) == "Gain");
    EXPECT_NEAR(runOnce(*plugin, 0.25f), 0.25f, 1e-6);                 // default gain 0.5 -> x1
    plugin->getHostedParameter(0)->setValue(1.0f);
    EXPECT_NEAR(runOnce(*plugin, 0.25f), 0.5f, 1e-6);                  // x2
    EXPECT(plugin->getHostedParameter(0)->getText(1.0f, 16) == "2.00");
    EXPECT_NEAR(plugin->getHostedParameter(1)->getValue(), 0.12f, 1e-4);   // the plugin saw tempo 120 via getTime

    std::printf("[ .. ] MIDI reaches the plugin\n");
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 10);
    midi.addEvent(juce::MidiMessage::noteOn(1, 64, 0.8f), 20);
    runOnce(*plugin, 0.0f, &midi);
    EXPECT(plugin->acceptsMidi());
    EXPECT_NEAR(plugin->getHostedParameter(2)->getValue(), 0.02f, 1e-6);

    std::printf("[ .. ] state round trip (chunk)\n");
    juce::MemoryBlock state;
    plugin->getStateInformation(state);
    plugin->getHostedParameter(0)->setValue(0.0f);
    EXPECT_NEAR(runOnce(*plugin, 0.25f), 0.0f, 1e-6);
    plugin->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    EXPECT_NEAR(runOnce(*plugin, 0.25f), 0.5f, 1e-6);

    std::printf("[ .. ] programs\n");
    EXPECT(plugin->getNumPrograms() == 3);
    plugin->setCurrentProgram(2);
    EXPECT(plugin->getCurrentProgram() == 2);
    EXPECT(plugin->getProgramName(1) == "Preset 2");

    std::printf("[ .. ] blocks larger than prepared are split\n");
    {
        juce::AudioBuffer<float> big(2, 1000);
        for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::fill(big.getWritePointer(c), 0.25f, 1000);
        juce::MidiBuffer none;
        plugin->processBlock(big, none);
        EXPECT_NEAR(big.getSample(0, 999), 0.5f, 1e-6);
    }
    plugin->releaseResources();
    plugin.reset();

    std::printf("[ .. ] shell plugins\n");
    juce::OwnedArray<juce::PluginDescription> shell;
    format.findAllTypesForFile(shell, argv[2]);
    EXPECT(shell.size() == 2);
    if (shell.size() == 2) {
        EXPECT(shell[0]->name == "FDM Shell A" && shell[1]->name == "FDM Shell B");
        EXPECT(shell[1]->uniqueId == 1002 && shell[1]->hasSharedContainer);
        auto b = Vst2Format::create(*shell[1], 48000.0, 256, error);
        EXPECT(b != nullptr);
        if (b != nullptr) {
            b->prepareToPlay(48000.0, 256);
            EXPECT_NEAR(runOnce(*b, 0.25f), 0.75f, 1e-6);              // shell B multiplies by 3
            b->releaseResources();
        }
    }

    std::printf("[ .. ] non-plugins are rejected cleanly\n");
    {
        juce::OwnedArray<juce::PluginDescription> none;
        format.findAllTypesForFile(none, "/nonexistent/thing.so");
        EXPECT(none.isEmpty());
    }

    std::printf("[ .. ] scanning in a child process\n");
    {
        const auto self = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
        auto ok = pluginscan::scanInChildProcess(self, "VST2", argv[1], 30000, {});
        EXPECT(ok.ok && ok.found.size() == 1 && ok.found[0]->name == "FDM Test Gain");

        auto shellResult = pluginscan::scanInChildProcess(self, "VST2", argv[2], 30000, {});
        EXPECT(shellResult.ok && shellResult.found.size() == 2);

        auto crashed = pluginscan::scanInChildProcess(self, "VST2", argv[3], 30000, {});
        EXPECT(!crashed.ok && !crashed.timedOut && crashed.error.contains("crashed"));
        std::printf("    crashing plugin -> %s\n", crashed.error.toRawUTF8());

        const auto t0 = juce::Time::getMillisecondCounter();
        auto hung = pluginscan::scanInChildProcess(self, "VST2", argv[4], 1500, {});
        EXPECT(!hung.ok && hung.timedOut);
        EXPECT(juce::Time::getMillisecondCounter() - t0 < 6000);
        std::printf("    hanging plugin  -> %s\n", hung.error.toRawUTF8());

        bool abortNow = false;
        auto aborted = pluginscan::scanInChildProcess(self, "VST2", argv[4], 30000, [&] { return std::exchange(abortNow, true); });
        EXPECT(aborted.aborted);
    }

    std::printf("[ .. ] instrument plugin played by the engine, sample-accurately\n");
    {
        juce::OwnedArray<juce::PluginDescription> synths;
        format.findAllTypesForFile(synths, argv[5]);
        EXPECT(synths.size() == 1 && synths[0]->isInstrument && synths[0]->name == "FDM Test Synth");
        if (synths.size() == 1) {
            auto plugin = Vst2Format::create(*synths[0], 48000.0, 512, error);
            EXPECT(plugin != nullptr && plugin->acceptsMidi());
            if (plugin != nullptr) {
                fdm::Engine engine;
                engine.prepare(48000.0, 512);
                engine.bpm.set(120.0f);                                  // 25 samples per tick
                PluginInstrument instrument(std::move(plugin));
                instrument.prepare(48000.0, 512);
                const int ch = engine.addChannel(fdm::InstrumentType::PolySynth);
                engine.channel(ch).external.store(&instrument);
                auto& clip = engine.channel(ch).clips[0];
                clip.create(1);
                fdm::NoteEvent n;
                n.pitch = 60;
                n.start = fdm::kTicksPerBeat + 7;                        // frame 24000 + 175 = 24175, mid-block
                n.length = fdm::kTicksPerStep;                           // 6000 frames
                clip.addNote(n);
                engine.channel(ch).addPlacement({0, fdm::kTicksPerBar, 0});
                std::vector<float> L, R;
                engine.renderOffline(fdm::Engine::kRenderArrangement, 1, 0.0f, L, R);
                int first = -1, last = -1;
                for (int i = 0; i < static_cast<int>(L.size()); ++i)
                    if (std::abs(L[static_cast<size_t>(i)]) > 0.01f) { if (first < 0) first = i; last = i; }
                std::printf("    note sounds from frame %d to %d\n", first, last);
                EXPECT(first == 24175);
                EXPECT(std::abs(last - (24175 + 6000 - 1)) <= 1);
                engine.channel(ch).external.store(nullptr);
            }
        }
    }

    std::printf(failures == 0 ? "VST2 host: all checks passed\n" : "VST2 host: %d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
