#pragma once

#include "../AppState.h"
#include "../Theme.h"
#include "../Widgets.h"

/** Shared zoom / coordinate maths for the ruler and the lanes. */
struct TimelineMetrics {
    double pxPerBar = 96.0;
    int totalBars = 64;

    int tickToX(int64_t t) const { return static_cast<int>(std::lround(static_cast<double>(t) * pxPerBar / fdm::kTicksPerBar)); }
    int64_t xToTick(int x) const { return std::max<int64_t>(0, std::llround(static_cast<double>(x) * fdm::kTicksPerBar / pxPerBar)); }
    int width() const { return tickToX(static_cast<int64_t>(totalBars) * fdm::kTicksPerBar); }

    /** Grid used when dragging: adapts to the zoom level, like Ableton. */
    int snapMode = 0;      // Preferences > Editing > Snap: 0 adaptive, 1 bar, 2 beat, 3 1/8, 4 1/16, 5 off

    int snapTicks(bool fine) const {
        if (fine) return fdm::kTicksPerStep;
        switch (snapMode) {
            case 1: return fdm::kTicksPerBar;
            case 2: return fdm::kTicksPerBeat;
            case 3: return fdm::kTicksPerBeat / 2;
            case 4: return fdm::kTicksPerStep;
            case 5: return 1;
            default: break;
        }
        if (pxPerBar >= 420.0) return fdm::kTicksPerBeat / 2;
        if (pxPerBar >= 170.0) return fdm::kTicksPerBeat;
        return fdm::kTicksPerBar;
    }
    int64_t snap(int64_t t, int grid) const { return (t + grid / 2) / grid * grid; }
    int64_t snapDown(int64_t t, int grid) const { return t / grid * grid; }
};

// ---------------------------------------------------------------------------
// Ruler: bar numbers, loop brace (top strip), playhead (click / drag)
// ---------------------------------------------------------------------------
class TimelineRuler : public juce::Component {
public:
    TimelineRuler(AppState& app, TimelineMetrics& m) : app_(app), m_(m) {}

    void paint(juce::Graphics& g) override {
        g.fillAll(theme::background);
        const auto clip = g.getClipBounds();

        // loop brace
        const int ls = app_.engine.loopStartTick.load(), le = app_.engine.loopEndTick.load();
        const bool loopOn = app_.engine.loopEnabled.load();
        auto brace = juce::Rectangle<int>(m_.tickToX(ls), 0, std::max(2, m_.tickToX(le) - m_.tickToX(ls)), 10);
        g.setColour(loopOn ? theme::accent : theme::accentDeep.withAlpha(0.8f));
        g.fillRect(brace);

        // bar numbers
        const int firstBar = std::max(0, static_cast<int>(m_.xToTick(clip.getX()) / fdm::kTicksPerBar) - 1);
        const int lastBar = std::min(m_.totalBars, static_cast<int>(m_.xToTick(clip.getRight()) / fdm::kTicksPerBar) + 1);
        int step = 1;
        while (m_.pxPerBar * step < 44.0) step *= 2;
        g.setFont(theme::labelFont(11.0f, false));
        for (int bar = firstBar; bar <= lastBar; ++bar) {
            const int x = m_.tickToX(static_cast<int64_t>(bar) * fdm::kTicksPerBar);
            const bool labelled = bar % step == 0;
            g.setColour(labelled ? theme::textDim : theme::border);
            g.drawVerticalLine(x, labelled ? 12.0f : 20.0f, static_cast<float>(getHeight()));
            if (labelled) {
                g.setColour(theme::text);
                g.drawText(juce::String(bar + 1), x + 4, 12, 40, getHeight() - 12, juce::Justification::centredLeft, false);
            }
            if (m_.pxPerBar >= 90.0)
                for (int beat = 1; beat < 4; ++beat) {
                    g.setColour(theme::border);
                    g.drawVerticalLine(x + m_.tickToX(static_cast<int64_t>(beat) * fdm::kTicksPerBeat), 23.0f, static_cast<float>(getHeight()));
                }
        }

        // playhead
        const int px = m_.tickToX(app_.engine.isPlaying() ? app_.engine.positionTicks() : app_.engine.positionTicks());
        juce::Path tri;
        tri.addTriangle(static_cast<float>(px) - 6.0f, 12.0f, static_cast<float>(px) + 6.0f, 12.0f, static_cast<float>(px), 24.0f);
        g.setColour(theme::accentHi);
        g.fillPath(tri);
        g.drawVerticalLine(px, 12.0f, static_cast<float>(getHeight()));

        g.setColour(juce::Colours::black);
        g.drawHorizontalLine(getHeight() - 1, 0.0f, static_cast<float>(getWidth()));
    }

    void mouseDown(const juce::MouseEvent& e) override {
        loopDrag_ = e.y < 11;
        anchor_ = m_.snapDown(m_.xToTick(e.x), m_.snapTicks(false));
        if (loopDrag_) {
            app_.engine.loopStartTick.store(static_cast<int>(anchor_));
            app_.engine.loopEndTick.store(static_cast<int>(anchor_ + fdm::kTicksPerBar));
            app_.engine.loopEnabled.store(true);
            if (onLoopChanged) onLoopChanged();
        } else {
            app_.engine.setPlayhead(m_.snap(m_.xToTick(e.x), m_.snapTicks(e.mods.isAltDown())));
        }
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        const int grid = m_.snapTicks(e.mods.isAltDown());
        if (loopDrag_) {
            const int64_t t = m_.snap(m_.xToTick(e.x), grid);
            const int64_t a = std::min(anchor_, t), b = std::max(anchor_, t);
            app_.engine.loopStartTick.store(static_cast<int>(a));
            app_.engine.loopEndTick.store(static_cast<int>(std::max(b, a + grid)));
            if (onLoopChanged) onLoopChanged();
        } else {
            app_.engine.setPlayhead(m_.snap(m_.xToTick(std::max(0, e.x)), grid));
        }
        repaint();
    }

    std::function<void()> onLoopChanged;

private:
    AppState& app_;
    TimelineMetrics& m_;
    bool loopDrag_ = false;
    int64_t anchor_ = 0;
};

