#include "TestHarness.h"

#include "fdm/DemoProject.h"

using namespace fdm;

namespace {

// At 120 BPM and 48 kHz one tick is exactly 25 samples, one beat 24000, one bar 96000.
constexpr double kSr = 48000.0;
constexpr int kBeat = 24000;
constexpr int kBar = 96000;

struct Fixture {
    Engine engine;
    MockInstrument mock;
    int ch = 0;

    Fixture() {
        engine.prepare(kSr, 512);
        engine.bpm.set(120.0f);
        engine.launchQuantizeTicks.store(0);
        ch = engine.addChannel(InstrumentType::DrumRack);
        engine.channel(ch).external.store(&mock);
        engine.prepare(kSr, 512);
    }
    Channel& channel() { return engine.channel(ch); }
    static void note(Clip& c, int pitch, int step, int lenSteps = 1, int vel = 100) {
        NoteEvent e;
        e.pitch = pitch; e.start = step * kTicksPerStep; e.length = lenSteps * kTicksPerStep; e.velocity = vel;
        c.addNote(e);
    }
};

}  // namespace

TEST(notes_land_sample_accurately_and_loop) {
    Fixture f;
    Clip& clip = f.channel().clips[0];
    clip.create(1);
    Fixture::note(clip, 60, 0);    // beat 1
    Fixture::note(clip, 62, 4);    // beat 2
    Fixture::note(clip, 64, 8);    // beat 3

    f.engine.launchClip(f.ch, 0);
    Rendered out;
    run(f.engine, 2 * kBar, out);

    // Bar 1 and bar 2 (the clip loops). Allow +-1 sample for tick rounding.
    for (int bar = 0; bar < 2; ++bar) {
        const long long base = static_cast<long long>(bar) * kBar;
        auto* a = f.mock.nth(60, true, bar);
        auto* b = f.mock.nth(62, true, bar);
        auto* c = f.mock.nth(64, true, bar);
        CHECK(a && b && c);
        if (!(a && b && c)) return;
        CHECK_NEAR(a->sample, base + 0, 1);
        CHECK_NEAR(b->sample, base + kBeat, 1);
        CHECK_NEAR(c->sample, base + 2 * kBeat, 1);
        // each note is one 16th (6000 samples) long
        auto* aOff = f.mock.nth(60, false, bar);
        CHECK(aOff != nullptr);
        if (aOff) CHECK_NEAR(aOff->sample, base + kBeat / 4, 1);
    }
    CHECK(f.mock.countOn(60) == 2);
}

TEST(timing_is_independent_of_block_size) {
    for (int block : {1, 7, 64, 512, 4096}) {
        Fixture f;
        Clip& clip = f.channel().clips[0];
        clip.create(1);
        Fixture::note(clip, 60, 5);   // 1.25 beats
        f.engine.launchClip(f.ch, 0);
        Rendered out;
        run(f.engine, kBar, out, block);
        auto* e = f.mock.nth(60, true, 0);
        CHECK(e != nullptr);
        if (e) CHECK_NEAR(e->sample, 5 * kBeat / 4, 1);
    }
}

TEST(tempo_changes_the_grid) {
    Fixture f;
    f.engine.bpm.set(60.0f);       // beat = 48000 samples
    Clip& clip = f.channel().clips[0];
    clip.create(1);
    Fixture::note(clip, 60, 4);
    f.engine.launchClip(f.ch, 0);
    Rendered out;
    run(f.engine, 3 * 48000, out);
    auto* e = f.mock.nth(60, true, 0);
    CHECK(e != nullptr);
    if (e) CHECK_NEAR(e->sample, 48000, 1);
}

TEST(swing_delays_odd_sixteenths) {
    Fixture f;
    f.engine.swing.set(0.25f);     // a quarter of a 16th = 60 ticks = 1500 samples
    Clip& clip = f.channel().clips[0];
    clip.create(1);
    Fixture::note(clip, 60, 0);    // on the grid
    Fixture::note(clip, 62, 1);    // odd 16th: delayed
    Fixture::note(clip, 64, 2);    // on the grid
    f.engine.launchClip(f.ch, 0);
    Rendered out;
    run(f.engine, kBar, out);
    CHECK_NEAR(f.mock.nth(60, true, 0)->sample, 0, 1);
    CHECK_NEAR(f.mock.nth(62, true, 0)->sample, kBeat / 4 + 1500, 1);
    CHECK_NEAR(f.mock.nth(64, true, 0)->sample, 2 * kBeat / 4, 1);
}

