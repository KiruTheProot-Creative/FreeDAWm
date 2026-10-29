#pragma once

#include "../AppState.h"
#include "../Theme.h"
#include "../Widgets.h"

class TransportBar : public juce::Component, private juce::Timer, private AppState::Listener {
public:
    explicit TransportBar(AppState& app)
        : app_(app), play_(IconButton::Icon::Play, theme::play), stop_(IconButton::Icon::Stop, theme::warn) {
        app_.addListener(this);

        play_.onClick = [this] { app_.engine.play(); };
        stop_.onClick = [this] { app_.engine.stop(); };
        addAndMakeVisible(play_);
        addAndMakeVisible(stop_);

        auto menuButton = [this](juce::TextButton& b, const char* text, std::function<void()> fn) {
            b.setButtonText(text);
            b.onClick = std::move(fn);
            addAndMakeVisible(b);
        };
        menuButton(fileButton_, "File", [this] { showFileMenu(); });
        menuButton(audioButton_, "Audio", [this] { showAudioSettings(); });
        menuButton(pluginsButton_, "Plugins", [this] { showPluginsMenu(); });

        bpm_.setSliderStyle(juce::Slider::LinearBarVertical);
        bpm_.setRange(40.0, 300.0, 0.1);
        bpm_.setValue(app_.engine.bpm.get(), juce::dontSendNotification);
        bpm_.setNumDecimalPlacesToDisplay(1);
        bpm_.setDoubleClickReturnValue(true, 120.0);
        bpm_.setTextValueSuffix(" BPM");
        bpm_.setMouseDragSensitivity(300);
        bpm_.setColour(juce::Slider::trackColourId, theme::panelRaised);
        bpm_.setColour(juce::Slider::textBoxTextColourId, theme::text);
        bpm_.onValueChange = [this] { app_.engine.bpm.set(static_cast<float>(bpm_.getValue())); };
        addAndMakeVisible(bpm_);

        swing_.setSliderStyle(juce::Slider::LinearBarVertical);
        swing_.setRange(0.0, 50.0, 1.0);
        swing_.setValue(app_.engine.swing.get() * 100.0, juce::dontSendNotification);
        swing_.setTextValueSuffix("% swing");
        swing_.setDoubleClickReturnValue(true, 0.0);
        swing_.setColour(juce::Slider::trackColourId, theme::panelRaised);
        swing_.setColour(juce::Slider::textBoxTextColourId, theme::text);
        swing_.onValueChange = [this] { app_.engine.swing.set(static_cast<float>(swing_.getValue() / 100.0)); };
        addAndMakeVisible(swing_);

        struct Q { const char* name; int ticks; };
        static const Q options[] = {{"Launch: None", 0}, {"Launch: 1/16", 240}, {"Launch: 1/8", 480}, {"Launch: 1/4", 960},
                                    {"Launch: 1/2", 1920}, {"Launch: 1 Bar", 3840}, {"Launch: 2 Bars", 7680}, {"Launch: 4 Bars", 15360}};
        for (int i = 0; i < 8; ++i) quant_.addItem(options[i].name, i + 1);
        quant_.onChange = [this] {
            static const int ticks[] = {0, 240, 480, 960, 1920, 3840, 7680, 15360};
            app_.engine.launchQuantizeTicks.store(ticks[quant_.getSelectedId() - 1]);
        };
        addAndMakeVisible(quant_);

        for (auto* l : {&position_, &cpu_, &project_}) {
            l->setFont(juce::Font(juce::FontOptions(l == &position_ ? 20.0f : 12.0f, l == &position_ ? juce::Font::bold : juce::Font::plain)));
            l->setColour(juce::Label::textColourId, l == &position_ ? theme::text : theme::textDim);
            addAndMakeVisible(*l);
        }
        position_.setJustificationType(juce::Justification::centred);
        cpu_.setJustificationType(juce::Justification::centredRight);

        syncFromEngine();
        startTimerHz(20);
    }

    ~TransportBar() override { app_.removeListener(this); }

    void paint(juce::Graphics& g) override {
        g.fillAll(theme::panel);
        g.setColour(theme::border);
        g.drawHorizontalLine(getHeight() - 1, 0.0f, static_cast<float>(getWidth()));
        g.setColour(theme::panelRaised);
        g.fillRoundedRectangle(position_.getBounds().toFloat(), 5.0f);
    }