// ---------------------------------------------------------------------------
// The lanes: one row per track. MIDI tracks hold clips, audio tracks hold sound files.
// ---------------------------------------------------------------------------
class TimelineLanes : public juce::Component, public juce::DragAndDropTarget, public juce::FileDragAndDropTarget {
public:
    TimelineLanes(AppState& app, TimelineMetrics& m) : app_(app), m_(m) { setWantsKeyboardFocus(true); }

    std::function<void(int tab)> requestTab;
    std::function<void()> onExtentChanged;        // clips moved or added: the scroll width may change
    std::vector<int> channels;                    // engine channel index per row

    /** A MIDI placement or an audio clip, seen the same way by the editing code. */
    struct Item { int start = 0, length = 0, ref = 0; };

    void paint(juce::Graphics& g) override {
        const auto clipB = g.getClipBounds();
        g.fillAll(theme::background);

        const int rows = static_cast<int>(channels.size());
        for (int r = 0; r < rows; ++r) {
            const int y = r * theme::trackHeight;
            if (y > clipB.getBottom() || y + theme::trackHeight < clipB.getY()) continue;
            const bool selected = app_.selection().kind == AppState::Selection::Kind::Channel && app_.selection().index == channels[static_cast<size_t>(r)];
            g.setColour(selected ? theme::panel.brighter(0.05f) : (r % 2 ? theme::panel.darker(0.25f) : theme::panel.darker(0.45f)));
            g.fillRect(0, y, getWidth(), theme::trackHeight);
            g.setColour(juce::Colours::black);
            g.drawHorizontalLine(y + theme::trackHeight - 1, 0.0f, static_cast<float>(getWidth()));
        }

        if (app_.engine.loopEnabled.load()) {
            const int a = m_.tickToX(app_.engine.loopStartTick.load()), b = m_.tickToX(app_.engine.loopEndTick.load());
            g.setColour(theme::accent.withAlpha(0.07f));
            g.fillRect(a, 0, b - a, getHeight());
            g.setColour(theme::accent.withAlpha(0.5f));
            g.drawVerticalLine(a, 0.0f, static_cast<float>(getHeight()));
            g.drawVerticalLine(b, 0.0f, static_cast<float>(getHeight()));
        }

        const int firstBar = std::max(0, static_cast<int>(m_.xToTick(clipB.getX()) / fdm::kTicksPerBar));
        const int lastBar = std::min(m_.totalBars, static_cast<int>(m_.xToTick(clipB.getRight()) / fdm::kTicksPerBar) + 1);
        for (int bar = firstBar; bar <= lastBar; ++bar) {
            const int x = m_.tickToX(static_cast<int64_t>(bar) * fdm::kTicksPerBar);
            g.setColour(theme::gridBar);
            g.drawVerticalLine(x, 0.0f, static_cast<float>(getHeight()));
            if (m_.pxPerBar >= 90.0)
                for (int beat = 1; beat < 4; ++beat) {
                    g.setColour(theme::gridStep);
                    g.drawVerticalLine(x + m_.tickToX(static_cast<int64_t>(beat) * fdm::kTicksPerBeat), 0.0f, static_cast<float>(getHeight()));
                }
        }

        for (int r = 0; r < rows; ++r) {
            auto& ch = app_.engine.channel(channels[static_cast<size_t>(r)]);
            for (int i = 0; i < fdm::kMaxPlacements; ++i) {
                Item it;
                if (!getItem(ch, i, it)) continue;
                const auto rect = itemRect(r, it);
                if (!rect.toNearestInt().intersects(clipB)) continue;
                const bool sel = selChannel_ == channels[static_cast<size_t>(r)] && selIndex_ == i;
                if (ch.isAudio()) paintAudioClip(g, ch, it, rect, sel);
                else paintMidiClip(g, ch, it, rect, sel);
            }
        }

        if (dropRow_ >= 0) {                                       // drag-and-drop ghost
            const int y = dropRow_ * theme::trackHeight;
            const int x = m_.tickToX(dropTick_);
            g.setColour(theme::accentHi.withAlpha(0.25f));
            g.fillRoundedRectangle(static_cast<float>(x), static_cast<float>(y + 3), 120.0f, static_cast<float>(theme::trackHeight - 6), 3.0f);
            g.setColour(theme::accentHi);
            g.drawRoundedRectangle(static_cast<float>(x), static_cast<float>(y + 3), 120.0f, static_cast<float>(theme::trackHeight - 6), 3.0f, 1.5f);
            g.setFont(theme::labelFont(10.5f));
            g.drawText(dropRow_ >= rows ? "NEW AUDIO TRACK" : "DROP HERE", x + 6, y + 6, 110, 14, juce::Justification::centredLeft);
        }

        const int px = m_.tickToX(app_.engine.positionTicks());
        g.setColour(theme::accentHi);
        g.drawVerticalLine(px, 0.0f, static_cast<float>(getHeight()));
    }

