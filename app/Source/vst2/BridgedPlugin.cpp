#include "BridgedPlugin.h"

#include <cstring>

#include "../../../bridge/BridgeProtocol.h"
#include "../../../bridge/Ipc.h"
#include "Vst2Abi.h"

namespace fb = fdm::bridge;
namespace v = fdm::vst2;

namespace bridged {

// ---------------------------------------------------------------------------
// Architecture detection
// ---------------------------------------------------------------------------
Arch architectureOf(const juce::File& binary) {
    juce::FileInputStream in(binary);
    if (!in.openedOk()) return Arch::unknown;
    unsigned char head[64] = {};
    if (in.read(head, sizeof(head)) < 20) return Arch::unknown;
    if (head[0] == 0x7f && head[1] == 'E' && head[2] == 'L' && head[3] == 'F') {        // ELF (Linux)
        const int machine = head[18] | (head[19] << 8);
        if (head[4] == 1) return machine == 3 ? Arch::x86 : Arch::other;               // EM_386
        if (head[4] == 2) return machine == 62 ? Arch::x64 : Arch::other;              // EM_X86_64
        return Arch::unknown;
    }
    if (head[0] == 'M' && head[1] == 'Z') {                                             // PE (Windows)
        const auto peOffset = static_cast<juce::int64>(head[0x3c] | (head[0x3d] << 8) | (head[0x3e] << 16) | (head[0x3f] << 24));
        unsigned char pe[6] = {};
        if (!in.setPosition(peOffset) || in.read(pe, 6) != 6 || pe[0] != 'P' || pe[1] != 'E') return Arch::unknown;
        const int machine = pe[4] | (pe[5] << 8);
        if (machine == 0x14c) return Arch::x86;
        if (machine == 0x8664) return Arch::x64;
        return Arch::other;
    }
    return Arch::unknown;
}

namespace {
juce::File& bridgeFolder() {
    static juce::File folder = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
    return folder;
}
}  // namespace

void setBridgeFolder(const juce::File& folder) { bridgeFolder() = folder; }

juce::File bridgeExecutable(Arch arch) {
    juce::String name = arch == Arch::x86 ? "fdm_bridge32" : "fdm_bridge64";
   #if JUCE_WINDOWS
    name << ".exe";
   #endif
    return bridgeFolder().getChildFile(name);
}

// ---------------------------------------------------------------------------
// One bridge process + its shared memory
// ---------------------------------------------------------------------------
namespace {

class Client {
public:
    ~Client() { shutdown(); }

    bool start(Arch arch, juce::String& error) {
        const auto exe = bridgeExecutable(arch);
        if (!exe.existsAsFile()) {
            error = "The " + juce::String(arch == Arch::x86 ? "32-bit" : "64-bit") + " plugin bridge (" + exe.getFileName()
                    + ") is missing next to FreeDAWm.";
            return false;
        }
        static std::atomic<int> counter{0};
        name_ = "fdm_" + juce::String(fb::currentProcessId()) + "_" + juce::String(counter++) + "_" + juce::String(juce::Random::getSystemRandom().nextInt(1 << 30));
        if (!session_.create(name_.toStdString())) { error = "Couldn't create shared memory for the plugin bridge."; return false; }
        if (!process_.start(juce::StringArray{exe.getFullPathName(), name_, juce::String(fb::currentProcessId())}, 0)) {
            error = "Couldn't start the plugin bridge.";
            return false;
        }
        const auto t0 = juce::Time::getMillisecondCounter();
        while (shared()->bridgeReady.load(std::memory_order_acquire) == 0) {
            if (!process_.isRunning()) { error = "The plugin bridge stopped right after starting (wrong build or missing system libraries?)."; return false; }
            if (juce::Time::getMillisecondCounter() - t0 > 10000) { error = "The plugin bridge didn't respond."; return false; }
            juce::Thread::sleep(2);
        }
        alive_ = true;
        return true;
    }

    fb::Shared* shared() const { return session_.shared(); }
    fb::Control& control() const { return shared()->control; }
    bool isAlive() const { return alive_.load(); }