    void resized() override {
        auto r = getLocalBounds().reduced(8, 7);
        fileButton_.setBounds(r.removeFromLeft(48));
        r.removeFromLeft(4);
        audioButton_.setBounds(r.removeFromLeft(54));
        r.removeFromLeft(4);
        pluginsButton_.setBounds(r.removeFromLeft(64));
        r.removeFromLeft(16);
        play_.setBounds(r.removeFromLeft(36));
        r.removeFromLeft(4);
        stop_.setBounds(r.removeFromLeft(36));
        r.removeFromLeft(14);
        position_.setBounds(r.removeFromLeft(130));
        r.removeFromLeft(10);
        bpm_.setBounds(r.removeFromLeft(100));
        r.removeFromLeft(6);
        swing_.setBounds(r.removeFromLeft(100));
        r.removeFromLeft(10);
        quant_.setBounds(r.removeFromLeft(130));
        cpu_.setBounds(r.removeFromRight(80));
        project_.setBounds(r.removeFromRight(220));
    }

    /** Space bar etc. */
    void togglePlay() { if (app_.engine.isPlaying()) app_.engine.stop(); else app_.engine.play(); }

private:
    void structureChanged() override { syncFromEngine(); }

    void syncFromEngine() {
        bpm_.setValue(app_.engine.bpm.get(), juce::dontSendNotification);
        swing_.setValue(app_.engine.swing.get() * 100.0, juce::dontSendNotification);
        const int q = app_.engine.launchQuantizeTicks.load();
        static const int ticks[] = {0, 240, 480, 960, 1920, 3840, 7680, 15360};
        int id = 6;
        for (int i = 0; i < 8; ++i) if (ticks[i] == q) id = i + 1;
        quant_.setSelectedId(id, juce::dontSendNotification);
        project_.setText(app_.projectName(), juce::dontSendNotification);
        project_.setJustificationType(juce::Justification::centredRight);
    }

    void timerCallback() override {
        const bool playing = app_.engine.isPlaying();
        if (play_.getToggleState() != playing) play_.setToggleState(playing, juce::dontSendNotification);

        const auto t = static_cast<int>(app_.engine.positionTicks());
        position_.setText(juce::String::formatted("%d . %d . %d", t / fdm::kTicksPerBar + 1,
                                                  (t % fdm::kTicksPerBar) / fdm::kTicksPerBeat + 1,
                                                  (t % fdm::kTicksPerBeat) / fdm::kTicksPerStep + 1),
                          juce::dontSendNotification);
        cpu_.setText("CPU " + juce::String(juce::roundToInt(app_.cpuUsage() * 100.0)) + "%", juce::dontSendNotification);
        if (project_.getText() != app_.projectName()) project_.setText(app_.projectName(), juce::dontSendNotification);
    }