TEST(clip_launch_is_quantized_to_the_bar) {
    Fixture f;
    f.engine.launchQuantizeTicks.store(kTicksPerBar);
    Clip& a = f.channel().clips[0];
    a.create(4);
    Fixture::note(a, 60, 0, 64);           // one note held for all 4 bars
    Clip& b = f.channel().clips[1];
    b.create(1);
    Fixture::note(b, 72, 0);

    f.engine.launchClip(f.ch, 0);
    Rendered out;
    run(f.engine, 30000, out);             // part-way through bar 1
    f.engine.launchClip(f.ch, 1);
    run(f.engine, 3 * kBar, out);

    // B starts exactly at the next bar line, not when it was clicked.
    auto* on72 = f.mock.nth(72, true, 0);
    CHECK(on72 != nullptr);
    if (on72) CHECK_NEAR(on72->sample, kBar, 1);
    // ...and A's long note is released at that moment (no stuck note).
    auto* off60 = f.mock.nth(60, false, 0);
    CHECK(off60 != nullptr);
    if (off60) CHECK_NEAR(off60->sample, kBar, 1);
    CHECK(f.channel().playingSlot.load() == 1);
}

TEST(launching_with_no_quantize_is_immediate) {
    Fixture f;
    Clip& a = f.channel().clips[0];
    a.create(1);
    Fixture::note(a, 60, 0);
    f.engine.launchClip(f.ch, 0);
    Rendered out;
    run(f.engine, 10000, out);
    CHECK(f.mock.countOn(60) == 1);
    CHECK(f.engine.isPlaying());
}

TEST(stop_clip_releases_notes_and_silences_the_channel) {
    Fixture f;
    f.engine.launchQuantizeTicks.store(kTicksPerBar);
    Clip& a = f.channel().clips[0];
    a.create(1);
    Fixture::note(a, 60, 0, 16);
    f.engine.launchClip(f.ch, 0);
    Rendered out;
    run(f.engine, 1000, out);
    f.engine.stopClip(f.ch);
    run(f.engine, 2 * kBar, out);
    CHECK(f.mock.countOn(60) == 1);        // never retriggered after the stop
    CHECK(f.channel().playingSlot.load() == -1);
}

TEST(stop_flushes_held_notes_and_resets_transport) {
    Fixture f;
    Clip& a = f.channel().clips[0];
    a.create(4);
    Fixture::note(a, 60, 0, 64);
    f.engine.launchClip(f.ch, 0);
    Rendered out;
    run(f.engine, 5000, out);
    CHECK(f.engine.isPlaying());
    f.engine.stop();
    run(f.engine, 512, out);
    CHECK(!f.engine.isPlaying());
    CHECK(f.mock.nth(60, false, 0) != nullptr);
    CHECK(f.engine.positionTicks() == 0);
}

TEST(scene_launch_starts_every_channel_together) {
    Engine e;
    e.prepare(kSr, 512);
    e.launchQuantizeTicks.store(0);
    MockInstrument m0, m1;
    const int c0 = e.addChannel(InstrumentType::PolySynth);
    const int c1 = e.addChannel(InstrumentType::PolySynth);
    e.channel(c0).external.store(&m0);
    e.channel(c1).external.store(&m1);
    for (int c : {c0, c1}) {
        e.channel(c).clips[2].create(1);
        Fixture::note(e.channel(c).clips[2], 60, 0);
    }
    e.launchScene(2);
    Rendered out;
    run(e, 4000, out);
    CHECK(m0.countOn(60) == 1);
    CHECK(m1.countOn(60) == 1);
    CHECK(m0.events[0].sample == m1.events[0].sample);
}

TEST(retriggering_the_same_pitch_cuts_the_old_note_first) {
    Fixture f;
    Clip& a = f.channel().clips[0];
    a.create(1);
    Fixture::note(a, 60, 0, 8);    // long note
    Fixture::note(a, 60, 4, 1);    // same pitch starts while the first still sounds
    f.engine.launchClip(f.ch, 0);
    Rendered out;
    run(f.engine, kBar / 2, out);
    // order at the second hit must be: off, then on
    int offs = 0;
    for (auto& e : f.mock.events)
        if (e.note == 60 && !e.on && e.sample == kBeat) ++offs;
    CHECK(offs >= 1);
}

TEST(live_notes_reach_the_instrument) {
    Fixture f;
    f.engine.liveNoteOn(f.ch, 64, 100);
    Rendered out;
    run(f.engine, 512, out);
    f.engine.liveNoteOff(f.ch, 64);
    run(f.engine, 512, out);
    CHECK(f.mock.countOn(64) == 1);
    CHECK(f.mock.nth(64, false, 0) != nullptr);
    CHECK(f.mock.nth(64, false, 0)->sample >= 512);
}

