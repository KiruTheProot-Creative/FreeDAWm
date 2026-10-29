#include "AppState.h"
#include "Theme.h"
#include "ui/MainComponent.h"

class FreeDAWmApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise(const juce::String& commandLine) override {
        juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel_);
        state_ = std::make_unique<AppState>();
        window_ = std::make_unique<MainWindow>(*state_);

        // `--screenshot out.png` renders the window once and quits (used for visual checks in CI).
        const auto args = juce::StringArray::fromTokens(commandLine, true);
        const int i = args.indexOf("--screenshot");
        if (i >= 0 && i + 1 < args.size()) {
            const juce::File out(args[i + 1].unquoted());
            const int scene = args.indexOf("--tab");
            const int sel = args.indexOf("--select");
            if (sel >= 0 && sel + 1 < args.size()) state_->selectChannel(args[sel + 1].getIntValue(), 0);
            juce::Timer::callAfterDelay(1200, [this, out, scene, args] {
                if (scene >= 0 && scene + 1 < args.size())
                    if (auto* c = dynamic_cast<MainComponent*>(window_->getContentComponent())) c->showTab(args[scene + 1].getIntValue());
                juce::Timer::callAfterDelay(400, [this, out] {
                    auto image = window_->createComponentSnapshot(window_->getLocalBounds());
                    out.deleteFile();
                    juce::FileOutputStream stream(out);
                    if (stream.openedOk()) juce::PNGImageFormat().writeImageToStream(image, stream);
                    quit();
                });
            });
            return;
        }

        // First run: pick up any VST3 effects shipped next to the app.
        juce::MessageManager::callAsync([this] {
            auto& host = *state_->pluginHost;
            if (host.availableEffects().isEmpty())
                for (auto& folder : host.bundledFolders())
                    if (!folder.findChildFiles(juce::File::findFilesAndDirectories, false, "*.vst3").isEmpty()) {
                        host.scanAsync([this](int) { state_->structureHasChanged(); });
                        break;
                    }
        });
    }

    void shutdown() override {
        if (state_) state_->saveSettings();
        window_.reset();
        state_.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    }

    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow : public juce::DocumentWindow {
    public:
        explicit MainWindow(AppState& state)
            : DocumentWindow("FreeDAWm", theme::background, DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(state), true);
            setResizable(true, true);
            setResizeLimits(980, 620, 4000, 3000);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    FdmLookAndFeel lookAndFeel_;
    std::unique_ptr<AppState> state_;
    std::unique_ptr<MainWindow> window_;
};

START_JUCE_APPLICATION(FreeDAWmApplication)
