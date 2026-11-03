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
    int snapTicks(bool fine) const {
        if (fine) return fdm::kTicksPerStep;
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
// The lanes: one row per channel, clips as draggable blocks
// ---------------------------------------------------------------------------
class TimelineLanes : public juce::Component {
public:
    TimelineLanes(AppState& app, TimelineMetrics& m) : app_(app), m_(m) { setWantsKeyboardFocus(true); }

    std::function<void(int tab)> requestTab;
    std::vector<int> channels;                    // engine channel index per row

    void paint(juce::Graphics& g) override {
        const auto clipB = g.getClipBounds();
        g.fillAll(theme::background);

        const int rows = static_cast<int>(channels.size());
        for (int r = 0; r < rows; ++r) {
            const int y = r * theme::kTrackHeight;
            if (y > clipB.getBottom() || y + theme::kTrackHeight < clipB.getY()) continue;
            const bool selected = app_.selection().kind == AppState::Selection::Kind::Channel && app_.selection().index == channels[static_cast<size_t>(r)];
            g.setColour(selected ? theme::panel.brighter(0.05f).withAlpha(1.0f) : (r % 2 ? theme::panel.darker(0.25f) : theme::panel.darker(0.45f)));
            g.fillRect(0, y, getWidth(), theme::kTrackHeight);
            g.setColour(juce::Colours::black);
            g.drawHorizontalLine(y + theme::kTrackHeight - 1, 0.0f, static_cast<float>(getWidth()));
        }

        // loop region
        if (app_.engine.loopEnabled.load()) {
            const int a = m_.tickToX(app_.engine.loopStartTick.load()), b = m_.tickToX(app_.engine.loopEndTick.load());
            g.setColour(theme::accent.withAlpha(0.07f));
            g.fillRect(a, 0, b - a, getHeight());
            g.setColour(theme::accent.withAlpha(0.5f));
            g.drawVerticalLine(a, 0.0f, static_cast<float>(getHeight()));
            g.drawVerticalLine(b, 0.0f, static_cast<float>(getHeight()));
        }

        // grid
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

        // clips
        for (int r = 0; r < rows; ++r) {
            auto& ch = app_.engine.channel(channels[static_cast<size_t>(r)]);
            for (int i = 0; i < fdm::kMaxPlacements; ++i) {
                fdm::Placement p;
                if (!ch.getPlacement(i, p)) continue;
                const auto rect = placementRect(r, p);
                if (!rect.toNearestInt().intersects(clipB)) continue;
                paintClip(g, ch, p, rect, selChannel_ == channels[static_cast<size_t>(r)] && selIndex_ == i);
            }
        }

        // playhead
        const int px = m_.tickToX(app_.engine.positionTicks());
        g.setColour(theme::accentHi);
        g.drawVerticalLine(px, 0.0f, static_cast<float>(getHeight()));
    }

    // ---- hit testing --------------------------------------------------------------
    juce::Rectangle<float> placementRect(int row, const fdm::Placement& p) const {
        const float x = static_cast<float>(m_.tickToX(p.startTick));
        const float w = std::max(4.0f, static_cast<float>(m_.tickToX(static_cast<int64_t>(p.startTick) + p.lengthTicks)) - x);
        return {x, static_cast<float>(row * theme::kTrackHeight + 3), w, static_cast<float>(theme::kTrackHeight - 6)};
    }

    bool findPlacement(int row, int x, int& index, fdm::Placement& out) const {
        if (row < 0 || row >= static_cast<int>(channels.size())) return false;
        auto& ch = app_.engine.channel(channels[static_cast<size_t>(row)]);
        bool found = false;
        for (int i = 0; i < fdm::kMaxPlacements; ++i) {
            fdm::Placement p;
            if (!ch.getPlacement(i, p)) continue;
            const auto r = placementRect(row, p);
            if (x >= r.getX() && x <= r.getRight() && (!found || p.startTick >= out.startTick)) { index = i; out = p; found = true; }
        }
        return found;
    }

    // ---- mouse --------------------------------------------------------------------
    void mouseDown(const juce::MouseEvent& e) override {
        grabKeyboardFocus();
        const int row = e.y / theme::kTrackHeight;
        if (row < 0 || row >= static_cast<int>(channels.size())) return;
        const int channel = channels[static_cast<size_t>(row)];
        int index = -1;
        fdm::Placement p;
        const bool hit = findPlacement(row, e.x, index, p);
        drag_ = Drag::None;

        if (e.mods.isPopupMenu()) {
            if (hit) select(channel, index, p.slot);
            else app_.selectChannel(channel, app_.selection().scene);
            showMenu(row, hit, index, p, e.x);
            return;
        }

        if (hit) {
            select(channel, index, p.slot);
            original_ = p;
            const auto r = placementRect(row, p);
            drag_ = e.x > r.getRight() - 7.0f ? Drag::Resize : Drag::Move;
            grab_ = m_.xToTick(e.x) - p.startTick;
            if (requestTab) requestTab(0);
        } else {
            selChannel_ = -1;
            selIndex_ = -1;
            app_.selectChannel(channel, app_.selection().scene);
            app_.engine.setPlayhead(m_.snap(m_.xToTick(e.x), m_.snapTicks(e.mods.isAltDown())));
        }
        repaint();
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override {
        const int row = e.y / theme::kTrackHeight;
        if (row < 0 || row >= static_cast<int>(channels.size()) || e.mods.isPopupMenu()) return;
        int index; fdm::Placement p;
        if (findPlacement(row, e.x, index, p)) return;
        createClipAt(row, m_.snapDown(m_.xToTick(e.x), fdm::kTicksPerBar));
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (drag_ == Drag::None || selChannel_ < 0) return;
        auto& ch = app_.engine.channel(selChannel_);
        fdm::Placement p = original_;
        const int grid = m_.snapTicks(e.mods.isAltDown());
        if (drag_ == Drag::Move) {
            p.startTick = static_cast<int>(std::max<int64_t>(0, m_.snap(m_.xToTick(e.x) - grab_, grid)));
        } else {
            const int64_t end = m_.snap(m_.xToTick(e.x), grid);
            p.lengthTicks = static_cast<int>(std::max<int64_t>(fdm::kTicksPerStep, end - p.startTick));
        }
        ch.setPlacement(selIndex_, p);
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override {
        if (drag_ != Drag::None && onExtentChanged) onExtentChanged();
        drag_ = Drag::None;
    }

    void mouseMove(const juce::MouseEvent& e) override {
        const int row = e.y / theme::kTrackHeight;
        int index; fdm::Placement p;
        bool resize = false;
        if (findPlacement(row, e.x, index, p)) resize = e.x > placementRect(row, p).getRight() - 7.0f;
        setMouseCursor(resize ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    }

    bool keyPressed(const juce::KeyPress& key) override {
        if (selChannel_ < 0) return false;
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) { removeSelected(); return true; }
        if (key == juce::KeyPress('d', juce::ModifierKeys::commandModifier, 0)) { duplicateSelected(); return true; }
        return false;
    }

    std::function<void()> onExtentChanged;        // placements moved or added: the ruler/scroll width may change

private:
    enum class Drag { None, Move, Resize };

    void select(int channel, int index, int slot) {
        selChannel_ = channel;
        selIndex_ = index;
        app_.selectChannel(channel, slot);
    }

    void paintClip(juce::Graphics& g, fdm::Channel& ch, const fdm::Placement& p, juce::Rectangle<float> r, bool selected) {
        auto& clip = ch.clips[static_cast<size_t>(p.slot)];
        const auto colour = theme::channelColour(ch.colorIndex.load());
        const bool playing = ch.playingSlot.load() == p.slot && app_.engine.isPlaying() && app_.engine.arrangementMode.load()
                             && app_.engine.positionTicks() >= p.startTick && app_.engine.positionTicks() < static_cast<int64_t>(p.startTick) + p.lengthTicks;
        g.setColour(colour.withMultipliedBrightness(playing ? 0.62f : 0.5f));
        g.fillRoundedRectangle(r, 3.0f);

        // title strip
        auto title = r.withHeight(15.0f);
        g.setColour(colour.withMultipliedBrightness(0.95f));
        g.fillRoundedRectangle(title, 3.0f);
        g.fillRect(title.withTop(title.getCentreY()));
        g.setColour(juce::Colours::black.withAlpha(0.85f));
        g.setFont(theme::labelFont(10.5f));
        g.drawText(juce::String(clip.name).isEmpty() ? "Clip" : juce::String(clip.name), title.toNearestInt().reduced(5, 0), juce::Justification::centredLeft, true);

        // notes (the clip repeats to fill the placement)
        const int clipLen = std::max(1, clip.lengthTicks.load());
        int lo = 127, hi = 0;
        std::array<fdm::NoteEvent, fdm::kMaxNotesPerClip> notes;
        int n = 0;
        for (int i = 0; i < fdm::kMaxNotesPerClip; ++i) {
            fdm::NoteEvent e;
            if (clip.getNote(i, e)) { notes[static_cast<size_t>(n++)] = e; lo = std::min(lo, e.pitch); hi = std::max(hi, e.pitch); }
        }
        auto body = r.withTrimmedTop(17.0f).reduced(0.0f, 3.0f);
        if (n > 0) {
            const float rowH = std::min(5.0f, body.getHeight() / static_cast<float>(hi - lo + 1));
            const float mid = body.getCentreY() - rowH * 0.5f * static_cast<float>(hi - lo + 1) + 0.0f;
            g.saveState();
            g.reduceClipRegion(r.toNearestInt());
            g.setColour(juce::Colours::white.withAlpha(0.85f));
            for (int off = 0; off < p.lengthTicks; off += clipLen) {
                if (off > 0) { g.setColour(juce::Colours::black.withAlpha(0.35f)); g.drawVerticalLine(m_.tickToX(static_cast<int64_t>(p.startTick) + off), r.getY() + 16.0f, r.getBottom()); g.setColour(juce::Colours::white.withAlpha(0.85f)); }
                for (int i = 0; i < n; ++i) {
                    const auto& e = notes[static_cast<size_t>(i)];
                    if (e.start >= clipLen || off + e.start >= p.lengthTicks) continue;
                    const float x = static_cast<float>(m_.tickToX(static_cast<int64_t>(p.startTick) + off + e.start));
                    const float w = std::max(1.5f, static_cast<float>(m_.tickToX(static_cast<int64_t>(p.startTick) + off + std::min(e.start + e.length, clipLen)) ) - static_cast<float>(m_.tickToX(static_cast<int64_t>(p.startTick) + off + e.start)));
                    g.fillRect(x, mid + static_cast<float>(hi - e.pitch) * rowH, w, std::max(1.5f, rowH - 0.5f));
                }
            }
            g.restoreState();
        }

        g.setColour(selected ? theme::text : juce::Colours::black.withAlpha(0.8f));
        g.drawRoundedRectangle(r, 3.0f, selected ? 1.8f : 1.0f);
    }

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
            clip.create(1);
            std::snprintf(clip.name, sizeof(clip.name), "Clip %d", slot + 1);
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

    void removeSelected() {
        if (selChannel_ < 0) return;
        app_.engine.channel(selChannel_).removePlacement(selIndex_);
        selChannel_ = selIndex_ = -1;
        if (onExtentChanged) onExtentChanged();
        repaint();
    }

    void duplicateSelected() {
        if (selChannel_ < 0) return;
        auto& ch = app_.engine.channel(selChannel_);
        fdm::Placement p;
        if (!ch.getPlacement(selIndex_, p)) return;
        p.startTick += p.lengthTicks;
        const int idx = ch.addPlacement(p);
        if (idx >= 0) selIndex_ = idx;
        if (onExtentChanged) onExtentChanged();
        repaint();
    }

    void makeUnique(int index, fdm::Placement p) {
        auto& ch = app_.engine.channel(selChannel_);
        int free = -1;
        for (int s = 0; s < fdm::kMaxScenes; ++s) if (!ch.clips[static_cast<size_t>(s)].exists.load()) { free = s; break; }
        if (free < 0) { juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "No free clip slot", "All 8 clip slots on this track are in use."); return; }
        auto& src = ch.clips[static_cast<size_t>(p.slot)];
        auto& dst = ch.clips[static_cast<size_t>(free)];
        dst.create(1);
        dst.setLengthTicks(src.lengthTicks.load());
        std::snprintf(dst.name, sizeof(dst.name), "%.28s*", src.name);
        fdm::NoteEvent e;
        for (int i = 0; i < fdm::kMaxNotesPerClip; ++i) if (src.getNote(i, e)) dst.addNote(e);
        p.slot = free;
        ch.setPlacement(index, p);
        app_.selectChannel(selChannel_, free);
        app_.structureHasChanged();
        repaint();
    }

    void showMenu(int row, bool hit, int index, fdm::Placement p, int x) {
        juce::PopupMenu m;
        const int channel = channels[static_cast<size_t>(row)];
        auto& ch = app_.engine.channel(channel);
        if (hit) {
            m.addItem(1, "Duplicate   (Ctrl+D)");
            m.addItem(2, "Make clip unique");
            m.addItem(3, "Delete   (Del)");
        } else {
            m.addItem(10, "Create clip here");
            juce::PopupMenu insert;
            for (int s = 0; s < fdm::kMaxScenes; ++s)
                if (ch.clips[static_cast<size_t>(s)].exists.load())
                    insert.addItem(20 + s, juce::String(ch.clips[static_cast<size_t>(s)].name));
            m.addSubMenu("Insert clip", insert, insert.getNumItems() > 0);
        }
        const int64_t at = m_.snapDown(m_.xToTick(x), fdm::kTicksPerBar);
        m.showMenuAsync(juce::PopupMenu::Options(), [this, safe = juce::Component::SafePointer<TimelineLanes>(this), row, index, p, at](int id) {
            if (safe == nullptr || id == 0) return;
            if (id == 1) duplicateSelected();
            else if (id == 2) makeUnique(index, p);
            else if (id == 3) removeSelected();
            else if (id == 10) createClipAt(row, at);
            else if (id >= 20) createClipAt(row, at, id - 20);
        });
    }

    AppState& app_;
    TimelineMetrics& m_;
    int selChannel_ = -1, selIndex_ = -1;
    Drag drag_ = Drag::None;
    fdm::Placement original_;
    int64_t grab_ = 0;
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
        g.drawText(ch.instrumentType.load() == static_cast<int>(fdm::InstrumentType::DrumRack) ? "DRUM RACK - TEMP" : "POLY SYNTH - TEMP",
                   16, 22, getWidth() - 60, 12, juce::Justification::centredLeft);

        // meter
        auto meter = juce::Rectangle<float>(static_cast<float>(getWidth()) - 14.0f, 6.0f, 6.0f, static_cast<float>(getHeight()) - 14.0f);
        g.setColour(theme::background);
        g.fillRect(meter);
        const float lin = std::max(meterL_, meterR_);
        const float pos = lin <= 0.0003f ? 0.0f : juce::jlimit(0.0f, 1.0f, (fdm::gainToDb(lin) + 60.0f) / 66.0f);
        g.setColour(lin > 1.0f ? theme::warn : (lin > 0.7f ? theme::accentHi : theme::play));
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
class ArrangementView : public juce::Component, private juce::Timer, private AppState::Listener, private juce::AsyncUpdater {
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

        app_.addListener(this);
        rebuild();
        startTimerHz(30);
    }

    ~ArrangementView() override { app_.removeListener(this); cancelPendingUpdate(); }

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
        const int h = std::max(rows * theme::kTrackHeight + 120, viewport_.getMaximumVisibleHeight());
        const int w = std::max(metrics_.width(), viewport_.getMaximumVisibleWidth());
        lanes_.setSize(w, h);
        ruler_.setSize(w, theme::kRulerHeight);
        headerColumn_.setSize(theme::kTrackHeaderW, h);
        addTrack_->setBounds(10, rows * theme::kTrackHeight + 10, theme::kTrackHeaderW - 20, 28);
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
                headers_.back()->setBounds(0, static_cast<int>(row) * theme::kTrackHeight, theme::kTrackHeaderW, theme::kTrackHeight);
            }
        }
        addTrack_->setVisible(app_.engine.activeChannelCount() < fdm::kMaxChannels);
        updateExtent();
    }

    void showAddMenu() {
        juce::PopupMenu m;
        m.addItem(1, "Drum Rack track  (placeholder instrument)");
        m.addItem(2, "Synth track  (placeholder instrument)");
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(addTrack_.get()), [this](int id) {
            if (id == 1) app_.addChannel(fdm::InstrumentType::DrumRack);
            else if (id == 2) app_.addChannel(fdm::InstrumentType::PolySynth);
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

        if (app_.engine.isPlaying()) {                              // keep the playhead on screen
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
