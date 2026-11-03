#include "Vst2Format.h"

#include <cstring>
#include <map>
#include <mutex>

#include "BridgedPlugin.h"
#include "Vst2Abi.h"

using namespace fdm::vst2;

namespace {

thread_local i32  tlShellIdToLoad = 0;        // answered to host::currentId while a shell's sub-plugin is created
thread_local bool tlInAudioCallback = false;
thread_local char tlPluginDirectory[1024] = {};  // answered to host::getDirectory while a plugin is created

/** Answered to host::getTime before an instance exists: some plugins read it without checking for null. */
TimeInfo& defaultTime() {
    static TimeInfo t = [] {
        TimeInfo x {};
        x.sampleRate = 44100.0;
        x.tempo = 120.0;
        x.timeSigNumerator = 4;
        x.timeSigDenominator = 4;
        x.flags = time::tempoValid | time::timeSigValid | time::ppqValid;
        return x;
    }();
    return t;
}

iptr FDM_VST2_CALL hostCallback(Plugin* p, i32 opcode, i32 index, iptr value, void* ptr, float opt);

/** Plugin strings are fixed-size C strings, usually ASCII, sometimes Latin-1 or UTF-8. */
juce::String textFrom(const char* s, size_t maxLen) {
    size_t n = 0;
    while (n < maxLen && s[n] != 0) ++n;
    if (juce::CharPointer_UTF8::isValidString(s, static_cast<int>(n))) return juce::String::fromUTF8(s, static_cast<int>(n)).trim();
    juce::String r;
    for (size_t i = 0; i < n; ++i) r += juce::String::charToString(static_cast<juce::juce_wchar>(static_cast<unsigned char>(s[i])));
    return r.trim();
}

// ---------------------------------------------------------------------------
// Loading the library
// ---------------------------------------------------------------------------
juce::File binaryFor(const juce::File& file) {
    if (!file.isDirectory()) return file;
    // macOS bundle: Contents/MacOS/<CFBundleExecutable>
    const auto macos = file.getChildFile("Contents").getChildFile("MacOS");
    const auto plist = file.getChildFile("Contents").getChildFile("Info.plist");
    if (plist.existsAsFile()) {
        const auto text = plist.loadFileAsString();
        const int at = text.indexOf("<key>CFBundleExecutable</key>");
        if (at >= 0) {
            const auto exe = text.substring(at).fromFirstOccurrenceOf("<string>", false, false).upToFirstOccurrenceOf("</string>", false, false).trim();
            if (exe.isNotEmpty() && macos.getChildFile(exe).existsAsFile()) return macos.getChildFile(exe);
        }
    }
    const auto files = macos.findChildFiles(juce::File::findFiles, false);
    return files.isEmpty() ? juce::File() : files[0];
}

struct Module {
    std::unique_ptr<juce::DynamicLibrary> library;
    EntryFn entry = nullptr;

    /** Libraries are loaded once and never unloaded: many plugins (Native Instruments, for one) keep
        background threads running after they're closed, and unloading their code under those threads crashes. */
    static std::shared_ptr<Module> load(const juce::File& file, juce::String& error) {
        static std::mutex lock;
        static std::map<juce::String, std::shared_ptr<Module>> cache;
        const std::lock_guard<std::mutex> guard(lock);
        if (auto it = cache.find(file.getFullPathName()); it != cache.end()) return it->second;
        auto m = loadUncached(file, error);
        if (m != nullptr) {
            m->directory = file.getParentDirectory().getFullPathName();
            cache[file.getFullPathName()] = m;
        }
        return m;
    }

    juce::String directory;

    static std::shared_ptr<Module> loadUncached(const juce::File& file, juce::String& error) {
        const auto binary = binaryFor(file);
        if (!binary.existsAsFile()) { error = "The plugin file is missing."; return nullptr; }
        auto m = std::make_shared<Module>();
        m->library = std::make_unique<juce::DynamicLibrary>();
        if (!m->library->open(binary.getFullPathName())) {
            error = "The library couldn't be loaded. It may be 32-bit, built for another system, or missing a dependency.";
            return nullptr;
        }
        for (const char* name : {"VSTPluginMain", "main_macho", "main"})
            if (auto* fn = m->library->getFunction(name)) { m->entry = reinterpret_cast<EntryFn>(fn); break; }
        if (m->entry == nullptr) { error = "This file isn't a VST2 plugin."; return nullptr; }
        return m;
    }

