#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "fdm/Types.h"

namespace fdm {

struct NoteEvent {
    int start    = 0;                 // ticks from the start of the clip
    int length   = kTicksPerStep;     // ticks
    int pitch    = 60;                // MIDI note, 0..127
    int velocity = 100;               // 1..127
};

/** A looping MIDI clip.

    Each note is packed into ONE atomic 64-bit word, so the UI thread can add,
    move and delete notes while the audio thread is playing the clip, with no
    locks and no half-updated notes. The audio thread only ever reads.
    Only one thread (the UI thread) may write. */
class Clip {
public:
    Clip();

    std::atomic<bool> exists{false};
    std::atomic<int>  lengthTicks{kTicksPerBar};
    char name[32] = {};               // UI only; the audio thread never reads it

    // --- packing helpers (also used by the audio thread) ---
    static uint64_t   pack(const NoteEvent& e);
    static NoteEvent  unpack(uint64_t word);
    static bool       isActive(uint64_t word) { return (word >> 63) != 0; }

    uint64_t loadSlot(int i) const { return slots_[static_cast<size_t>(i)].load(std::memory_order_relaxed); }

    // --- UI-thread editing ---
    /** Adds a note. Returns the slot index, or -1 if the clip is full. */
    int  addNote(const NoteEvent& e);
    bool getNote(int index, NoteEvent& out) const;
    void setNote(int index, const NoteEvent& e);
    void removeNote(int index);
    void clearNotes();
    int  countNotes() const;

    /** Index of a note of `pitch` that is sounding at `tick`, or -1. */
    int  findNoteAt(int pitch, int tick) const;

    /** Sets the length (clamped to 1 step .. kMaxClipBars) and drops notes that start beyond it. */
    void setLengthTicks(int ticks);

    void create(int bars = 1);        // makes the clip exist, empty
    void clear();                     // deletes the clip

private:
    std::array<std::atomic<uint64_t>, kMaxNotesPerClip> slots_;
};

}  // namespace fdm