// ---------------------------------------------------------------------------
// Mixer
// ---------------------------------------------------------------------------

namespace {
struct MixFixture {
    Engine e;
    MockInstrument m0, m1;
    int c0, c1;
    MixFixture() {
        e.prepare(kSr, 512);
        c0 = e.addChannel(InstrumentType::PolySynth);
        c1 = e.addChannel(InstrumentType::PolySynth);
        m0.dc = 0.25f;
        m1.dc = 0.125f;
        e.channel(c0).external.store(&m0);
        e.channel(c1).external.store(&m1);
    }
    // Render a few blocks so gain ramps settle, return the last block.
    Rendered settle() {
        Rendered o;
        run(e, 512 * 6, o);
        Rendered last;
        last.l.assign(o.l.end() - 512, o.l.end());
        last.r.assign(o.r.end() - 512, o.r.end());
        return last;
    }
};
}  // namespace

TEST(mixer_sums_channels) {
    MixFixture f;
    auto o = f.settle();
    CHECK_NEAR(o.l[100], 0.375, 1e-4);
    CHECK_NEAR(o.r[100], 0.375, 1e-4);
}

TEST(mixer_volume_in_db) {
    MixFixture f;
    f.e.channel(f.c0).volumeDb.store(-6.0206f);     // x0.5
    auto o = f.settle();
    CHECK_NEAR(o.l[100], 0.125 + 0.125, 1e-3);
}

TEST(mixer_pan_is_a_balance_control) {
    MixFixture f;
    f.e.channel(f.c0).pan.store(1.0f);              // hard right: left side silenced
    auto o = f.settle();
    CHECK_NEAR(o.l[100], 0.125, 1e-4);              // only channel 1 on the left
    CHECK_NEAR(o.r[100], 0.375, 1e-4);              // both on the right
}

TEST(mixer_mute) {
    MixFixture f;
    f.e.channel(f.c0).mute.store(true);
    auto o = f.settle();
    CHECK_NEAR(o.l[100], 0.125, 1e-4);
}

TEST(mixer_solo_silences_the_others) {
    MixFixture f;
    f.e.channel(f.c1).solo.store(true);
    auto o = f.settle();
    CHECK_NEAR(o.l[100], 0.125, 1e-4);
    f.e.channel(f.c0).solo.store(true);              // both soloed: both audible
    auto o2 = f.settle();
    CHECK_NEAR(o2.l[100], 0.375, 1e-4);
}

TEST(mixer_changes_ramp_instead_of_jumping) {
    MixFixture f;
    f.settle();
    f.e.channel(f.c0).mute.store(true);
    Rendered o;
    run(f.e, 512, o);
    // The first sample of the block must still be close to the old level, the last close to the new one.
    CHECK(o.l.front() > 0.30f);
    CHECK_NEAR(o.l.back(), 0.125, 1e-3);
    float maxStep = 0.0f;
    for (size_t i = 1; i < o.l.size(); ++i) maxStep = std::max(maxStep, std::fabs(o.l[i] - o.l[i - 1]));
    CHECK(maxStep < 0.01f);
}

TEST(master_limiter_prevents_clipping) {
    Engine e;
    e.prepare(kSr, 512);
    std::vector<MockInstrument> mocks(6);
    for (auto& m : mocks) {
        const int c = e.addChannel(InstrumentType::PolySynth);
        m.dc = 0.8f;
        e.channel(c).external.store(&m);
    }
    Rendered o;
    run(e, 20000, o);
    CHECK(peakOf(o.l) <= 1.0f);
    CHECK(peakOf(o.r) <= 1.0f);
    CHECK(allFinite(o.l));
}

TEST(sends_feed_the_return_chain) {
    MixFixture f;
    f.e.channel(f.c1).mute.store(true);                              // only channel 0 (0.25) is dry
    f.e.channel(f.c0).sends[0].store(1.0f);
    f.e.swapEffect(f.e.returnStrip(0).fx[0], std::make_unique<TestGain>(0.5f));
    auto o = f.settle();
    // dry 0.25 + return (0.25 * send 1.0 * fx 0.5) = 0.375
    CHECK_NEAR(o.l[100], 0.375, 1e-4);
}

TEST(sends_are_post_fader) {
    MixFixture f;
    f.e.channel(f.c1).mute.store(true);
    f.e.channel(f.c0).sends[1].store(1.0f);
    f.e.channel(f.c0).volumeDb.store(-90.0f);                        // fader all the way down
    auto o = f.settle();
    CHECK_NEAR(o.l[100], 0.0, 1e-4);
}