    Plugin* instantiate(i32 shellId) {
        tlShellIdToLoad = shellId;
        directory.copyToUTF8(tlPluginDirectory, sizeof(tlPluginDirectory));
        Plugin* p = entry(hostCallback);
        tlShellIdToLoad = 0;
        if (p == nullptr || p->magic != kMagic) return nullptr;
        p->hostReserved1 = 0;
        return p;
    }
};

iptr call(Plugin* p, i32 opcode, i32 index = 0, iptr value = 0, void* ptr = nullptr, float opt = 0.0f) {
    return p->dispatch(p, opcode, index, value, ptr, opt);
}

juce::String query(Plugin* p, i32 opcode, i32 index = 0) {
    char buf[512] = {};                              // generous: many plugins overrun the old limits
    call(p, opcode, index, 0, buf);
    return textFrom(buf, sizeof(buf) - 1);
}

bool canDo(Plugin* p, const char* what) { return call(p, op::canDo, 0, 0, const_cast<char*>(what)) > 0; }

const char* categoryName(i32 c) {
    switch (c) {
        case category::effect:      return "Effect";
        case category::instrument:  return "Instrument";
        case category::analysis:    return "Analyzer";
        case category::mastering:   return "Mastering";
        case category::spatial:     return "Spatial";
        case category::roomFx:      return "Reverb";
        case category::restoration: return "Restoration";
        case category::generator:   return "Generator";
        default:                    return "Effect";
    }
}

void describe(Plugin* p, const juce::File& file, i32 shellId, const juce::String& shellName, juce::PluginDescription& d) {
    const i32 cat = static_cast<i32>(call(p, op::getCategory));
    d.pluginFormatName  = Vst2Format::formatName();
    d.fileOrIdentifier  = file.getFullPathName();
    d.lastFileModTime   = file.getLastModificationTime();
    d.lastInfoUpdateTime = juce::Time::getCurrentTime();
    d.name              = shellName.isNotEmpty() ? shellName : query(p, op::getEffectName);
    if (d.name.isEmpty()) d.name = query(p, op::getProduct);
    if (d.name.isEmpty()) d.name = file.getFileNameWithoutExtension();
    d.descriptiveName   = d.name;
    d.manufacturerName  = query(p, op::getVendor);
    d.version           = juce::String(static_cast<int>(call(p, op::getVendorVersion)));
    d.category          = categoryName(cat);
    d.isInstrument      = (p->flags & flag::isInstrument) != 0 || cat == category::instrument;
    d.numInputChannels  = p->numInputs;
    d.numOutputChannels = p->numOutputs;
    d.uniqueId          = shellId != 0 ? shellId : p->uniqueId;
    d.deprecatedUid     = d.uniqueId;
    d.hasSharedContainer = shellId != 0;
}

// ---------------------------------------------------------------------------
// The plugin instance
// ---------------------------------------------------------------------------
class Vst2Instance;

class Vst2Editor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit Vst2Editor(Vst2Instance& owner);
    ~Vst2Editor() override;
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colours::black); }
    void parentHierarchyChanged() override { tryOpen(); }
    void visibilityChanged() override { tryOpen(); }
    void resizeTo(int w, int h) { if (w > 0 && h > 0) setSize(w, h); }

private:
    void tryOpen();
    void timerCallback() override;
    Vst2Instance& owner_;
    bool open_ = false;
};

