#include "TestHarness.h"

#include "fdm/DrumRack.h"
#include "fdm/PolySynth.h"

using namespace fdm;

namespace {

constexpr double kSr = 48000.0;

std::vector<float> renderInstrument(IInstrument& inst, int frames) {
    std::vector<float> l(static_cast<size_t>(frames), 0.0f), r(static_cast<size_t>(frames), 0.0f);
    int done = 0;
    while (done < frames) {
        const int n = std::min(256, frames - done);
        inst.render(l.data() + done, r.data() + done, n);
        done += n;
    }
    return l;
}

int zeroCrossings(const std::vector<float>& v, size_t from, size_t to) {
    int c = 0;
    for (size_t i = from + 1; i < to; ++i)
        if ((v[i - 1] < 0.0f) != (v[i] < 0.0f)) ++c;
    return c;
}

void makePureSine(PolySynth& s) {
    s.params().find("osc1.wave")->set(3);       // sine
    s.params().find("osc2.level")->set(0);
    s.params().find("cutoff")->set(20000);
    s.params().find("resonance")->set(0);
    s.params().find("env.amount")->set(0);
    s.params().find("keytrack")->set(0);
    s.params().find("amp.a")->set(0.001f);
    s.params().find("amp.s")->set(1.0f);
}

}  // namespace

// ---------------------------------------------------------------------------
// Drum rack
// ---------------------------------------------------------------------------

TEST(drum_pads_follow_the_general_midi_map) {
    CHECK(DrumRack::padForNote(36) == 0);   // kick
    CHECK(DrumRack::padForNote(38) == 2);   // snare
    CHECK(DrumRack::padForNote(42) == 5);   // closed hat
    CHECK(DrumRack::padForNote(46) == 6);   // open hat
    CHECK(DrumRack::padForNote(60) == -1);
    for (int p = 0; p < DrumRack::kNumPads; ++p) CHECK(DrumRack::padForNote(DrumRack::padNote(p)) == p);
}

TEST(every_drum_makes_sound_then_decays_to_silence_without_nans) {
    for (int pad = 0; pad < DrumRack::kNumPads; ++pad) {
        DrumRack d;
        d.prepare(kSr, 256);
        d.noteOn(DrumRack::padNote(pad), 1.0f);
        const auto out = renderInstrument(d, static_cast<int>(kSr * 3));
        CHECK(allFinite(out));
        const float early = peakOf(out, 0, 4800);
        CHECK(early > 0.1f);
        CHECK(early < 1.6f);
        CHECK(peakOf(out, out.size() - 2000) < 1.0e-3f);          // fully decayed
        if (early <= 0.1f) std::printf("    (pad %d: %s was silent)\n", pad, DrumRack::padName(pad));
    }
}

TEST(drum_velocity_scales_level) {
    DrumRack a, b;
    a.prepare(kSr, 256);
    b.prepare(kSr, 256);
    a.noteOn(38, 1.0f);
    b.noteOn(38, 0.5f);
    const auto oa = renderInstrument(a, 4800);
    const auto ob = renderInstrument(b, 4800);
    CHECK_NEAR(peakOf(ob) / peakOf(oa), 0.5, 0.02);
}

TEST(kick_is_low_frequency_and_hat_is_high_frequency) {
    DrumRack d;
    d.prepare(kSr, 256);
    d.noteOn(36, 1.0f);
    const auto kick = renderInstrument(d, 24000);
    // body after the pitch drop: ~50 Hz => about 100 zero crossings per second
    const int kickZc = zeroCrossings(kick, 6000, 18000);                  // 0.25 s window
    CHECK(kickZc < 40);

    DrumRack h;
    h.prepare(kSr, 256);
    h.noteOn(42, 1.0f);
    const auto hat = renderInstrument(h, 4800);
    const int hatZc = zeroCrossings(hat, 0, 1500);
    CHECK(hatZc > 150);                                                    // lots of high-frequency content
}

TEST(closed_hat_chokes_open_hat) {
    auto energyAfter = [](bool chokeWithClosed) {
        DrumRack d;
        d.prepare(kSr, 256);
        d.noteOn(46, 1.0f);                                    // open hat
        renderInstrument(d, 4800);                             // 100 ms
        if (chokeWithClosed) d.noteOn(42, 0.01f);              // a nearly silent closed hat
        auto tail = renderInstrument(d, 9600);                 // next 200 ms
        // Skip the first 20 ms: the choke is a deliberate 4 ms fade rather than a click.
        return rmsOf(tail, 960);
    };
    const float ringing = energyAfter(false);
    const float choked  = energyAfter(true);
    CHECK(ringing > 0.01f);
    CHECK(choked < ringing * 0.01f);
}

TEST(pad_tune_decay_pan_and_volume_parameters_work) {
    DrumRack base, tuned;
    base.prepare(kSr, 256);
    tuned.prepare(kSr, 256);
    tuned.params().find("pad0.tune")->set(12);                 // kick up an octave
    base.noteOn(36, 1.0f);
    tuned.noteOn(36, 1.0f);
    const auto a = renderInstrument(base, 24000);
    const auto b = renderInstrument(tuned, 24000);
    CHECK(zeroCrossings(b, 6000, 18000) > zeroCrossings(a, 6000, 18000) * 3 / 2);

    DrumRack panned;
    panned.prepare(kSr, 256);
    panned.params().find("pad2.pan")->set(1.0f);               // snare hard right
    panned.noteOn(38, 1.0f);
    std::vector<float> l(2000, 0.0f), r(2000, 0.0f);
    panned.render(l.data(), r.data(), 2000);
    CHECK(peakOf(l) == 0.0f);
    CHECK(peakOf(r) > 0.1f);
}

