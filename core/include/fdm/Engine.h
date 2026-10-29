#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "fdm/Channel.h"
#include "fdm/dsp/Dsp.h"

namespace fdm {

/** The whole audio engine: transport, channels, mixer and master bus.

    Threading model
      * process() runs on the audio thread and never allocates, locks or blocks.
      * Everything else is called from the UI/message thread. Shared state is
        either atomic (parameters, mixer, clip notes) or goes through a
        lock-free queue (live MIDI) or a handshake (swapEffect()).

    Signal flow
      instrument -> FX chain -> fader/balance -> master
                                     \-> sends -> returns (FX chain, fader) -> master
      master -> FX chain -> fader -> safety limiter -> output */
class Engine {
public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    /** Call with the audio stopped, before the first process() and whenever the device changes. */
    void   prepare(double sampleRate, int maxBlockSize);
    double sampleRate() const { return sampleRate_; }

    /** Audio thread. Overwrites the output buffers with the stereo mix. */
    void process(float* outL, float* outR, int numFrames);

    // ---- transport -------------------------------------------------------
    void    play();
    void    stop();                        // stops the transport and all clips
    bool    isPlaying() const { return playingFlag_.load(std::memory_order_relaxed); }
    int64_t positionTicks() const { return positionTicks_.load(std::memory_order_relaxed); }

    Param bpm   {"bpm",   "Tempo", 40.0f, 300.0f, 120.0f};
    Param swing {"swing", "Swing", 0.0f, 0.5f, 0.0f};     // fraction of a 16th step the off-beat 16ths are delayed
    std::atomic<int> launchQuantizeTicks{kTicksPerBar};   // 0 = launch at the next tick

    // ---- channels & mixer ------------------------------------------------
    Channel& channel(int i) { return channels_[static_cast<size_t>(i)]; }
    Strip&   returnStrip(int i) { return returns_[static_cast<size_t>(i)]; }
    Strip&   master() { return master_; }

    int  activeChannelCount() const;
    int  addChannel(InstrumentType type);   // index of the new channel, or -1 if full
    void removeChannel(int index);

    // ---- session clip launching (all quantized to launchQuantizeTicks) ----
    void launchClip(int channel, int slot);
    void stopClip(int channel);
    void launchScene(int slot);
    void stopAllClips();

    // ---- live input (e.g. a MIDI keyboard) -------------------------------
    void liveNoteOn(int channel, int note, int velocity);
    void liveNoteOff(int channel, int note);

    // ---- effects ---------------------------------------------------------
    /** Puts `fx` in the slot and returns whatever was there. When the audio thread is
        running, waits for it to finish its current block so the old effect can be
        destroyed safely. Call from the UI thread only. */
    std::unique_ptr<IEffect> swapEffect(EffectSlot& slot, std::unique_ptr<IEffect> fx);

    /** Tell the engine whether a real-time thread is calling process(). */
    void setAudioThreadRunning(bool running) { audioRunning_.store(running); }

    // ---- offline rendering (WAV export, tests) ---------------------------
    /** Renders `bars` bars starting from tick 0, then `tailSeconds` of tail with the clips stopped.
        scene >= 0 plays that scene; scene < 0 plays whatever clips are currently playing.
        The audio thread must NOT be running (remove the audio callback first). Leaves the transport stopped. */
    void renderOffline(int scene, int bars, float tailSeconds, std::vector<float>& left, std::vector<float>& right);

private:
    struct LiveEvent { uint8_t channel = 0, note = 0, velocity = 0; bool on = false; };

    void processChunk(float* outL, float* outR, int n);
    void applyTransportCommand();
    void stopTransportInternal();
    void drainLiveEvents();
    void processTick();
    void triggerNotes(Channel& ch, Clip& clip, int local, int len, IInstrument& inst);
    void renderInstruments(int pos, int count);
    void mix(float* outL, float* outR, int n);
    void processStrip(Strip& s, float* L, float* R, int n, bool audible, bool meter = true);
    void pushLive(const LiveEvent& e);
    void prepareInstruments();
    double samplesPerTick() const { return sampleRate_ * 60.0 / (static_cast<double>(bpm.get()) * kPPQ); }

    std::array<Channel, kMaxChannels> channels_;
    std::array<Strip, kNumReturns>    returns_;
    Strip                             master_;

    std::array<std::vector<float>, kMaxChannels> chL_, chR_;
    std::array<std::vector<float>, kNumReturns>  retL_, retR_;
    std::vector<float> mixL_, mixR_;
    dsp::Limiter       limiter_;

    double sampleRate_ = 48000.0;
    int    blockSize_  = 512;

    // transport (audio-thread owned, except the atomics)
    static constexpr int kCmdStart = 1, kCmdStop = 2;
    std::atomic<int>     transportCmd_{0};
    bool                 playing_    = false;
    int64_t              globalTick_ = 0;
    double               countdown_  = 0.0;     // samples until the next tick fires
    std::atomic<bool>    playingFlag_{false};
    std::atomic<int64_t> positionTicks_{0};

    // live MIDI queue: many producers (spin-locked), one consumer (audio thread)
    static constexpr uint32_t kLiveCap = 1024;
    std::array<LiveEvent, kLiveCap> live_{};
    std::atomic<uint32_t>           liveWrite_{0}, liveRead_{0};
    std::atomic_flag                liveLock_ = ATOMIC_FLAG_INIT;

    // swapEffect handshake
    std::atomic<bool>     audioRunning_{false};
    std::atomic<uint64_t> blockCounter_{0};
};

}  // namespace fdm