class Vst2Instance final : public juce::AudioPluginInstance, private juce::AsyncUpdater {
public:
    Vst2Instance(std::shared_ptr<Module> module, Plugin* plugin, const juce::PluginDescription& desc)
        : juce::AudioPluginInstance(busesFor(plugin)), module_(std::move(module)), p_(plugin), desc_(desc) {
        p_->hostReserved1 = reinterpret_cast<iptr>(this);
        for (int i = 0; i < p_->numParams; ++i) addHostedParameter(std::make_unique<Param>(*this, i));
        acceptsMidi_ = desc_.isInstrument || canDo(p_, "receiveVstMidiEvent") || canDo(p_, "receiveVstEvents");
        producesMidi_ = canDo(p_, "sendVstMidiEvent");
        eventBytes_.assign(sizeof(Events) + kMaxEvents * sizeof(void*), 0);
        eventStore_.resize(kMaxEvents);
        std::memset(&time_, 0, sizeof(time_));
        setLatencySamples(p_->latency);
    }

    ~Vst2Instance() override {
        cancelPendingUpdate();
        if (active_) { call(p_, op::stopProcessing); call(p_, op::setActive, 0, 0); }
        call(p_, op::close);                         // the plugin frees itself here
        p_ = nullptr;
    }

    Plugin* plugin() const { return p_; }

    // ---- AudioProcessor -------------------------------------------------------------
    const juce::String getName() const override { return desc_.name; }

    bool isBusesLayoutSupported(const BusesLayout& layout) const override {
        return layout.getMainInputChannels() <= 2 && layout.getMainOutputChannels() <= 2;
    }

    void prepareToPlay(double sampleRate, int blockSize) override {
        if (active_) { call(p_, op::stopProcessing); call(p_, op::setActive, 0, 0); }
        maxBlock_ = std::max(16, blockSize);
        call(p_, op::setSampleRate, 0, 0, nullptr, static_cast<float>(sampleRate));
        call(p_, op::setBlockSize, 0, maxBlock_);
        call(p_, op::setActive, 0, 1);
        call(p_, op::startProcessing);
        active_ = true;

        ins_.assign(static_cast<size_t>(std::max(1, p_->numInputs)), std::vector<float>(static_cast<size_t>(maxBlock_), 0.0f));
        outs_.assign(static_cast<size_t>(std::max(1, p_->numOutputs)), std::vector<float>(static_cast<size_t>(maxBlock_), 0.0f));
        inPtrs_.clear();
        outPtrs_.clear();
        for (auto& v : ins_) inPtrs_.push_back(v.data());
        for (auto& v : outs_) outPtrs_.push_back(v.data());
        time_.sampleRate = sampleRate;
        time_.samplePos = 0.0;
        setLatencySamples(p_->latency);
    }

