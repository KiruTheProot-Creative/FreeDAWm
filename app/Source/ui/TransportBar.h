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

        loop_.setButtonText("Loop");
        loop_.setClickingTogglesState(true);
        loop_.onClick = [this] { app_.engine.loopEnabled.store(loop_.getToggleState()); app_.structureHasChanged(); };
        addAndMakeVisible(loop_);


        auto menuButton = [this](juce::TextButton& b, const char* text, std::function<void()> fn) {
            b.setButtonText(text);
            b.onClick = std::move(fn);
            addAndMakeVisible(b);
        };
        menuButton(fileButton_, "File", [this] { showFileMenu(); });
        menuButton(audioButton_, "Audio", [this] { showAudioSettings(); });
        menuButton(pluginsButton_, "Plugins", [this] { showPluginsMenu(); });
        menuButton(prefsButton_, "Preferences", [this] { if (onShowPreferences) onShowPreferences(); });
        fileButton_.setTooltip("New, open, save, export");
        audioButton_.setTooltip("Audio device settings");
        pluginsButton_.setTooltip("Scan for VST3 plugins");
        prefsButton_.setTooltip("All settings");
        play_.setTooltip("Play");
        stop_.setTooltip("Stop and return to the start marker");
        loop_.setTooltip("Loop the region marked above the ruler");

        bpm_.setSliderStyle(juce::Slider::LinearBarVertical);
        bpm_.setColour(juce::Slider::backgroundColourId, theme::background);
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


        for (auto* l : {&position_, &time_, &cpu_, &project_}) {
            const bool big = l == &position_ || l == &time_;
            l->setFont(theme::labelFont(big ? (l == &position_ ? 30.0f : 20.0f) : 11.0f, big));
            l->setColour(juce::Label::textColourId, big ? theme::readout : theme::textDim);
            addAndMakeVisible(*l);
        }
        position_.setJustificationType(juce::Justification::centred);
        time_.setJustificationType(juce::Justification::centred);
        cpu_.setJustificationType(juce::Justification::centredRight);

        syncFromEngine();
        startTimerHz(20);
    }

    ~TransportBar() override { app_.removeListener(this); }

    void paint(juce::Graphics& g) override {
        g.setGradientFill(juce::ColourGradient(theme::panelRaised.brighter(0.1f), 0.0f, 0.0f, theme::panel.darker(0.3f), 0.0f, static_cast<float>(getHeight()), false));
        g.fillAll();
        g.setColour(juce::Colours::black);
        g.drawHorizontalLine(getHeight() - 1, 0.0f, static_cast<float>(getWidth()));
        // red LCD
        auto lcd = readoutBounds_.toFloat();
        g.setColour(theme::background);
        g.fillRoundedRectangle(lcd, 6.0f);
        g.setColour(theme::accentDeep);
        g.drawRoundedRectangle(lcd, 6.0f, 1.5f);
        g.setColour(theme::border);
        g.drawVerticalLine(position_.getRight() + 4, lcd.getY() + 8.0f, lcd.getBottom() - 8.0f);
        g.setColour(theme::textDim);
        g.setFont(theme::labelFont(9.0f));
        g.drawText("BAR . BEAT . STEP", position_.getX(), 3, position_.getWidth(), 10, juce::Justification::centred);
        g.drawText("TIME", time_.getX(), 3, time_.getWidth(), 10, juce::Justification::centred);
    }

    void resized() override {
        auto r = getLocalBounds().reduced(10, 9);
        fileButton_.setBounds(r.removeFromLeft(54));
        r.removeFromLeft(4);
        audioButton_.setBounds(r.removeFromLeft(58));
        r.removeFromLeft(4);
        pluginsButton_.setBounds(r.removeFromLeft(70));
        r.removeFromLeft(18);
        play_.setBounds(r.removeFromLeft(40));
        r.removeFromLeft(4);
        stop_.setBounds(r.removeFromLeft(40));
        r.removeFromLeft(4);
        loop_.setBounds(r.removeFromLeft(54));
        r.removeFromLeft(16);
        readoutBounds_ = r.removeFromLeft(360);
        auto lcd = readoutBounds_.reduced(8, 0);
        position_.setBounds(lcd.removeFromLeft(190).withTrimmedTop(8));
        time_.setBounds(lcd.withTrimmedTop(12));
        r.removeFromLeft(16);
        bpm_.setBounds(r.removeFromLeft(104));
        r.removeFromLeft(6);
        swing_.setBounds(r.removeFromLeft(104));
        prefsButton_.setBounds(r.removeFromRight(110));
        r.removeFromRight(10);
        cpuBounds_ = r.removeFromRight(74);
        cpu_.setBounds(cpuBounds_);
        project_.setBounds(r.removeFromRight(200));
    }

    /** Space bar etc. */
    void togglePlay() { if (app_.engine.isPlaying()) app_.engine.stop(); else app_.engine.play(); }

    std::function<void()> onShowPreferences;

    // ---- actions, also reachable through keyboard shortcuts -------------------------
    void newProject() { app_.newProject(); }
    void save() { if (app_.projectFile() == juce::File()) saveProjectAs(); else report(app_.saveProject(app_.projectFile())); }
    void toggleLoop() { app_.engine.loopEnabled.store(!app_.engine.loopEnabled.load()); syncFromEngine(); app_.structureHasChanged(); }

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
        auto options = app_.exportOptionsFromPrefs();
        if (!app_.prefs->getBool("exportAsk")) { chooseExportFile(options); return; }

        auto* w = new juce::AlertWindow("Export audio", "Renders the whole timeline, from bar 1 to the end of the last clip.", juce::MessageBoxIconType::NoIcon);
        w->addComboBox("format", {"16-bit WAV", "24-bit WAV", "32-bit float WAV"}, "Format");
        w->getComboBoxComponent("format")->setSelectedItemIndex(juce::jlimit(0, 2, app_.prefs->getInt("exportBitDepth")));
        w->addComboBox("rate", {"Same as audio device", "44.1 kHz", "48 kHz", "88.2 kHz", "96 kHz"}, "Sample rate");
        w->getComboBoxComponent("rate")->setSelectedItemIndex(juce::jlimit(0, 4, app_.prefs->getInt("exportSampleRate")));
        w->addTextEditor("tail", juce::String(options.tailSeconds, 1), "Tail (seconds)");
        w->addComboBox("normalize", {"Don't normalize", "Normalize to " + juce::String(options.normalizeDb, 1) + " dBFS"}, "Normalize");
        w->getComboBoxComponent("normalize")->setSelectedItemIndex(options.normalize ? 1 : 0);
        w->addButton("Export", 1, juce::KeyPress(juce::KeyPress::returnKey));
        w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        w->enterModalState(true, juce::ModalCallbackFunction::create([this, w, options](int result) mutable {
            std::unique_ptr<juce::AlertWindow> owner(w);
            if (result != 1) return;
            const int fmt = w->getComboBoxComponent("format")->getSelectedItemIndex();
            options.format = fmt == 0 ? fdm::WavFormat::Pcm16 : (fmt == 1 ? fdm::WavFormat::Pcm24 : fdm::WavFormat::Float32);
            static const double rates[] = {0.0, 44100.0, 48000.0, 88200.0, 96000.0};
            options.sampleRate = rates[juce::jlimit(0, 4, w->getComboBoxComponent("rate")->getSelectedItemIndex())];
            options.tailSeconds = juce::jlimit(0.0f, 60.0f, w->getTextEditorContents("tail").getFloatValue());
            options.normalize = w->getComboBoxComponent("normalize")->getSelectedItemIndex() == 1;
            chooseExportFile(options);
        }), false);
    }

