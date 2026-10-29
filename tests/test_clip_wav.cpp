#include <cstdio>

#include "TestHarness.h"

#include "fdm/Clip.h"
#include "fdm/Wav.h"

using namespace fdm;

TEST(clip_note_packing_round_trips) {
    NoteEvent e;
    e.start = 1234; e.length = 480; e.pitch = 71; e.velocity = 93;
    const NoteEvent r = Clip::unpack(Clip::pack(e));
    CHECK(r.start == 1234);
    CHECK(r.length == 480);
    CHECK(r.pitch == 71);
    CHECK(r.velocity == 93);
    CHECK(Clip::isActive(Clip::pack(e)));
    CHECK(!Clip::isActive(0));
}

TEST(clip_packing_clamps_out_of_range_values) {
    NoteEvent e;
    e.start = -5; e.length = 0; e.pitch = 500; e.velocity = 0;
    const NoteEvent r = Clip::unpack(Clip::pack(e));
    CHECK(r.start == 0);
    CHECK(r.length == 1);
    CHECK(r.pitch == 127);
    CHECK(r.velocity == 1);
}

TEST(clip_add_edit_remove_and_find) {
    Clip c;
    c.create(2);
    CHECK(c.exists.load());
    CHECK(c.lengthTicks.load() == 2 * kTicksPerBar);

    NoteEvent e;
    e.pitch = 60; e.start = 480; e.length = 960;
    const int a = c.addNote(e);
    CHECK(a >= 0);
    CHECK(c.countNotes() == 1);
    CHECK(c.findNoteAt(60, 480) == a);
    CHECK(c.findNoteAt(60, 1439) == a);
    CHECK(c.findNoteAt(60, 1440) == -1);      // end is exclusive
    CHECK(c.findNoteAt(61, 600) == -1);

    e.start = 0;
    c.setNote(a, e);
    NoteEvent out;
    CHECK(c.getNote(a, out) && out.start == 0);

    c.removeNote(a);
    CHECK(c.countNotes() == 0);
    CHECK(!c.getNote(a, out));
}

TEST(clip_fills_up_and_reports_it) {
    Clip c;
    c.create(1);
    NoteEvent e;
    for (int i = 0; i < kMaxNotesPerClip; ++i) CHECK(c.addNote(e) >= 0);
    CHECK(c.addNote(e) == -1);
    CHECK(c.countNotes() == kMaxNotesPerClip);
    c.clearNotes();
    CHECK(c.countNotes() == 0);
}

TEST(clip_shortening_drops_notes_that_no_longer_fit) {
    Clip c;
    c.create(2);
    NoteEvent early, late;
    early.start = 100;
    late.start = kTicksPerBar + 100;
    c.addNote(early);
    c.addNote(late);
    c.setLengthTicks(kTicksPerBar);
    CHECK(c.countNotes() == 1);
    c.setLengthTicks(1 << 30);
    CHECK(c.lengthTicks.load() == kMaxClipBars * kTicksPerBar);
}

TEST(clip_clear_deletes_the_clip) {
    Clip c;
    c.create(1);
    c.addNote({});
    c.clear();
    CHECK(!c.exists.load());
    CHECK(c.countNotes() == 0);
}

TEST(wav_round_trip_all_formats) {
    std::vector<float> l, r;
    for (int i = 0; i < 4800; ++i) {
        l.push_back(0.8f * std::sin(0.05f * static_cast<float>(i)));
        r.push_back(-0.5f * std::sin(0.031f * static_cast<float>(i)));
    }
    struct Case { WavFormat fmt; double tol; const char* name; };
    for (auto c : {Case{WavFormat::Float32, 1e-7, "f32"}, Case{WavFormat::Pcm24, 2e-7, "pcm24"},
                   Case{WavFormat::Pcm16, 1e-3, "pcm16"}}) {
        const std::string path = std::string("fdm_test_") + c.name + ".wav";
        CHECK(writeWav(path, l, r, 44100, c.fmt));
        WavData d;
        CHECK(readWav(path, d));
        CHECK(d.sampleRate == 44100);
        CHECK(d.channels == 2);
        CHECK(d.left.size() == l.size());
        double maxErr = 0.0;
        for (size_t i = 0; i < l.size() && i < d.left.size(); ++i) {
            maxErr = std::max(maxErr, static_cast<double>(std::fabs(d.left[i] - l[i])));
            maxErr = std::max(maxErr, static_cast<double>(std::fabs(d.right[i] - r[i])));
        }
        CHECK(maxErr <= c.tol);
        std::remove(path.c_str());
    }
}

TEST(wav_reader_rejects_garbage) {
    const std::string path = "fdm_test_garbage.wav";
    FILE* f = std::fopen(path.c_str(), "wb");
    std::fputs("this is not a wav file, not even close, no sir", f);
    std::fclose(f);
    WavData d;
    CHECK(!readWav(path, d));
    CHECK(!readWav("does_not_exist.wav", d));
    std::remove(path.c_str());
}

TEST(wav_writer_clamps_instead_of_wrapping) {
    std::vector<float> l = {2.0f, -2.0f, 0.5f}, r = l;
    CHECK(writeWav("fdm_test_clamp.wav", l, r, 48000, WavFormat::Pcm24));
    WavData d;
    CHECK(readWav("fdm_test_clamp.wav", d));
    CHECK_NEAR(d.left[0], 1.0, 1e-4);
    CHECK_NEAR(d.left[1], -1.0, 1e-4);
    std::remove("fdm_test_clamp.wav");
}
