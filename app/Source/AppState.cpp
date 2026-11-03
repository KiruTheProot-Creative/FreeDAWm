#include "AppState.h"

// ===========================================================================
// Construction / destruction
// ===========================================================================

AppState::AppState() {
    juce::PropertiesFile::Options options;
    options.applicationName     = "FreeDAWm";
    options.filenameSuffix      = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    options.folderName          = "FreeDAWm";
    options.storageFormat       = juce::PropertiesFile::storeAsXML;
    properties.setStorageParameters(options);

    pluginHost = std::make_unique<PluginHost>(properties.getUserSettings());
    monoScratch_.assign(fdm::kMaxBlockSize, 0.0f);
    formats_.registerBasicFormats();
    previewBuffer_.setSize(2, fdm::kMaxBlockSize);
    previewThread_.startThread();

    // Audio: restore the last device setup if there was one.
    std::unique_ptr<juce::XmlElement> saved;
    if (auto* settings = properties.getUserSettings()) saved = settings->getXmlValue("audioDevice");
    audioError_ = deviceManager.initialise(0, 2, saved.get(), true);
    attachAudio();

    // MIDI: listen to every input that is present, and play whichever channel is selected.
    for (auto& device : juce::MidiInput::getAvailableDevices()) {
        deviceManager.setMidiInputDeviceEnabled(device.identifier, true);
        deviceManager.addMidiInputDeviceCallback(device.identifier, this);
    }

    fdm::buildDemoProject(engine);
    engine.arrangementMode.store(true);
    if (const int audio = engine.addChannel(fdm::InstrumentType::Audio); audio >= 0) {
        engine.channel(audio).setName("Audio");
        engine.channel(audio).colorIndex.store(4);
    }
    selection_ = {Selection::Kind::Channel, 0, 0};
    armedChannel_.store(0);
}

AppState::~AppState() {
    saveSettings();
    for (auto& device : juce::MidiInput::getAvailableDevices())
        deviceManager.removeMidiInputDeviceCallback(device.identifier, this);
    detachAudio();
    previewTransport_.setSource(nullptr);
    previewThread_.stopThread(2000);
    editors.clear();
    clearAllEffects();
}

void AppState::saveSettings() {
    if (auto* settings = properties.getUserSettings()) {
        if (auto xml = deviceManager.createStateXml()) settings->setValue("audioDevice", xml.get());
        settings->saveIfNeeded();
    }
    if (pluginHost) pluginHost->saveSettings();
}

void AppState::attachAudio() {
    if (audioAttached_) return;
    deviceManager.addAudioCallback(this);
    audioAttached_ = true;
}

void AppState::detachAudio() {
    if (!audioAttached_) return;
    deviceManager.removeAudioCallback(this);      // blocks until the callback has finished
    engine.setAudioThreadRunning(false);
    audioAttached_ = false;
}

void AppState::notifyStructureChanged() { listeners_.call([](Listener& l) { l.structureChanged(); }); }

// ===========================================================================
// Selection
// ===========================================================================

void AppState::select(const Selection& s) {
    if (s == selection_) return;
    selection_ = s;
    armedChannel_.store(s.kind == Selection::Kind::Channel ? s.index : -1);
    listeners_.call([](Listener& l) { l.selectionChanged(); });
}

void AppState::selectChannel(int channel, int scene) { select({Selection::Kind::Channel, channel, scene}); }

fdm::Strip* AppState::selectedStrip() {
    switch (selection_.kind) {
        case Selection::Kind::Channel: return &engine.channel(selection_.index);
        case Selection::Kind::Return:  return &engine.returnStrip(selection_.index);
        case Selection::Kind::Master:  return &engine.master();
    }
    return nullptr;
}

fdm::Channel* AppState::selectedChannel() {
    return selection_.kind == Selection::Kind::Channel ? &engine.channel(selection_.index) : nullptr;
}

fdm::Clip* AppState::selectedClip() {
    auto* ch = selectedChannel();
    if (ch == nullptr || selection_.scene < 0 || selection_.scene >= fdm::kMaxScenes) return nullptr;
    auto& clip = ch->clips[static_cast<size_t>(selection_.scene)];
    return clip.exists.load() ? &clip : nullptr;
}

// ===========================================================================
// Channels
// ===========================================================================

