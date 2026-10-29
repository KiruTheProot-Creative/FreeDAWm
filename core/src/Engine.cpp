#include "fdm/Engine.h"

#include <chrono>
#include <cmath>
#include <thread>

namespace fdm {

// ===========================================================================
// Construction
// ===========================================================================

Engine::Engine() {
    for (int i = 0; i < kMaxChannels; ++i) {
        chL_[static_cast<size_t>(i)].assign(kMaxBlockSize, 0.0f);
        chR_[static_cast<size_t>(i)].assign(kMaxBlockSize, 0.0f);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "Channel %d", i + 1);
        channels_[static_cast<size_t>(i)].setName(buf);
    }
    for (int r = 0; r < kNumReturns; ++r) {
        retL_[static_cast<size_t>(r)].assign(kMaxBlockSize, 0.0f);
        retR_[static_cast<size_t>(r)].assign(kMaxBlockSize, 0.0f);
        const char name[2] = {static_cast<char>('A' + r), 0};
        returns_[static_cast<size_t>(r)].setName(name);
    }
    mixL_.assign(kMaxBlockSize, 0.0f);
    mixR_.assign(kMaxBlockSize, 0.0f);
    master_.setName("Master");
    prepare(48000.0, 512);
}

Engine::~Engine() {
    auto freeFx = [](Strip& s) {
        for (auto& slot : s.fx) delete slot.effect.exchange(nullptr);
    };
    for (auto& c : channels_) freeFx(c);
    for (auto& r : returns_) freeFx(r);
    freeFx(master_);
}

void Engine::prepareInstruments() {
    for (auto& ch : channels_) {
        ch.drums.prepare(sampleRate_, blockSize_);
        ch.synth.prepare(sampleRate_, blockSize_);
        if (auto* ext = ch.external.load()) ext->prepare(sampleRate_, blockSize_);
        ch.numPending = 0;
    }
}

void Engine::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate;
    blockSize_  = std::clamp(maxBlockSize, 16, kMaxBlockSize);
    prepareInstruments();

    auto prepFx = [&](Strip& s) {
        s.lastGain = -1.0f;
        for (auto& slot : s.fx)
            if (auto* fx = slot.effect.load()) fx->prepare(sampleRate_, blockSize_);
    };
    for (auto& c : channels_) prepFx(c);
    for (auto& r : returns_) prepFx(r);
    prepFx(master_);

    limiter_.setSampleRate(sampleRate);
    limiter_.reset();
    countdown_ = 0.0;
}

// ===========================================================================
// Transport and clip launching (UI thread)
// ===========================================================================

void Engine::play() { transportCmd_.store(kCmdStart, std::memory_order_release); }
void Engine::stop() { transportCmd_.store(kCmdStop,  std::memory_order_release); }

int Engine::activeChannelCount() const {
    int n = 0;
    for (auto& c : channels_) n += c.active.load() ? 1 : 0;
    return n;
}

int Engine::addChannel(InstrumentType type) {
    for (int i = 0; i < kMaxChannels; ++i) {
        Channel& c = channels_[static_cast<size_t>(i)];
        if (c.active.load()) continue;

        for (auto& clip : c.clips) clip.clear();
        c.playingSlot.store(-1);
        c.queuedSlot.store(kNoQueue);
        c.numPending = 0;
        c.volumeDb.store(0.0f);
        c.pan.store(0.0f);
        c.mute.store(false);
        c.solo.store(false);
        for (auto& s : c.sends) s.store(0.0f);
        c.lastGain = -1.0f;
        c.instrumentType.store(static_cast<int>(type));
        c.drums.params().resetAll();
        c.synth.params().resetAll();
        c.active.store(true, std::memory_order_release);
        return i;
    }
    return -1;
}

void Engine::removeChannel(int index) {
    if (index < 0 || index >= kMaxChannels) return;
    Channel& c = channels_[static_cast<size_t>(index)];
    c.active.store(false, std::memory_order_release);
    c.queuedSlot.store(kNoQueue);
    c.playingSlot.store(-1);
    for (auto& clip : c.clips) clip.clear();
}

