#include "fdm/DrumRack.h"

#include <cmath>
#include <string>

namespace fdm {

namespace {

enum class Kind { Kick, Rim, Snare, Clap, Tom, ClosedHat, OpenHat, Cowbell };

constexpr int kNotes[DrumRack::kNumPads] = {36, 37, 38, 39, 41, 42, 46, 56};
constexpr const char* kNames[DrumRack::kNumPads] = {
    "Kick", "Rim", "Snare", "Clap", "Tom", "Closed Hat", "Open Hat", "Cowbell"};
constexpr Kind kKinds[DrumRack::kNumPads] = {
    Kind::Kick, Kind::Rim, Kind::Snare, Kind::Clap, Kind::Tom, Kind::ClosedHat, Kind::OpenHat, Kind::Cowbell};

constexpr int kClosedHatPad = 5;
constexpr int kOpenHatPad   = 6;

// Per-sample exponential decay coefficient for a time constant in seconds.
inline float coefFor(float seconds, float sr) { return std::exp(-1.0f / (std::max(seconds, 1.0e-4f) * sr)); }

}  // namespace

// One drum voice. Each pad owns one (monophonic: a new hit restarts it).
struct DrumRack::Voice {
    Kind  kind = Kind::Kick;
    bool  active = false;
    float sr = 48000.0f;
    float vel = 1.0f;

    // generic state
    float ph1 = 0, ph2 = 0;
    float env1 = 0, env2 = 0, env3 = 0;
    float c1 = 0, c2 = 0, c3 = 0;       // decay coefficients
    float f1 = 0, f2 = 0;               // frequencies
    float pitchEnv = 0, pitchCoef = 0;
    int   n = 0;                        // samples since trigger
    float hatPhase[6] = {};
    dsp::Svf   filt;
    dsp::Noise noise;

    void prepare(float sampleRate, uint32_t seed) {
        sr = sampleRate;
        filt.setSampleRate(sampleRate);
        noise.seed(seed);
        active = false;
    }

    void choke() {
        if (!active) return;
        c1 = c2 = c3 = coefFor(0.004f, sr);   // 4 ms fade, no click
    }

    void trigger(Kind k, float velocity, float tuneSemis, float decay) {
        kind = k;
        vel = velocity;
        active = true;
        n = 0;
        ph1 = ph2 = 0.0f;
        filt.reset();
        const float tune = std::exp2(tuneSemis / 12.0f);

        switch (k) {
            case Kind::Kick:
                f2 = 50.0f * tune;             // resting pitch
                f1 = f2 * 3.5f;                // pitch at the click
                pitchEnv = 1.0f;
                pitchCoef = coefFor(0.035f, sr);
                env1 = 1.0f;  c1 = coefFor(0.11f * decay, sr);
                env2 = 1.0f;  c2 = coefFor(0.0015f, sr);        // click
                break;
            case Kind::Rim:
                f1 = 1700.0f * tune;  f2 = 480.0f * tune;
                env1 = 1.0f;  c1 = coefFor(0.006f * decay, sr);
                env2 = 1.0f;  c2 = coefFor(0.002f, sr);
                break;
            case Kind::Snare:
                f1 = 190.0f * tune;
                env1 = 1.0f;  c1 = coefFor(0.045f * decay, sr);  // tone
                env2 = 1.0f;  c2 = coefFor(0.075f * decay, sr);  // noise
                filt.set(1500.0f * tune, 0.1f);
                break;
            case Kind::Clap:
                filt.set(1250.0f * tune, 0.45f);
                c1 = coefFor(0.0022f, sr);
                c2 = coefFor(0.075f * decay, sr);
                env1 = 1.0f;
                env2 = 1.0f;
                break;
            case Kind::Tom:
                f2 = 95.0f * tune;
                f1 = f2 * 1.7f;
                pitchEnv = 1.0f;
                pitchCoef = coefFor(0.05f, sr);
                env1 = 1.0f;  c1 = coefFor(0.13f * decay, sr);
                break;
            case Kind::ClosedHat:
            case Kind::OpenHat:
                filt.set(7000.0f * tune, 0.15f);
                for (float& p : hatPhase) p = 0.0f;
                f1 = tune;
                env1 = 1.0f;
                c1 = coefFor((k == Kind::ClosedHat ? 0.02f : 0.17f) * decay, sr);
                break;
            case Kind::Cowbell:
                f1 = 560.0f * tune;  f2 = 845.0f * tune;
                filt.set(2600.0f * tune, 0.35f);
                env1 = 1.0f;  c1 = coefFor(0.08f * decay, sr);
                break;
        }
    }