int AppState::addChannel(fdm::InstrumentType type) {
    int index;
    {
        ScopedAudioPause pause(*this);
        index = engine.addChannel(type);
        if (index >= 0) {
            auto& ch = engine.channel(index);
            ch.setName(type == fdm::InstrumentType::DrumRack ? "Drums" : (type == fdm::InstrumentType::Audio ? "Audio" : "Synth"));
            ch.colorIndex.store(index);
        }
    }
    if (index >= 0) {
        notifyStructureChanged();
        selectChannel(index, 0);
    }
    return index;
}

void AppState::removeChannel(int index) {
    if (index < 0 || index >= fdm::kMaxChannels || engine.activeChannelCount() <= 1) return;
    {
        ScopedAudioPause pause(*this);
        auto& ch = engine.channel(index);
        closeEditorsFor(ch);
        for (int s = 0; s < fdm::kMaxFxSlots; ++s) engine.swapEffect(ch.fx[static_cast<size_t>(s)], nullptr);
        engine.removeChannel(index);
    }
    // Select something that still exists.
    int next = 0;
    for (int i = 0; i < fdm::kMaxChannels; ++i)
        if (engine.channel(i).active.load()) { next = i; break; }
    notifyStructureChanged();
    selectChannel(next, 0);
}

// ===========================================================================
// Effects
// ===========================================================================

Vst3Effect* AppState::effectAt(fdm::Strip& strip, int slot) {
    if (slot < 0 || slot >= fdm::kMaxFxSlots) return nullptr;
    return dynamic_cast<Vst3Effect*>(strip.fx[static_cast<size_t>(slot)].effect.load());
}

int AppState::nextFreeFxSlot(fdm::Strip& strip) {
    int last = -1;
    for (int s = 0; s < fdm::kMaxFxSlots; ++s)
        if (strip.fx[static_cast<size_t>(s)].effect.load() != nullptr) last = s;
    if (last + 1 < fdm::kMaxFxSlots) return last + 1;
    for (int s = 0; s < fdm::kMaxFxSlots; ++s)
        if (strip.fx[static_cast<size_t>(s)].effect.load() == nullptr) return s;
    return -1;
}

int AppState::effectCount(fdm::Strip& strip) {
    int n = 0;
    for (auto& slot : strip.fx) n += slot.effect.load() != nullptr ? 1 : 0;
    return n;
}

juce::String AppState::loadEffect(fdm::Strip& strip, int slot, const juce::PluginDescription& description,
                                  const juce::MemoryBlock* savedState) {
    if (slot < 0 || slot >= fdm::kMaxFxSlots) return "Invalid slot.";

    juce::String error;
    auto effect = pluginHost->createEffect(description, engine.sampleRate(), blockSize_, error);
    if (effect == nullptr) return error;

    if (savedState != nullptr && savedState->getSize() > 0)
        static_cast<Vst3Effect*>(effect.get())->plugin().setStateInformation(savedState->getData(),
                                                                            static_cast<int>(savedState->getSize()));

    if (auto* old = effectAt(strip, slot)) editors.erase(old);     // close its window first
    auto old = engine.swapEffect(strip.fx[static_cast<size_t>(slot)], std::move(effect));
    strip.fx[static_cast<size_t>(slot)].bypass.store(false);
    old.reset();
    notifyStructureChanged();
    return {};
}

void AppState::clearEffect(fdm::Strip& strip, int slot) {
    if (slot < 0 || slot >= fdm::kMaxFxSlots) return;
    if (auto* fx = effectAt(strip, slot)) editors.erase(fx);
    auto old = engine.swapEffect(strip.fx[static_cast<size_t>(slot)], nullptr);
    old.reset();
    notifyStructureChanged();
}

void AppState::showEffectEditor(fdm::Strip& strip, int slot) {
    auto* fx = effectAt(strip, slot);
    if (fx == nullptr) return;
    auto it = editors.find(fx);
    if (it != editors.end()) { it->second->toFront(true); return; }
    editors[fx] = std::make_unique<PluginWindow>(*fx, [this, fx] {
        juce::MessageManager::callAsync([this, fx] { editors.erase(fx); });
    });
}

void AppState::closeEditorsFor(fdm::Strip& strip) {
    for (auto& slot : strip.fx)
        if (auto* fx = dynamic_cast<Vst3Effect*>(slot.effect.load())) editors.erase(fx);
}