    /** Sends the control request already written into control(). Message thread mostly; serialised. */
    bool request(fb::Command cmd, int timeoutMs = 15000) {
        const juce::ScopedLock sl(lock_);
        if (!alive_) return false;
        control().cmd = cmd;
        session_.controlRequest.post();
        const auto t0 = juce::Time::getMillisecondCounter();
        while (!session_.controlReply.wait(100)) {
            if (!process_.isRunning() || juce::Time::getMillisecondCounter() - t0 > static_cast<juce::uint32>(timeoutMs)) {
                markDead();
                return false;
            }
        }
        return true;
    }

    juce::CriticalSection& lock() { return lock_; }

    void setData(const juce::String& s) {
        const auto utf8 = s.toStdString();
        const auto n = std::min(utf8.size(), static_cast<size_t>(fb::kDataBytes - 1));
        std::memcpy(control().data, utf8.data(), n);
        control().data[n] = 0;
        control().dataSize = static_cast<std::int32_t>(n);
    }
    juce::String dataString() const {
        const int n = juce::jlimit(0, fb::kDataBytes - 1, control().dataSize);
        return juce::String::fromUTF8(control().data, n);
    }

    /** Audio thread: one block round trip. Returns false (and stays silent from then on) if the bridge died or hung. */
    bool processBlock(int timeoutMs) {
        if (!alive_) return false;
        session_.audioRequest.post();
        if (session_.audioReply.wait(timeoutMs)) return true;
        if (!process_.isRunning()) { markDead(); return false; }
        // Very slow block (e.g. a plugin loading samples): give it one more chance before giving up.
        if (session_.audioReply.wait(timeoutMs * 4)) return true;
        markDead();
        return false;
    }

    void shutdown() {
        if (alive_) {
            const juce::ScopedLock sl(lock_);
            control().cmd = fb::cmdClose;
            session_.controlRequest.post();
            session_.controlReply.wait(3000);
        }
        alive_ = false;
        if (process_.isRunning() && !process_.waitForProcessToFinish(2000)) process_.kill();
    }

private:
    void markDead() {
        alive_ = false;
        if (process_.isRunning()) process_.kill();
    }