    // ---- mouse ---------------------------------------------------------------------
    void mouseDown(const juce::MouseEvent& e) override {
        grabKeyboardFocus();
        const int row = e.y / theme::trackHeight;
        if (row < 0 || row >= static_cast<int>(channels.size())) return;
        const int channel = channels[static_cast<size_t>(row)];
        auto& ch = app_.engine.channel(channel);
        int index = -1;
        Item it;
        const bool hit = findItem(row, e.x, index, it);
        drag_ = Drag::None;

        if (e.mods.isPopupMenu()) {
            if (hit) select(channel, index, ch.isAudio() ? app_.selection().scene : it.ref);
            else app_.selectChannel(channel, app_.selection().scene);
            showMenu(row, hit, index, it, e.x);
            return;
        }

        if (hit) {
            select(channel, index, ch.isAudio() ? app_.selection().scene : it.ref);
            original_ = it;
            drag_ = e.x > itemRect(row, it).getRight() - 7.0f ? Drag::Resize : Drag::Move;
            grab_ = m_.xToTick(e.x) - it.start;
            if (requestTab && !ch.isAudio()) requestTab(0);
        } else {
            selChannel_ = selIndex_ = -1;
            app_.selectChannel(channel, app_.selection().scene);
            app_.engine.setPlayhead(m_.snap(m_.xToTick(e.x), m_.snapTicks(e.mods.isAltDown())));
        }
        repaint();
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override {
        const int row = e.y / theme::trackHeight;
        if (row < 0 || row >= static_cast<int>(channels.size()) || e.mods.isPopupMenu()) return;
        int index; Item it;
        if (findItem(row, e.x, index, it)) return;
        const int64_t at = m_.snapDown(m_.xToTick(e.x), fdm::kTicksPerBar);
        if (app_.engine.channel(channels[static_cast<size_t>(row)]).isAudio()) importAt(row, at);
        else createClipAt(row, at);
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (drag_ == Drag::None || selChannel_ < 0) return;
        auto& ch = app_.engine.channel(selChannel_);
        Item it = original_;
        const int grid = m_.snapTicks(e.mods.isAltDown());
        if (drag_ == Drag::Move) {
            it.start = static_cast<int>(std::max<int64_t>(0, m_.snap(m_.xToTick(e.x) - grab_, grid)));
        } else {
            const int64_t end = m_.snap(m_.xToTick(e.x), grid);
            it.length = static_cast<int>(std::max<int64_t>(fdm::kTicksPerStep, end - it.start));
            if (ch.isAudio()) it.length = std::min(it.length, app_.sampleLengthTicks(it.ref));     // audio can be shortened, not stretched
        }
        setItem(ch, selIndex_, it);
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override {
        if (drag_ != Drag::None && onExtentChanged) onExtentChanged();
        drag_ = Drag::None;
    }

    void mouseMove(const juce::MouseEvent& e) override {
        const int row = e.y / theme::trackHeight;
        int index; Item it;
        bool resize = false;
        if (findItem(row, e.x, index, it)) resize = e.x > itemRect(row, it).getRight() - 7.0f;
        setMouseCursor(resize ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    }

    bool keyPressed(const juce::KeyPress& key) override {
        if (selChannel_ < 0) return false;
        const auto action = app_.prefs->actionFor(key);
        if (action == "delete" || key == juce::KeyPress::backspaceKey) { removeSelected(); return true; }
        if (action == "duplicate") { duplicateSelected(); return true; }
        return false;
    }

    void deleteSelectedClip() { removeSelected(); }
    void duplicateSelectedClip() { duplicateSelected(); }

    std::function<void(double factor)> onZoom;

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override {
        const bool wheelZooms = app_.prefs->getInt("wheelZoom") == 1;
        const bool zoom = wheelZooms ? !e.mods.isShiftDown() : (e.mods.isCtrlDown() || e.mods.isCommandDown());
        if (zoom && onZoom) { onZoom(w.deltaY > 0 ? 1.15 : 1.0 / 1.15); return; }
        if (wheelZooms && e.mods.isShiftDown()) {           // Shift + wheel scrolls sideways
            auto d = w;
            d.deltaX = w.deltaY;
            d.deltaY = 0.0f;
            Component::mouseWheelMove(e.withNewPosition(e.position), d);
            return;
        }
        Component::mouseWheelMove(e, w);
    }

    // ---- drag and drop: samples from the explorer, files from the desktop ----------
    bool isInterestedInDragSource(const SourceDetails& d) override { return d.description.toString() == "fdm-sample"; }
    void itemDragMove(const SourceDetails& d) override { updateDrop(d.localPosition.toInt()); }
    void itemDragExit(const SourceDetails&) override { clearDrop(); }
    void itemDropped(const SourceDetails& d) override {
        updateDrop(d.localPosition.toInt());
        if (auto* tree = dynamic_cast<juce::FileTreeComponent*>(d.sourceComponent.get()))
            dropFiles({tree->getSelectedFile().getFullPathName()});
        clearDrop();
    }

    bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
    void fileDragMove(const juce::StringArray&, int x, int y) override { updateDrop({x, y}); }
    void fileDragExit(const juce::StringArray&) override { clearDrop(); }
    void filesDropped(const juce::StringArray& files, int x, int y) override { updateDrop({x, y}); dropFiles(files); clearDrop(); }

private:
    enum class Drag { None, Move, Resize };

    // ---- items ----------------------------------------------------------------------
    static bool getItem(fdm::Channel& ch, int i, Item& out) {
        if (ch.isAudio()) {
            fdm::AudioPlacement a;
            if (!ch.getAudioClip(i, a)) return false;
            out = {a.startTick, a.lengthTicks, a.sample};
            return true;
        }
        fdm::Placement p;
        if (!ch.getPlacement(i, p)) return false;
        out = {p.startTick, p.lengthTicks, p.slot};
        return true;
    }
    static void setItem(fdm::Channel& ch, int i, const Item& it) {
        if (ch.isAudio()) ch.setAudioClip(i, {it.start, it.length, it.ref});
        else ch.setPlacement(i, {it.start, it.length, it.ref});
    }
    static int addItem(fdm::Channel& ch, const Item& it) {
        return ch.isAudio() ? ch.addAudioClip({it.start, it.length, it.ref}) : ch.addPlacement({it.start, it.length, it.ref});
    }
    static void removeItem(fdm::Channel& ch, int i) {
        if (ch.isAudio()) ch.removeAudioClip(i);
        else ch.removePlacement(i);
    }

    juce::Rectangle<float> itemRect(int row, const Item& it) const {
        const float x = static_cast<float>(m_.tickToX(it.start));
        const float w = std::max(4.0f, static_cast<float>(m_.tickToX(static_cast<int64_t>(it.start) + it.length)) - x);
        return {x, static_cast<float>(row * theme::trackHeight + 3), w, static_cast<float>(theme::trackHeight - 6)};
    }

    bool findItem(int row, int x, int& index, Item& out) const {
        if (row < 0 || row >= static_cast<int>(channels.size())) return false;
        auto& ch = app_.engine.channel(channels[static_cast<size_t>(row)]);
        bool found = false;
        for (int i = 0; i < fdm::kMaxPlacements; ++i) {
            Item it;
            if (!getItem(ch, i, it)) continue;
            const auto r = itemRect(row, it);
            if (x >= r.getX() && x <= r.getRight() && (!found || it.start >= out.start)) { index = i; out = it; found = true; }
        }
        return found;
    }

    void select(int channel, int index, int slot) {
        selChannel_ = channel;
        selIndex_ = index;
        app_.selectChannel(channel, slot);
    }

    // ---- painting -------------------------------------------------------------------
    void paintFrame(juce::Graphics& g, juce::Colour colour, const juce::String& title, juce::Rectangle<float> r, bool selected, bool playing) {
        g.setColour(colour.withMultipliedBrightness(playing ? 0.62f : 0.5f));
        g.fillRoundedRectangle(r, 3.0f);
        auto strip = r.withHeight(15.0f);
        g.setColour(colour.withMultipliedBrightness(0.95f));
        g.fillRoundedRectangle(strip, 3.0f);
        g.fillRect(strip.withTop(strip.getCentreY()));
        g.setColour(juce::Colours::black.withAlpha(0.85f));
        g.setFont(theme::labelFont(10.5f));
        g.drawText(title, strip.toNearestInt().reduced(5, 0), juce::Justification::centredLeft, true);
        juce::ignoreUnused(selected);
    }

    void paintBorder(juce::Graphics& g, juce::Rectangle<float> r, bool selected) {
        g.setColour(selected ? theme::text : juce::Colours::black.withAlpha(0.8f));
        g.drawRoundedRectangle(r, 3.0f, selected ? 1.8f : 1.0f);
    }

    bool isUnderPlayhead(const Item& it) const {
        const auto pos = app_.engine.positionTicks();
        return app_.engine.isPlaying() && pos >= it.start && pos < static_cast<int64_t>(it.start) + it.length;
    }

    void paintMidiClip(juce::Graphics& g, fdm::Channel& ch, const Item& p, juce::Rectangle<float> r, bool selected) {
        auto& clip = ch.clips[static_cast<size_t>(p.ref)];
        const auto colour = theme::channelColour(ch.colorIndex.load());
        paintFrame(g, colour, juce::String(clip.name).isEmpty() ? juce::String("Clip") : juce::String(clip.name), r, selected, isUnderPlayhead(p));

        const int clipLen = std::max(1, clip.lengthTicks.load());
        int lo = 127, hi = 0, n = 0;
        std::array<fdm::NoteEvent, fdm::kMaxNotesPerClip> notes;
        for (int i = 0; i < fdm::kMaxNotesPerClip; ++i) {
            fdm::NoteEvent e;
            if (clip.getNote(i, e)) { notes[static_cast<size_t>(n++)] = e; lo = std::min(lo, e.pitch); hi = std::max(hi, e.pitch); }
        }
        auto body = r.withTrimmedTop(17.0f).reduced(0.0f, 3.0f);
        if (n > 0) {
            const float rowH = std::min(5.0f, body.getHeight() / static_cast<float>(hi - lo + 1));
            const float top = body.getCentreY() - rowH * 0.5f * static_cast<float>(hi - lo + 1);
            g.saveState();
            g.reduceClipRegion(r.toNearestInt());
            for (int off = 0; off < p.length; off += clipLen) {
                if (off > 0) {
                    g.setColour(juce::Colours::black.withAlpha(0.35f));
                    g.drawVerticalLine(m_.tickToX(static_cast<int64_t>(p.start) + off), r.getY() + 16.0f, r.getBottom());
                }
                g.setColour(juce::Colours::white.withAlpha(0.85f));
                for (int i = 0; i < n; ++i) {
                    const auto& e = notes[static_cast<size_t>(i)];
                    if (e.start >= clipLen || off + e.start >= p.length) continue;
                    const int64_t s0 = static_cast<int64_t>(p.start) + off + e.start;
                    const int64_t s1 = static_cast<int64_t>(p.start) + off + std::min(e.start + e.length, clipLen);
                    const float x = static_cast<float>(m_.tickToX(s0));
                    const float w = std::max(1.5f, static_cast<float>(m_.tickToX(s1)) - x);
                    g.fillRect(x, top + static_cast<float>(hi - e.pitch) * rowH, w, std::max(1.5f, rowH - 0.5f));
                }
            }
            g.restoreState();
        }
        paintBorder(g, r, selected);
    }

    void paintAudioClip(juce::Graphics& g, fdm::Channel& ch, const Item& it, juce::Rectangle<float> r, bool selected) {
        const auto* s = app_.engine.sample(it.ref);
        const auto colour = theme::channelColour(ch.colorIndex.load());
        paintFrame(g, colour, s != nullptr ? juce::String(s->name) : juce::String("(missing)"), r, selected, isUnderPlayhead(it));
        if (s != nullptr && !s->peaks.empty()) {
            auto body = r.withTrimmedTop(16.0f).reduced(0.0f, 2.0f);
            const auto visible = body.getIntersection(g.getClipBounds().toFloat());
            const double framesPerTick = s->sampleRate * 60.0 / (static_cast<double>(app_.engine.bpm.get()) * fdm::kPPQ);
            const float mid = body.getCentreY(), half = body.getHeight() * 0.5f;
            g.setColour(juce::Colours::white.withAlpha(0.8f));
            for (int x = static_cast<int>(visible.getX()); x < static_cast<int>(visible.getRight()); ++x) {
                const double t0 = static_cast<double>(m_.xToTick(x)) - it.start, t1 = static_cast<double>(m_.xToTick(x + 1)) - it.start;
                if (t0 >= it.length) break;
                const auto p0 = static_cast<size_t>(std::max(0.0, t0 * framesPerTick) / fdm::AudioSample::kPeakBlock);
                const auto p1 = static_cast<size_t>(std::max(0.0, t1 * framesPerTick) / fdm::AudioSample::kPeakBlock);
                if (p0 >= s->peaks.size()) break;
                float peak = 0.0f;
                for (size_t k = p0; k <= std::min(p1, s->peaks.size() - 1); ++k) peak = std::max(peak, s->peaks[k]);
                const float h = std::max(0.5f, std::min(1.0f, peak) * half);
                g.fillRect(static_cast<float>(x), mid - h, 1.0f, h * 2.0f);
            }
        }
        paintBorder(g, r, selected);
    }

    // ---- editing --------------------------------------------------------------------
    void createClipAt(int row, int64_t startTick, int forcedSlot = -1) {
        auto& ch = app_.engine.channel(channels[static_cast<size_t>(row)]);
        int slot = forcedSlot;
        if (slot < 0)
            for (int s = 0; s < fdm::kMaxScenes; ++s)
                if (!ch.clips[static_cast<size_t>(s)].exists.load()) { slot = s; break; }
        if (slot < 0) {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "This track is full",
                                                   "A track holds up to 8 different clips. Delete a clip you don't use, or insert an existing one with right-click > Insert clip.");
            return;
        }
        auto& clip = ch.clips[static_cast<size_t>(slot)];
        int lengthTicks = fdm::kTicksPerBar;
        if (!clip.exists.load()) {
            const int bars = juce::jlimit(1, 8, app_.prefs->getInt("defaultClipBars") + 1);
            clip.create(bars);
            lengthTicks = bars * fdm::kTicksPerBar;
            switch (app_.prefs->getInt("clipNaming")) {
                case 1:  std::snprintf(clip.name, sizeof(clip.name), "%s", ch.name); break;
                case 2:  std::snprintf(clip.name, sizeof(clip.name), "%.24s %d", ch.name, slot + 1); break;
                default: std::snprintf(clip.name, sizeof(clip.name), "Clip %d", slot + 1); break;
            }
        } else {
            lengthTicks = clip.lengthTicks.load();
        }
        const int idx = ch.addPlacement({static_cast<int>(startTick), lengthTicks, slot});
        select(channels[static_cast<size_t>(row)], idx, slot);
        app_.structureHasChanged();
        if (onExtentChanged) onExtentChanged();
        if (requestTab) requestTab(0);
        repaint();
    }

    void importAt(int row, int64_t startTick) {
        const int channel = channels[static_cast<size_t>(row)];
        chooser_ = std::make_unique<juce::FileChooser>("Import audio", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, channel, startTick, safe = juce::Component::SafePointer<TimelineLanes>(this)](const juce::FileChooser& fc) {
                                  if (safe == nullptr || !fc.getResult().existsAsFile()) return;
                                  const int idx = app_.addAudioClip(channel, fc.getResult(), startTick);
                                  if (idx >= 0) select(channel, idx, app_.selection().scene);
                                  if (onExtentChanged) onExtentChanged();
                              });
    }

    void updateDrop(juce::Point<int> p) {
        dropRow_ = std::max(0, p.y / theme::trackHeight);
        if (dropRow_ < static_cast<int>(channels.size()) && !app_.engine.channel(channels[static_cast<size_t>(dropRow_)]).isAudio())
            dropRow_ = -1;                                   // MIDI tracks don't take audio
        dropTick_ = m_.snapDown(m_.xToTick(std::max(0, p.x)), m_.snapTicks(false));
        repaint();
    }
    void clearDrop() { dropRow_ = -1; repaint(); }

    void dropFiles(const juce::StringArray& files) {
        if (dropRow_ < 0) return;
        int channel;
        if (dropRow_ < static_cast<int>(channels.size())) {
            channel = channels[static_cast<size_t>(dropRow_)];
        } else {
            channel = app_.addChannel(fdm::InstrumentType::Audio);         // dropped below the last track
            if (channel < 0) return;
            app_.engine.channel(channel).setName(juce::File(files[0]).getFileNameWithoutExtension().substring(0, 30).toRawUTF8());
        }
        int64_t at = dropTick_;
        for (auto& f : files) {                                            // several files go one after another
            const juce::File file(f);
            if (!file.existsAsFile()) continue;
            const int idx = app_.addAudioClip(channel, file, at);
            if (idx < 0) continue;
            fdm::AudioPlacement a;
            if (app_.engine.channel(channel).getAudioClip(idx, a)) at = a.startTick + a.lengthTicks;
            select(channel, idx, app_.selection().scene);
        }
        if (onExtentChanged) onExtentChanged();
    }

    void removeSelected() {
        if (selChannel_ < 0) return;
        removeItem(app_.engine.channel(selChannel_), selIndex_);
        selChannel_ = selIndex_ = -1;
        if (onExtentChanged) onExtentChanged();
        repaint();
    }

    void duplicateSelected() {
        if (selChannel_ < 0) return;
        auto& ch = app_.engine.channel(selChannel_);
        Item it;
        if (!getItem(ch, selIndex_, it)) return;
        it.start += it.length;
        const int idx = addItem(ch, it);
        if (idx >= 0) selIndex_ = idx;
        if (onExtentChanged) onExtentChanged();
        repaint();
    }

    void makeUnique(int index, Item it) {
        auto& ch = app_.engine.channel(selChannel_);
        int free = -1;
        for (int s = 0; s < fdm::kMaxScenes; ++s) if (!ch.clips[static_cast<size_t>(s)].exists.load()) { free = s; break; }
        if (free < 0) { juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "No free clip slot", "All 8 clips on this track are in use."); return; }
        auto& src = ch.clips[static_cast<size_t>(it.ref)];
        auto& dst = ch.clips[static_cast<size_t>(free)];
        dst.create(1);
        dst.setLengthTicks(src.lengthTicks.load());
        std::snprintf(dst.name, sizeof(dst.name), "%.28s*", src.name);
        fdm::NoteEvent e;
        for (int i = 0; i < fdm::kMaxNotesPerClip; ++i) if (src.getNote(i, e)) dst.addNote(e);
        it.ref = free;
        setItem(ch, index, it);
        app_.selectChannel(selChannel_, free);
        app_.structureHasChanged();
        repaint();
    }