    void releaseResources() override {
        if (!active_) return;
        call(p_, op::stopProcessing);
        call(p_, op::setActive, 0, 0);
        active_ = false;
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override {
        if (!active_) { buffer.clear(); return; }
        juce::ScopedNoDenormals noDenormals;
        tlInAudioCallback = true;
        const int n = buffer.getNumSamples();
        const int chans = buffer.getNumChannels();
        updateTime();
        sendMidi(midi);

        const bool replacing = (p_->flags & flag::canReplace) != 0 && p_->processReplacing != nullptr;
        for (int done = 0; done < n;) {
            const int k = std::min(n - done, maxBlock_);
            for (int i = 0; i < p_->numInputs; ++i) {
                auto& in = ins_[static_cast<size_t>(i)];
                if (chans > 0) std::memcpy(in.data(), buffer.getReadPointer(std::min(i, chans - 1), done), sizeof(float) * static_cast<size_t>(k));
                else std::fill_n(in.data(), k, 0.0f);
            }
            for (auto& out : outs_) std::fill_n(out.data(), k, 0.0f);

            if (replacing) p_->processReplacing(p_, inPtrs_.data(), outPtrs_.data(), k);
            else if (p_->processAccumulating != nullptr) p_->processAccumulating(p_, inPtrs_.data(), outPtrs_.data(), k);

            for (int c = 0; c < chans; ++c) {
                if (p_->numOutputs > 0) buffer.copyFrom(c, done, outs_[static_cast<size_t>(std::min(c, p_->numOutputs - 1))].data(), k);
                else buffer.clear(c, done, k);
            }
            done += k;
        }
        time_.samplePos += n;
        midi.clear();
        tlInAudioCallback = false;
    }

    double getTailLengthSeconds() const override {
        const auto tail = call(p_, op::getTailSamples);
        return tail > 1 && getSampleRate() > 0.0 ? static_cast<double>(tail) / getSampleRate() : 0.0;
    }
    bool acceptsMidi() const override { return acceptsMidi_; }
    bool producesMidi() const override { return producesMidi_; }

    bool hasEditor() const override { return (p_->flags & flag::hasEditor) != 0; }
    juce::AudioProcessorEditor* createEditor() override {
        if (!hasEditor()) return nullptr;
        auto* e = new Vst2Editor(*this);
        editor_ = e;
        return e;
    }

    int getNumPrograms() override { return std::max(1, p_->numPrograms); }
    int getCurrentProgram() override { return static_cast<int>(call(p_, op::getProgram)); }
    void setCurrentProgram(int index) override {
        if (index >= 0 && index < p_->numPrograms) call(p_, op::setProgram, 0, index);
    }
    const juce::String getProgramName(int index) override {
        char buf[512] = {};
        if (call(p_, op::getProgramNameAt, index, -1, buf) > 0) return textFrom(buf, sizeof(buf) - 1);
        if (index == getCurrentProgram()) return query(p_, op::getProgramName);
        return "Program " + juce::String(index + 1);
    }
    void changeProgramName(int index, const juce::String& name) override {
        if (index != getCurrentProgram()) return;
        char buf[25] = {};
        name.copyToUTF8(buf, sizeof(buf));
        call(p_, op::setProgramName, 0, 0, buf);
    }

    // State: "FDV2", version, program, then the plugin's own chunk or every parameter value.
    void getStateInformation(juce::MemoryBlock& dest) override {
        juce::MemoryOutputStream out(dest, false);
        out.writeInt(kStateMagic);
        out.writeInt(1);
        out.writeInt(getCurrentProgram());
        if ((p_->flags & flag::chunks) != 0) {
            void* data = nullptr;
            const auto size = call(p_, op::getChunk, 0, 0, &data);
            out.writeInt(1);
            out.writeInt(static_cast<int>(data != nullptr && size > 0 ? size : 0));
            if (data != nullptr && size > 0) out.write(data, static_cast<size_t>(size));
        } else {
            out.writeInt(0);
            out.writeInt(p_->numParams);
            for (int i = 0; i < p_->numParams; ++i) out.writeFloat(p_->getParameter(p_, i));
        }
    }

    void setStateInformation(const void* data, int sizeInBytes) override {
        juce::MemoryInputStream in(data, static_cast<size_t>(sizeInBytes), false);
        if (sizeInBytes < 16 || in.readInt() != kStateMagic) return;
        in.readInt();
        const int program = in.readInt();
        const int kind = in.readInt();
        if (kind == 1) {
            const int size = in.readInt();
            juce::MemoryBlock chunk;
            if (size > 0 && in.readIntoMemoryBlock(chunk, size) == static_cast<size_t>(size))
                call(p_, op::setChunk, 0, static_cast<iptr>(chunk.getSize()), chunk.getData());
        } else {
            setCurrentProgram(program);
            const int count = in.readInt();
            for (int i = 0; i < count && !in.isExhausted(); ++i) {
                const float v = in.readFloat();
                if (i < p_->numParams) p_->setParameter(p_, i, v);
            }
        }
    }

    void fillInPluginDescription(juce::PluginDescription& d) const override { d = desc_; }

    // ---- host callback for this instance ---------------------------------------
    iptr handleHost(i32 opcode, i32 index, iptr value, void* ptr, float opt);

    juce::Point<int> editorSize() {
        Rect* r = nullptr;
        call(p_, op::editorGetRect, 0, 0, &r);
        if (r == nullptr) return {};
        return {r->right - r->left, r->bottom - r->top};
    }
    void editorGone(Vst2Editor* e) { if (editor_ == e) editor_ = nullptr; }

private:
    static constexpr int kMaxEvents = 1024;
    static constexpr int kStateMagic = 0x46445632;          // "FDV2"

    struct Param final : public juce::AudioPluginInstance::HostedParameter {
        Param(Vst2Instance& o, int i) : owner(o), index(i) {
            defaultValue = o.p_->getParameter(o.p_, i);
            name = query(o.p_, op::getParamName, i);
            if (name.isEmpty()) name = "Parameter " + juce::String(i + 1);
            unit = query(o.p_, op::getParamUnit, i);
        }
        float getValue() const override { return owner.p_->getParameter(owner.p_, index); }
        void setValue(float v) override { owner.p_->setParameter(owner.p_, index, v); }
        float getDefaultValue() const override { return defaultValue; }
        juce::String getName(int maxLen) const override { return name.substring(0, maxLen); }
        juce::String getLabel() const override { return unit; }
        juce::String getText(float v, int maxLen) const override {
            if (std::abs(v - getValue()) < 1.0e-6f) {
                const auto t = query(owner.p_, op::getParamText, index);
                if (t.isNotEmpty()) return t.substring(0, maxLen);
            }
            return juce::String(v, 3).substring(0, maxLen);
        }
        float getValueForText(const juce::String& text) const override { return juce::jlimit(0.0f, 1.0f, text.getFloatValue()); }
        juce::String getParameterID() const override { return juce::String(index); }

        Vst2Instance& owner;
        int index;
        float defaultValue = 0.0f;
        juce::String name, unit;
    };

    static BusesProperties busesFor(Plugin* p) {
        BusesProperties b;
        if (p->numInputs > 0) b = b.withInput("Input", p->numInputs >= 2 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::mono(), true);
        if (p->numOutputs > 0) b = b.withOutput("Output", p->numOutputs >= 2 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::mono(), true);
        return b;
    }

    void handleAsyncUpdate() override {
        if (p_ == nullptr) return;
        setLatencySamples(p_->latency);
        updateHostDisplay();
    }

    void updateTime() {
        auto& t = time_;
        t.sampleRate = getSampleRate();
        t.tempo = 120.0;
        t.timeSigNumerator = 4;
        t.timeSigDenominator = 4;
        t.flags = time::tempoValid | time::timeSigValid | time::ppqValid;
        if (auto* head = getPlayHead())
            if (auto pos = head->getPosition()) {
                if (auto s = pos->getTimeInSamples()) t.samplePos = static_cast<double>(*s);
                if (auto b = pos->getBpm()) t.tempo = *b;
                if (auto ts = pos->getTimeSignature()) { t.timeSigNumerator = ts->numerator; t.timeSigDenominator = ts->denominator; }
                if (auto bar = pos->getPpqPositionOfLastBarStart()) { t.barStartPos = *bar; t.flags |= time::barsValid; }
                if (pos->getIsPlaying()) t.flags |= time::playing;
                if (pos->getIsLooping()) {
                    t.flags |= time::looping;
                    if (auto loop = pos->getLoopPoints()) { t.loopStartPos = loop->ppqStart; t.loopEndPos = loop->ppqEnd; t.flags |= time::loopValid; }
                }
                if (auto secs = pos->getTimeInSeconds()) { t.nanoSeconds = *secs * 1.0e9; t.flags |= time::nanosValid; }
                const bool playingNow = pos->getIsPlaying();
                if (playingNow != lastPlaying_) { t.flags |= time::changed; lastPlaying_ = playingNow; }
                if (auto q = pos->getPpqPosition()) { t.ppqPos = *q; return; }
            }
        t.ppqPos = t.sampleRate > 0.0 ? t.samplePos / t.sampleRate * t.tempo / 60.0 : 0.0;
    }

    void sendMidi(const juce::MidiBuffer& midi) {
        if (midi.isEmpty() || !acceptsMidi_) return;
        auto* list = reinterpret_cast<Events*>(eventBytes_.data());
        auto** slots = reinterpret_cast<MidiEvent**>(eventBytes_.data() + offsetof(Events, events));
        int count = 0;
        for (const auto meta : midi) {
            if (count == kMaxEvents) break;
            const auto* raw = meta.data;
            if (meta.numBytes < 1 || meta.numBytes > 3 || raw[0] == 0xf0) continue;
            auto& e = eventStore_[static_cast<size_t>(count)];
            std::memset(&e, 0, sizeof(e));
            e.type = 1;
            e.byteSize = sizeof(MidiEvent);
            e.frameOffset = meta.samplePosition;
            std::memcpy(e.data, raw, static_cast<size_t>(meta.numBytes));
            slots[count++] = &e;
        }
        if (count == 0) return;
        list->count = count;
        list->reserved = 0;
        call(p_, op::sendEvents, 0, 0, list);
    }

    std::shared_ptr<Module> module_;          // keeps the library loaded while the plugin exists
    Plugin* p_;
    juce::PluginDescription desc_;
    bool active_ = false, acceptsMidi_ = false, producesMidi_ = false, lastPlaying_ = false;
    int maxBlock_ = 512;
    std::vector<std::vector<float>> ins_, outs_;
    std::vector<float*> inPtrs_, outPtrs_;
    std::vector<char> eventBytes_;
    std::vector<MidiEvent> eventStore_;
    TimeInfo time_;
    juce::Component::SafePointer<Vst2Editor> editor_;
};

// ---------------------------------------------------------------------------
// Editor: the plugin draws its own window inside ours
// ---------------------------------------------------------------------------
Vst2Editor::Vst2Editor(Vst2Instance& owner) : juce::AudioProcessorEditor(owner), owner_(owner) {
    setOpaque(true);
    const auto size = owner_.editorSize();
    setSize(std::max(120, size.x), std::max(60, size.y));
}

Vst2Editor::~Vst2Editor() {
    stopTimer();
    if (open_) call(owner_.plugin(), op::editorClose);
    owner_.editorGone(this);
}

void Vst2Editor::tryOpen() {
    if (open_ || !isShowing()) return;
    auto* peer = getPeer();
    if (peer == nullptr) return;
    open_ = true;
    call(owner_.plugin(), op::editorOpen, 0, 0, peer->getNativeHandle());
    const auto size = owner_.editorSize();
    resizeTo(size.x, size.y);
    startTimerHz(30);
}

void Vst2Editor::timerCallback() { call(owner_.plugin(), op::editorIdle); }

// ---------------------------------------------------------------------------
// Host callback
// ---------------------------------------------------------------------------
iptr genericHost(i32 opcode, i32 index, iptr value, void* ptr, float opt) {
    juce::ignoreUnused(index, value, opt);
    switch (opcode) {
        case host::version:          return 2400;
        case host::currentId:        return tlShellIdToLoad;
        case host::getSampleRate:    return 44100;
        case host::getBlockSize:     return 512;
        case host::getProcessLevel:  return tlInAudioCallback ? 2 : 1;
        case host::getAutomationState: return 1;
        case host::getLanguage:      return 1;
        case host::getVendorVersion: return 100;
        case host::getTime:          return reinterpret_cast<iptr>(&defaultTime());
        case host::getDirectory:     return reinterpret_cast<iptr>(tlPluginDirectory);
        case host::getVendor:
        case host::getProduct:
            if (ptr != nullptr) std::strcpy(static_cast<char*>(ptr), "FreeDAWm");
            return 1;
        case host::canDo: {
            if (ptr == nullptr) return 0;
            const juce::String what(static_cast<const char*>(ptr));
            static const juce::StringArray yes {"sendVstEvents", "sendVstMidiEvent", "sendVstTimeInfo", "receiveVstEvents",
                                                "receiveVstMidiEvent", "sizeWindow", "supplyIdle", "shellCategory", "startStopProcess"};
            return yes.contains(what) ? 1 : 0;
        }
        default: return 0;
    }
}

iptr Vst2Instance::handleHost(i32 opcode, i32 index, iptr value, void* ptr, float opt) {
    switch (opcode) {
        case host::automate:
            if (auto* p = getHostedParameter(index)) p->sendValueChangedMessageToListeners(opt);
            return 0;
        case host::beginEdit:  if (auto* p = getHostedParameter(index)) p->beginChangeGesture(); return 1;
        case host::endEdit:    if (auto* p = getHostedParameter(index)) p->endChangeGesture(); return 1;
        case host::getTime:    return reinterpret_cast<iptr>(&time_);
        case host::getSampleRate: return getSampleRate() > 0.0 ? static_cast<iptr>(getSampleRate()) : 44100;
        case host::getBlockSize:  return maxBlock_;
        case host::ioChanged:
        case host::updateDisplay: triggerAsyncUpdate(); return 1;
        case host::sizeWindow: {
            const int w = index, h = static_cast<int>(value);
            juce::MessageManager::callAsync([safe = editor_, w, h] { if (auto* e = safe.getComponent()) e->resizeTo(w, h); });
            return 1;
        }
        default: return genericHost(opcode, index, value, ptr, opt);
    }
}

iptr FDM_VST2_CALL hostCallback(Plugin* p, i32 opcode, i32 index, iptr value, void* ptr, float opt) {
    if (p != nullptr && p->hostReserved1 != 0)
        return reinterpret_cast<Vst2Instance*>(p->hostReserved1)->handleHost(opcode, index, value, ptr, opt);
    return genericHost(opcode, index, value, ptr, opt);
}

void scanFile(juce::OwnedArray<juce::PluginDescription>& results, const juce::File& file) {
    juce::String error;
    auto module = Module::load(file, error);
    if (module == nullptr) return;
    Plugin* p = module->instantiate(0);
    if (p == nullptr) return;
    call(p, op::open);

    if (call(p, op::getCategory) == category::shell) {             // one file, several plugins
        std::vector<std::pair<i32, juce::String>> children;
        for (int guard = 0; guard < 1000; ++guard) {
            char name[512] = {};
            const auto id = static_cast<i32>(call(p, op::shellNextPlugin, 0, 0, name));
            if (id == 0) break;
            children.emplace_back(id, textFrom(name, sizeof(name) - 1));
        }
        call(p, op::close);
        for (auto& [id, name] : children) {
            Plugin* child = module->instantiate(id);
            if (child == nullptr) continue;
            call(child, op::open);
            auto d = std::make_unique<juce::PluginDescription>();
            describe(child, file, id, name, *d);
            call(child, op::close);
            results.add(d.release());
        }
        return;
    }

    auto d = std::make_unique<juce::PluginDescription>();
    describe(p, file, 0, {}, *d);
    call(p, op::close);
    results.add(d.release());
}

void collect(const juce::File& dir, bool recursive, juce::StringArray& out, Vst2Format& format) {
    for (const auto& entry : juce::RangedDirectoryIterator(dir, false, "*", juce::File::findFilesAndDirectories)) {
        const auto f = entry.getFile();
        if (format.fileMightContainThisPluginType(f.getFullPathName())) out.add(f.getFullPathName());
        else if (recursive && f.isDirectory() && !f.hasFileExtension("vst3;vst;component;app;bundle")) collect(f, recursive, out, format);
    }
}

}  // namespace