    juce::String name_;
    fb::Session session_;
    juce::ChildProcess process_;
    std::atomic<bool> alive_{false};
    juce::CriticalSection lock_;
};

juce::String categoryName(int c) {
    switch (c) {
        case v::category::instrument: return "Instrument";
        case v::category::analysis:   return "Analyzer";
        case v::category::mastering:  return "Mastering";
        case v::category::spatial:    return "Spatial";
        case v::category::roomFx:     return "Reverb";
        case v::category::generator:  return "Generator";
        default:                      return "Effect";
    }
}

juce::String archSuffix(Arch arch) { return arch == Arch::x86 ? " (32-bit)" : juce::String(); }

bool openPlugin(Client& client, const juce::File& file, int shellId, fb::PluginInfo& info, juce::String& error) {
    client.setData(file.getFullPathName());
    client.control().i0 = shellId;
    if (!client.request(fb::cmdOpen, 60000)) { error = "The plugin crashed or hung inside the bridge."; return false; }
    if (client.control().result != 0) { error = client.dataString(); return false; }
    std::memcpy(&info, client.control().data, sizeof(info));
    info.name[sizeof(info.name) - 1] = info.vendor[sizeof(info.vendor) - 1] = info.product[sizeof(info.product) - 1] = 0;
    return true;
}

void fillDescription(const fb::PluginInfo& info, const juce::File& file, int shellId, const juce::String& shellName, Arch arch,
                     juce::PluginDescription& d) {
    d.pluginFormatName = "VST2";
    d.fileOrIdentifier = file.getFullPathName();
    d.lastFileModTime = file.getLastModificationTime();
    d.lastInfoUpdateTime = juce::Time::getCurrentTime();
    d.name = shellName.isNotEmpty() ? shellName : juce::String::fromUTF8(info.name);
    if (d.name.isEmpty()) d.name = juce::String::fromUTF8(info.product);
    if (d.name.isEmpty()) d.name = file.getFileNameWithoutExtension();
    d.descriptiveName = d.name + archSuffix(arch);
    d.manufacturerName = juce::String::fromUTF8(info.vendor);
    d.version = juce::String(info.vendorVersion);
    d.category = categoryName(info.category);
    d.isInstrument = (info.flags & v::flag::isInstrument) != 0 || info.category == v::category::instrument;
    d.numInputChannels = info.numInputs;
    d.numOutputChannels = info.numOutputs;
    d.uniqueId = shellId != 0 ? shellId : info.uniqueId;
    d.deprecatedUid = d.uniqueId;
    d.hasSharedContainer = shellId != 0;
}

}  // namespace

bool describe(const juce::File& file, Arch arch, juce::OwnedArray<juce::PluginDescription>& results, juce::String& error) {
    // 1. Is it a shell? (One short-lived bridge just for the question.)
    std::vector<std::pair<int, juce::String>> children;
    {
        Client client;
        if (!client.start(arch, error)) return false;
        client.setData(file.getFullPathName());
        if (!client.request(fb::cmdShellList, 60000)) { error = "The plugin crashed or hung inside the bridge."; return false; }
        if (client.control().result != 0) { error = client.dataString(); return false; }
        for (auto& line : juce::StringArray::fromLines(client.dataString())) {
            if (line.isEmpty()) continue;
            children.emplace_back(line.upToFirstOccurrenceOf("\t", false, false).getIntValue(), line.fromFirstOccurrenceOf("\t", false, false));
        }
    }
    if (children.empty()) children.emplace_back(0, juce::String());

    // 2. Describe each plugin in the file.
    for (auto& [id, shellName] : children) {
        Client client;
        if (!client.start(arch, error)) return false;
        fb::PluginInfo info {};
        if (!openPlugin(client, file, id, info, error)) return false;
        auto d = std::make_unique<juce::PluginDescription>();
        fillDescription(info, file, id, shellName, arch, *d);
        results.add(d.release());
    }
    return true;
}

// ---------------------------------------------------------------------------
// The instance
// ---------------------------------------------------------------------------
namespace {

class BridgedInstance;

class BridgedEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    BridgedEditor(BridgedInstance& owner, juce::Point<int> size);
    ~BridgedEditor() override;
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colours::black); }
    void parentHierarchyChanged() override { tryOpen(); }
    void visibilityChanged() override { tryOpen(); }

private:
    void tryOpen();
    void timerCallback() override;
    BridgedInstance& owner_;
    bool open_ = false;
    int lastSizeSeq_ = 0;
};

class BridgedInstance final : public juce::AudioPluginInstance, private juce::Timer {
public:
    BridgedInstance(std::unique_ptr<Client> client, const fb::PluginInfo& info, const juce::PluginDescription& desc)
        : juce::AudioPluginInstance(busesFor(info)), client_(std::move(client)), info_(info), desc_(desc) {
        for (int i = 0; i < std::min(info_.numParams, fb::kMaxParams); ++i) addHostedParameter(std::make_unique<Param>(*this, i));
        setLatencySamples(info_.latency);
        startTimerHz(20);           // automation and latency changes reported by the plugin
    }

    ~BridgedInstance() override { stopTimer(); client_->shutdown(); }

    Client& client() { return *client_; }
    const fb::PluginInfo& info() const { return info_; }

    // ---- AudioProcessor ------------------------------------------------------------
    const juce::String getName() const override { return desc_.name; }
    bool isBusesLayoutSupported(const BusesLayout& l) const override { return l.getMainInputChannels() <= 2 && l.getMainOutputChannels() <= 2; }

    void prepareToPlay(double sampleRate, int blockSize) override {
        maxBlock_ = juce::jlimit(16, fb::kMaxBlock, blockSize);
        const juce::ScopedLock sl(client_->lock());
        client_->control().d0 = sampleRate;
        client_->control().i0 = maxBlock_;
        client_->request(fb::cmdPrepare);
    }