void AppState::clearAllEffects() {
    auto clear = [this](fdm::Strip& s) {
        for (auto& slot : s.fx) {
            auto old = engine.swapEffect(slot, nullptr);
            old.reset();
        }
    };
    for (int i = 0; i < fdm::kMaxChannels; ++i) clear(engine.channel(i));
    for (int r = 0; r < fdm::kNumReturns; ++r) clear(engine.returnStrip(r));
    clear(engine.master());
}

// ===========================================================================
// Live notes
// ===========================================================================

void AppState::previewNoteOn(int note, int velocity) {
    const int ch = armedChannel_.load();
    if (ch >= 0) engine.liveNoteOn(ch, note, velocity);
}

void AppState::previewNoteOff(int note) {
    const int ch = armedChannel_.load();
    if (ch >= 0) engine.liveNoteOff(ch, note);
}

void AppState::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) {
    const int ch = armedChannel_.load();
    if (ch < 0) return;
    if (m.isNoteOn())       engine.liveNoteOn(ch, m.getNoteNumber(), m.getVelocity());
    else if (m.isNoteOff()) engine.liveNoteOff(ch, m.getNoteNumber());
}

// ===========================================================================
// Audio callback
// ===========================================================================

void AppState::audioDeviceAboutToStart(juce::AudioIODevice* device) {
    blockSize_ = juce::jmax(64, device->getCurrentBufferSizeSamples());
    engine.prepare(device->getCurrentSampleRate(), blockSize_);
    previewTransport_.prepareToPlay(blockSize_, device->getCurrentSampleRate());
    engine.setAudioThreadRunning(true);
}

void AppState::audioDeviceStopped() {
    engine.setAudioThreadRunning(false);
    previewTransport_.releaseResources();
}

void AppState::audioDeviceIOCallbackWithContext(const float* const*, int, float* const* output, int numOutputs,
                                                int numSamples, const juce::AudioIODeviceCallbackContext&) {
    juce::ScopedNoDenormals noDenormals;

    if (numOutputs >= 2) {
        engine.process(output[0], output[1], numSamples);
        if (previewTransport_.isPlaying() && numSamples <= previewBuffer_.getNumSamples()) {
            previewBuffer_.clear();
            juce::AudioSourceChannelInfo info(&previewBuffer_, 0, numSamples);
            previewTransport_.getNextAudioBlock(info);
            juce::FloatVectorOperations::add(output[0], previewBuffer_.getReadPointer(0), numSamples);
            juce::FloatVectorOperations::add(output[1], previewBuffer_.getReadPointer(1), numSamples);
        }
        for (int c = 2; c < numOutputs; ++c)
            if (output[c] != nullptr) juce::FloatVectorOperations::clear(output[c], numSamples);
    } else if (numOutputs == 1) {
        for (int done = 0; done < numSamples;) {
            const int n = juce::jmin(numSamples - done, static_cast<int>(monoScratch_.size()));
            engine.process(output[0] + done, monoScratch_.data(), n);
            for (int i = 0; i < n; ++i) output[0][done + i] = 0.5f * (output[0][done + i] + monoScratch_[static_cast<size_t>(i)]);
            done += n;
        }
    }
}

// ===========================================================================
// Audio tracks
// ===========================================================================

int AppState::importSample(const juce::File& file, juce::String* error) {
    const auto path = file.getFullPathName().toStdString();
    if (const int existing = engine.findSample(path); existing >= 0) return existing;
    auto fail = [error](const juce::String& msg) { if (error) *error = msg; return -1; };

    if (!file.existsAsFile()) return fail("The file is missing.");
    std::unique_ptr<juce::AudioFormatReader> reader(formats_.createReaderFor(file));
    if (reader == nullptr) return fail("That isn't an audio file FreeDAWm can read.");
    if (reader->lengthInSamples <= 1) return fail("The file is empty.");
    if (static_cast<double>(reader->lengthInSamples) / reader->sampleRate > 20.0 * 60.0)
        return fail("Files longer than 20 minutes aren't supported yet.");

    const int frames = static_cast<int>(reader->lengthInSamples);
    juce::AudioBuffer<float> buffer(2, frames);
    reader->read(&buffer, 0, frames, 0, true, true);       // a mono file fills both channels

    auto sample = std::make_unique<fdm::AudioSample>();
    sample->path = path;
    sample->name = file.getFileNameWithoutExtension().toStdString();
    sample->sampleRate = reader->sampleRate;
    sample->left.assign(buffer.getReadPointer(0), buffer.getReadPointer(0) + frames);
    sample->right.assign(buffer.getReadPointer(1), buffer.getReadPointer(1) + frames);
    sample->computePeaks();
    const int index = engine.addSample(std::move(sample));
    if (index < 0) return fail("The project already uses the maximum of " + juce::String(fdm::kMaxSamples) + " sound files.");
    return index;
}

