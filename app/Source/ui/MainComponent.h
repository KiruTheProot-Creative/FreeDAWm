#pragma once

#include "ArrangementView.h"
#include "ClipEditor.h"
#include "Explorer.h"
#include "DevicePanel.h"
#include "MixerView.h"
#include "PreferencesWindow.h"
#include "TransportBar.h"

class MainComponent : public juce::Component, public juce::DragAndDropContainer, private juce::ChangeListener {
public:
    explicit MainComponent(AppState& app)
        : app_(app), transport_(app), explorer_(app), arrangement_(app), mixer_(app), clipEditor_(app), devicePanel_(app) {
        addAndMakeVisible(transport_);
        addAndMakeVisible(explorer_);
        addAndMakeVisible(arrangement_);
        addAndMakeVisible(splitter_);
        addAndMakeVisible(tabs_);
        tabs_.setTabBarDepth(32);
        tabs_.setOutline(0);
        tabs_.addTab("Piano Roll", theme::panel, &clipEditor_, false);
        tabs_.addTab("FX Chain", theme::panel, &devicePanel_, false);
        tabs_.addTab("Mixer", theme::panel, &mixer_, false);
        mixer_.requestTab = [this](int tab) { tabs_.setCurrentTabIndex(tab); };
        arrangement_.requestTab = [this](int tab) { tabs_.setCurrentTabIndex(tab); };
        splitter_.onDrag = [this](int dy) { bottomHeight_ = juce::jlimit(220, std::max(240, getHeight() - 260), bottomHeight_ - dy); resized(); };

        transport_.onShowPreferences = [this] { showPreferences(); };

        app_.prefs->addChangeListener(this);
        applyPrefs();
        setWantsKeyboardFocus(true);
        setSize(1500, 900);
    }

    ~MainComponent() override { app_.prefs->removeChangeListener(this); }

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

    void showPreferences(int tab = -1) {
        if (prefsWindow_ == nullptr) prefsWindow_ = std::make_unique<PreferencesWindow>(app_);
        if (tab >= 0) prefsWindow_->showTab(tab);
        prefsWindow_->setVisible(true);
        prefsWindow_->toFront(true);
    }
    PreferencesWindow* preferencesWindow() { return prefsWindow_.get(); }

    // ---- keyboard: shortcuts from Preferences, then the computer-keyboard piano --------
    bool keyPressed(const juce::KeyPress& key) override { return perform(app_.prefs->actionFor(key)); }

    bool perform(const juce::String& action) {
        if (action.isEmpty()) return false;
        if (action == "playStop") transport_.togglePlay();
        else if (action == "toStart") app_.engine.setPlayhead(0);
        else if (action == "loop") transport_.toggleLoop();
        else if (action == "newProject") transport_.newProject();
        else if (action == "openProject") transport_.openProject();
        else if (action == "save") transport_.save();
        else if (action == "saveAs") transport_.saveProjectAs();
        else if (action == "export") transport_.exportAudio();
        else if (action == "preferences") showPreferences();
        else if (action == "addAudioTrack") app_.addChannel(fdm::InstrumentType::Audio);
        else if (action == "addMidiTrack") app_.addChannel(fdm::InstrumentType::PolySynth);
        else if (action == "duplicate") arrangement_.duplicateSelected();
        else if (action == "delete") arrangement_.deleteSelected();
        else if (action == "zoomIn") arrangement_.zoomBy(1.5);
        else if (action == "zoomOut") arrangement_.zoomBy(1.0 / 1.5);
        else if (action == "showPianoRoll") showTab(0);
        else if (action == "showFx") showTab(1);
        else if (action == "showMixer") showTab(2);
        else if (action == "octaveDown") octave_ = std::max(0, octave_ - 1);
        else if (action == "octaveUp") octave_ = std::min(7, octave_ + 1);
        else return false;
        return true;
    }

    bool keyStateChanged(bool) override {
        if (!app_.prefs->getBool("keyboardPiano")) return false;
        static const char keys[] = "awsedftgyhujk";
        const int velocity = juce::jlimit(1, 127, app_.prefs->getInt("keyboardVelocity"));
        for (int i = 0; i < 13; ++i) {
            const bool down = juce::KeyPress::isKeyCurrentlyDown(keys[i]) && !juce::ModifierKeys::getCurrentModifiers().isCommandDown();
            if (down && !held_[static_cast<size_t>(i)]) {
                held_[static_cast<size_t>(i)] = true;
                sounding_[static_cast<size_t>(i)] = octave_ * 12 + i;
                app_.previewNoteOn(sounding_[static_cast<size_t>(i)], velocity);
            } else if (!down && held_[static_cast<size_t>(i)]) {
                held_[static_cast<size_t>(i)] = false;
                app_.previewNoteOff(sounding_[static_cast<size_t>(i)]);
            }
        }
        return false;
    }

    void mouseDown(const juce::MouseEvent&) override { grabKeyboardFocus(); }

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { applyPrefs(); }

    void applyPrefs() {
        auto& p = *app_.prefs;
        static const float scales[] = {0.8f, 0.9f, 1.0f, 1.1f, 1.25f, 1.5f, 1.75f, 2.0f};
        const float scale = scales[juce::jlimit(0, 7, p.getInt("uiScale"))];
        if (std::abs(juce::Desktop::getInstance().getGlobalScaleFactor() - scale) > 0.001f) juce::Desktop::getInstance().setGlobalScaleFactor(scale);
        if (p.getBool("tooltips") && tooltips_ == nullptr) tooltips_ = std::make_unique<juce::TooltipWindow>(this, 600);
        if (!p.getBool("tooltips")) tooltips_.reset();
        if (p.getInt("keyboardOctave") != lastOctavePref_) { lastOctavePref_ = p.getInt("keyboardOctave"); octave_ = lastOctavePref_; }
        theme::peakHold = p.getBool("peakHold");
        theme::meterScheme = p.getInt("meterColours");
        repaint();
    }

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
    MixerView mixer_;
    ClipEditor clipEditor_;
    DevicePanel devicePanel_;
    juce::TabbedComponent tabs_{juce::TabbedButtonBar::TabsAtTop};
    int octave_ = 4, lastOctavePref_ = -1;
    std::unique_ptr<juce::TooltipWindow> tooltips_;
    std::unique_ptr<PreferencesWindow> prefsWindow_;
    std::array<bool, 13> held_{};
    std::array<int, 13> sounding_{};
};