    void releaseResources() override { client_->request(fb::cmdRelease); }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override {
        const int n = buffer.getNumSamples(), chans = buffer.getNumChannels();
        if (!client_->isAlive()) { buffer.clear(); midi.clear(); return; }
        auto& a = client_->shared()->audio;
        writeTransport(a);

        int done = 0;
        bool first = true;
        while (done < n) {
            const int k = std::min(n - done, maxBlock_);
            // MIDI for this slice (all events go with the first slice they fall into)
            int count = 0;
            if (acceptsMidi())
                for (const auto meta : midi) {
                    if (meta.samplePosition < done || meta.samplePosition >= done + k || meta.numBytes > 3 || count == fb::kMaxEvents) continue;
                    a.events[count].frame = meta.samplePosition - done;
                    std::memset(a.events[count].bytes, 0, 4);
                    std::memcpy(a.events[count].bytes, meta.data, static_cast<size_t>(meta.numBytes));
                    ++count;
                }
            a.numEvents = count;
            a.frames = k;
            for (int i = 0; i < std::min(info_.numInputs, fb::kMaxChannels); ++i) {
                if (chans > 0) std::memcpy(a.in[i], buffer.getReadPointer(std::min(i, chans - 1), done), sizeof(float) * static_cast<size_t>(k));
                else std::fill_n(a.in[i], k, 0.0f);
            }
            if (!client_->processBlock(first ? 2000 : 1000)) { buffer.clear(); midi.clear(); return; }
            first = false;
            const int outs = std::min(info_.numOutputs, fb::kMaxChannels);
            for (int c = 0; c < chans; ++c) {
                if (outs > 0) buffer.copyFrom(c, done, a.out[std::min(c, outs - 1)], k);
                else buffer.clear(c, done, k);
            }
            a.samplePos += k;
            done += k;
        }
        midi.clear();
    }

    double getTailLengthSeconds() const override {
        auto* self = const_cast<BridgedInstance*>(this);
        if (!self->client_->request(fb::cmdTail)) return 0.0;
        const int tail = self->client_->control().result;
        return tail > 1 && getSampleRate() > 0.0 ? tail / getSampleRate() : 0.0;
    }
    bool acceptsMidi() const override { return info_.acceptsMidi != 0 || desc_.isInstrument; }
    bool producesMidi() const override { return info_.producesMidi != 0; }
    bool hasEditor() const override { return (info_.flags & v::flag::hasEditor) != 0; }

    juce::AudioProcessorEditor* createEditor() override {
        if (!hasEditor()) return nullptr;
        juce::Point<int> size;
        if (client_->request(fb::cmdEditorRect)) size = {client_->control().i0, client_->control().i1};
        return new BridgedEditor(*this, size);
    }

    int getNumPrograms() override { return std::max(1, info_.numPrograms); }
    int getCurrentProgram() override { return client_->request(fb::cmdGetProgram) ? client_->control().result : 0; }
    void setCurrentProgram(int index) override {
        if (index < 0 || index >= info_.numPrograms) return;
        const juce::ScopedLock sl(client_->lock());
        client_->control().i0 = index;
        client_->request(fb::cmdSetProgram);
    }
    const juce::String getProgramName(int index) override {
        const juce::ScopedLock sl(client_->lock());
        client_->control().i0 = index;
        if (!client_->request(fb::cmdProgramName)) return {};
        const auto name = client_->dataString();
        return name.isNotEmpty() ? name : "Program " + juce::String(index + 1);
    }
    void changeProgramName(int, const juce::String& name) override {
        const juce::ScopedLock sl(client_->lock());
        client_->setData(name);
        client_->request(fb::cmdSetProgramName);
    }

