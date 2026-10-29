#pragma once

#include "../AppState.h"
#include "../Theme.h"
#include "../Widgets.h"

/** The note grid: a step sequencer for the drum rack (8 pad rows) and a piano roll for synths. */
class NoteGrid : public juce::Component, private juce::Timer {
public:
    explicit NoteGrid(AppState& app) : app_(app) { startTimerHz(30); }

    static constexpr int kKeyW = 74;
    static constexpr int kLowNote = 24, kHighNote = 96;

    void setSnap(int ticks) { snap_ = ticks; }
    void setStepWidth(float w) { stepW_ = w; updateSize(); }
    float stepWidth() const { return stepW_; }

    bool isDrum() const {
        auto* ch = app_.selectedChannel();
        return ch != nullptr && ch->instrumentType.load() == static_cast<int>(fdm::InstrumentType::DrumRack);
    }
    int numRows() const { return isDrum() ? fdm::DrumRack::kNumPads : kHighNote - kLowNote + 1; }
    int rowHeight() const { return isDrum() ? 24 : 16; }
    int pitchForRow(int row) const { return isDrum() ? fdm::DrumRack::padNote(fdm::DrumRack::kNumPads - 1 - row) : kHighNote - row; }
    int rowForPitch(int pitch) const {
        if (isDrum()) { const int pad = fdm::DrumRack::padForNote(pitch); return pad < 0 ? -1 : fdm::DrumRack::kNumPads - 1 - pad; }
        return pitch < kLowNote || pitch > kHighNote ? -1 : kHighNote - pitch;
    }

    void updateSize() {
        auto* clip = app_.selectedClip();
        const int ticks = clip != nullptr ? clip->lengthTicks.load() : fdm::kTicksPerBar;
        setSize(kKeyW + static_cast<int>(std::ceil(ticks * stepW_ / fdm::kTicksPerStep)) + 8, numRows() * rowHeight());
        repaint();
    }

    /** Scrolls so the notes (or middle C) are in view; returns a y to scroll to. */
    int suggestedScrollY() const {
        if (isDrum()) return 0;
        int lo = 127, hi = 0;
        if (auto* clip = app_.selectedClip()) {
            fdm::NoteEvent e;
            for (int i = 0; i < fdm::kMaxNotesPerClip; ++i)
                if (clip->getNote(i, e)) { lo = std::min(lo, e.pitch); hi = std::max(hi, e.pitch); }
        }
        const int centre = hi >= lo ? (lo + hi) / 2 : 60;
        return std::max(0, rowForPitch(juce::jlimit(kLowNote, kHighNote, centre)) * rowHeight() - 90);
    }