TEST(drum_rack_is_deterministic) {
    auto go = []() {
        DrumRack d;
        d.prepare(kSr, 256);
        d.noteOn(38, 1.0f);
        d.noteOn(39, 0.8f);
        return renderInstrument(d, 20000);
    };
    CHECK(go() == go());
}

// ---------------------------------------------------------------------------
// Poly synth
// ---------------------------------------------------------------------------

TEST(synth_plays_the_right_pitch) {
    PolySynth s;
    s.prepare(kSr, 256);
    makePureSine(s);
    s.noteOn(69, 1.0f);                                        // A4 = 440 Hz
    const auto out = renderInstrument(s, static_cast<int>(kSr));
    const int zc = zeroCrossings(out, 4800, static_cast<size_t>(kSr) - 4800);
    // window is 0.8 s: 440 Hz * 0.8 s * 2 crossings = 704
    CHECK_NEAR(zc, 704, 4);
}

TEST(synth_octaves_are_exact) {
    auto freq = [](int note) {
        PolySynth s;
        s.prepare(kSr, 256);
        makePureSine(s);
        s.noteOn(note, 1.0f);
        const auto out = renderInstrument(s, static_cast<int>(kSr));
        return zeroCrossings(out, 4800, static_cast<size_t>(kSr) - 4800) / 2.0 / 0.8;
    };
    CHECK_NEAR(freq(81) / freq(69), 2.0, 0.02);
    CHECK_NEAR(freq(57) / freq(69), 0.5, 0.01);
}

TEST(synth_note_off_releases_and_frees_the_voice) {
    PolySynth s;
    s.prepare(kSr, 256);
    s.params().find("amp.r")->set(0.1f);
    s.noteOn(60, 1.0f);
    renderInstrument(s, 4800);
    CHECK(s.activeVoiceCount() == 1);
    s.noteOff(60);
    const auto tail = renderInstrument(s, static_cast<int>(kSr));
    CHECK(s.activeVoiceCount() == 0);
    CHECK(peakOf(tail, tail.size() - 1000) == 0.0f);
    CHECK(peakOf(tail, 0, 200) > 0.0f);                        // it did ring for a moment first
}

TEST(synth_is_polyphonic_and_steals_voices_gracefully) {
    PolySynth s;
    s.prepare(kSr, 256);
    for (int n = 48; n < 48 + 8; ++n) s.noteOn(n, 0.8f);
    CHECK(s.activeVoiceCount() == PolySynth::kNumVoices);
    s.noteOn(80, 0.8f);                                        // 9th note steals, never grows
    CHECK(s.activeVoiceCount() == PolySynth::kNumVoices);
    const auto out = renderInstrument(s, 9600);
    CHECK(allFinite(out));
    CHECK(peakOf(out) > 0.05f);
    // Nine dense notes at once is a worst case; the master limiter handles anything above 1.0,
    // but the synth itself must leave sensible headroom.
    CHECK(peakOf(out) < 1.5f);
}

TEST(synth_single_voice_is_bounded_with_headroom) {
    PolySynth s;
    s.prepare(kSr, 256);
    s.noteOn(48, 1.0f);
    const auto out = renderInstrument(s, 24000);
    CHECK(peakOf(out) > 0.1f);
    CHECK(peakOf(out) < 0.45f);                                // about -7 dBFS at most
}

TEST(synth_retriggering_a_held_note_does_not_use_another_voice) {
    PolySynth s;
    s.prepare(kSr, 256);
    s.noteOn(60, 1.0f);
    s.noteOn(60, 1.0f);
    s.noteOn(60, 1.0f);
    CHECK(s.activeVoiceCount() == 1);
}

TEST(synth_filter_cutoff_changes_brightness) {
    // Brightness = how much of the signal's energy is in its sample-to-sample change.
    auto brightness = [](float cutoff) {
        PolySynth s;
        s.prepare(kSr, 256);
        s.params().find("env.amount")->set(0);
        s.params().find("keytrack")->set(0);
        s.params().find("cutoff")->set(cutoff);
        s.noteOn(48, 1.0f);
        const auto out = renderInstrument(s, 24000);
        double d = 0.0, e = 0.0;
        for (size_t i = 4801; i < out.size(); ++i) {
            const double x = static_cast<double>(out[i]) - static_cast<double>(out[i - 1]);
            d += x * x;
            e += static_cast<double>(out[i]) * static_cast<double>(out[i]);
        }
        return std::sqrt(d / e);
    };
    CHECK(brightness(8000) > brightness(300) * 3.0);
}

TEST(synth_survives_extreme_settings) {
    PolySynth s;
    s.prepare(kSr, 256);
    s.params().find("resonance")->set(1.0f);
    s.params().find("cutoff")->set(20000);
    s.params().find("env.amount")->set(1.0f);
    s.params().find("amp.a")->set(0.001f);
    s.params().find("volume")->set(6.0f);
    for (int n = 20; n < 28; ++n) s.noteOn(n * 4, 1.0f);       // very low to very high
    CHECK(allFinite(renderInstrument(s, 48000)));
}

TEST(synth_is_deterministic) {
    auto go = []() {
        PolySynth s;
        s.prepare(kSr, 256);
        s.noteOn(60, 1.0f);
        s.noteOn(64, 0.7f);
        return renderInstrument(s, 12000);
    };
    CHECK(go() == go());
}