TEST(fx_chain_processes_in_order_and_bypass_works) {
    MixFixture f;
    f.e.channel(f.c1).mute.store(true);
    auto& slots = f.e.channel(f.c0).fx;
    f.e.swapEffect(slots[0], std::make_unique<TestGain>(0.5f));
    f.e.swapEffect(slots[1], std::make_unique<TestGain>(0.5f));
    CHECK_NEAR(f.settle().l[100], 0.0625, 1e-4);                     // 0.25 * 0.5 * 0.5
    slots[1].bypass.store(true);
    CHECK_NEAR(f.settle().l[100], 0.125, 1e-4);
}

TEST(swap_effect_returns_the_old_effect_and_can_clear_a_slot) {
    MixFixture f;
    f.e.channel(f.c1).mute.store(true);
    auto& slot = f.e.channel(f.c0).fx[0];
    CHECK(f.e.swapEffect(slot, std::make_unique<TestGain>(0.5f)) == nullptr);
    auto old = f.e.swapEffect(slot, nullptr);
    CHECK(old != nullptr);
    CHECK(old && std::string(old->name()) == "TestGain");
    CHECK_NEAR(f.settle().l[100], 0.25, 1e-4);
}

TEST(master_strip_has_an_fx_chain_and_fader) {
    MixFixture f;
    f.e.master().volumeDb.store(-6.0206f);
    f.e.swapEffect(f.e.master().fx[0], std::make_unique<TestGain>(0.5f));
    CHECK_NEAR(f.settle().l[100], 0.375 * 0.5 * 0.5, 1e-3);
}

TEST(meters_report_peaks_and_reset_when_read) {
    MixFixture f;
    f.settle();
    CHECK_NEAR(f.e.channel(f.c0).takePeakL(), 0.25, 1e-3);
    CHECK_NEAR(f.e.master().takePeakL(), 0.375, 1e-3);
    CHECK(f.e.channel(f.c0).takePeakL() == 0.0f);
}

TEST(channels_can_be_added_and_removed) {
    Engine e;
    CHECK(e.activeChannelCount() == 0);
    for (int i = 0; i < kMaxChannels; ++i) CHECK(e.addChannel(InstrumentType::PolySynth) == i);
    CHECK(e.addChannel(InstrumentType::PolySynth) == -1);            // full
    e.removeChannel(3);
    CHECK(e.activeChannelCount() == kMaxChannels - 1);
    CHECK(e.addChannel(InstrumentType::DrumRack) == 3);              // slot is reused
}

// ---------------------------------------------------------------------------
// Whole-engine sanity
// ---------------------------------------------------------------------------

TEST(demo_project_renders_clean_audio) {
    Engine e;
    e.prepare(kSr, 512);
    buildDemoProject(e);
    CHECK(e.activeChannelCount() == 4);

    e.launchQuantizeTicks.store(0);
    e.launchScene(1);
    Rendered o;
    run(e, static_cast<long long>(kSr * 12), o);

    CHECK(allFinite(o.l) && allFinite(o.r));
    CHECK(peakOf(o.l) <= 1.0f);
    CHECK(peakOf(o.l) > 0.3f);                       // loud enough to be a mix
    CHECK(rmsOf(o.l) > 0.03f);
    CHECK(rmsOf(o.l) < 0.5f);
}

TEST(offline_render_is_deterministic_and_stops_the_transport) {
    auto render = []() {
        Engine e;
        e.prepare(kSr, 512);
        buildDemoProject(e);
        Rendered r;
        e.renderOffline(0, 2, 0.5f, r.l, r.r);
        CHECK(!e.isPlaying());
        return r;
    };
    const Rendered a = render();
    const Rendered b = render();
    CHECK(a.l.size() == b.l.size());
    CHECK(a.l == b.l);
    CHECK(a.r == b.r);
    // 2 bars at 92 BPM + 0.5 s tail
    const double expected = 2.0 * 4.0 * 60.0 / 92.0 * kSr + 0.5 * kSr;
    CHECK_NEAR(a.l.size(), expected, 2);
    CHECK(rmsOf(a.l) > 0.03f);
    // tail is quiet, not cut off mid-note: last 100 samples are nearly silent
    CHECK(peakOf(a.l, a.l.size() - 100) < 0.05f);
}

TEST(offline_render_can_run_twice_on_the_same_engine) {
    Engine e;
    e.prepare(kSr, 512);
    buildDemoProject(e);
    Rendered a, b;
    e.renderOffline(0, 1, 0.25f, a.l, a.r);
    e.renderOffline(0, 1, 0.25f, b.l, b.r);
    CHECK(a.l == b.l);
}