    void showMenu(int row, bool hit, int index, Item it, int x) {
        juce::PopupMenu m;
        auto& ch = app_.engine.channel(channels[static_cast<size_t>(row)]);
        const bool audio = ch.isAudio();
        if (hit) {
            m.addItem(1, "Duplicate   (Ctrl+D)");
            if (!audio) m.addItem(2, "Make clip unique");
            m.addItem(3, "Delete   (Del)");
        } else if (audio) {
            m.addItem(11, "Import audio file here...");
        } else {
            m.addItem(10, "Create clip here");
            juce::PopupMenu insert;
            for (int s = 0; s < fdm::kMaxScenes; ++s)
                if (ch.clips[static_cast<size_t>(s)].exists.load())
                    insert.addItem(20 + s, juce::String(ch.clips[static_cast<size_t>(s)].name));
            m.addSubMenu("Insert clip", insert, insert.getNumItems() > 0);
        }
        const int64_t at = m_.snapDown(m_.xToTick(x), fdm::kTicksPerBar);
        m.showMenuAsync(juce::PopupMenu::Options(), [this, safe = juce::Component::SafePointer<TimelineLanes>(this), row, index, it, at](int id) {
            if (safe == nullptr || id == 0) return;
            if (id == 1) duplicateSelected();
            else if (id == 2) makeUnique(index, it);
            else if (id == 3) removeSelected();
            else if (id == 10) createClipAt(row, at);
            else if (id == 11) importAt(row, at);
            else if (id >= 20) createClipAt(row, at, id - 20);
        });
    }

