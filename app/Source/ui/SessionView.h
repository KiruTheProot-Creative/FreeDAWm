#pragma once

#include "../AppState.h"
#include "../Theme.h"
#include "../Widgets.h"

// ---------------------------------------------------------------------------
// One clip slot in a channel column
// ---------------------------------------------------------------------------
class ClipSlotButton : public juce::Component {
public:
    ClipSlotButton(AppState& app, int channel, int slot) : app_(app), channel_(channel), slot_(slot) {}

    std::function<void(int tab)> requestTab;

    /** Called from the column's timer; repaints only when something visible changed. */
    void update() {
        auto& ch = app_.engine.channel(channel_);
        const int h = ch.playingSlot.load() * 1000 + ch.queuedSlot.load() * 31 + (ch.clips[static_cast<size_t>(slot_)].exists.load() ? 7 : 0)
                      + (ch.playingSlot.load() == slot_ ? ch.playPosTicks.load() / 60 * 131 : 0)
                      + (isSelected() ? 99991 : 0) + (blink() ? 17 : 0);
        if (h != lastHash_) { lastHash_ = h; repaint(); }
    }

    void paint(juce::Graphics& g) override {
        auto& ch = app_.engine.channel(channel_);
        auto& clip = ch.clips[static_cast<size_t>(slot_)];
        const auto colour = theme::channelColour(ch.colorIndex.load());
        auto r = getLocalBounds().toFloat().reduced(2.0f, 1.5f);
        const bool playing = ch.playingSlot.load() == slot_;
        const bool queued = ch.queuedSlot.load() == slot_;

        if (clip.exists.load()) {
            g.setColour(colour.withMultipliedBrightness(playing ? 1.0f : 0.62f));
            g.fillRoundedRectangle(r, 3.0f);

            if (playing) {
                const float frac = static_cast<float>(ch.playPosTicks.load()) / static_cast<float>(std::max(1, clip.lengthTicks.load()));
                g.setColour(juce::Colours::white.withAlpha(0.28f));
                g.fillRoundedRectangle(r.withWidth(r.getWidth() * juce::jlimit(0.0f, 1.0f, frac)), 3.0f);
            }

            // launch triangle
            auto tri = r.removeFromLeft(24.0f).reduced(7.0f, 6.0f);
            juce::Path p;
            p.addTriangle(tri.getX(), tri.getY(), tri.getX(), tri.getBottom(), tri.getRight(), tri.getCentreY());
            g.setColour(theme::background.withAlpha(queued && blink() ? 0.2f : 0.85f));
            g.fillPath(p);

            g.setColour(theme::background.withAlpha(0.9f));
            g.setFont(juce::Font(juce::FontOptions(11.5f)));
            g.drawText(clip.name, r.reduced(2.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, true);
        } else {
            g.setColour(theme::panelRaised.withAlpha(0.55f));
            g.fillRoundedRectangle(r, 3.0f);
            if (queued) {                                            // a stop is queued (not used for empty slots, kept for safety)
            }
            auto sq = r.removeFromLeft(24.0f).reduced(9.0f, 9.0f);
            g.setColour(theme::border);
            g.fillRoundedRectangle(sq, 1.0f);
        }

        if (isSelected()) {
            g.setColour(theme::text);
            g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.5f, 1.0f), 3.0f, 1.5f);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        auto& ch = app_.engine.channel(channel_);
        auto& clip = ch.clips[static_cast<size_t>(slot_)];

        if (e.mods.isPopupMenu()) { showMenu(); return; }
        app_.selectChannel(channel_, slot_);
        if (requestTab) requestTab(clip.exists.load() ? 0 : 1);

        if (clip.exists.load() && e.x < 26) app_.engine.launchClip(channel_, slot_);
        else if (!clip.exists.load() && e.x < 26) { /* empty slot: select only */ }
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override {
        auto& clip = app_.engine.channel(channel_).clips[static_cast<size_t>(slot_)];
        if (e.mods.isPopupMenu()) return;
        if (!clip.exists.load()) createClip(1);
        else if (e.x >= 26) app_.engine.launchClip(channel_, slot_);
    }

private:
    bool isSelected() const {
        const auto& s = app_.selection();
        return s.kind == AppState::Selection::Kind::Channel && s.index == channel_ && s.scene == slot_;
    }
    static bool blink() { return (juce::Time::getMillisecondCounter() / 220) % 2 == 0; }

    void createClip(int bars) {
        auto& ch = app_.engine.channel(channel_);
        auto& clip = ch.clips[static_cast<size_t>(slot_)];
        clip.create(bars);
        std::snprintf(clip.name, sizeof(clip.name), "Clip %d", slot_ + 1);
        app_.selectChannel(channel_, slot_);
        app_.structureHasChanged();
        if (requestTab) requestTab(0);
        repaint();
    }

    void showMenu() {
        auto& ch = app_.engine.channel(channel_);
        auto& clip = ch.clips[static_cast<size_t>(slot_)];
        const bool exists = clip.exists.load();
        juce::PopupMenu m;
        if (!exists) {
            m.addItem(1, "Create 1-bar clip");
            m.addItem(2, "Create 2-bar clip");
            m.addItem(4, "Create 4-bar clip");
        } else {
            m.addItem(10, "Launch");
            m.addItem(11, "Stop channel");
            m.addSeparator();
            if (slot_ + 1 < fdm::kMaxScenes) m.addItem(12, "Duplicate to next scene", !ch.clips[static_cast<size_t>(slot_ + 1)].exists.load());
            m.addItem(13, "Clear notes");
            m.addItem(14, "Delete clip");
        }
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this, safe = juce::Component::SafePointer<ClipSlotButton>(this)](int id) {
            if (safe == nullptr || id == 0) return;
            auto& ch = app_.engine.channel(channel_);
            auto& clip = ch.clips[static_cast<size_t>(slot_)];
            if (id == 1 || id == 2 || id == 4) createClip(id);
            else if (id == 10) app_.engine.launchClip(channel_, slot_);
            else if (id == 11) app_.engine.stopClip(channel_);
            else if (id == 12) {
                auto& dst = ch.clips[static_cast<size_t>(slot_ + 1)];
                dst.create(clip.lengthTicks.load() / fdm::kTicksPerBar);
                dst.setLengthTicks(clip.lengthTicks.load());
                std::snprintf(dst.name, sizeof(dst.name), "%s", clip.name);
                fdm::NoteEvent e;
                for (int i = 0; i < fdm::kMaxNotesPerClip; ++i)
                    if (clip.getNote(i, e)) dst.addNote(e);
                app_.structureHasChanged();
            } else if (id == 13) { clip.clearNotes(); app_.structureHasChanged(); }
            else if (id == 14) {
                if (ch.playingSlot.load() == slot_) app_.engine.stopClip(channel_);
                clip.clear();
                app_.structureHasChanged();
            }
            repaint();
        });
    }

    AppState& app_;
    int channel_, slot_;
    int lastHash_ = -1;
};