    void paint(juce::Graphics& g) override {
        const int rh = rowHeight(), rows = numRows();
        const bool drum = isDrum();
        auto* ch = app_.selectedChannel();
        auto* clip = app_.selectedClip();
        const juce::Colour colour = theme::channelColour(ch != nullptr ? ch->colorIndex.load() : 0);

        g.fillAll(theme::background);
        const int gridW = getWidth() - kKeyW;

        // rows
        for (int r = 0; r < rows; ++r) {
            const int pitch = pitchForRow(r);
            const bool black = !drum && juce::MidiMessage::isMidiNoteBlack(pitch);
            g.setColour(black ? theme::background.darker(0.3f) : theme::panel.withAlpha(0.55f));
            g.fillRect(kKeyW, r * rh, gridW, rh);
            g.setColour(theme::gridStep);
            g.drawHorizontalLine((r + 1) * rh - 1, static_cast<float>(kKeyW), static_cast<float>(getWidth()));
        }

        // vertical grid
        const int length = clip != nullptr ? clip->lengthTicks.load() : fdm::kTicksPerBar;
        for (int t = 0; t <= length; t += fdm::kTicksPerStep) {
            const int x = tickToX(t);
            const bool bar = t % fdm::kTicksPerBar == 0, beat = t % fdm::kTicksPerBeat == 0;
            g.setColour(bar ? theme::gridBar : (beat ? theme::gridBeat : theme::gridStep));
            g.drawVerticalLine(x, 0.0f, static_cast<float>(getHeight()));
        }

        // key column
        for (int r = 0; r < rows; ++r) {
            const int pitch = pitchForRow(r);
            juce::Rectangle<int> key(0, r * rh, kKeyW, rh);
            if (drum) {
                g.setColour(theme::panelRaised);
                g.fillRect(key.reduced(0, 1));
                g.setColour(theme::text);
                g.setFont(juce::Font(juce::FontOptions(12.0f)));
                g.drawText(fdm::DrumRack::padName(fdm::DrumRack::kNumPads - 1 - r), key.reduced(8, 0), juce::Justification::centredLeft);
            } else {
                const bool black = juce::MidiMessage::isMidiNoteBlack(pitch);
                g.setColour(black ? juce::Colour(0xff20232b) : juce::Colour(0xffd5d9e2));
                g.fillRect(key.withTrimmedBottom(1).withTrimmedRight(black ? 18 : 0));
                if (black) { g.setColour(theme::panel); g.fillRect(key.withLeft(kKeyW - 18)); }
                if (pitch % 12 == 0) {
                    g.setColour(juce::Colour(0xff20232b));
                    g.setFont(juce::Font(juce::FontOptions(10.5f)));
                    g.drawText("C" + juce::String(pitch / 12 - 1), key.withTrimmedRight(4), juce::Justification::centredRight);
                }
            }
        }

        // notes
        if (clip != nullptr) {
            fdm::NoteEvent e;
            for (int i = 0; i < fdm::kMaxNotesPerClip; ++i) {
                if (!clip->getNote(i, e)) continue;
                const int row = rowForPitch(e.pitch);
                if (row < 0) continue;
                auto r = noteRect(e).toFloat().reduced(0.0f, 1.0f);
                g.setColour(colour.withMultipliedBrightness(0.45f + 0.55f * static_cast<float>(e.velocity) / 127.0f));
                g.fillRoundedRectangle(r, 3.0f);
                g.setColour(colour.brighter(0.5f).withAlpha(0.9f));
                g.drawRoundedRectangle(r, 3.0f, 1.0f);
            }
        } else {
            g.setColour(theme::textDim);
            g.setFont(juce::Font(juce::FontOptions(14.0f)));
            g.drawText("Double-click an empty clip slot to create a clip", getLocalBounds().withLeft(kKeyW), juce::Justification::centred);
        }

        // playhead
        if (playhead_ >= 0) {
            g.setColour(theme::text.withAlpha(0.85f));
            g.drawVerticalLine(tickToX(playhead_), 0.0f, static_cast<float>(getHeight()));
        }
    }