    AppState& app_;
    TimelineMetrics& m_;
    int selChannel_ = -1, selIndex_ = -1;
    Drag drag_ = Drag::None;
    Item original_;
    int64_t grab_ = 0;
    int dropRow_ = -1;
    int64_t dropTick_ = 0;
    std::unique_ptr<juce::FileChooser> chooser_;
};

// ---------------------------------------------------------------------------
// Track header (REAPER-style): colour tab, name, M/S, volume, meter
// ---------------------------------------------------------------------------
class TrackHeader : public juce::Component, private juce::Timer, private AppState::Listener {
public:
    TrackHeader(AppState& app, int index, std::function<void(int)> requestTab)
        : app_(app), index_(index), requestTab_(std::move(requestTab)) {
        auto& ch = app_.engine.channel(index_);

        name_.setText(ch.name, juce::dontSendNotification);
        name_.setEditable(false, true, false);
        name_.setFont(theme::labelFont(12.5f));
        name_.setColour(juce::Label::textColourId, theme::text);
        name_.onTextChange = [this] { app_.engine.channel(index_).setName(name_.getText().toRawUTF8()); };
        name_.addMouseListener(this, false);
        addAndMakeVisible(name_);

        auto toggle = [this](juce::TextButton& b, const char* t, juce::Colour on) {
            b.setButtonText(t);
            b.setClickingTogglesState(true);
            b.setColour(juce::TextButton::buttonOnColourId, on);
            addAndMakeVisible(b);
        };
        toggle(mute_, "M", theme::accent);
        toggle(solo_, "S", theme::play);
        mute_.setToggleState(ch.mute.load(), juce::dontSendNotification);
        solo_.setToggleState(ch.solo.load(), juce::dontSendNotification);
        mute_.onClick = [this] { app_.engine.channel(index_).mute.store(mute_.getToggleState()); };
        solo_.onClick = [this] { app_.engine.channel(index_).solo.store(solo_.getToggleState()); };

        volume_.setSliderStyle(juce::Slider::LinearHorizontal);
        volume_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        volume_.setRange(-70.0, 6.0, 0.1);
        volume_.setSkewFactorFromMidPoint(-18.0);
        volume_.setValue(std::max(-70.0f, ch.volumeDb.load()), juce::dontSendNotification);
        volume_.setDoubleClickReturnValue(true, 0.0);
        volume_.setPopupDisplayEnabled(true, true, nullptr);
        volume_.textFromValueFunction = [](double v) { return v <= -69.0 ? juce::String("-inf dB") : juce::String(v, 1) + " dB"; };
        volume_.onValueChange = [this] { app_.engine.channel(index_).volumeDb.store(volume_.getValue() <= -69.0 ? -90.0f : static_cast<float>(volume_.getValue())); };
        addAndMakeVisible(volume_);

        app_.addListener(this);
        startTimerHz(30);
    }