// ---------------------------------------------------------------------------
// A mixer strip: pan, sends, fader, mute / solo. Works for channels, returns and master.
// ---------------------------------------------------------------------------
class MixerStrip : public juce::Component {
public:
    MixerStrip(fdm::Strip& strip, fdm::Channel* channel, juce::Colour colour)
        : strip_(strip), channel_(channel), fader_(strip), colour_(colour) {
        pan_.setSliderStyle(juce::Slider::LinearHorizontal);
        pan_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        pan_.setRange(-1.0, 1.0, 0.01);
        pan_.setValue(strip.pan.load(), juce::dontSendNotification);
        pan_.setDoubleClickReturnValue(true, 0.0);
        pan_.setColour(juce::Slider::trackColourId, colour);
        pan_.setPopupDisplayEnabled(true, true, nullptr);
        pan_.textFromValueFunction = [](double v) {
            if (std::abs(v) < 0.01) return juce::String("C");
            return juce::String(juce::roundToInt(std::abs(v) * 50.0)) + (v < 0 ? " L" : " R");
        };
        pan_.onValueChange = [this] { strip_.pan.store(static_cast<float>(pan_.getValue())); };
        addAndMakeVisible(pan_);

        if (channel_ != nullptr) {
            for (int i = 0; i < fdm::kNumReturns; ++i) {
                auto& s = sends_[static_cast<size_t>(i)];
                s.setSliderStyle(juce::Slider::LinearHorizontal);
                s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
                s.setRange(0.0, 1.0, 0.01);
                s.setValue(channel_->sends[static_cast<size_t>(i)].load(), juce::dontSendNotification);
                s.setDoubleClickReturnValue(true, 0.0);
                s.setColour(juce::Slider::trackColourId, theme::accent);
                s.setPopupDisplayEnabled(true, true, nullptr);
                s.onValueChange = [this, i] { channel_->sends[static_cast<size_t>(i)].store(static_cast<float>(sends_[static_cast<size_t>(i)].getValue())); };
                addAndMakeVisible(s);
            }
        }

        addAndMakeVisible(fader_);
        fader_.onChange = [this] { updateDb(); };

        dbLabel_.setJustificationType(juce::Justification::centred);
        dbLabel_.setFont(juce::Font(juce::FontOptions(11.0f)));
        dbLabel_.setColour(juce::Label::textColourId, theme::textDim);
        addAndMakeVisible(dbLabel_);
        updateDb();

        auto setup = [this](juce::TextButton& b, const char* text, juce::Colour on) {
            b.setButtonText(text);
            b.setClickingTogglesState(true);
            b.setColour(juce::TextButton::buttonOnColourId, on);
            b.setColour(juce::TextButton::textColourOnId, theme::background);
            addAndMakeVisible(b);
        };
        setup(mute_, "M", theme::accent);
        mute_.setToggleState(strip.mute.load(), juce::dontSendNotification);
        mute_.onClick = [this] { strip_.mute.store(mute_.getToggleState()); };
        if (channel_ != nullptr) {
            setup(solo_, "S", theme::play);
            solo_.setToggleState(channel_->solo.load(), juce::dontSendNotification);
            solo_.onClick = [this] { channel_->solo.store(solo_.getToggleState()); };
        }
    }