void Engine::launchClip(int channel, int slot) {
    if (channel < 0 || channel >= kMaxChannels || slot < 0 || slot >= kMaxScenes) return;
    Channel& c = channels_[static_cast<size_t>(channel)];
    if (!c.active.load() || !c.clips[static_cast<size_t>(slot)].exists.load()) return;
    c.queuedSlot.store(slot, std::memory_order_release);
    if (!isPlaying()) play();      // like Ableton: launching a clip starts the transport
}

void Engine::stopClip(int channel) {
    if (channel < 0 || channel >= kMaxChannels) return;
    channels_[static_cast<size_t>(channel)].queuedSlot.store(kQueueStop, std::memory_order_release);
}

void Engine::launchScene(int slot) {
    for (int i = 0; i < kMaxChannels; ++i) launchClip(i, slot);
}

void Engine::stopAllClips() {
    for (int i = 0; i < kMaxChannels; ++i)
        if (channels_[static_cast<size_t>(i)].active.load()) stopClip(i);
}

void Engine::liveNoteOn(int channel, int note, int velocity) {
    LiveEvent e;
    e.channel = static_cast<uint8_t>(std::clamp(channel, 0, kMaxChannels - 1));
    e.note = static_cast<uint8_t>(std::clamp(note, 0, 127));
    e.velocity = static_cast<uint8_t>(std::clamp(velocity, 1, 127));
    e.on = true;
    pushLive(e);
}

void Engine::liveNoteOff(int channel, int note) {
    LiveEvent e;
    e.channel = static_cast<uint8_t>(std::clamp(channel, 0, kMaxChannels - 1));
    e.note = static_cast<uint8_t>(std::clamp(note, 0, 127));
    e.on = false;
    pushLive(e);
}

void Engine::pushLive(const LiveEvent& e) {
    while (liveLock_.test_and_set(std::memory_order_acquire)) {}
    const uint32_t w = liveWrite_.load(std::memory_order_relaxed);
    const uint32_t r = liveRead_.load(std::memory_order_acquire);
    if (w - r < kLiveCap) {
        live_[w % kLiveCap] = e;
        liveWrite_.store(w + 1, std::memory_order_release);
    }
    liveLock_.clear(std::memory_order_release);
}

// ===========================================================================
// Effects
// ===========================================================================