// ===========================================================================
// Vst2Format
// ===========================================================================

namespace {
std::atomic<bool> sandboxAll{false};
}

void Vst2Format::setRunInSeparateProcess(bool on) { sandboxAll = on; }
bool Vst2Format::runsInSeparateProcess() { return sandboxAll; }

bridged::Arch Vst2Format::bridgeFor(const juce::File& file) {
    const auto arch = bridged::architectureOf(binaryFor(file));
    constexpr bool host64 = sizeof(void*) == 8;
    if (host64 && arch == bridged::Arch::x86) return bridged::Arch::x86;               // 32-bit plugin: always bridged
    if (sandboxAll && arch == (host64 ? bridged::Arch::x64 : bridged::Arch::x86)) return arch;
    return bridged::Arch::unknown;                                                       // load in-process
}

bool Vst2Format::fileMightContainThisPluginType(const juce::String& fileOrIdentifier) {
    const juce::File f(fileOrIdentifier);
   #if JUCE_WINDOWS
    return f.existsAsFile() && f.hasFileExtension("dll");
   #elif JUCE_MAC
    return f.isDirectory() && f.hasFileExtension("vst");
   #else
    return f.existsAsFile() && f.hasFileExtension("so");
   #endif
}

juce::String Vst2Format::getNameOfPluginFromIdentifier(const juce::String& id) { return juce::File(id).getFileNameWithoutExtension(); }