    void paint(juce::Graphics& g) override {
        g.setColour(theme::textDim);
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText("PAN", panLabelBounds_, juce::Justification::centredLeft);
        if (channel_ != nullptr) {
            g.drawText("A", sendLabelBounds_[0], juce::Justification::centredLeft);
            g.drawText("B", sendLabelBounds_[1], juce::Justification::centredLeft);
        }
    }

    void resized() override {
        auto r = getLocalBounds().reduced(6, 4);
        auto row = r.removeFromTop(16);
        panLabelBounds_ = row.removeFromLeft(26);
        pan_.setBounds(row);
        if (channel_ != nullptr) {
            for (int i = 0; i < fdm::kNumReturns; ++i) {
                auto sr = r.removeFromTop(16);
                sendLabelBounds_[static_cast<size_t>(i)] = sr.removeFromLeft(26);
                sends_[static_cast<size_t>(i)].setBounds(sr);
            }
        }
        r.removeFromTop(4);
        auto buttons = r.removeFromBottom(24);
        if (channel_ != nullptr) {
            mute_.setBounds(buttons.removeFromLeft(buttons.getWidth() / 2).reduced(2, 0));
            solo_.setBounds(buttons.reduced(2, 0));
        } else {
            mute_.setBounds(buttons.reduced(10, 0));
        }
        dbLabel_.setBounds(r.removeFromBottom(16));
        fader_.setBounds(r);
    }

private:
    void updateDb() { dbLabel_.setText(dbText(strip_.volumeDb.load()) + " dB", juce::dontSendNotification); }

    fdm::Strip& strip_;
    fdm::Channel* channel_;
    Fader fader_;
    juce::Colour colour_;
    juce::Slider pan_;
    std::array<juce::Slider, fdm::kNumReturns> sends_;
    juce::TextButton mute_, solo_;
    juce::Label dbLabel_;
    juce::Rectangle<int> panLabelBounds_, sendLabelBounds_[2];
};

