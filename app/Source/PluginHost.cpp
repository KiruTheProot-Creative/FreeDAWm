#include "PluginHost.h"

// ===========================================================================
// Vst3Effect
// ===========================================================================

Vst3Effect::Vst3Effect(std::unique_ptr<juce::AudioPluginInstance> plugin)
    : plugin_(std::move(plugin)), name_(plugin_->getName()) {}

Vst3Effect::~Vst3Effect() {
    if (prepared_) plugin_->releaseResources();
}

void Vst3Effect::prepare(double sampleRate, int maxBlockSize) {
    if (prepared_) plugin_->releaseResources();
    maxBlock_ = juce::jmax(16, maxBlockSize);
    plugin_->setRateAndBufferSizeDetails(sampleRate, maxBlock_);
    plugin_->prepareToPlay(sampleRate, maxBlock_);
    midi_.ensureSize(256);
    prepared_ = true;
}

void Vst3Effect::process(float* left, float* right, int numFrames) {
    if (!prepared_ || plugin_->isSuspended()) return;     // pass the audio through untouched

    juce::ScopedNoDenormals noDenormals;
    // The engine may hand us more frames than we prepared for; plugins must never see that.
    for (int done = 0; done < numFrames;) {
        const int n = juce::jmin(maxBlock_, numFrames - done);
        float* channels[2] = {left + done, right + done};
        juce::AudioBuffer<float> buffer(channels, 2, n);
        midi_.clear();
        plugin_->processBlock(buffer, midi_);
        done += n;
    }
}

void Vst3Effect::reset() { plugin_->reset(); }

int Vst3Effect::latencySamples() const { return plugin_->getLatencySamples(); }

// ===========================================================================
// PluginWindow
// ===========================================================================

PluginWindow::PluginWindow(Vst3Effect& effect, std::function<void()> onCloseRequested)
    : juce::DocumentWindow(effect.plugin().getName(), juce::Colour(0xff1c1f26), juce::DocumentWindow::closeButton),
      onClose_(std::move(onCloseRequested)) {
    auto& plugin = effect.plugin();
    juce::AudioProcessorEditor* editor = plugin.hasEditor() ? plugin.createEditorIfNeeded() : nullptr;
    if (editor == nullptr) editor = new juce::GenericAudioProcessorEditor(plugin);

    setUsingNativeTitleBar(true);
    setContentOwned(editor, true);
    setResizable(editor->isResizable(), false);
    centreAroundComponent(nullptr, getWidth(), getHeight());
    setVisible(true);
}

// ===========================================================================
// PluginHost
// ===========================================================================

class PluginHost::ScanJob : public juce::ThreadWithProgressWindow {
public:
    ScanJob(PluginHost& host, std::function<void(int)> done)
        : juce::ThreadWithProgressWindow("Scanning for VST3 plugins...", true, true),
          host_(host), onDone_(std::move(done)) {}

    void run() override {
        for (int i = 0; i < host_.formatManager.getNumFormats() && !threadShouldExit(); ++i) {
            auto* format = host_.formatManager.getFormat(i);
            if (format->getName() != "VST3") continue;

            juce::PluginDirectoryScanner scanner(host_.knownPlugins, *format, host_.searchPaths(), true,
                                                 host_.deadMansPedalFile());
            juce::String name;
            while (!threadShouldExit() && scanner.scanNextFile(true, name)) {
                setProgress(scanner.getProgress());
                setStatusMessage(name);
            }
        }
    }

    void threadComplete(bool /*userPressedCancel*/) override {
        host_.saveSettings();
        const int found = host_.availableEffects().size();
        auto done = std::move(onDone_);
        // We are owned by the host; let the current call stack unwind before destroying ourselves.
        juce::MessageManager::callAsync([&host = host_, found, done] {
            host.scanJob_.reset();
            if (done) done(found);
        });
    }

private:
    PluginHost& host_;
    std::function<void(int)> onDone_;
};

PluginHost::PluginHost(juce::PropertiesFile* settings) : settings_(settings) {
    juce::addDefaultFormatsToManager(formatManager);

    if (settings_ != nullptr) {
        if (auto xml = settings_->getXmlValue("knownPlugins")) knownPlugins.recreateFromXml(*xml);
        extraFolders_.addTokens(settings_->getValue("pluginFolders"), "|", "");
        extraFolders_.removeEmptyStrings();
        useSystemFolders_ = settings_->getBoolValue("scanSystemVst3", false);
    }
}

