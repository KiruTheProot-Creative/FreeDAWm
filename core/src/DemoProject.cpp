#include "fdm/DemoProject.h"

#include <cstdio>
#include <initializer_list>
#include <utility>

namespace fdm {

namespace {

void setName(Clip& c, const char* n) { std::snprintf(c.name, sizeof(c.name), "%s", n); }

void note(Clip& c, int pitch, int step, float lengthSteps, int vel) {
    NoteEvent e;
    e.pitch = pitch;
    e.start = step * kTicksPerStep;
    e.length = static_cast<int>(lengthSteps * static_cast<float>(kTicksPerStep));
    e.velocity = vel;
    c.addNote(e);
}

void set(Channel& ch, std::initializer_list<std::pair<const char*, float>> values) {
    for (auto& [id, v] : values)
        if (auto* p = ch.synth.params().find(id)) p->set(v);
}

}  // namespace

void buildDemoProject(Engine& engine) {
    engine.bpm.set(92.0f);
    engine.swing.set(0.15f);

    const int drumsIdx = engine.addChannel(InstrumentType::DrumRack);
    const int bassIdx  = engine.addChannel(InstrumentType::PolySynth);
    const int keysIdx  = engine.addChannel(InstrumentType::PolySynth);
    const int leadIdx  = engine.addChannel(InstrumentType::PolySynth);

    Channel& drums = engine.channel(drumsIdx);
    Channel& bass  = engine.channel(bassIdx);
    Channel& keys  = engine.channel(keysIdx);
    Channel& lead  = engine.channel(leadIdx);

    drums.setName("Drums");  drums.colorIndex.store(0);
    bass.setName("Bass");    bass.colorIndex.store(1);
    keys.setName("Keys");    keys.colorIndex.store(2);
    lead.setName("Lead");    lead.colorIndex.store(3);

    // ---- sounds -----------------------------------------------------------
    set(bass, {{"osc1.wave", 0}, {"osc2.wave", 1}, {"osc2.semi", -12}, {"osc2.fine", 5}, {"osc2.level", 0.7f},
               {"cutoff", 380}, {"resonance", 0.25f}, {"env.amount", 0.55f}, {"keytrack", 0.15f},
               {"amp.a", 0.003f}, {"amp.d", 0.2f}, {"amp.s", 0.7f}, {"amp.r", 0.08f},
               {"flt.d", 0.18f}, {"flt.s", 0.15f}, {"volume", 3}});
    set(keys, {{"osc1.wave", 0}, {"osc2.wave", 0}, {"osc2.fine", 12}, {"osc2.level", 0.8f},
               {"cutoff", 1500}, {"resonance", 0.1f}, {"env.amount", 0.25f}, {"keytrack", 0.4f},
               {"amp.a", 0.02f}, {"amp.d", 0.4f}, {"amp.s", 0.55f}, {"amp.r", 0.5f},
               {"flt.a", 0.01f}, {"flt.d", 0.6f}, {"flt.s", 0.4f}, {"flt.r", 0.5f}, {"volume", 0}});
    set(lead, {{"osc1.wave", 0}, {"osc2.wave", 1}, {"osc2.semi", 12}, {"osc2.fine", 6}, {"osc2.level", 0.4f},
               {"cutoff", 1400}, {"resonance", 0.35f}, {"env.amount", 0.8f}, {"keytrack", 0.5f},
               {"amp.a", 0.002f}, {"amp.d", 0.3f}, {"amp.s", 0.3f}, {"amp.r", 0.25f},
               {"flt.d", 0.14f}, {"flt.s", 0.0f}, {"volume", 6}});

    // ---- mixer ------------------------------------------------------------
    drums.volumeDb.store(-1.5f);
    keys.volumeDb.store(-2.0f);  keys.pan.store(-0.15f);
    lead.volumeDb.store(-3.0f);  lead.pan.store(0.2f);

    // ---- scene 0: drums A, bass, chord stabs ------------------------------
    {
        Clip& c = drums.clips[0];  c.create(1);  setName(c, "Groove");
        for (int s : {0, 7, 10})       note(c, 36, s, 1, s == 0 ? 120 : 95);
        for (int s : {4, 12})          note(c, 38, s, 1, 112);
        note(c, 38, 15, 1, 45);
        for (int s : {4, 12})          note(c, 39, s, 1, 72);
        const int hatVel[] = {95, 60, 85, 60, 90, 60, 85};
        for (int i = 0; i < 7; ++i)    note(c, 42, i * 2, 1, hatVel[i]);
        note(c, 46, 14, 1, 85);
    }
    {
        Clip& c = bass.clips[0];  c.create(2);  setName(c, "Bass");
        // bar 1: Am
        note(c, 33, 0, 3, 105);  note(c, 33, 3, 2, 85);  note(c, 36, 6, 2, 95);
        note(c, 33, 8, 2, 100);  note(c, 40, 11, 2, 92); note(c, 31, 14, 2, 90);
        // bar 2: F
        note(c, 29, 16, 3, 105); note(c, 29, 19, 2, 85); note(c, 33, 22, 2, 95);
        note(c, 29, 24, 2, 100); note(c, 36, 27, 2, 92); note(c, 40, 30, 2, 90);
    }
    {
        Clip& c = keys.clips[0];  c.create(2);  setName(c, "Chords");
        const int am7[] = {57, 60, 64, 67};
        const int fm7[] = {53, 57, 60, 64};
        for (int p : am7) { note(c, p, 0, 5, 80);   note(c, p, 6, 3, 65);   note(c, p, 10, 6, 70); }
        for (int p : fm7) { note(c, p, 16, 5, 80);  note(c, p, 22, 3, 65);  note(c, p, 26, 6, 70); }
    }

    // ---- scene 1: busier drums, same bass/chords, plus an arp -------------
    {
        Clip& c = drums.clips[1];  c.create(1);  setName(c, "Busy");
        for (int s : {0, 6, 8, 10})    note(c, 36, s, 1, s == 0 ? 120 : 98);
        for (int s : {4, 12})          note(c, 38, s, 1, 115);
        note(c, 38, 14, 1, 50);
        note(c, 37, 2, 1, 70);
        for (int i = 0; i < 16; ++i)   note(c, 42, i, 1, (i % 4 == 0) ? 100 : (i % 2 == 0 ? 75 : 50));
        note(c, 56, 6, 1, 70);   note(c, 56, 14, 1, 70);
        note(c, 41, 11, 1, 90);  note(c, 41, 13, 1, 100);
    }
    {   // bass and chords repeat in scene 1 (clips hold atomics, so notes are copied one by one)
        Clip& src = bass.clips[0];
        Clip& dst = bass.clips[1];
        dst.create(2);  setName(dst, "Bass");
        for (int i = 0; i < kMaxNotesPerClip; ++i) {
            NoteEvent e;
            if (src.getNote(i, e)) dst.addNote(e);
        }
    }
    {
        Clip& src = keys.clips[0];
        Clip& dst = keys.clips[1];
        dst.create(2);  setName(dst, "Chords");
        for (int i = 0; i < kMaxNotesPerClip; ++i) {
            NoteEvent e;
            if (src.getNote(i, e)) dst.addNote(e);
        }
    }
    {
        Clip& c = lead.clips[1];  c.create(2);  setName(c, "Arp");
        const int bar1[][2] = {{0, 69}, {2, 72}, {3, 76}, {6, 74}, {8, 72}, {10, 69}, {12, 67}, {14, 64}};
        const int bar2[][2] = {{16, 65}, {18, 69}, {19, 72}, {22, 76}, {24, 74}, {26, 72}, {28, 69}, {30, 65}};
        for (auto& n : bar1) note(c, n[1], n[0], 1.5f, 92);
        for (auto& n : bar2) note(c, n[1], n[0], 1.5f, 92);
    }
}

}  // namespace fdm