// ---------------------------------------------------------------------------
// A full channel column: header, 8 clip slots, stop button, mixer strip
// ---------------------------------------------------------------------------
class ChannelColumn : public juce::Component, private juce::Timer, private AppState::Listener {
public:
    ChannelColumn(AppState& app, int index, std::function<void(int)> requestTab)
        : app_(app), index_(index), mixer_(app.engine.channel(index), &app.engine.channel(index),
                                           theme::channelColour(app.engine.channel(index).colorIndex.load())) {
        for (int s = 0; s < fdm::kMaxScenes; ++s) {
            slots_.push_back(std::make_unique<ClipSlotButton>(app_, index_, s));
            slots_.back()->requestTab = requestTab;
            addAndMakeVisible(*slots_.back());
        }
        stop_ = std::make_unique<IconButton>(IconButton::Icon::Stop, theme::warn);
        stop_->onClick = [this] { app_.engine.stopClip(index_); };
        addAndMakeVisible(*stop_);
        addAndMakeVisible(mixer_);

        name_.setText(app_.engine.channel(index_).name, juce::dontSendNotification);
        name_.setEditable(false, true, false);
        name_.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        name_.setColour(juce::Label::textColourId, theme::background);
        name_.setColour(juce::Label::textWhenEditingColourId, theme::text);
        name_.setColour(juce::Label::backgroundWhenEditingColourId, theme::background);
        name_.onTextChange = [this] { app_.engine.channel(index_).setName(name_.getText().toRawUTF8()); };
        name_.addMouseListener(this, false);
        addAndMakeVisible(name_);

        requestTab_ = std::move(requestTab);
        app_.addListener(this);
        startTimerHz(30);
    }

    ~ChannelColumn() override { app_.removeListener(this); }

    void paint(juce::Graphics& g) override {
        g.setColour(theme::panel);
        g.fillRect(getLocalBounds());
        const auto colour = theme::channelColour(app_.engine.channel(index_).colorIndex.load());
        auto header = getLocalBounds().removeFromTop(theme::kHeaderHeight).reduced(2, 2);
        g.setColour(colour);
        g.fillRoundedRectangle(header.toFloat(), 4.0f);

        const auto& s = app_.selection();
        if (s.kind == AppState::Selection::Kind::Channel && s.index == index_) {
            g.setColour(theme::text);
            g.drawRoundedRectangle(header.toFloat(), 4.0f, 1.5f);
        }
        g.setColour(theme::border);
        g.drawVerticalLine(getWidth() - 1, 0.0f, static_cast<float>(getHeight()));
    }

    void resized() override {
        auto r = getLocalBounds();
        auto header = r.removeFromTop(theme::kHeaderHeight).reduced(4, 2);
        name_.setBounds(header);
        for (auto& s : slots_) s->setBounds(r.removeFromTop(theme::kSlotHeight));
        stop_->setBounds(r.removeFromTop(theme::kStopHeight).reduced(24, 2));
        mixer_.setBounds(r);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.eventComponent != this && e.eventComponent != &name_) return;
        if (e.mods.isPopupMenu()) { showMenu(); return; }
        app_.selectChannel(index_, app_.selection().kind == AppState::Selection::Kind::Channel ? app_.selection().scene : 0);
        if (requestTab_) requestTab_(1);
    }

private:
    void selectionChanged() override { repaint(); }
    void structureChanged() override {}

    void timerCallback() override {
        for (auto& s : slots_) s->update();
    }

    void showMenu() {
        juce::PopupMenu m;
        m.addItem(1, "Rename");
        m.addItem(2, "Delete channel", app_.engine.activeChannelCount() > 1);
        m.addSeparator();
        for (int i = 0; i < 8; ++i) {
            juce::PopupMenu::Item item("Colour " + juce::String(i + 1));
            item.itemID = 100 + i;
            item.colour = theme::channelColour(i);
            m.addItem(item);
        }
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                        [this, safe = juce::Component::SafePointer<ChannelColumn>(this)](int id) {
                            if (safe == nullptr || id == 0) return;
                            if (id == 1) name_.showEditor();
                            else if (id == 2) {
                                const int idx = index_;
                                juce::MessageManager::callAsync([&app = app_, idx] { app.removeChannel(idx); });
                            } else if (id >= 100) {
                                app_.engine.channel(index_).colorIndex.store(id - 100);
                                app_.structureHasChanged();
                            }
                        });
    }

    AppState& app_;
    int index_;
    std::vector<std::unique_ptr<ClipSlotButton>> slots_;
    std::unique_ptr<IconButton> stop_;
    MixerStrip mixer_;
    juce::Label name_;
    std::function<void(int)> requestTab_;
};