int AppState::sampleLengthTicks(int sampleIndex) {
    const auto* s = engine.sample(sampleIndex);
    if (s == nullptr) return fdm::kTicksPerBeat;
    return std::max(1, static_cast<int>(std::ceil(s->seconds() * static_cast<double>(engine.bpm.get()) / 60.0 * fdm::kTicksPerBeat)));
}

int AppState::addAudioClip(int channel, const juce::File& file, int64_t startTick) {
    if (channel < 0 || channel >= fdm::kMaxChannels || !engine.channel(channel).isAudio()) return -1;
    juce::String error;
    const int sample = importSample(file, &error);
    if (sample < 0) {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Couldn't add " + file.getFileName(), error);
        return -1;
    }
    const int index = engine.channel(channel).addAudioClip({static_cast<int>(startTick), sampleLengthTicks(sample), sample});
    notifyStructureChanged();
    return index;
}

// ===========================================================================
// Samples
// ===========================================================================

juce::StringArray AppState::sampleFolders() {
    juce::StringArray folders;
    if (auto* settings = properties.getUserSettings()) folders.addTokens(settings->getValue("sampleFolders"), "|", "");
    folders.removeEmptyStrings();
    if (folders.isEmpty()) {
        auto def = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("FreeDAWm").getChildFile("Samples");
        def.createDirectory();
        folders.add(def.getFullPathName());
    }
    return folders;
}

void AppState::addSampleFolder(const juce::File& folder) {
    auto folders = sampleFolders();
    folders.addIfNotAlreadyThere(folder.getFullPathName());
    if (auto* settings = properties.getUserSettings()) { settings->setValue("sampleFolders", folders.joinIntoString("|")); settings->saveIfNeeded(); }
}

void AppState::removeSampleFolder(const juce::String& path) {
    auto folders = sampleFolders();
    folders.removeString(path);
    if (auto* settings = properties.getUserSettings()) { settings->setValue("sampleFolders", folders.joinIntoString("|")); settings->saveIfNeeded(); }
}

void AppState::previewSample(const juce::File& file) {
    previewTransport_.stop();
    previewTransport_.setSource(nullptr);
    previewSource_.reset();
    if (!file.existsAsFile()) return;
    std::unique_ptr<juce::AudioFormatReader> reader(formats_.createReaderFor(file));
    if (reader == nullptr) return;
    const double rate = reader->sampleRate;
    previewSource_ = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);
    previewTransport_.setSource(previewSource_.get(), 32768, &previewThread_, rate);
    previewTransport_.setPosition(0.0);
    previewTransport_.start();
}

void AppState::stopSamplePreview() { previewTransport_.stop(); }

// ===========================================================================
// Projects
// ===========================================================================

juce::String AppState::projectName() const {
    return projectFile_ == juce::File() ? juce::String("Untitled") : projectFile_.getFileNameWithoutExtension();
}

void AppState::resetMixerAndTransport() {
    engine.stop();
    engine.bpm.set(120.0f);
    engine.swing.set(0.0f);
    engine.launchQuantizeTicks.store(fdm::kTicksPerBar);
    engine.arrangementMode.store(true);
    engine.loopEnabled.store(false);
    engine.loopStartTick.store(0);
    engine.loopEndTick.store(fdm::kTicksPerBar * 4);
    engine.setPlayhead(0);
    for (int r = 0; r < fdm::kNumReturns; ++r) {
        auto& s = engine.returnStrip(r);
        s.volumeDb.store(0.0f);
        s.pan.store(0.0f);
        s.mute.store(false);
    }
    engine.master().volumeDb.store(0.0f);
    engine.master().pan.store(0.0f);
}