    ~TrackHeader() override { app_.removeListener(this); }

    void paint(juce::Graphics& g) override {
        auto& ch = app_.engine.channel(index_);
        const auto colour = theme::channelColour(ch.colorIndex.load());
        const bool selected = app_.selection().kind == AppState::Selection::Kind::Channel && app_.selection().index == index_;

        g.setGradientFill(juce::ColourGradient(theme::panelRaised.brighter(selected ? 0.2f : 0.05f), 0.0f, 0.0f, theme::panel, 0.0f, static_cast<float>(getHeight()), false));
        g.fillRect(getLocalBounds());
        g.setColour(colour);
        g.fillRect(0, 0, 7, getHeight());
        g.setColour(juce::Colours::black);
        g.drawHorizontalLine(getHeight() - 1, 0.0f, static_cast<float>(getWidth()));
        g.drawVerticalLine(getWidth() - 1, 0.0f, static_cast<float>(getHeight()));
        if (selected) { g.setColour(theme::accent); g.drawRect(getLocalBounds().withTrimmedLeft(7), 1); }

        g.setColour(theme::textDim);
        g.setFont(theme::labelFont(9.5f, false));
        g.drawText(ch.instrumentType.load() == static_cast<int>(fdm::InstrumentType::DrumRack) ? "DRUM RACK - TEMP" : (ch.isAudio() ? "AUDIO" : "POLY SYNTH - TEMP"),
                   16, 22, getWidth() - 60, 12, juce::Justification::centredLeft);

        // meter
        auto meter = juce::Rectangle<float>(static_cast<float>(getWidth()) - 14.0f, 6.0f, 6.0f, static_cast<float>(getHeight()) - 14.0f);
        g.setColour(theme::background);
        g.fillRect(meter);
        const float lin = std::max(meterL_, meterR_);
        const float pos = lin <= 0.0003f ? 0.0f : juce::jlimit(0.0f, 1.0f, (fdm::gainToDb(lin) + 60.0f) / 66.0f);
        g.setColour(theme::meterColour(lin));
        g.fillRect(meter.withTop(meter.getBottom() - meter.getHeight() * pos));
    }