private:
    void chooseExportFile(AppState::ExportOptions options) {
        auto folder = app_.prefs->exportFolder();
        folder.createDirectory();
        chooser_ = std::make_unique<juce::FileChooser>("Export audio", folder.getChildFile(app_.projectName() + ".wav"), "*.wav");
        chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, options](const juce::FileChooser& fc) {
                                  if (fc.getResult() == juce::File()) return;
                                  const auto result = app_.exportTimeline(fc.getResult().withFileExtension("wav"), options);
                                  if (result.failed()) report(result);
                                  else juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Export finished", fc.getResult().getFileName() + " was written.");
                              });
    }

    void paintOverChildren(juce::Graphics& g) override {
        if (app_.prefs->getInt("cpuMeter") != 1) return;                 // bar style
        auto bar = cpuBounds_.reduced(6, 20).toFloat();
        g.setColour(theme::background);
        g.fillRoundedRectangle(bar, 3.0f);
        const float load = static_cast<float>(juce::jlimit(0.0, 1.0, app_.cpuUsage()));
        g.setColour(load > 0.8f ? theme::warn : theme::accent);
        g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * load), 3.0f);
    }

    void structureChanged() override { syncFromEngine(); }

    void syncFromEngine() {
        bpm_.setValue(app_.engine.bpm.get(), juce::dontSendNotification);
        swing_.setValue(app_.engine.swing.get() * 100.0, juce::dontSendNotification);
        project_.setText(app_.projectName(), juce::dontSendNotification);
        project_.setJustificationType(juce::Justification::centredRight);
        loop_.setToggleState(app_.engine.loopEnabled.load(), juce::dontSendNotification);
    }

    void timerCallback() override {
        const bool playing = app_.engine.isPlaying();
        if (play_.getToggleState() != playing) play_.setToggleState(playing, juce::dontSendNotification);

        const auto t = static_cast<int>(app_.engine.positionTicks());
        position_.setText(juce::String::formatted("%03d.%d.%02d", t / fdm::kTicksPerBar + 1,
                                                  (t % fdm::kTicksPerBar) / fdm::kTicksPerBeat + 1,
                                                  (t % fdm::kTicksPerBeat) / fdm::kTicksPerStep + 1),
                          juce::dontSendNotification);
        const double seconds = static_cast<double>(t) / fdm::kTicksPerBeat * 60.0 / static_cast<double>(app_.engine.bpm.get());
        time_.setText(juce::String::formatted("%d:%02d.%03d", static_cast<int>(seconds) / 60, static_cast<int>(seconds) % 60,
                                              static_cast<int>((seconds - std::floor(seconds)) * 1000.0)),
                      juce::dontSendNotification);
        const int meter = app_.prefs->getInt("cpuMeter");
        cpu_.setVisible(meter == 0);
        cpu_.setText("CPU " + juce::String(juce::roundToInt(app_.cpuUsage() * 100.0)) + "%", juce::dontSendNotification);
        if (meter == 1) repaint(cpuBounds_);
        if (project_.getText() != app_.projectName()) project_.setText(app_.projectName(), juce::dontSendNotification);
    }

    juce::File projectsFolder() const { return app_.prefs->projectFolder(); }

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
                case 3: save(); break;
                case 4: saveProjectAs(); break;
                case 5: exportAudio(); break;
                default: break;
            }
        });
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
    juce::TextButton fileButton_, audioButton_, pluginsButton_, prefsButton_, loop_;
    juce::Rectangle<int> cpuBounds_;
    juce::Rectangle<int> readoutBounds_;
    juce::Slider bpm_, swing_;
    juce::Label position_, time_, cpu_, project_;
    std::unique_ptr<juce::FileChooser> chooser_;
};