std::unique_ptr<IEffect> Engine::swapEffect(EffectSlot& slot, std::unique_ptr<IEffect> fx) {
    if (fx) fx->prepare(sampleRate_, blockSize_);
    IEffect* old = slot.effect.exchange(fx.release(), std::memory_order_acq_rel);

    // The audio thread may still be inside old->process(). The block counter is
    // bumped at the end of every block, so once it moves the old pointer is dead.
    if (old && audioRunning_.load()) {
        const uint64_t start = blockCounter_.load(std::memory_order_acquire);
        const auto t0 = std::chrono::steady_clock::now();
        while (blockCounter_.load(std::memory_order_acquire) == start
               && std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(250))
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return std::unique_ptr<IEffect>(old);
}

// ===========================================================================
// Audio thread
// ===========================================================================

void Engine::process(float* outL, float* outR, int numFrames) {
    dsp::ScopedFlushDenormals noDenormals;
    int done = 0;
    while (done < numFrames) {
        const int n = std::min(numFrames - done, kMaxBlockSize);
        processChunk(outL + done, outR + done, n);
        done += n;
    }
}

void Engine::applyTransportCommand() {
    const int cmd = transportCmd_.exchange(0, std::memory_order_acq_rel);
    if (cmd == kCmdStart && !playing_) {
        playing_    = true;
        globalTick_ = 0;
        countdown_  = 0.0;
        playingFlag_.store(true, std::memory_order_relaxed);
    } else if (cmd == kCmdStop) {
        stopTransportInternal();
    }
}

void Engine::stopTransportInternal() {
    for (auto& ch : channels_) {
        if (!ch.active.load(std::memory_order_relaxed)) continue;
        IInstrument& inst = ch.instrument();
        ch.releaseAll(inst);
        inst.allNotesOff();
        ch.playingSlot.store(-1, std::memory_order_relaxed);
        ch.queuedSlot.store(kNoQueue, std::memory_order_relaxed);
        ch.playPosTicks.store(0, std::memory_order_relaxed);
    }
    playing_    = false;
    globalTick_ = 0;
    countdown_  = 0.0;
    playingFlag_.store(false, std::memory_order_relaxed);
    positionTicks_.store(0, std::memory_order_relaxed);
}

void Engine::drainLiveEvents() {
    uint32_t r = liveRead_.load(std::memory_order_relaxed);
    const uint32_t w = liveWrite_.load(std::memory_order_acquire);
    while (r != w) {
        const LiveEvent e = live_[r % kLiveCap];
        ++r;
        Channel& ch = channels_[e.channel];
        if (!ch.active.load(std::memory_order_relaxed)) continue;
        IInstrument& inst = ch.instrument();
        if (e.on) inst.noteOn(e.note, static_cast<float>(e.velocity) / 127.0f);
        else      inst.noteOff(e.note);
    }
    liveRead_.store(r, std::memory_order_release);
}

void Engine::triggerNotes(Channel& ch, Clip& clip, int local, int len, IInstrument& inst) {
    const int swingShift = static_cast<int>(std::lround(swing.get() * static_cast<float>(kTicksPerStep)));

    for (int i = 0; i < kMaxNotesPerClip; ++i) {
        const uint64_t w = clip.loadSlot(i);
        if (!Clip::isActive(w)) continue;
        const NoteEvent e = Clip::unpack(w);
        if (e.start >= len) continue;

        // Swing delays every second 16th step.
        int when = e.start;
        if (swingShift > 0 && ((e.start / kTicksPerStep) & 1)) when += swingShift;
        when = std::min(when, len - 1);
        if (when != local) continue;

        ch.releaseNote(e.pitch, inst);                      // same pitch still sounding: cut it first
        const int endLocal = std::min(when + std::max(e.length, 1), len);
        ch.addPending(e.pitch, globalTick_ + (endLocal - local), inst);
        inst.noteOn(e.pitch, static_cast<float>(e.velocity) / 127.0f);
    }
}

void Engine::processTick() {
    const int  quant  = launchQuantizeTicks.load(std::memory_order_relaxed);
    const bool onGrid = quant <= 1 || (globalTick_ % quant) == 0;

    for (auto& ch : channels_) {
        if (!ch.active.load(std::memory_order_relaxed)) continue;
        IInstrument& inst = ch.instrument();

        // 1. Note-offs that are due.
        ch.releaseDue(globalTick_, inst);

        // 2. Launches queued from the UI, applied on the quantization grid.
        const int queued = ch.queuedSlot.load(std::memory_order_acquire);
        if (queued != kNoQueue && onGrid) {
            ch.releaseAll(inst);
            ch.queuedSlot.store(kNoQueue, std::memory_order_relaxed);
            if (queued == kQueueStop) {
                ch.playingSlot.store(-1, std::memory_order_relaxed);
            } else if (queued >= 0 && queued < kMaxScenes
                       && ch.clips[static_cast<size_t>(queued)].exists.load(std::memory_order_acquire)) {
                ch.playingSlot.store(queued, std::memory_order_relaxed);
                ch.clipStartTick = globalTick_;
            }
        }

        // 3. Note-ons from the playing clip.
        const int slot = ch.playingSlot.load(std::memory_order_relaxed);
        if (slot < 0) continue;
        Clip& clip = ch.clips[static_cast<size_t>(slot)];
        if (!clip.exists.load(std::memory_order_acquire)) {      // deleted while playing
            ch.releaseAll(inst);
            ch.playingSlot.store(-1, std::memory_order_relaxed);
            continue;
        }
        const int len   = std::max(1, clip.lengthTicks.load(std::memory_order_relaxed));
        const int local = static_cast<int>((globalTick_ - ch.clipStartTick) % len);
        ch.playPosTicks.store(local, std::memory_order_relaxed);
        triggerNotes(ch, clip, local, len, inst);
    }
}

void Engine::renderInstruments(int pos, int count) {
    for (int i = 0; i < kMaxChannels; ++i) {
        Channel& ch = channels_[static_cast<size_t>(i)];
        if (!ch.active.load(std::memory_order_relaxed)) continue;
        ch.instrument().render(chL_[static_cast<size_t>(i)].data() + pos,
                               chR_[static_cast<size_t>(i)].data() + pos, count);
    }
}

void Engine::processStrip(Strip& s, float* L, float* R, int n, bool audible, bool meter) {
    for (auto& slot : s.fx) {
        IEffect* fx = slot.effect.load(std::memory_order_acquire);
        if (fx && !slot.bypass.load(std::memory_order_relaxed)) fx->process(L, R, n);
    }

    const float target = audible ? dbToGain(s.volumeDb.load(std::memory_order_relaxed)) : 0.0f;
    const float g0     = s.lastGain < 0.0f ? target : s.lastGain;
    const float pan    = std::clamp(s.pan.load(std::memory_order_relaxed), -1.0f, 1.0f);
    const float pl     = pan > 0.0f ? 1.0f - pan : 1.0f;
    const float pr     = pan < 0.0f ? 1.0f + pan : 1.0f;
    const float step   = (target - g0) / static_cast<float>(n);

    float pkL = 0.0f, pkR = 0.0f, g = g0;
    for (int i = 0; i < n; ++i) {
        g += step;
        L[i] *= g * pl;
        R[i] *= g * pr;
        pkL = std::max(pkL, std::fabs(L[i]));
        pkR = std::max(pkR, std::fabs(R[i]));
    }
    s.lastGain = target;
    if (meter) s.updatePeak(pkL, pkR);
}

void Engine::mix(float* outL, float* outR, int n) {
    const auto un = static_cast<size_t>(n);
    std::fill_n(mixL_.begin(), un, 0.0f);
    std::fill_n(mixR_.begin(), un, 0.0f);
    for (int r = 0; r < kNumReturns; ++r) {
        std::fill_n(retL_[static_cast<size_t>(r)].begin(), un, 0.0f);
        std::fill_n(retR_[static_cast<size_t>(r)].begin(), un, 0.0f);
    }

    bool anySolo = false;
    for (auto& ch : channels_)
        if (ch.active.load(std::memory_order_relaxed) && ch.solo.load(std::memory_order_relaxed)) anySolo = true;

    float* mL = mixL_.data();
    float* mR = mixR_.data();

    for (int c = 0; c < kMaxChannels; ++c) {
        Channel& ch = channels_[static_cast<size_t>(c)];
        if (!ch.active.load(std::memory_order_relaxed)) continue;

        float* L = chL_[static_cast<size_t>(c)].data();
        float* R = chR_[static_cast<size_t>(c)].data();
        const bool audible = !ch.mute.load(std::memory_order_relaxed)
                          && (!anySolo || ch.solo.load(std::memory_order_relaxed));
        processStrip(ch, L, R, n, audible);

        for (int r = 0; r < kNumReturns; ++r) {
            const float send = ch.sends[static_cast<size_t>(r)].load(std::memory_order_relaxed);
            if (send <= 0.0f) continue;
            float* rl = retL_[static_cast<size_t>(r)].data();
            float* rr = retR_[static_cast<size_t>(r)].data();
            for (int i = 0; i < n; ++i) { rl[i] += L[i] * send; rr[i] += R[i] * send; }
        }
        for (int i = 0; i < n; ++i) { mL[i] += L[i]; mR[i] += R[i]; }
    }

    for (int r = 0; r < kNumReturns; ++r) {
        Strip& s = returns_[static_cast<size_t>(r)];
        float* rl = retL_[static_cast<size_t>(r)].data();
        float* rr = retR_[static_cast<size_t>(r)].data();
        processStrip(s, rl, rr, n, !s.mute.load(std::memory_order_relaxed));
        for (int i = 0; i < n; ++i) { mL[i] += rl[i]; mR[i] += rr[i]; }
    }

    processStrip(master_, mL, mR, n, true, /*meter=*/false);
    limiter_.process(mL, mR, n);

    float pkL = 0.0f, pkR = 0.0f;
    for (int i = 0; i < n; ++i) {
        outL[i] = mL[i];
        outR[i] = mR[i];
        pkL = std::max(pkL, std::fabs(mL[i]));
        pkR = std::max(pkR, std::fabs(mR[i]));
    }
    master_.updatePeak(pkL, pkR);
}

void Engine::processChunk(float* outL, float* outR, int n) {
    applyTransportCommand();
    drainLiveEvents();

    for (int c = 0; c < kMaxChannels; ++c) {
        if (!channels_[static_cast<size_t>(c)].active.load(std::memory_order_relaxed)) continue;
        std::fill_n(chL_[static_cast<size_t>(c)].begin(), static_cast<size_t>(n), 0.0f);
        std::fill_n(chR_[static_cast<size_t>(c)].begin(), static_cast<size_t>(n), 0.0f);
    }

    if (playing_) {
        // Walk the block tick by tick. Events fire at the sample where their tick begins;
        // instruments render in segments between ticks, so notes start sample-accurately,
        // while effects (which want whole blocks) run afterwards in mix().
        int pos = 0;
        while (pos < n) {
            if (countdown_ <= 0.0) {
                processTick();
                ++globalTick_;
                countdown_ += samplesPerTick();
            }
            const int seg = std::clamp(static_cast<int>(std::ceil(countdown_)), 1, n - pos);
            renderInstruments(pos, seg);
            pos += seg;
            countdown_ -= seg;
        }
        positionTicks_.store(globalTick_, std::memory_order_relaxed);
    } else {
        renderInstruments(0, n);
    }

    mix(outL, outR, n);
    blockCounter_.fetch_add(1, std::memory_order_release);
}

// ===========================================================================
// Offline rendering
// ===========================================================================

void Engine::renderOffline(int scene, int bars, float tailSeconds,
                           std::vector<float>& left, std::vector<float>& right) {
    dsp::ScopedFlushDenormals noDenormals;

    // What should play?
    std::array<int, kMaxChannels> slots{};
    for (int i = 0; i < kMaxChannels; ++i) {
        Channel& ch = channels_[static_cast<size_t>(i)];
        int s = -1;
        if (ch.active.load()) {
            if (scene >= 0) {
                s = (scene < kMaxScenes && ch.clips[static_cast<size_t>(scene)].exists.load()) ? scene : -1;
            } else {
                const int q = ch.queuedSlot.load();
                s = q >= 0 ? q : (q == kQueueStop ? -1 : ch.playingSlot.load());
            }
        }
        slots[static_cast<size_t>(i)] = s;
    }

    // Start from a clean slate: no leftover voices, effect tails or limiter state.
    stopTransportInternal();
    transportCmd_.store(0);
    liveRead_.store(liveWrite_.load());
    prepareInstruments();
    for (auto& ch : channels_)
        for (auto& slot : ch.fx) if (auto* fx = slot.effect.load()) fx->reset();
    for (auto& r : returns_)
        for (auto& slot : r.fx) if (auto* fx = slot.effect.load()) fx->reset();
    for (auto& slot : master_.fx) if (auto* fx = slot.effect.load()) fx->reset();
    limiter_.reset();
    for (auto& ch : channels_) ch.lastGain = -1.0f;
    for (auto& r : returns_) r.lastGain = -1.0f;
    master_.lastGain = -1.0f;

    for (int i = 0; i < kMaxChannels; ++i)
        if (slots[static_cast<size_t>(i)] >= 0)
            channels_[static_cast<size_t>(i)].queuedSlot.store(slots[static_cast<size_t>(i)]);

    playing_ = true;
    globalTick_ = 0;
    countdown_ = 0.0;

    const auto totalFrames = static_cast<int64_t>(
        std::llround(static_cast<double>(std::max(bars, 1)) * kTicksPerBar * samplesPerTick()));
    const auto tailFrames = static_cast<int64_t>(std::max(tailSeconds, 0.0f) * sampleRate_);

    left.assign(static_cast<size_t>(totalFrames + tailFrames), 0.0f);
    right.assign(static_cast<size_t>(totalFrames + tailFrames), 0.0f);

    const int chunk = std::min(blockSize_, 1024);
    int64_t done = 0;
    bool clipsStopped = false;
    while (done < totalFrames + tailFrames) {
        if (!clipsStopped && done >= totalFrames) {
            for (auto& ch : channels_) {
                if (!ch.active.load()) continue;
                IInstrument& inst = ch.instrument();
                ch.releaseAll(inst);
                inst.allNotesOff();
                ch.playingSlot.store(-1);
                ch.queuedSlot.store(kNoQueue);
            }
            clipsStopped = true;
        }
        int64_t limit = clipsStopped ? (totalFrames + tailFrames) : totalFrames;
        const int n = static_cast<int>(std::min<int64_t>(chunk, limit - done));
        processChunk(left.data() + done, right.data() + done, n);
        done += n;
    }

    stopTransportInternal();
}

}  // namespace fdm