    void resized() override {
        auto r = getLocalBounds().withTrimmedLeft(12).withTrimmedRight(22).reduced(0, 4);
        name_.setBounds(r.removeFromTop(18));
        r.removeFromTop(14);
        auto row = r.removeFromTop(22);
        mute_.setBounds(row.removeFromLeft(26));
        row.removeFromLeft(3);
        solo_.setBounds(row.removeFromLeft(26));
        row.removeFromLeft(6);
        volume_.setBounds(row);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu()) { showMenu(); return; }
        app_.selectChannel(index_, app_.selection().kind == AppState::Selection::Kind::Channel ? app_.selection().scene : 0);
        if (requestTab_) requestTab_(1);
    }

private:
    void selectionChanged() override { repaint(); }

    void timerCallback() override {
        auto& ch = app_.engine.channel(index_);
        const float l = ch.takePeakL(), r = ch.takePeakR();
        meterL_ = std::max(l, meterL_ * 0.85f);
        meterR_ = std::max(r, meterR_ * 0.85f);
        if (meterL_ < 0.0003f) meterL_ = 0.0f;
        if (meterR_ < 0.0003f) meterR_ = 0.0f;
        if (meterL_ != lastL_ || meterR_ != lastR_) { lastL_ = meterL_; lastR_ = meterR_; repaint(getWidth() - 16, 0, 12, getHeight()); }
    }

    void showMenu() {
        juce::PopupMenu m;
        m.addItem(1, "Rename");
        m.addItem(2, "Delete track", app_.engine.activeChannelCount() > 1);
        m.addSeparator();
        for (int i = 0; i < 8; ++i) {
            juce::PopupMenu::Item item("Colour " + juce::String(i + 1));
            item.itemID = 100 + i;
            item.colour = theme::channelColour(i);
            m.addItem(item);
        }
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this, safe = juce::Component::SafePointer<TrackHeader>(this)](int id) {
            if (safe == nullptr || id == 0) return;
            if (id == 1) name_.showEditor();
            else if (id == 2) {
                const int idx = index_;
                juce::MessageManager::callAsync([&app = app_, idx] { app.removeChannel(idx); });
            } else if (id >= 100) { app_.engine.channel(index_).colorIndex.store(id - 100); app_.structureHasChanged(); }
        });
    }

    AppState& app_;
    int index_;
    std::function<void(int)> requestTab_;
    juce::Label name_;
    juce::TextButton mute_, solo_;
    juce::Slider volume_;
    float meterL_ = 0.0f, meterR_ = 0.0f, lastL_ = -1.0f, lastR_ = -1.0f;
};