// ---------------------------------------------------------------------------
// Scene launch column
// ---------------------------------------------------------------------------
class SceneColumn : public juce::Component {
public:
    explicit SceneColumn(AppState& app) : app_(app) {
        for (int s = 0; s < fdm::kMaxScenes; ++s) {
            auto b = std::make_unique<IconButton>(IconButton::Icon::Play, theme::play);
            b->onClick = [this, s] { app_.engine.launchScene(s); };
            addAndMakeVisible(*b);
            buttons_.push_back(std::move(b));
        }
        stopAll_ = std::make_unique<IconButton>(IconButton::Icon::Stop, theme::warn);
        stopAll_->onClick = [this] { app_.engine.stopAllClips(); };
        addAndMakeVisible(*stopAll_);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(theme::background.brighter(0.04f));
        g.setColour(theme::textDim);
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText("SCENES", getLocalBounds().removeFromTop(theme::kHeaderHeight), juce::Justification::centred);
        g.setColour(theme::border);
        g.drawVerticalLine(getWidth() - 1, 0.0f, static_cast<float>(getHeight()));
    }

    void resized() override {
        auto r = getLocalBounds();
        r.removeFromTop(theme::kHeaderHeight);
        for (auto& b : buttons_) b->setBounds(r.removeFromTop(theme::kSlotHeight).reduced(14, 2));
        stopAll_->setBounds(r.removeFromTop(theme::kStopHeight).reduced(14, 2));
    }

private:
    AppState& app_;
    std::vector<std::unique_ptr<IconButton>> buttons_;
    std::unique_ptr<IconButton> stopAll_;
};

// ---------------------------------------------------------------------------
// Return / master column (mixer strip only, selectable to edit its FX)
// ---------------------------------------------------------------------------
class BusColumn : public juce::Component, private AppState::Listener {
public:
    BusColumn(AppState& app, AppState::Selection::Kind kind, int index, juce::Colour colour,
              std::function<void(int)> requestTab)
        : app_(app), kind_(kind), index_(index), colour_(colour), requestTab_(std::move(requestTab)),
          mixer_(kind == AppState::Selection::Kind::Master ? app.engine.master() : app.engine.returnStrip(index), nullptr, colour) {
        addAndMakeVisible(mixer_);
        app_.addListener(this);
    }
    ~BusColumn() override { app_.removeListener(this); }

    void paint(juce::Graphics& g) override {
        g.fillAll(theme::panel);
        auto header = getLocalBounds().removeFromTop(theme::kHeaderHeight).reduced(2, 2);
        g.setColour(colour_);
        g.fillRoundedRectangle(header.toFloat(), 4.0f);
        const auto& s = app_.selection();
        if (s.kind == kind_ && s.index == index_) {
            g.setColour(theme::text);
            g.drawRoundedRectangle(header.toFloat(), 4.0f, 1.5f);
        }
        g.setColour(theme::background);
        g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        g.drawText(kind_ == AppState::Selection::Kind::Master ? juce::String("Master")
                                                              : juce::String(app_.engine.returnStrip(index_).name),
                   header, juce::Justification::centred);
        g.setColour(theme::border);
        g.drawVerticalLine(getWidth() - 1, 0.0f, static_cast<float>(getHeight()));
    }

    void resized() override {
        auto r = getLocalBounds();
        r.removeFromTop(theme::kHeaderHeight);
        r.removeFromTop(fdm::kMaxScenes * theme::kSlotHeight + theme::kStopHeight - ((kind_ == AppState::Selection::Kind::Master) ? 0 : 0));
        mixer_.setBounds(r);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.y > theme::kHeaderHeight) return;
        app_.select({kind_, index_, 0});
        if (requestTab_) requestTab_(1);
    }

private:
    void selectionChanged() override { repaint(); }

    AppState& app_;
    AppState::Selection::Kind kind_;
    int index_;
    juce::Colour colour_;
    std::function<void(int)> requestTab_;
    MixerStrip mixer_;
};

