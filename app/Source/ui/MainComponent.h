#pragma once

#include "ClipEditor.h"
#include "DevicePanel.h"
#include "SessionView.h"
#include "TransportBar.h"

class MainComponent : public juce::Component {
public:
    explicit MainComponent(AppState& app)
        : app_(app), transport_(app), session_(app), clipEditor_(app), devicePanel_(app) {
        addAndMakeVisible(transport_);
        addAndMakeVisible(session_);
        addAndMakeVisible(tabs_);
        tabs_.setTabBarDepth(28);
        tabs_.setOutline(0);
        tabs_.addTab("Clip", theme::panel, &clipEditor_, false);
        tabs_.addTab("Device", theme::panel, &devicePanel_, false);
        session_.requestTab = [this](int tab) { tabs_.setCurrentTabIndex(tab); };

        setWantsKeyboardFocus(true);
        setSize(1280, 800);
    }

    void paint(juce::Graphics& g) override { g.fillAll(theme::background); }

    void resized() override {
        auto r = getLocalBounds();
        transport_.setBounds(r.removeFromTop(theme::kTransportHeight));
        tabs_.setBounds(r.removeFromBottom(juce::jlimit(300, 440, getHeight() / 3)));
        session_.setBounds(r);
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
    TransportBar transport_;
    SessionView session_;
    ClipEditor clipEditor_;
    DevicePanel devicePanel_;
    juce::TabbedComponent tabs_{juce::TabbedButtonBar::TabsAtTop};
    int octave_ = 4;
    std::array<bool, 13> held_{};
    std::array<int, 13> sounding_{};
};
