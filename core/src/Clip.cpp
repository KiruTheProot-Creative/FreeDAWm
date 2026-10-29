#include "fdm/Clip.h"

#include <algorithm>
#include <cstring>

namespace fdm {

namespace {
// Layout of a packed note:
//   bit 63        active flag
//   bits 47..53   velocity (7)
//   bits 40..46   pitch    (7)
//   bits 20..39   length   (20)
//   bits  0..19   start    (20)
constexpr uint64_t kActive  = 1ull << 63;
constexpr uint64_t kMask20  = (1ull << 20) - 1;
constexpr uint64_t kMask7   = 0x7f;
}  // namespace

Clip::Clip() {
    for (auto& s : slots_) s.store(0, std::memory_order_relaxed);
}

uint64_t Clip::pack(const NoteEvent& e) {
    const uint64_t start  = static_cast<uint64_t>(std::clamp(e.start, 0, static_cast<int>(kMask20)));
    const uint64_t length = static_cast<uint64_t>(std::clamp(e.length, 1, static_cast<int>(kMask20)));
    const uint64_t pitch  = static_cast<uint64_t>(std::clamp(e.pitch, 0, 127));
    const uint64_t vel    = static_cast<uint64_t>(std::clamp(e.velocity, 1, 127));
    return kActive | (vel << 47) | (pitch << 40) | (length << 20) | start;
}

NoteEvent Clip::unpack(uint64_t w) {
    NoteEvent e;
    e.start    = static_cast<int>(w & kMask20);
    e.length   = static_cast<int>((w >> 20) & kMask20);
    e.pitch    = static_cast<int>((w >> 40) & kMask7);
    e.velocity = static_cast<int>((w >> 47) & kMask7);
    return e;
}

int Clip::addNote(const NoteEvent& e) {
    for (int i = 0; i < kMaxNotesPerClip; ++i) {
        auto& slot = slots_[static_cast<size_t>(i)];
        if (!isActive(slot.load(std::memory_order_relaxed))) {
            slot.store(pack(e), std::memory_order_release);
            return i;
        }
    }
    return -1;
}

bool Clip::getNote(int index, NoteEvent& out) const {
    if (index < 0 || index >= kMaxNotesPerClip) return false;
    const uint64_t w = loadSlot(index);
    if (!isActive(w)) return false;
    out = unpack(w);
    return true;
}

void Clip::setNote(int index, const NoteEvent& e) {
    if (index < 0 || index >= kMaxNotesPerClip) return;
    slots_[static_cast<size_t>(index)].store(pack(e), std::memory_order_release);
}

void Clip::removeNote(int index) {
    if (index < 0 || index >= kMaxNotesPerClip) return;
    slots_[static_cast<size_t>(index)].store(0, std::memory_order_release);
}

void Clip::clearNotes() {
    for (auto& s : slots_) s.store(0, std::memory_order_release);
}

int Clip::countNotes() const {
    int n = 0;
    for (int i = 0; i < kMaxNotesPerClip; ++i)
        if (isActive(loadSlot(i))) ++n;
    return n;
}

int Clip::findNoteAt(int pitch, int tick) const {
    for (int i = 0; i < kMaxNotesPerClip; ++i) {
        const uint64_t w = loadSlot(i);
        if (!isActive(w)) continue;
        const NoteEvent e = unpack(w);
        if (e.pitch == pitch && tick >= e.start && tick < e.start + e.length) return i;
    }
    return -1;
}

void Clip::setLengthTicks(int ticks) {
    ticks = std::clamp(ticks, kTicksPerStep, kMaxClipBars * kTicksPerBar);
    lengthTicks.store(ticks, std::memory_order_relaxed);
    for (int i = 0; i < kMaxNotesPerClip; ++i) {
        const uint64_t w = loadSlot(i);
        if (isActive(w) && unpack(w).start >= ticks) removeNote(i);
    }
}

void Clip::create(int bars) {
    clearNotes();
    lengthTicks.store(std::clamp(bars, 1, kMaxClipBars) * kTicksPerBar, std::memory_order_relaxed);
    exists.store(true, std::memory_order_release);
}

void Clip::clear() {
    exists.store(false, std::memory_order_release);
    clearNotes();
    std::memset(name, 0, sizeof(name));
}

}  // namespace fdm