    // ---- mouse -------------------------------------------------------------
    void mouseDown(const juce::MouseEvent& e) override {
        auto* clip = app_.selectedClip();
        if (clip == nullptr) return;
        const int row = juce::jlimit(0, numRows() - 1, e.y / rowHeight());

        if (e.x < kKeyW) {                          // key column: audition
            auditionPitch_ = pitchForRow(row);
            app_.previewNoteOn(auditionPitch_);
            return;
        }

        dragIndex_ = noteAt(*clip, e.x, e.y);
        dragMode_ = Drag::None;

        if (dragIndex_ >= 0) {
            clip->getNote(dragIndex_, original_);
            if (e.mods.isPopupMenu() || (isDrum() && !e.mods.isShiftDown())) {      // delete
                clip->removeNote(dragIndex_);
                dragIndex_ = -1;
                repaint();
                return;
            }
            if (e.mods.isShiftDown()) { dragMode_ = Drag::Velocity; return; }
            const auto r = noteRect(original_);
            dragMode_ = (!isDrum() && e.x > r.getRight() - 7) ? Drag::Resize : Drag::Move;
            grab_ = xToTick(e.x, false) - original_.start;
            audition(original_.pitch);
        } else if (!e.mods.isPopupMenu()) {
            fdm::NoteEvent n;
            n.pitch = pitchForRow(row);
            n.start = juce::jlimit(0, clip->lengthTicks.load() - snap(1), xToTick(e.x, true));
            n.length = isDrum() ? fdm::kTicksPerStep : lastLength_;
            n.length = std::min(n.length, clip->lengthTicks.load() - n.start);
            n.velocity = velocity_;
            dragIndex_ = clip->addNote(n);
            original_ = n;
            grab_ = 0;
            dragMode_ = dragIndex_ >= 0 ? Drag::Move : Drag::None;
            audition(n.pitch);
        }
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        auto* clip = app_.selectedClip();
        if (clip == nullptr || dragIndex_ < 0 || dragMode_ == Drag::None) return;
        fdm::NoteEvent n = original_;
        const int len = clip->lengthTicks.load();

        switch (dragMode_) {
            case Drag::Move: {
                n.start = juce::jlimit(0, len - 1, xToTick(e.x - 0, true, grab_));
                n.length = std::min(original_.length, len - n.start);
                const int row = juce::jlimit(0, numRows() - 1, e.y / rowHeight());
                n.pitch = pitchForRow(row);
                break;
            }
            case Drag::Resize: {
                const int end = juce::jlimit(n.start + 1, len, xToTick(e.x, true));
                n.length = std::max(snap(1) > 1 ? snap(1) : 1, end - n.start);
                n.length = std::min(n.length, len - n.start);
                lastLength_ = n.length;
                break;
            }
            case Drag::Velocity:
                n.velocity = juce::jlimit(1, 127, original_.velocity - e.getDistanceFromDragStartY());
                break;
            case Drag::None: break;
        }

        if (n.pitch != original_.pitch && dragMode_ == Drag::Move && n.pitch != lastAudition_) audition(n.pitch);
        clip->setNote(dragIndex_, n);
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override {
        if (auditionPitch_ >= 0) { app_.previewNoteOff(auditionPitch_); auditionPitch_ = -1; }
        if (lastAudition_ >= 0) { app_.previewNoteOff(lastAudition_); lastAudition_ = -1; }
        if (auto* clip = app_.selectedClip(); clip != nullptr && dragIndex_ >= 0 && dragMode_ == Drag::Move) {
            fdm::NoteEvent n;
            if (clip->getNote(dragIndex_, n)) lastLength_ = n.length;
        }
        dragIndex_ = -1;
        dragMode_ = Drag::None;
    }

    void setVelocity(int v) { velocity_ = v; }

private:
    enum class Drag { None, Move, Resize, Velocity };

    int snap(int) const { return std::max(1, snap_); }
    float ticksToPixels() const { return stepW_ / fdm::kTicksPerStep; }
    int tickToX(int t) const { return kKeyW + static_cast<int>(std::lround(t * ticksToPixels())); }

    int xToTick(int x, bool snapIt, int grab = 0) const {
        int t = static_cast<int>(std::lround((x - kKeyW) / ticksToPixels())) - grab;
        if (snapIt) t = static_cast<int>(std::floor(static_cast<double>(t) / snap_ + 0.5)) * snap_;
        return std::max(0, t);
    }

    juce::Rectangle<int> noteRect(const fdm::NoteEvent& e) const {
        const int row = rowForPitch(e.pitch);
        const int x = tickToX(e.start);
        return {x, row * rowHeight(), std::max(4, tickToX(e.start + e.length) - x), rowHeight()};
    }

    int noteAt(fdm::Clip& clip, int x, int y) const {
        fdm::NoteEvent e;
        for (int i = fdm::kMaxNotesPerClip - 1; i >= 0; --i)
            if (clip.getNote(i, e) && rowForPitch(e.pitch) >= 0 && noteRect(e).contains(x, y)) return i;
        return -1;
    }

    void audition(int pitch) {
        if (lastAudition_ >= 0) app_.previewNoteOff(lastAudition_);
        lastAudition_ = pitch;
        app_.previewNoteOn(pitch, 100);
    }

    void timerCallback() override {
        int pos = -1;
        if (auto* ch = app_.selectedChannel())
            if (ch->playingSlot.load() == app_.selection().scene && app_.engine.isPlaying()) pos = ch->playPosTicks.load();
        if (pos != playhead_) { playhead_ = pos; repaint(); }
    }

    AppState& app_;
    float stepW_ = 34.0f;
    int snap_ = fdm::kTicksPerStep;
    int velocity_ = 100;
    int lastLength_ = fdm::kTicksPerStep;
    int playhead_ = -1;
    int dragIndex_ = -1, grab_ = 0, auditionPitch_ = -1, lastAudition_ = -1;
    Drag dragMode_ = Drag::None;
    fdm::NoteEvent original_;
};

/** The "Clip" tab: a toolbar plus the scrollable note grid. */
class ClipEditor : public juce::Component, private AppState::Listener {
public:
    explicit ClipEditor(AppState& app) : app_(app), grid_(app) {
        app_.addListener(this);

        viewport_.setViewedComponent(&grid_, false);
        viewport_.setScrollBarsShown(true, true);
        addAndMakeVisible(viewport_);

        title_.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
        addAndMakeVisible(title_);

        auto label = [this](juce::Label& l, const char* text) {
            l.setText(text, juce::dontSendNotification);
            l.setColour(juce::Label::textColourId, theme::textDim);
            l.setFont(juce::Font(juce::FontOptions(12.0f)));
            addAndMakeVisible(l);
        };
        label(lengthLabel_, "Length");
        label(snapLabel_, "Snap");
        label(velLabel_, "Velocity");

        for (int bars : {1, 2, 3, 4, 6, 8, 12, 16}) lengthBox_.addItem(juce::String(bars) + (bars == 1 ? " bar" : " bars"), bars);
        lengthBox_.onChange = [this] {
            if (auto* clip = app_.selectedClip()) { clip->setLengthTicks(lengthBox_.getSelectedId() * fdm::kTicksPerBar); grid_.updateSize(); }
        };
        addAndMakeVisible(lengthBox_);

        snapBox_.addItem("1/32", 1);
        snapBox_.addItem("1/16", 2);
        snapBox_.addItem("1/8", 3);
        snapBox_.addItem("1/4", 4);
        snapBox_.setSelectedId(2, juce::dontSendNotification);
        snapBox_.onChange = [this] {
            static const int ticks[] = {120, 240, 480, 960};
            grid_.setSnap(ticks[snapBox_.getSelectedId() - 1]);
        };
        addAndMakeVisible(snapBox_);

        velocity_.setRange(1, 127, 1);
        velocity_.setValue(100, juce::dontSendNotification);
        velocity_.setSliderStyle(juce::Slider::LinearHorizontal);
        velocity_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 34, 20);
        velocity_.onValueChange = [this] { grid_.setVelocity(static_cast<int>(velocity_.getValue())); };
        addAndMakeVisible(velocity_);