    // Same state format as the in-process host ("FDV2"), so a plugin can move between the two.
    void getStateInformation(juce::MemoryBlock& dest) override {
        juce::MemoryOutputStream out(dest, false);
        out.writeInt(kStateMagic);
        out.writeInt(1);
        out.writeInt(getCurrentProgram());
        if ((info_.flags & v::flag::chunks) != 0) {
            const juce::ScopedLock sl(client_->lock());
            const int size = client_->request(fb::cmdChunkGet) ? client_->control().result : 0;
            out.writeInt(1);
            out.writeInt(size);
            for (int offset = 0; offset < size;) {
                client_->control().v0 = offset;
                if (!client_->request(fb::cmdChunkRead) || client_->control().dataSize <= 0) break;
                out.write(client_->control().data, static_cast<size_t>(client_->control().dataSize));
                offset += client_->control().dataSize;
            }
        } else {
            out.writeInt(0);
            out.writeInt(info_.numParams);
            for (int i = 0; i < info_.numParams; ++i)
                out.writeFloat(i < fb::kMaxParams ? client_->shared()->params[i].load() : 0.0f);
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
            if (size <= 0 || in.readIntoMemoryBlock(chunk, size) != static_cast<size_t>(size)) return;
            const juce::ScopedLock sl(client_->lock());
            for (size_t offset = 0; offset < chunk.getSize();) {
                const auto n = std::min(chunk.getSize() - offset, static_cast<size_t>(fb::kDataBytes));
                client_->control().v0 = static_cast<std::int64_t>(offset);
                std::memcpy(client_->control().data, static_cast<const char*>(chunk.getData()) + offset, n);
                client_->control().dataSize = static_cast<std::int32_t>(n);
                if (!client_->request(fb::cmdChunkWrite)) return;
                offset += n;
            }
            client_->control().v0 = static_cast<std::int64_t>(chunk.getSize());
            client_->request(fb::cmdChunkSet);
        } else {
            setCurrentProgram(program);
            const int count = in.readInt();
            for (int i = 0; i < count && !in.isExhausted(); ++i) {
                const float value = in.readFloat();
                if (i < info_.numParams) setParam(i, value);
            }
        }
    }

    void fillInPluginDescription(juce::PluginDescription& d) const override { d = desc_; }

    void setParam(int index, float value) {
        const juce::ScopedLock sl(client_->lock());
        client_->control().i0 = index;
        client_->control().d0 = value;
        client_->request(fb::cmdSetParam);
    }

    juce::String paramString(fb::Command cmd, int index) {
        const juce::ScopedLock sl(client_->lock());
        client_->control().i0 = index;
        return client_->request(cmd) ? client_->dataString() : juce::String();
    }

private:
    static constexpr int kStateMagic = 0x46445632;

    struct Param final : public juce::AudioPluginInstance::HostedParameter {
        Param(BridgedInstance& o, int i) : owner(o), index(i) {
            defaultValue = o.client_->shared()->params[i].load();
            name = o.paramString(fdm::bridge::cmdParamName, i);
            if (name.isEmpty()) name = "Parameter " + juce::String(i + 1);
            unit = o.paramString(fdm::bridge::cmdParamUnit, i);
        }
        float getValue() const override { return owner.client_->shared()->params[index].load(std::memory_order_relaxed); }
        void setValue(float v) override { owner.setParam(index, v); }
        float getDefaultValue() const override { return defaultValue; }
        juce::String getName(int maxLen) const override { return name.substring(0, maxLen); }
        juce::String getLabel() const override { return unit; }
        juce::String getText(float v, int maxLen) const override {
            if (std::abs(v - getValue()) < 1.0e-6f) {
                const auto t = owner.paramString(fdm::bridge::cmdParamText, index);
                if (t.isNotEmpty()) return t.substring(0, maxLen);
            }
            return juce::String(v, 3).substring(0, maxLen);
        }
        float getValueForText(const juce::String& text) const override { return juce::jlimit(0.0f, 1.0f, text.getFloatValue()); }
        juce::String getParameterID() const override { return juce::String(index); }

        BridgedInstance& owner;
        int index;
        float defaultValue = 0.0f;
        juce::String name, unit;
    };

    static BusesProperties busesFor(const fb::PluginInfo& info) {
        BusesProperties b;
        if (info.numInputs > 0) b = b.withInput("Input", info.numInputs >= 2 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::mono(), true);
        if (info.numOutputs > 0) b = b.withOutput("Output", info.numOutputs >= 2 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::mono(), true);
        return b;
    }