PluginHost::~PluginHost() {
    scanJob_.reset();
    saveSettings();
}

void PluginHost::saveSettings() {
    if (settings_ == nullptr) return;
    if (auto xml = knownPlugins.createXml()) settings_->setValue("knownPlugins", xml.get());
    settings_->setValue("pluginFolders", extraFolders_.joinIntoString("|"));
    settings_->setValue("scanSystemVst3", useSystemFolders_);
    settings_->saveIfNeeded();
}

juce::File PluginHost::primaryBundledFolder() const {
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory().getChildFile("plugins");
}

juce::Array<juce::File> PluginHost::bundledFolders() const {
    const auto exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
    juce::Array<juce::File> result;
    for (auto f : {exeDir.getChildFile("plugins"),
                   exeDir.getParentDirectory().getChildFile("Resources").getChildFile("plugins"),   // macOS app bundle
                   exeDir.getParentDirectory().getChildFile("PlugIns")})
        if (f.isDirectory()) result.addIfNotAlreadyThere(f);
    return result;
}

juce::FileSearchPath PluginHost::searchPaths() const {
    juce::FileSearchPath paths;
    for (auto& f : bundledFolders()) paths.add(f);
    for (auto& f : extraFolders_) paths.add(juce::File(f));
    if (useSystemFolders_) {
        for (int i = 0; i < formatManager.getNumFormats(); ++i) {
            auto* format = formatManager.getFormat(i);
            if (format->getName() == "VST3") paths.addPath(format->getDefaultLocationsToSearch());
        }
    }
    return paths;
}

void PluginHost::addFolder(const juce::File& folder) {
    if (folder.isDirectory() && !extraFolders_.contains(folder.getFullPathName()))
        extraFolders_.add(folder.getFullPathName());
    saveSettings();
}

juce::File PluginHost::deadMansPedalFile() const {
    if (settings_ != nullptr)
        return settings_->getFile().getSiblingFile("vst3-scan-crashed.txt");
    return juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("fdm-vst3-scan-crashed.txt");
}

void PluginHost::scanAsync(std::function<void(int)> onDone) {
    if (scanJob_ != nullptr) return;     // already scanning
    scanJob_ = std::make_unique<ScanJob>(*this, std::move(onDone));
    scanJob_->launchThread();
}

juce::Array<juce::PluginDescription> PluginHost::availableEffects() const {
    juce::Array<juce::PluginDescription> effects;
    for (auto& d : knownPlugins.getTypes())
        if (!d.isInstrument && d.pluginFormatName == "VST3") effects.add(d);

    struct ByName {
        static int compareElements(const juce::PluginDescription& a, const juce::PluginDescription& b) {
            return a.name.compareNatural(b.name);
        }
    } sorter;
    effects.sort(sorter);
    return effects;
}

std::unique_ptr<fdm::IEffect> PluginHost::createEffect(const juce::PluginDescription& description, double sampleRate,
                                                       int blockSize, juce::String& error) {
    auto instance = formatManager.createPluginInstance(description, sampleRate, blockSize, error);
    if (instance == nullptr) {
        if (error.isEmpty()) error = "The plugin could not be loaded.";
        return nullptr;
    }

    // FreeDAWm's effect chain is stereo in, stereo out. Switch extra buses (sidechains etc.) off.
    if (instance->getBusCount(true) < 1 || instance->getBusCount(false) < 1) {
        error = description.name + " has no audio input, so it can't be used as an effect.";
        return nullptr;
    }
    auto layout = instance->getBusesLayout();
    for (int i = 0; i < layout.inputBuses.size(); ++i)
        layout.inputBuses.getReference(i) = i == 0 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
    for (int i = 0; i < layout.outputBuses.size(); ++i)
        layout.outputBuses.getReference(i) = i == 0 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();

    if (!instance->setBusesLayout(layout)) {
        error = description.name + " doesn't support a stereo input and output.";
        return nullptr;
    }
    return std::make_unique<Vst3Effect>(std::move(instance));
}