        clearButton_.setButtonText("Clear notes");
        clearButton_.onClick = [this] { if (auto* c = app_.selectedClip()) { c->clearNotes(); grid_.repaint(); } };
        addAndMakeVisible(clearButton_);

        zoom_.setRange(10.0, 56.0, 1.0);
        zoom_.setValue(34.0, juce::dontSendNotification);
        zoom_.setSliderStyle(juce::Slider::LinearHorizontal);
        zoom_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        zoom_.onValueChange = [this] { grid_.setStepWidth(static_cast<float>(zoom_.getValue())); };
        addAndMakeVisible(zoom_);
        label(zoomLabel_, "Zoom");

        refresh();
    }

    ~ClipEditor() override { app_.removeListener(this); }

    void paint(juce::Graphics& g) override { g.fillAll(theme::panel); }

    void resized() override {
        auto r = getLocalBounds().reduced(8, 6);
        auto bar = r.removeFromTop(26);
        title_.setBounds(bar.removeFromLeft(190));
        lengthLabel_.setBounds(bar.removeFromLeft(48));
        lengthBox_.setBounds(bar.removeFromLeft(86));
        bar.removeFromLeft(12);
        snapLabel_.setBounds(bar.removeFromLeft(38));
        snapBox_.setBounds(bar.removeFromLeft(70));
        bar.removeFromLeft(12);
        velLabel_.setBounds(bar.removeFromLeft(56));
        velocity_.setBounds(bar.removeFromLeft(140));
        bar.removeFromLeft(12);
        clearButton_.setBounds(bar.removeFromLeft(90));
        bar.removeFromLeft(12);
        zoomLabel_.setBounds(bar.removeFromLeft(40));
        zoom_.setBounds(bar.removeFromLeft(110));
        r.removeFromTop(6);
        viewport_.setBounds(r);
    }

private:
    void selectionChanged() override { refresh(); }
    void structureChanged() override { refresh(); }

    void refresh() {
        auto* ch = app_.selectedChannel();
        auto* clip = app_.selectedClip();
        const bool has = clip != nullptr;
        title_.setText(ch == nullptr ? "Select a channel"
                                     : juce::String(ch->name) + "  -  " + (has ? juce::String(clip->name) : "Scene " + juce::String(app_.selection().scene + 1) + " (empty)"),
                       juce::dontSendNotification);
        for (juce::Component* c : std::initializer_list<juce::Component*>{&lengthBox_, &snapBox_, &velocity_, &clearButton_})
            c->setEnabled(has);
        if (has) lengthBox_.setSelectedId(clip->lengthTicks.load() / fdm::kTicksPerBar, juce::dontSendNotification);

        const bool newClip = clip != lastClip_ || ch != lastChannel_;
        lastClip_ = clip;
        lastChannel_ = ch;
        grid_.updateSize();
        if (newClip) viewport_.setViewPosition(0, grid_.suggestedScrollY());
        grid_.repaint();
    }

    AppState& app_;
    NoteGrid grid_;
    juce::Viewport viewport_;
    juce::Label title_, lengthLabel_, snapLabel_, velLabel_, zoomLabel_;
    juce::ComboBox lengthBox_, snapBox_;
    juce::Slider velocity_, zoom_;
    juce::TextButton clearButton_;
    fdm::Clip* lastClip_ = nullptr;
    fdm::Channel* lastChannel_ = nullptr;
};