    void writeTransport(fb::Audio& a) {
        a.sampleRate = getSampleRate();
        a.tempo = 120.0;
        a.sigNum = 4;
        a.sigDen = 4;
        a.timeFlags = v::time::tempoValid | v::time::timeSigValid | v::time::ppqValid;
        if (auto* head = getPlayHead())
            if (auto pos = head->getPosition()) {
                if (auto s = pos->getTimeInSamples()) a.samplePos = static_cast<double>(*s);
                if (auto b = pos->getBpm()) a.tempo = *b;
                if (auto ts = pos->getTimeSignature()) { a.sigNum = ts->numerator; a.sigDen = ts->denominator; }
                if (auto bar = pos->getPpqPositionOfLastBarStart()) { a.barStart = *bar; a.timeFlags |= v::time::barsValid; }
                if (auto secs = pos->getTimeInSeconds()) { a.seconds = *secs; a.timeFlags |= v::time::nanosValid; }
                if (pos->getIsPlaying()) a.timeFlags |= v::time::playing;
                if (pos->getIsLooping()) {
                    a.timeFlags |= v::time::looping;
                    if (auto loop = pos->getLoopPoints()) { a.loopStart = loop->ppqStart; a.loopEnd = loop->ppqEnd; a.timeFlags |= v::time::loopValid; }
                }
                if (pos->getIsPlaying() != lastPlaying_) { a.timeFlags |= v::time::changed; lastPlaying_ = pos->getIsPlaying(); }
                if (auto q = pos->getPpqPosition()) { a.ppq = *q; return; }
            }
        a.ppq = a.sampleRate > 0.0 ? a.samplePos / a.sampleRate * a.tempo / 60.0 : 0.0;
    }

    void timerCallback() override {
        auto* s = client_->shared();
        for (std::uint32_t r = s->autoRead.load(); r != s->autoWrite.load(std::memory_order_acquire); ++r) {
            const auto e = s->automation[r % fb::kAutomationRing];
            if (auto* p = getHostedParameter(e.index)) p->sendValueChangedMessageToListeners(e.value);
            s->autoRead.store(r + 1, std::memory_order_release);
        }
        if (s->displayChanged.exchange(0) != 0) { setLatencySamples(s->latency.load()); updateHostDisplay(); }
    }

    std::unique_ptr<Client> client_;
    fb::PluginInfo info_;
    juce::PluginDescription desc_;
    int maxBlock_ = 512;
    bool lastPlaying_ = false;
};

BridgedEditor::BridgedEditor(BridgedInstance& owner, juce::Point<int> size) : juce::AudioProcessorEditor(owner), owner_(owner) {
    setOpaque(true);
    setSize(std::max(120, size.x), std::max(60, size.y));
}

BridgedEditor::~BridgedEditor() {
    stopTimer();
    if (open_) owner_.client().request(fb::cmdEditorClose);
}

void BridgedEditor::tryOpen() {
    if (open_ || !isShowing()) return;
    auto* peer = getPeer();
    if (peer == nullptr) return;
    open_ = true;
    // The bridge process puts the plugin's window inside ours (Windows: child HWND; Linux: X11 embedding).
    const juce::ScopedLock sl(owner_.client().lock());
    owner_.client().control().v0 = static_cast<std::int64_t>(reinterpret_cast<std::intptr_t>(peer->getNativeHandle()));
    if (owner_.client().request(fb::cmdEditorOpen) && owner_.client().control().i0 > 0)
        setSize(owner_.client().control().i0, owner_.client().control().i1);
    lastSizeSeq_ = owner_.client().shared()->sizeSeq.load();
    startTimerHz(20);
}

void BridgedEditor::timerCallback() {                 // the plugin asked to resize its window
    auto* s = owner_.client().shared();
    const int seq = s->sizeSeq.load();
    if (seq != lastSizeSeq_ && s->sizeW > 0 && s->sizeH > 0) { lastSizeSeq_ = seq; setSize(s->sizeW, s->sizeH); }
}

}  // namespace

std::unique_ptr<juce::AudioPluginInstance> create(const juce::PluginDescription& desc, Arch arch, double sampleRate, int blockSize,
                                                  juce::String& error) {
    auto client = std::make_unique<Client>();
    if (!client->start(arch, error)) return nullptr;
    fb::PluginInfo info {};
    if (!openPlugin(*client, juce::File(desc.fileOrIdentifier), desc.hasSharedContainer ? desc.uniqueId : 0, info, error)) return nullptr;
    auto instance = std::make_unique<BridgedInstance>(std::move(client), info, desc);
    instance->setRateAndBufferSizeDetails(sampleRate, blockSize);
    return instance;
}

}  // namespace bridged