    float next() {
        if (!active) return 0.0f;
        float out = 0.0f;
        const float invSr = 1.0f / sr;

        switch (kind) {
            case Kind::Kick: {
                const float f = f2 + (f1 - f2) * pitchEnv;
                ph1 += f * invSr;  ph1 -= std::floor(ph1);
                const float body  = std::tanh(1.6f * std::sin(kTwoPi * ph1)) * 0.85f;
                const float click = noise.next() * env2 * 0.25f;
                out = body * env1 + click;
                pitchEnv *= pitchCoef;  env1 *= c1;  env2 *= c2;
                if (env1 < 1.0e-4f) active = false;
                break;
            }
            case Kind::Rim: {
                ph1 += f1 * invSr;  ph1 -= std::floor(ph1);
                ph2 += f2 * invSr;  ph2 -= std::floor(ph2);
                out = (0.5f * std::sin(kTwoPi * ph1) + 0.5f * std::sin(kTwoPi * ph2)) * env1
                    + noise.next() * env2 * 0.3f;
                env1 *= c1;  env2 *= c2;
                if (env1 < 1.0e-4f) active = false;
                break;
            }
            case Kind::Snare: {
                ph1 += f1 * (1.0f + 0.25f * env1) * invSr;  ph1 -= std::floor(ph1);
                const float tone = std::sin(kTwoPi * ph1) * env1 * 0.6f;
                const float nz   = filt.process(noise.next(), dsp::Svf::Mode::HighPass) * env2 * 0.8f;
                out = std::tanh(tone + nz);
                env1 *= c1;  env2 *= c2;
                if (env2 < 1.0e-4f) active = false;
                break;
            }
            case Kind::Clap: {
                const float t = static_cast<float>(n) * invSr;
                float burst = 0.0f;
                for (int k = 0; k < 3; ++k) {
                    const float dt = t - 0.011f * static_cast<float>(k);
                    if (dt >= 0.0f) burst = std::max(burst, std::exp(-dt / 0.0022f));
                }
                float tail = 0.0f;
                if (t >= 0.033f) { tail = env2 * 0.9f;  env2 *= c2; }
                const float env = std::max(burst, tail);
                out = filt.process(noise.next(), dsp::Svf::Mode::BandPass) * env * 1.8f;
                if (t > 0.033f && env2 < 1.0e-3f) active = false;
                break;
            }
            case Kind::Tom: {
                const float f = f2 + (f1 - f2) * pitchEnv;
                ph1 += f * invSr;  ph1 -= std::floor(ph1);
                out = std::sin(kTwoPi * ph1) * env1 + noise.next() * env1 * 0.04f;
                pitchEnv *= pitchCoef;  env1 *= c1;
                if (env1 < 1.0e-4f) active = false;
                break;
            }
            case Kind::ClosedHat:
            case Kind::OpenHat: {
                // Six detuned square waves at the classic 808 ratios, high-passed.
                static constexpr float kRatios[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};
                float sum = 0.0f;
                for (int k = 0; k < 6; ++k) {
                    hatPhase[k] += kRatios[k] * f1 * invSr;
                    hatPhase[k] -= std::floor(hatPhase[k]);
                    sum += hatPhase[k] < 0.5f ? 1.0f : -1.0f;
                }
                const float src = sum * (1.0f / 6.0f) + noise.next() * 0.25f;
                out = filt.process(src, dsp::Svf::Mode::HighPass) * env1 * 0.9f;
                env1 *= c1;
                if (env1 < 1.0e-4f) active = false;
                break;
            }
            case Kind::Cowbell: {
                ph1 += f1 * invSr;  ph1 -= std::floor(ph1);
                ph2 += f2 * invSr;  ph2 -= std::floor(ph2);
                const float sq = (ph1 < 0.5f ? 0.5f : -0.5f) + (ph2 < 0.5f ? 0.5f : -0.5f);
                out = filt.process(sq, dsp::Svf::Mode::BandPass) * env1 * 1.5f;
                env1 *= c1;
                if (env1 < 1.0e-4f) active = false;
                break;
            }
        }

        ++n;
        return out * vel;
    }
};

// ---------------------------------------------------------------------------

DrumRack::DrumRack() {
    volume_ = &params_.add("volume", "Volume", -36.0f, 6.0f, -3.0f);
    for (int i = 0; i < kNumPads; ++i) {
        const std::string id = "pad" + std::to_string(i) + ".";
        const std::string nm = std::string(kNames[i]) + " ";
        pad_[static_cast<size_t>(i)] = {
            &params_.add(id + "vol",   nm + "Vol",    -36.0f, 6.0f, 0.0f),
            &params_.add(id + "tune",  nm + "Tune",   -12.0f, 12.0f, 0.0f),
            &params_.add(id + "decay", nm + "Decay",  0.3f, 3.0f, 1.0f, true),
            &params_.add(id + "pan",   nm + "Pan",    -1.0f, 1.0f, 0.0f)};
        voices_[static_cast<size_t>(i)] = std::make_unique<Voice>();
    }
}

DrumRack::~DrumRack() = default;

int DrumRack::padNote(int pad) { return (pad >= 0 && pad < kNumPads) ? kNotes[pad] : -1; }
const char* DrumRack::padName(int pad) { return (pad >= 0 && pad < kNumPads) ? kNames[pad] : ""; }
int DrumRack::padForNote(int note) {
    for (int i = 0; i < kNumPads; ++i)
        if (kNotes[i] == note) return i;
    return -1;
}

void DrumRack::prepare(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    for (int i = 0; i < kNumPads; ++i)
        voices_[static_cast<size_t>(i)]->prepare(static_cast<float>(sampleRate), 0x9e3779b9u * static_cast<uint32_t>(i + 1));
}

void DrumRack::noteOn(int note, float velocity01) {
    const int pad = padForNote(note);
    if (pad < 0) return;
    const auto& p = pad_[static_cast<size_t>(pad)];
    voices_[static_cast<size_t>(pad)]->trigger(kKinds[pad], std::clamp(velocity01, 0.0f, 1.0f),
                                               p.tune->get(), p.decay->get());
    // A closed hat chokes an open hat.
    if (pad == kClosedHatPad) voices_[kOpenHatPad]->choke();
}

void DrumRack::render(float* left, float* right, int numFrames) {
    const float master = dbToGain(volume_->get());
    for (int pad = 0; pad < kNumPads; ++pad) {
        auto& v = *voices_[static_cast<size_t>(pad)];
        if (!v.active) continue;
        const auto& p = pad_[static_cast<size_t>(pad)];
        const float g   = dbToGain(p.volume->get()) * master;
        const float pan = p.pan->get();
        const float gl = g * (pan > 0.0f ? 1.0f - pan : 1.0f);
        const float gr = g * (pan < 0.0f ? 1.0f + pan : 1.0f);
        for (int i = 0; i < numFrames; ++i) {
            const float s = v.next();
            left[i]  += s * gl;
            right[i] += s * gr;
            if (!v.active) break;
        }
    }
}

}  // namespace fdm