// ---------------------------------------------------------------------------
// The whole arrangement: ruler, track headers, lanes, zoom
// ---------------------------------------------------------------------------
class ArrangementView : public juce::Component, private juce::Timer, private AppState::Listener, private juce::AsyncUpdater,
                        private juce::ChangeListener {
public:
    explicit ArrangementView(AppState& app)
        : app_(app), ruler_(app, metrics_), lanes_(app, metrics_) {
        viewport_.setViewedComponent(&lanes_, false);
        viewport_.setScrollBarsShown(true, true);
        viewport_.onScroll = [this](int x, int y) { syncScroll(x, y); };
        addAndMakeVisible(viewport_);

        rulerHolder_.addAndMakeVisible(ruler_);
        headerHolder_.addAndMakeVisible(headerColumn_);
        addAndMakeVisible(rulerHolder_);
        addAndMakeVisible(headerHolder_);
        addAndMakeVisible(corner_);

        zoomIn_ = std::make_unique<IconButton>(IconButton::Icon::Plus);
        zoomOut_ = std::make_unique<juce::TextButton>("-");
        zoomIn_->onClick = [this] { setZoom(metrics_.pxPerBar * 1.5); };
        zoomOut_->onClick = [this] { setZoom(metrics_.pxPerBar / 1.5); };
        corner_.addAndMakeVisible(*zoomIn_);
        corner_.addAndMakeVisible(*zoomOut_);

        addTrack_ = std::make_unique<juce::TextButton>("+ Add track");
        addTrack_->onClick = [this] { showAddMenu(); };
        headerColumn_.addAndMakeVisible(*addTrack_);

        lanes_.requestTab = [this](int t) { if (requestTab) requestTab(t); };
        lanes_.onExtentChanged = [this] { updateExtent(); };
        ruler_.onLoopChanged = [this] { lanes_.repaint(); };
        lanes_.onZoom = [this](double f) { zoomBy(f); };
        app_.prefs->addChangeListener(this);
        applyPrefs();

        app_.addListener(this);
        rebuild();
        startTimerHz(30);
    }

    ~ArrangementView() override { app_.prefs->removeChangeListener(this); app_.removeListener(this); cancelPendingUpdate(); }

    void zoomBy(double factor) { setZoom(metrics_.pxPerBar * factor); }
    void duplicateSelected() { lanes_.duplicateSelectedClip(); }
    void deleteSelected() { lanes_.deleteSelectedClip(); }

    std::function<void(int tab)> requestTab;

    void paint(juce::Graphics& g) override { g.fillAll(theme::background); }

    void resized() override {
        auto r = getLocalBounds();
        auto top = r.removeFromTop(theme::kRulerHeight);
        corner_.setBounds(top.removeFromLeft(theme::kTrackHeaderW));
        rulerHolder_.setBounds(top.withTrimmedRight(10));
        headerHolder_.setBounds(r.removeFromLeft(theme::kTrackHeaderW).withTrimmedBottom(10));
        viewport_.setBounds(r);
        zoomOut_->setBounds(corner_.getWidth() - 62, 4, 26, theme::kRulerHeight - 8);
        zoomIn_->setBounds(corner_.getWidth() - 32, 4, 26, theme::kRulerHeight - 8);
        updateExtent();
    }

    bool keyPressed(const juce::KeyPress&) override { return false; }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override {
        if (e.mods.isCommandDown() || e.mods.isCtrlDown()) setZoom(metrics_.pxPerBar * (w.deltaY > 0 ? 1.15 : 1.0 / 1.15));
        else Component::mouseWheelMove(e, w);
    }

    void setZoom(double px) {
        const double oldPx = metrics_.pxPerBar;
        px = juce::jlimit(18.0, 640.0, px);
        if (px == oldPx) return;
        const auto view = viewport_.getViewPosition();
        const int64_t centreTick = metrics_.xToTick(view.x + viewport_.getMaximumVisibleWidth() / 2);
        metrics_.pxPerBar = px;
        updateExtent();
        viewport_.setViewPosition(std::max(0, metrics_.tickToX(centreTick) - viewport_.getMaximumVisibleWidth() / 2), view.y);
        ruler_.repaint();
        lanes_.repaint();
    }

private:
    struct SyncViewport : juce::Viewport {
        std::function<void(int, int)> onScroll;
        void visibleAreaChanged(const juce::Rectangle<int>& area) override {
            juce::Viewport::visibleAreaChanged(area);
            if (onScroll) onScroll(area.getX(), area.getY());
        }
    };
    struct Holder : juce::Component { Holder() { setInterceptsMouseClicks(false, true); } };
    struct Corner : juce::Component {
        void paint(juce::Graphics& g) override {
            g.setGradientFill(juce::ColourGradient(theme::panelRaised, 0.0f, 0.0f, theme::panel, 0.0f, static_cast<float>(getHeight()), false));
            g.fillRect(getLocalBounds());
            g.setColour(theme::textDim);
            g.setFont(theme::labelFont(10.5f));
            g.drawText("TRACKS", 12, 0, 80, getHeight(), juce::Justification::centredLeft);
            g.setColour(juce::Colours::black);
            g.drawHorizontalLine(getHeight() - 1, 0.0f, static_cast<float>(getWidth()));
            g.drawVerticalLine(getWidth() - 1, 0.0f, static_cast<float>(getHeight()));
        }
    };

    void changeListenerCallback(juce::ChangeBroadcaster*) override { applyPrefs(); }

    void applyPrefs() {
        metrics_.snapMode = app_.prefs->getInt("timelineSnap");
        static const int heights[] = {48, 66, 92};
        const int h = heights[juce::jlimit(0, 2, app_.prefs->getInt("trackHeight"))];
        if (h != theme::trackHeight) {
            theme::trackHeight = h;
            signature_.clear();                  // re-create the track headers at the new height
            rebuild();
        }
    }

    void selectionChanged() override { lanes_.repaint(); }
    void structureChanged() override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override { rebuild(); }

    void syncScroll(int x, int y) {
        ruler_.setTopLeftPosition(-x, 0);
        headerColumn_.setTopLeftPosition(0, -y);
    }

    void updateExtent() {
        const int64_t end = app_.engine.arrangementEndTick();
        metrics_.totalBars = std::max(64, static_cast<int>(end / fdm::kTicksPerBar) + 32);
        const int rows = static_cast<int>(lanes_.channels.size());
        const int h = std::max(rows * theme::trackHeight + 120, viewport_.getMaximumVisibleHeight());
        const int w = std::max(metrics_.width(), viewport_.getMaximumVisibleWidth());
        lanes_.setSize(w, h);
        ruler_.setSize(w, theme::kRulerHeight);
        headerColumn_.setSize(theme::kTrackHeaderW, h);
        addTrack_->setBounds(10, rows * theme::trackHeight + 10, theme::kTrackHeaderW - 20, 28);
        syncScroll(viewport_.getViewPositionX(), viewport_.getViewPositionY());
        lanes_.repaint();
        ruler_.repaint();
    }

    void rebuild() {
        std::vector<int> active;
        for (int i = 0; i < fdm::kMaxChannels; ++i)
            if (app_.engine.channel(i).active.load()) active.push_back(i);
        std::vector<int> sig = active;
        for (int i : active) sig.push_back(app_.engine.channel(i).colorIndex.load() + 1000);

        lanes_.channels = active;
        if (sig != signature_) {
            signature_ = sig;
            headers_.clear();
            for (size_t row = 0; row < active.size(); ++row) {
                headers_.push_back(std::make_unique<TrackHeader>(app_, active[row], [this](int t) { if (requestTab) requestTab(t); }));
                headerColumn_.addAndMakeVisible(*headers_.back());
                headers_.back()->setBounds(0, static_cast<int>(row) * theme::trackHeight, theme::kTrackHeaderW, theme::trackHeight);
            }
        }
        addTrack_->setVisible(app_.engine.activeChannelCount() < fdm::kMaxChannels);
        updateExtent();
    }

    void showAddMenu() {
        juce::PopupMenu m;
        m.addItem(3, "Audio track");
        m.addItem(1, "MIDI track  (temp drum rack)");
        m.addItem(2, "MIDI track  (temp synth)");
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(addTrack_.get()), [this](int id) {
            if (id == 1) app_.addChannel(fdm::InstrumentType::DrumRack);
            else if (id == 2) app_.addChannel(fdm::InstrumentType::PolySynth);
            else if (id == 3) app_.addChannel(fdm::InstrumentType::Audio);
        });
    }

    void timerCallback() override {
        const int64_t pos = app_.engine.positionTicks();
        if (pos == lastPos_) return;
        const int oldX = metrics_.tickToX(lastPos_), newX = metrics_.tickToX(pos);
        lastPos_ = pos;
        lanes_.repaint(oldX - 3, 0, 7, lanes_.getHeight());
        lanes_.repaint(newX - 3, 0, 7, lanes_.getHeight());
        ruler_.repaint(oldX - 8, 0, 17, theme::kRulerHeight);
        ruler_.repaint(newX - 8, 0, 17, theme::kRulerHeight);

        if (app_.engine.isPlaying() && app_.prefs->getBool("followPlayhead")) {        // keep the playhead on screen
            const auto view = viewport_.getViewPosition();
            const int visible = viewport_.getMaximumVisibleWidth();
            if (newX < view.x || newX > view.x + visible - 30)
                viewport_.setViewPosition(std::max(0, newX - 40), view.y);
        }
    }

    AppState& app_;
    TimelineMetrics metrics_;
    TimelineRuler ruler_;
    TimelineLanes lanes_;
    SyncViewport viewport_;
    Holder rulerHolder_, headerHolder_;
    Corner corner_;
    Holder headerColumn_;
    std::unique_ptr<IconButton> zoomIn_;
    std::unique_ptr<juce::TextButton> zoomOut_, addTrack_;
    std::vector<std::unique_ptr<TrackHeader>> headers_;
    std::vector<int> signature_;
    int64_t lastPos_ = -1;
};