bool Vst2Format::pluginNeedsRescanning(const juce::PluginDescription& d) {
    return juce::File(d.fileOrIdentifier).getLastModificationTime() != d.lastFileModTime;
}

bool Vst2Format::doesPluginStillExist(const juce::PluginDescription& d) { return juce::File(d.fileOrIdentifier).exists(); }

void Vst2Format::findAllTypesForFile(juce::OwnedArray<juce::PluginDescription>& results, const juce::String& fileOrIdentifier) {
    if (!fileMightContainThisPluginType(fileOrIdentifier)) return;
    const juce::File file(fileOrIdentifier);
    if (const auto arch = bridgeFor(file); arch != bridged::Arch::unknown) {     // 32-bit or sandboxed: ask the bridge
        juce::String error;
        if (!bridged::describe(file, arch, results, error)) juce::Logger::writeToLog("VST2 bridge scan failed: " + error);
        return;
    }
    // Many VST2 plugins expect to be created on the UI thread.
    if (juce::MessageManager::existsAndIsCurrentThread()) scanFile(results, file);
    else juce::MessageManager::callSync([&] { scanFile(results, file); });
}

juce::StringArray Vst2Format::searchPathsForPlugins(const juce::FileSearchPath& paths, bool recursive, bool) {
    juce::StringArray out;
    for (int i = 0; i < paths.getNumPaths(); ++i) collect(paths[i], recursive, out, *this);
    return out;
}