// ---------------------------------------------------------------------------
// The whole session: scrolling channel columns on the left, fixed scene / return / master on the right
// ---------------------------------------------------------------------------
class SessionView : public juce::Component, private AppState::Listener, private juce::AsyncUpdater {
public:
    explicit SessionView(AppState& app) : app_(app), scenes_(app) {
        content_ = std::make_unique<Content>();
        viewport_.setViewedComponent(content_.get(), false);
        viewport_.setScrollBarsShown(false, true);
        addAndMakeVisible(viewport_);
        addAndMakeVisible(scenes_);

        for (int r = 0; r < fdm::kNumReturns; ++r) {
            app_.engine.returnStrip(r).setName(r == 0 ? "Return A" : "Return B");
            returns_.push_back(std::make_unique<BusColumn>(app_, AppState::Selection::Kind::Return, r,
                                                           theme::channelColour(r == 0 ? 5 : 6), [this](int t) { if (requestTab) requestTab(t); }));
            addAndMakeVisible(*returns_.back());
        }
        master_ = std::make_unique<BusColumn>(app_, AppState::Selection::Kind::Master, 0, theme::text.withAlpha(0.9f),
                                              [this](int t) { if (requestTab) requestTab(t); });
        addAndMakeVisible(*master_);

        addButton_ = std::make_unique<IconButton>(IconButton::Icon::Plus);
        addButton_->onClick = [this] { showAddMenu(); };
        content_->addAndMakeVisible(*addButton_);

        app_.addListener(this);
        rebuild();
    }

    ~SessionView() override { app_.removeListener(this); cancelPendingUpdate(); }

    std::function<void(int)> requestTab;

    void paint(juce::Graphics& g) override { g.fillAll(theme::background); }

    void resized() override {
        auto r = getLocalBounds();
        master_->setBounds(r.removeFromRight(theme::kMasterWidth));
        for (int i = fdm::kNumReturns - 1; i >= 0; --i) returns_[static_cast<size_t>(i)]->setBounds(r.removeFromRight(theme::kReturnWidth));
        scenes_.setBounds(r.removeFromRight(theme::kSceneWidth));
        viewport_.setBounds(r);
        layoutContent();
    }

private:
    struct Content : juce::Component {};

    void selectionChanged() override {}
    void structureChanged() override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override { rebuild(); }

    void rebuild() {
        std::vector<int> active;
        for (int i = 0; i < fdm::kMaxChannels; ++i)
            if (app_.engine.channel(i).active.load()) active.push_back(i);

        // only rebuild the columns when the set of channels (or their colours) changed
        std::vector<int> signature = active;
        for (int i : active) signature.push_back(app_.engine.channel(i).colorIndex.load() + 1000);
        if (signature == lastSignature_) { repaint(); return; }
        lastSignature_ = signature;

        columns_.clear();
        for (int i : active) {
            columns_.push_back(std::make_unique<ChannelColumn>(app_, i, [this](int t) { if (requestTab) requestTab(t); }));
            content_->addAndMakeVisible(*columns_.back());
        }
        layoutContent();
    }

    void layoutContent() {
        const int height = std::max(300, viewport_.getMaximumVisibleHeight());
        int x = 0;
        for (auto& c : columns_) { c->setBounds(x, 0, theme::kChannelWidth, height); x += theme::kChannelWidth; }
        addButton_->setBounds(x + 12, 6, 32, 28);
        addButton_->setVisible(app_.engine.activeChannelCount() < fdm::kMaxChannels);
        content_->setSize(std::max(x + 60, viewport_.getMaximumVisibleWidth()), height);
    }

    void showAddMenu() {
        juce::PopupMenu m;
        m.addItem(1, "Drum Rack channel");
        m.addItem(2, "Synth channel");
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(addButton_.get()), [this](int id) {
            if (id == 1) app_.addChannel(fdm::InstrumentType::DrumRack);
            else if (id == 2) app_.addChannel(fdm::InstrumentType::PolySynth);
        });
    }

    AppState& app_;
    juce::Viewport viewport_;
    std::unique_ptr<Content> content_;
    std::vector<std::unique_ptr<ChannelColumn>> columns_;
    std::unique_ptr<IconButton> addButton_;
    SceneColumn scenes_;
    std::vector<std::unique_ptr<BusColumn>> returns_;
    std::unique_ptr<BusColumn> master_;
    std::vector<int> lastSignature_;
};
