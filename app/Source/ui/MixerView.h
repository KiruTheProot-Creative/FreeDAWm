#pragma once

#include "../AppState.h"
#include "../Theme.h"
#include "../Widgets.h"

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
// One column of the mixer: header (click to select) + strip
// ---------------------------------------------------------------------------
class MixerColumn : public juce::Component, private AppState::Listener {
public:
    MixerColumn(AppState& app, AppState::Selection::Kind kind, int index, std::function<void(int)> requestTab)
        : app_(app), kind_(kind), index_(index), requestTab_(std::move(requestTab)),
          strip_(stripRef(app, kind, index), kind == AppState::Selection::Kind::Channel ? &app.engine.channel(index) : nullptr, colour()) {
        addAndMakeVisible(strip_);
        app_.addListener(this);
    }
    ~MixerColumn() override { app_.removeListener(this); }

    void paint(juce::Graphics& g) override {
        g.fillAll(kind_ == AppState::Selection::Kind::Channel ? theme::panel : theme::panel.darker(0.25f));
        auto header = getLocalBounds().removeFromTop(34).reduced(3, 3);
        g.setGradientFill(juce::ColourGradient(colour().brighter(0.1f), 0.0f, static_cast<float>(header.getY()), colour().darker(0.45f), 0.0f,
                                               static_cast<float>(header.getBottom()), false));
        g.fillRoundedRectangle(header.toFloat(), 4.0f);
        const auto& s = app_.selection();
        if (s.kind == kind_ && s.index == index_) {
            g.setColour(theme::text);
            g.drawRoundedRectangle(header.toFloat(), 4.0f, 1.5f);
        }
        g.setColour(juce::Colours::black.withAlpha(0.85f));
        g.setFont(theme::labelFont(11.5f));
        g.drawText(juce::String(stripRef(app_, kind_, index_).name).toUpperCase(), header.reduced(6, 0), juce::Justification::centred, true);
        g.setColour(juce::Colours::black);
        g.drawVerticalLine(getWidth() - 1, 0.0f, static_cast<float>(getHeight()));
    }

    void resized() override { strip_.setBounds(getLocalBounds().withTrimmedTop(36)); }

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.y > 36) return;
        if (kind_ == AppState::Selection::Kind::Channel) app_.selectChannel(index_, app_.selection().scene);
        else app_.select({kind_, index_, 0});
        if (requestTab_) requestTab_(1);
    }

private:
    static fdm::Strip& stripRef(AppState& app, AppState::Selection::Kind kind, int index) {
        switch (kind) {
            case AppState::Selection::Kind::Channel: return app.engine.channel(index);
            case AppState::Selection::Kind::Return:  return app.engine.returnStrip(index);
            case AppState::Selection::Kind::Master:  break;
        }
        return app.engine.master();
    }
    juce::Colour colour() const {
        switch (kind_) {
            case AppState::Selection::Kind::Channel: return theme::channelColour(app_.engine.channel(index_).colorIndex.load());
            case AppState::Selection::Kind::Return:  return theme::channelColour(index_ == 0 ? 5 : 6);
            case AppState::Selection::Kind::Master:  break;
        }
        return juce::Colour(0xffd0d0d6);
    }
    void selectionChanged() override { repaint(); }

    AppState& app_;
    AppState::Selection::Kind kind_;
    int index_;
    std::function<void(int)> requestTab_;
    MixerStrip strip_;
};

// ---------------------------------------------------------------------------
// The Mixer tab: every track, then the returns and the master on the right
// ---------------------------------------------------------------------------
class MixerView : public juce::Component, private AppState::Listener, private juce::AsyncUpdater {
public:
    explicit MixerView(AppState& app) : app_(app) {
        viewport_.setViewedComponent(&content_, false);
        viewport_.setScrollBarsShown(false, true);
        addAndMakeVisible(viewport_);
        for (int r = 0; r < fdm::kNumReturns; ++r) {
            app_.engine.returnStrip(r).setName(r == 0 ? "Return A" : "Return B");
            buses_.push_back(std::make_unique<MixerColumn>(app_, AppState::Selection::Kind::Return, r, [this](int t) { if (requestTab) requestTab(t); }));
            addAndMakeVisible(*buses_.back());
        }
        app_.engine.master().setName("Master");
        buses_.push_back(std::make_unique<MixerColumn>(app_, AppState::Selection::Kind::Master, 0, [this](int t) { if (requestTab) requestTab(t); }));
        addAndMakeVisible(*buses_.back());
        app_.addListener(this);
        rebuild();
    }
    ~MixerView() override { app_.removeListener(this); cancelPendingUpdate(); }

    std::function<void(int)> requestTab;

    void paint(juce::Graphics& g) override { g.fillAll(theme::background); }

    void resized() override {
        auto r = getLocalBounds();
        for (int i = static_cast<int>(buses_.size()) - 1; i >= 0; --i) buses_[static_cast<size_t>(i)]->setBounds(r.removeFromRight(kColumnW));
        r.removeFromRight(6);
        viewport_.setBounds(r);
        layout();
    }

private:
    static constexpr int kColumnW = 104;
    struct Content : juce::Component {};

    void structureChanged() override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override { rebuild(); }

    void rebuild() {
        std::vector<int> sig;
        for (int i = 0; i < fdm::kMaxChannels; ++i)
            if (app_.engine.channel(i).active.load()) { sig.push_back(i); sig.push_back(1000 + app_.engine.channel(i).colorIndex.load()); }
        if (sig == signature_) { repaint(); return; }
        signature_ = sig;
        columns_.clear();
        for (int i = 0; i < fdm::kMaxChannels; ++i) {
            if (!app_.engine.channel(i).active.load()) continue;
            columns_.push_back(std::make_unique<MixerColumn>(app_, AppState::Selection::Kind::Channel, i, [this](int t) { if (requestTab) requestTab(t); }));
            content_.addAndMakeVisible(*columns_.back());
        }
        layout();
    }

    void layout() {
        const int h = std::max(200, viewport_.getMaximumVisibleHeight());
        int x = 0;
        for (auto& c : columns_) { c->setBounds(x, 0, kColumnW, h); x += kColumnW; }
        content_.setSize(std::max(x, viewport_.getMaximumVisibleWidth()), h);
    }

    AppState& app_;
    juce::Viewport viewport_;
    Content content_;
    std::vector<std::unique_ptr<MixerColumn>> columns_;
    std::vector<std::unique_ptr<MixerColumn>> buses_;
    std::vector<int> signature_;
};