juce::FileSearchPath Vst2Format::getDefaultLocationsToSearch() {
    juce::FileSearchPath p;
   #if JUCE_WINDOWS
    const auto programs = juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory);
    p.add(programs.getChildFile("VSTPlugins"));
    p.add(programs.getChildFile("Steinberg").getChildFile("VSTPlugins"));
    p.add(programs.getChildFile("Common Files").getChildFile("VST2"));
    p.add(programs.getChildFile("Common Files").getChildFile("Steinberg").getChildFile("VST2"));
   #elif JUCE_MAC
    p.add(juce::File("/Library/Audio/Plug-Ins/VST"));
    p.add(juce::File("~/Library/Audio/Plug-Ins/VST"));
   #else
    p.add(juce::File("~/.vst"));
    p.add(juce::File("~/.lxvst"));
    p.add(juce::File("/usr/lib/vst"));
    p.add(juce::File("/usr/local/lib/vst"));
    p.add(juce::File("/usr/lib/lxvst"));
    for (auto& dir : juce::StringArray::fromTokens(juce::SystemStats::getEnvironmentVariable("VST_PATH", {}), ":", {}))
        if (dir.isNotEmpty()) p.add(juce::File(dir));
   #endif
    p.removeNonExistentPaths();
    return p;
}

std::unique_ptr<juce::AudioPluginInstance> Vst2Format::create(const juce::PluginDescription& desc, double sampleRate, int blockSize,
                                                              juce::String& error) {
    if (const auto arch = bridgeFor(juce::File(desc.fileOrIdentifier)); arch != bridged::Arch::unknown)
        return bridged::create(desc, arch, sampleRate, blockSize, error);
    auto module = Module::load(juce::File(desc.fileOrIdentifier), error);
    if (module == nullptr) return nullptr;
    Plugin* p = module->instantiate(desc.hasSharedContainer ? desc.uniqueId : 0);
    if (p == nullptr) { error = "The plugin refused to start."; return nullptr; }
    call(p, op::open);
    auto instance = std::make_unique<Vst2Instance>(std::move(module), p, desc);
    instance->setRateAndBufferSizeDetails(sampleRate, blockSize);
    return instance;
}

void Vst2Format::createPluginInstance(const juce::PluginDescription& desc, double sampleRate, int blockSize, PluginCreationCallback callback) {
    juce::String error;
    auto instance = create(desc, sampleRate, blockSize, error);
    callback(std::move(instance), error);
}