void AppState::newProject() {
    {
        ScopedAudioPause pause(*this);
        editors.clear();
        clearAllEffects();
        for (int i = 0; i < fdm::kMaxChannels; ++i) engine.removeChannel(i);
        engine.clearSamples();
        resetMixerAndTransport();

        const int audio = engine.addChannel(fdm::InstrumentType::Audio);
        engine.channel(audio).setName("Audio");
        engine.channel(audio).colorIndex.store(2);
        const int drums = engine.addChannel(fdm::InstrumentType::DrumRack);
        const int synth = engine.addChannel(fdm::InstrumentType::PolySynth);
        engine.channel(drums).setName("Drums");
        engine.channel(drums).colorIndex.store(0);
        engine.channel(synth).setName("Synth");
        engine.channel(synth).colorIndex.store(1);
    }
    projectFile_ = juce::File();
    notifyStructureChanged();
    selection_ = {Selection::Kind::Channel, -1, 0};      // force a selection change below
    selectChannel(0, 0);
}

namespace {

using juce::DynamicObject;
using juce::var;

var paramsToVar(fdm::ParamList& list) {
    auto obj = new DynamicObject();
    for (int i = 0; i < list.size(); ++i) obj->setProperty(juce::String(list.at(i).id()), list.at(i).get());
    return var(obj);
}

void varToParams(const var& v, fdm::ParamList& list) {
    if (auto* obj = v.getDynamicObject())
        for (auto& prop : obj->getProperties())
            if (auto* p = list.find(prop.name.toString().toStdString())) p->set(static_cast<float>(static_cast<double>(prop.value)));
}

}  // namespace

juce::Result AppState::saveProject(const juce::File& file) {
    auto stripToVar = [this](fdm::Strip& s) {
        auto obj = new DynamicObject();
        obj->setProperty("name", juce::String(s.name));
        obj->setProperty("volumeDb", s.volumeDb.load());
        obj->setProperty("pan", s.pan.load());
        obj->setProperty("mute", s.mute.load());

        juce::Array<var> fx;
        for (int slot = 0; slot < fdm::kMaxFxSlots; ++slot) {
            auto* effect = effectAt(s, slot);
            if (effect == nullptr) continue;
            auto f = new DynamicObject();
            f->setProperty("slot", slot);
            f->setProperty("bypass", s.fx[static_cast<size_t>(slot)].bypass.load());
            if (auto xml = effect->plugin().getPluginDescription().createXml()) f->setProperty("plugin", xml->toString());
            juce::MemoryBlock state;
            effect->plugin().getStateInformation(state);
            f->setProperty("state", state.toBase64Encoding());
            fx.add(var(f));
        }
        obj->setProperty("fx", fx);
        return obj;
    };

    auto root = new DynamicObject();
    root->setProperty("format", "FreeDAWm project");
    root->setProperty("version", 1);
    root->setProperty("bpm", engine.bpm.get());
    root->setProperty("swing", engine.swing.get());
    root->setProperty("launchQuantizeTicks", engine.launchQuantizeTicks.load());
    juce::Array<var> samples;                        // [poolIndex, path] pairs
    for (int si = 0; si < fdm::kMaxSamples; ++si)
        if (auto* smp = engine.sample(si)) {
            juce::Array<var> entry;
            entry.add(si); entry.add(juce::String(smp->path));
            samples.add(var(entry));
        }
    root->setProperty("samples", samples);
    root->setProperty("loopEnabled", engine.loopEnabled.load());
    root->setProperty("loopStart", engine.loopStartTick.load());
    root->setProperty("loopEnd", engine.loopEndTick.load());

    juce::Array<var> channels;
    for (int i = 0; i < fdm::kMaxChannels; ++i) {
        auto& ch = engine.channel(i);
        if (!ch.active.load()) continue;

        auto obj = stripToVar(ch);
        const bool drums = ch.instrumentType.load() == static_cast<int>(fdm::InstrumentType::DrumRack);
        obj->setProperty("instrument", ch.isAudio() ? "audio" : (drums ? "drums" : "synth"));
        juce::Array<var> audioClips;
        for (int ai = 0; ai < fdm::kMaxPlacements; ++ai) {
            fdm::AudioPlacement a;
            if (!ch.getAudioClip(ai, a)) continue;
            juce::Array<var> entry;
            entry.add(a.startTick); entry.add(a.lengthTicks); entry.add(a.sample);
            audioClips.add(var(entry));
        }
        obj->setProperty("audioClips", audioClips);
        obj->setProperty("color", ch.colorIndex.load());
        obj->setProperty("solo", ch.solo.load());
        juce::Array<var> sends;
        for (auto& s : ch.sends) sends.add(s.load());
        obj->setProperty("sends", sends);
        obj->setProperty("params", paramsToVar(drums ? ch.drums.params() : ch.synth.params()));

        juce::Array<var> clips;
        for (int slot = 0; slot < fdm::kMaxScenes; ++slot) {
            auto& clip = ch.clips[static_cast<size_t>(slot)];
            if (!clip.exists.load()) continue;
            auto c = new DynamicObject();
            c->setProperty("slot", slot);
            c->setProperty("name", juce::String(clip.name));
            c->setProperty("length", clip.lengthTicks.load());
            juce::Array<var> notes;
            for (int n = 0; n < fdm::kMaxNotesPerClip; ++n) {
                fdm::NoteEvent e;
                if (!clip.getNote(n, e)) continue;
                juce::Array<var> note;
                note.add(e.start); note.add(e.length); note.add(e.pitch); note.add(e.velocity);
                notes.add(var(note));
            }
            c->setProperty("notes", notes);
            clips.add(var(c));
        }
        obj->setProperty("clips", clips);

        juce::Array<var> placements;
        for (int pi = 0; pi < fdm::kMaxPlacements; ++pi) {
            fdm::Placement pl;
            if (!ch.getPlacement(pi, pl)) continue;
            juce::Array<var> entry;
            entry.add(pl.startTick); entry.add(pl.lengthTicks); entry.add(pl.slot);
            placements.add(var(entry));
        }
        obj->setProperty("placements", placements);
        channels.add(var(obj));
    }
    root->setProperty("channels", channels);

    juce::Array<var> returns;
    for (int r = 0; r < fdm::kNumReturns; ++r) returns.add(var(stripToVar(engine.returnStrip(r))));
    root->setProperty("returns", returns);
    root->setProperty("master", var(stripToVar(engine.master())));

    const juce::String json = juce::JSON::toString(var(root), false);
    if (!file.replaceWithText(json)) return juce::Result::fail("Couldn't write " + file.getFullPathName());
    projectFile_ = file;
    return juce::Result::ok();
}

