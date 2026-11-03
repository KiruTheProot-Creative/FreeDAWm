#pragma once

#include "ArrangementView.h"
#include "ClipEditor.h"
#include "Explorer.h"
#include "DevicePanel.h"
#include "SessionView.h"
#include "TransportBar.h"

/** The session grid needs about 540 px; scroll when the bottom panel is shorter. */
class SessionHolder : public juce::Component {
public:
    explicit SessionHolder(AppState& app) : session(app) {
        viewport_.setViewedComponent(&session, false);
        viewport_.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport_);
    }
    void resized() override {
        viewport_.setBounds(getLocalBounds());
        session.setSize(viewport_.getMaximumVisibleWidth(), std::max(540, viewport_.getMaximumVisibleHeight()));
    }
    SessionView session;
private:
    juce::Viewport viewport_;
};

class MainComponent : public juce::Component {
public:
    explicit MainComponent(AppState& app)
        : app_(app), transport_(app), explorer_(app), arrangement_(app), sessionHolder_(app), clipEditor_(app), devicePanel_(app) {
        addAndMakeVisible(transport_);
        addAndMakeVisible(explorer_);
        addAndMakeVisible(arrangement_);
        addAndMakeVisible(splitter_);
        addAndMakeVisible(tabs_);
        tabs_.setTabBarDepth(32);
        tabs_.setOutline(0);
        tabs_.addTab("Piano Roll", theme::panel, &clipEditor_, false);
        tabs_.addTab("FX Chain", theme::panel, &devicePanel_, false);
        tabs_.addTab("Session", theme::panel, &sessionHolder_, false);
        sessionHolder_.session.requestTab = [this](int tab) { tabs_.setCurrentTabIndex(tab); };
        arrangement_.requestTab = [this](int tab) { tabs_.setCurrentTabIndex(tab); };
        splitter_.onDrag = [this](int dy) { bottomHeight_ = juce::jlimit(220, std::max(240, getHeight() - 260), bottomHeight_ - dy); resized(); };

        setWantsKeyboardFocus(true);
        setSize(1500, 900);
    }

    void paint(juce::Graphics& g) override { g.fillAll(theme::background); }

    void resized() override {
        auto r = getLocalBounds();
        transport_.setBounds(r.removeFromTop(theme::kTransportHeight));
        explorer_.setBounds(r.removeFromLeft(theme::kExplorerWidth));
        bottomHeight_ = juce::jlimit(220, std::max(240, getHeight() - 260), bottomHeight_);
        tabs_.setBounds(r.removeFromBottom(bottomHeight_));
        splitter_.setBounds(r.removeFromBottom(6));
        arrangement_.setBounds(r);
    }

    void showTab(int i) { tabs_.setCurrentTabIndex(i); }

    // ---- keyboard: space = play/stop, A-K row = piano ------------------------
    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::spaceKey) { transport_.togglePlay(); return true; }
        if (key.getTextCharacter() == 'z') { octave_ = std::max(1, octave_ - 1); return true; }
        if (key.getTextCharacter() == 'x') { octave_ = std::min(7, octave_ + 1); return true; }
        return false;
    }

    bool keyStateChanged(bool) override {
        static const char keys[] = "awsedftgyhujk";
        for (int i = 0; i < 13; ++i) {
            const bool down = juce::KeyPress::isKeyCurrentlyDown(keys[i]);
            if (down && !held_[static_cast<size_t>(i)]) {
                held_[static_cast<size_t>(i)] = true;
                sounding_[static_cast<size_t>(i)] = octave_ * 12 + i;
                app_.previewNoteOn(sounding_[static_cast<size_t>(i)], 100);
            } else if (!down && held_[static_cast<size_t>(i)]) {
                held_[static_cast<size_t>(i)] = false;
                app_.previewNoteOff(sounding_[static_cast<size_t>(i)]);
            }
        }
        return false;
    }

    void mouseDown(const juce::MouseEvent&) override { grabKeyboardFocus(); }

private:
    AppState& app_;
    struct Splitter : juce::Component {
        std::function<void(int)> onDrag;
        Splitter() { setMouseCursor(juce::MouseCursor::UpDownResizeCursor); }
        void paint(juce::Graphics& g) override {
            g.fillAll(theme::panel.darker(0.4f));
            g.setColour(theme::accentDeep);
            g.fillRect(getWidth() / 2 - 20, 2, 40, 2);
        }
        void mouseDrag(const juce::MouseEvent& e) override { if (onDrag) onDrag(e.getDistanceFromDragStartY() - lastDy_); lastDy_ = e.getDistanceFromDragStartY(); }
        void mouseUp(const juce::MouseEvent&) override { lastDy_ = 0; }
        int lastDy_ = 0;
    };

    TransportBar transport_;
    ExplorerPanel explorer_;
    ArrangementView arrangement_;
    Splitter splitter_;
    int bottomHeight_ = 360;
    SessionHolder sessionHolder_;
    ClipEditor clipEditor_;
    DevicePanel devicePanel_;
    juce::TabbedComponent tabs_{juce::TabbedButtonBar::TabsAtTop};
    int octave_ = 4;
    std::array<bool, 13> held_{};
    std::array<int, 13> sounding_{};
};