    // ---- menus --------------------------------------------------------------
    void showFileMenu() {
        juce::PopupMenu m;
        m.addItem(1, "New project");
        m.addItem(2, "Open project...");
        m.addItem(3, "Save");
        m.addItem(4, "Save as...");
        m.addSeparator();
        m.addItem(5, "Export audio (WAV)...");
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&fileButton_), [this](int id) {
            switch (id) {
                case 1: app_.newProject(); break;
                case 2: openProject(); break;
                case 3: if (app_.projectFile() == juce::File()) saveProjectAs(); else report(app_.saveProject(app_.projectFile())); break;
                case 4: saveProjectAs(); break;
                case 5: exportAudio(); break;
                default: break;
            }
        });
    }

    static juce::File projectsFolder() {
        return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("FreeDAWm");
    }

    void openProject() {
        projectsFolder().createDirectory();
        chooser_ = std::make_unique<juce::FileChooser>("Open project", projectsFolder(), "*.fdm");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
            if (fc.getResult() != juce::File()) report(app_.loadProject(fc.getResult()));
        });
    }

    void saveProjectAs() {
        projectsFolder().createDirectory();
        chooser_ = std::make_unique<juce::FileChooser>("Save project", projectsFolder().getChildFile(app_.projectName() + ".fdm"), "*.fdm");
        chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this](const juce::FileChooser& fc) {
                                  if (fc.getResult() != juce::File()) report(app_.saveProject(fc.getResult().withFileExtension("fdm")));
                              });
    }

    void exportAudio() {
        auto* w = new juce::AlertWindow("Export audio", "Renders from the start of the project, exactly as it would play.", juce::MessageBoxIconType::NoIcon);
        w->addComboBox("scene", {"What's playing now", "Scene 1", "Scene 2", "Scene 3", "Scene 4", "Scene 5", "Scene 6", "Scene 7", "Scene 8"}, "Source");
        w->addComboBox("format", {"16-bit WAV", "24-bit WAV", "32-bit float WAV"}, "Format");
        w->getComboBoxComponent("scene")->setSelectedItemIndex(1);
        w->getComboBoxComponent("format")->setSelectedItemIndex(1);
        w->addTextEditor("bars", "8", "Length in bars");
        w->addButton("Export", 1, juce::KeyPress(juce::KeyPress::returnKey));
        w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int result) {
            std::unique_ptr<juce::AlertWindow> owner(w);
            if (result != 1) return;
            const int scene = w->getComboBoxComponent("scene")->getSelectedItemIndex() - 1;
            const int fmt = w->getComboBoxComponent("format")->getSelectedItemIndex();
            const int bars = juce::jlimit(1, 128, w->getTextEditorContents("bars").getIntValue());
            const auto format = fmt == 0 ? fdm::WavFormat::Pcm16 : (fmt == 1 ? fdm::WavFormat::Pcm24 : fdm::WavFormat::Float32);
            chooser_ = std::make_unique<juce::FileChooser>("Export audio", projectsFolder().getChildFile(app_.projectName() + ".wav"), "*.wav");
            chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [this, scene, bars, format](const juce::FileChooser& fc) {
                                      if (fc.getResult() == juce::File()) return;
                                      const auto result = app_.exportWav(fc.getResult().withFileExtension("wav"), scene, bars, format);
                                      if (result.failed()) report(result);
                                      else juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Export finished", fc.getResult().getFileName() + " was written.");
                                  });
        }), false);
    }

    void showAudioSettings() {
        auto* selector = new juce::AudioDeviceSelectorComponent(app_.deviceManager, 0, 0, 2, 2, true, false, true, false);
        selector->setSize(500, 420);
        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned(selector);
        o.dialogTitle = "Audio & MIDI settings";
        o.dialogBackgroundColour = theme::panel;
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar = true;
        o.resizable = false;
        o.launchAsync();
    }

    void showPluginsMenu() {
        auto& host = *app_.pluginHost;
        juce::PopupMenu m;
        m.addItem(1, "Scan for plugins now");
        m.addItem(2, "Add plugin folder...");
        m.addItem(3, "Also scan system VST3 folders", true, host.useSystemFolders());
        m.addItem(4, "Show bundled plugins folder");
        m.addSeparator();
        m.addItem(-1, juce::String(host.availableEffects().size()) + " effects available", false);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&pluginsButton_), [this](int id) {
            auto& host = *app_.pluginHost;
            if (id == 1) host.scanAsync([this](int) { app_.structureHasChanged(); });
            else if (id == 2) {
                chooser_ = std::make_unique<juce::FileChooser>("Choose a folder containing .vst3 plugins");
                chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& fc) {
                    if (fc.getResult().isDirectory()) { app_.pluginHost->addFolder(fc.getResult()); app_.pluginHost->scanAsync([this](int) { app_.structureHasChanged(); }); }
                });
            } else if (id == 3) { host.setUseSystemFolders(!host.useSystemFolders()); host.scanAsync([this](int) { app_.structureHasChanged(); }); }
            else if (id == 4) { auto f = host.primaryBundledFolder(); f.createDirectory(); f.revealToUser(); }
        });
    }

    static void report(const juce::Result& r) {
        if (r.failed()) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "FreeDAWm", r.getErrorMessage());
    }

    AppState& app_;
    IconButton play_, stop_;
    juce::TextButton fileButton_, audioButton_, pluginsButton_;
    juce::Slider bpm_, swing_;
    juce::ComboBox quant_;
    juce::Label position_, cpu_, project_;
    std::unique_ptr<juce::FileChooser> chooser_;
};