juce::Result AppState::loadProject(const juce::File& file) {
    if (!file.existsAsFile()) return juce::Result::fail("That file doesn't exist.");
    const var root = juce::JSON::parse(file);
    if (!root.isObject() || root["format"].toString() != "FreeDAWm project")
        return juce::Result::fail("That isn't a FreeDAWm project file.");

    juce::StringArray problems;

    auto restoreStrip = [this, &problems](const var& v, fdm::Strip& s) {
        if (v["name"].toString().isNotEmpty()) s.setName(v["name"].toString().toRawUTF8());
        s.volumeDb.store(static_cast<float>(static_cast<double>(v["volumeDb"])));
        s.pan.store(static_cast<float>(static_cast<double>(v["pan"])));
        s.mute.store(static_cast<bool>(v["mute"]));

        if (auto* fx = v["fx"].getArray()) {
            for (auto& f : *fx) {
                const int slot = f["slot"];
                std::unique_ptr<juce::XmlElement> xml = juce::XmlDocument::parse(f["plugin"].toString());
                juce::PluginDescription desc;
                if (xml == nullptr || !desc.loadFromXml(*xml)) { problems.add("A plugin entry was unreadable."); continue; }
                juce::MemoryBlock state;
                state.fromBase64Encoding(f["state"].toString());
                const auto error = loadEffect(s, slot, desc, &state);
                if (error.isNotEmpty()) problems.add(desc.name + ": " + error);
                else s.fx[static_cast<size_t>(slot)].bypass.store(static_cast<bool>(f["bypass"]));
            }
        }
    };

    {
        ScopedAudioPause pause(*this);
        editors.clear();
        clearAllEffects();
        for (int i = 0; i < fdm::kMaxChannels; ++i) engine.removeChannel(i);
        resetMixerAndTransport();

        engine.bpm.set(static_cast<float>(static_cast<double>(root["bpm"])));
        engine.swing.set(static_cast<float>(static_cast<double>(root["swing"])));
        engine.launchQuantizeTicks.store(static_cast<int>(root["launchQuantizeTicks"]));
        engine.clearSamples();
        engine.arrangementMode.store(true);
        std::map<int, int> sampleMap;                  // saved pool index -> new pool index
        if (auto* samples = root["samples"].getArray())
            for (auto& sv : *samples)
                if (auto* a = sv.getArray(); a != nullptr && a->size() >= 2) {
                    juce::String err;
                    const juce::File f((*a)[1].toString());
                    const int idx = importSample(f, &err);
                    if (idx >= 0) sampleMap[static_cast<int>((*a)[0])] = idx;
                    else problems.add(f.getFileName() + ": " + err);
                }
        engine.loopStartTick.store(root.hasProperty("loopStart") ? static_cast<int>(root["loopStart"]) : 0);
        engine.loopEndTick.store(root.hasProperty("loopEnd") ? static_cast<int>(root["loopEnd"]) : fdm::kTicksPerBar * 4);
        engine.loopEnabled.store(static_cast<bool>(root["loopEnabled"]));

        if (auto* channels = root["channels"].getArray()) {
            for (auto& cv : *channels) {
                const bool drums = cv["instrument"].toString() == "drums";
                const bool audio = cv["instrument"].toString() == "audio";
                const int index = engine.addChannel(audio ? fdm::InstrumentType::Audio
                                                          : (drums ? fdm::InstrumentType::DrumRack : fdm::InstrumentType::PolySynth));
                if (index < 0) { problems.add("Too many channels."); break; }
                auto& ch = engine.channel(index);
                restoreStrip(cv, ch);
                ch.colorIndex.store(static_cast<int>(cv["color"]));
                ch.solo.store(static_cast<bool>(cv["solo"]));
                if (auto* sends = cv["sends"].getArray())
                    for (int s = 0; s < fdm::kNumReturns && s < sends->size(); ++s)
                        ch.sends[static_cast<size_t>(s)].store(static_cast<float>(static_cast<double>((*sends)[s])));
                varToParams(cv["params"], drums ? ch.drums.params() : ch.synth.params());
                if (auto* audioClips = cv["audioClips"].getArray())
                    for (auto& av : *audioClips)
                        if (auto* a = av.getArray(); a != nullptr && a->size() >= 3)
                            if (auto it = sampleMap.find(static_cast<int>((*a)[2])); it != sampleMap.end())
                                ch.addAudioClip({static_cast<int>((*a)[0]), static_cast<int>((*a)[1]), it->second});
                if (auto* placements = cv["placements"].getArray())
                    for (auto& pv : *placements)
                        if (auto* a = pv.getArray(); a != nullptr && a->size() >= 3)
                            ch.addPlacement({static_cast<int>((*a)[0]), static_cast<int>((*a)[1]), static_cast<int>((*a)[2])});

                if (auto* clips = cv["clips"].getArray()) {
                    for (auto& clipVar : *clips) {
                        const int slot = clipVar["slot"];
                        if (slot < 0 || slot >= fdm::kMaxScenes) continue;
                        auto& clip = ch.clips[static_cast<size_t>(slot)];
                        clip.create(1);
                        clip.setLengthTicks(static_cast<int>(clipVar["length"]));
                        std::snprintf(clip.name, sizeof(clip.name), "%s", clipVar["name"].toString().toRawUTF8());
                        if (auto* notes = clipVar["notes"].getArray()) {
                            for (auto& n : *notes) {
                                if (auto* a = n.getArray(); a != nullptr && a->size() >= 4) {
                                    fdm::NoteEvent e;
                                    e.start = (*a)[0]; e.length = (*a)[1]; e.pitch = (*a)[2]; e.velocity = (*a)[3];
                                    clip.addNote(e);
                                }
                            }
                        }
                    }
                }
            }
        }
        if (auto* returns = root["returns"].getArray())
            for (int r = 0; r < fdm::kNumReturns && r < returns->size(); ++r) restoreStrip((*returns)[r], engine.returnStrip(r));
        restoreStrip(root["master"], engine.master());
    }

    projectFile_ = file;
    notifyStructureChanged();
    selection_ = {Selection::Kind::Channel, -1, 0};
    selectChannel(0, 0);

    if (problems.isEmpty()) return juce::Result::ok();
    return juce::Result::fail("The project loaded, but:\n" + problems.joinIntoString("\n"));
}

juce::Result AppState::exportWav(const juce::File& file, int scene, int bars, fdm::WavFormat format) {
    std::vector<float> left, right;
    const double sampleRate = engine.sampleRate();
    {
        ScopedAudioPause pause(*this);          // nothing else may touch the engine while we render
        engine.renderOffline(scene, bars, 2.0f, left, right);
    }
    if (!fdm::writeWav(file.getFullPathName().toStdString(), left, right, static_cast<int>(sampleRate), format))
        return juce::Result::fail("Couldn't write " + file.getFullPathName());
    return juce::Result::ok();
}
