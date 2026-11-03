#pragma once

#include "../AppState.h"
#include "../Theme.h"
#include "../Widgets.h"

/** VST list: bundled plugins first, then everything else the scanner found. Double-click loads into the selected track/bus. */
class VstExplorer : public juce::Component, private juce::ListBoxModel, private AppState::Listener {
public:
    explicit VstExplorer(AppState& app) : app_(app) {
        list_.setModel(this);
        list_.setRowHeight(24);
        list_.setColour(juce::ListBox::backgroundColourId, theme::background);
        list_.setColour(juce::ListBox::outlineColourId, theme::border);
        addAndMakeVisible(list_);

        scan_.setButtonText("Scan");
        scan_.onClick = [this] { app_.pluginHost->scanAsync([this](int) { app_.structureHasChanged(); }); };
        addAndMakeVisible(scan_);
        folder_.setButtonText("Folder");
        folder_.onClick = [this] {
            auto f = app_.pluginHost->primaryBundledFolder();
            f.createDirectory();
            f.revealToUser();
        };
        addAndMakeVisible(folder_);

        status_.setFont(theme::labelFont(10.0f));
        status_.setColour(juce::Label::textColourId, theme::readout);
        addAndMakeVisible(status_);

        app_.addListener(this);
        refresh();
    }

    ~VstExplorer() override { app_.removeListener(this); }

    void setFilter(const juce::String& f) { filter_ = f.trim(); refresh(); }

    void resized() override {
        auto r = getLocalBounds();
        auto bar = r.removeFromTop(28);
        scan_.setBounds(bar.removeFromLeft(bar.getWidth() / 2).reduced(0, 0).withTrimmedRight(3));
        folder_.setBounds(bar.withTrimmedLeft(3));
        r.removeFromTop(6);
        status_.setBounds(r.removeFromBottom(30));
        list_.setBounds(r);
    }

    void structureChanged() override { refresh(); }

    // ---- ListBoxModel ----------------------------------------------------------------
    int getNumRows() override { return static_cast<int>(rows_.size()); }

    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override {
        if (row < 0 || row >= getNumRows()) return;
        const auto& r = rows_[static_cast<size_t>(row)];
        if (r.header) {
            g.setColour(theme::panel);
            g.fillRect(0, 0, w, h);
            g.setColour(theme::text);
            g.setFont(theme::labelFont(11.0f));
            juce::Path tri;
            tri.addTriangle(8.0f, 9.0f, 16.0f, 9.0f, 12.0f, 15.0f);
            g.setColour(theme::accent);
            g.fillPath(tri);
            g.setColour(theme::text);
            g.drawText(r.title.toUpperCase(), 24, 0, w - 24, h, juce::Justification::centredLeft);
            g.setColour(theme::accentDeep);
            g.fillRect(8, h - 1, w - 16, 1);
            return;
        }
        if (selected) { g.setColour(theme::accentDeep); g.fillRect(0, 0, w, h); }
        g.setColour(theme::textDim);
        g.drawRect(juce::Rectangle<float>(24.0f, 6.0f, 14.0f, 11.0f), 1.0f);
        g.setColour(selected ? theme::text : theme::text.withAlpha(0.88f));
        g.setFont(theme::labelFont(12.0f, false));
        g.drawText(r.title, 46, 0, w - 50, h, juce::Justification::centredLeft, true);
    }

    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override { load(row); }
    void listBoxItemClicked(int row, const juce::MouseEvent& e) override {
        if (!e.mods.isPopupMenu() || row < 0 || row >= getNumRows() || rows_[static_cast<size_t>(row)].header) return;
        auto* strip = app_.selectedStrip();
        if (strip == nullptr) return;
        juce::PopupMenu m;
        m.addItem(1000, "Add to end of chain");
        juce::PopupMenu replace;
        int number = 0;
        for (int s = 0; s < fdm::kMaxFxSlots; ++s)
            if (auto* fx = app_.effectAt(*strip, s)) replace.addItem(s + 1, "FX " + juce::String(++number) + ": " + juce::String(fx->name()));
        m.addSubMenu("Replace", replace, replace.getNumItems() > 0);
        m.showMenuAsync(juce::PopupMenu::Options(), [this, row](int id) {
            if (id == 1000) load(row);
            else if (id > 0) load(row, id - 1);
        });
    }

private:
    struct Row {
        bool header = false;
        juce::String title;
        juce::PluginDescription desc;
    };

    void load(int row, int forcedSlot = -1) {
        if (row < 0 || row >= getNumRows() || rows_[static_cast<size_t>(row)].header) return;
        auto* strip = app_.selectedStrip();
        if (strip == nullptr) return;
        const int slot = forcedSlot >= 0 ? forcedSlot : app_.nextFreeFxSlot(*strip);
        if (slot < 0) {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Effect chain is full",
                                                   "\"" + juce::String(strip->name) + "\" already has " + juce::String(fdm::kMaxFxSlots)
                                                       + " effects. Remove one first, or right-click the plugin to replace one.");
            return;
        }
        const auto error = app_.loadEffect(*strip, slot, rows_[static_cast<size_t>(row)].desc);
        if (error.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Couldn't load plugin", error);
    }

    void refresh() {
        rows_.clear();
        const auto all = app_.pluginHost->availableEffects();
        const auto bundled = app_.pluginHost->bundledFolders();

        std::vector<Row> official, other;
        for (auto& d : all) {
            if (filter_.isNotEmpty() && !d.name.containsIgnoreCase(filter_) && !d.manufacturerName.containsIgnoreCase(filter_)) continue;
            bool isBundled = false;
            for (auto& f : bundled)
                if (juce::File(d.fileOrIdentifier).isAChildOf(f)) isBundled = true;
            Row r;
            r.title = d.name;
            r.desc = d;
            (isBundled ? official : other).push_back(r);
        }
        auto addSection = [this](const juce::String& title, const std::vector<Row>& items) {
            Row h; h.header = true; h.title = title + "  (" + juce::String(static_cast<int>(items.size())) + ")";
            rows_.push_back(h);
            for (auto& i : items) rows_.push_back(i);
        };
        addSection("Bundled effects", official);
        addSection("Installed VST3", other);
        list_.updateContent();
        list_.repaint();

        status_.setText(all.isEmpty() ? "NO VST3 EFFECTS FOUND YET. PUT .VST3 FILES IN THE PLUGINS FOLDER, THEN PRESS SCAN."
                                      : "DOUBLE-CLICK A PLUGIN TO ADD IT TO THE SELECTED TRACK",
                        juce::dontSendNotification);
    }

    AppState& app_;
    juce::ListBox list_;
    juce::TextButton scan_, folder_;
    juce::Label status_;
    std::vector<Row> rows_;
    juce::String filter_;
};

/** Sample browser: pick a root folder, browse the tree, click a file to audition it. */
class SampleExplorer : public juce::Component, private juce::FileBrowserListener, private juce::Timer {
public:
    explicit SampleExplorer(AppState& app)
        : app_(app), thread_("Sample scan"),
          filter_("*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg", "*", "Audio files"),
          contents_(&filter_, thread_), tree_(contents_) {
        thread_.startThread();
        tree_.setColour(juce::TreeView::backgroundColourId, theme::background);
        tree_.setColour(juce::TreeView::linesColourId, theme::border);
        tree_.setColour(juce::DirectoryContentsDisplayComponent::textColourId, theme::text);
        tree_.setColour(juce::DirectoryContentsDisplayComponent::highlightColourId, theme::accentDeep);
        tree_.setColour(juce::DirectoryContentsDisplayComponent::highlightedTextColourId, theme::text);
        tree_.setItemHeight(22);
        tree_.addListener(this);
        addAndMakeVisible(tree_);

        root_.onChange = [this] { setRoot(); };
        addAndMakeVisible(root_);
        add_.setButtonText("+");
        add_.onClick = [this] {
            chooser_ = std::make_unique<juce::FileChooser>("Choose a sample folder");
            chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& fc) {
                if (fc.getResult().isDirectory()) { app_.addSampleFolder(fc.getResult()); reloadRoots(fc.getResult().getFullPathName()); }
            });
        };
        addAndMakeVisible(add_);
        remove_.setButtonText("-");
        remove_.onClick = [this] {
            if (root_.getSelectedId() > 0) { app_.removeSampleFolder(root_.getText().isNotEmpty() ? roots_[static_cast<size_t>(root_.getSelectedId() - 1)] : juce::String()); reloadRoots({}); }
        };
        addAndMakeVisible(remove_);
        stop_.setButtonText("Stop");
        stop_.onClick = [this] { app_.stopSamplePreview(); };
        addAndMakeVisible(stop_);

        status_.setFont(theme::labelFont(10.0f));
        status_.setColour(juce::Label::textColourId, theme::readout);
        status_.setText("CLICK A FILE TO PREVIEW IT", juce::dontSendNotification);
        addAndMakeVisible(status_);

        reloadRoots({});
        startTimerHz(4);
    }

    ~SampleExplorer() override { app_.stopSamplePreview(); tree_.removeListener(this); thread_.stopThread(1000); }

    void setFilter(const juce::String& f) {
        filterText_ = f.trim();
        filter_ = juce::WildcardFileFilter(filterText_.isEmpty() ? "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg"
                                                                  : "*" + filterText_ + "*.wav;*" + filterText_ + "*.aif;*" + filterText_ + "*.aiff;*" + filterText_ + "*.flac;*" + filterText_ + "*.mp3;*" + filterText_ + "*.ogg",
                                           "*", "Audio files");
        contents_.refresh();
    }

    void resized() override {
        auto r = getLocalBounds();
        auto bar = r.removeFromTop(28);
        remove_.setBounds(bar.removeFromRight(26));
        bar.removeFromRight(3);
        add_.setBounds(bar.removeFromRight(26));
        bar.removeFromRight(3);
        root_.setBounds(bar);
        r.removeFromTop(6);
        auto foot = r.removeFromBottom(30);
        stop_.setBounds(foot.removeFromRight(56));
        status_.setBounds(foot);
        tree_.setBounds(r);
    }

private:
    void reloadRoots(const juce::String& select) {
        roots_ = app_.sampleFolders();
        root_.clear(juce::dontSendNotification);
        int selectId = 1;
        for (int i = 0; i < roots_.size(); ++i) {
            root_.addItem(juce::File(roots_[i]).getFileName().isEmpty() ? roots_[i] : juce::File(roots_[i]).getFileName(), i + 1);
            if (roots_[i] == select) selectId = i + 1;
        }
        root_.setSelectedId(selectId, juce::sendNotificationSync);
    }

    void setRoot() {
        const int id = root_.getSelectedId();
        if (id <= 0 || id > roots_.size()) return;
        contents_.setDirectory(juce::File(roots_[id - 1]), true, true);
    }

    void selectionChanged() override {
        const auto f = tree_.getSelectedFile();
        if (f.existsAsFile()) { app_.previewSample(f); status_.setText("PLAYING  " + f.getFileName().toUpperCase(), juce::dontSendNotification); }
    }
    void fileClicked(const juce::File&, const juce::MouseEvent&) override {}
    void fileDoubleClicked(const juce::File&) override {}
    void browserRootChanged(const juce::File&) override {}

    void timerCallback() override {
        if (!app_.isPreviewingSample() && status_.getText().startsWith("PLAYING"))
            status_.setText("CLICK A FILE TO PREVIEW IT", juce::dontSendNotification);
    }

    AppState& app_;
    juce::TimeSliceThread thread_;
    juce::WildcardFileFilter filter_;
    juce::DirectoryContentsList contents_;
    juce::FileTreeComponent tree_;
    juce::ComboBox root_;
    juce::TextButton add_, remove_, stop_;
    juce::Label status_;
    juce::StringArray roots_;
    juce::String filterText_;
    std::unique_ptr<juce::FileChooser> chooser_;
};

/** The left sidebar: EXPLORER header, VST / SAMPLES tabs, search box. */
class ExplorerPanel : public juce::Component {
public:
    explicit ExplorerPanel(AppState& app) : vst_(app), samples_(app) {
        vstTab_.setButtonText("VST");
        samplesTab_.setButtonText("Samples");
        for (auto* b : {&vstTab_, &samplesTab_}) {
            b->setClickingTogglesState(true);
            b->setRadioGroupId(31);
            b->setColour(juce::TextButton::buttonOnColourId, theme::accent);
            addAndMakeVisible(*b);
        }
        vstTab_.onClick = [this] { showTab(0); };
        samplesTab_.onClick = [this] { showTab(1); };

        search_.setTextToShowWhenEmpty("SEARCH NAME, MANUFACTURER...", theme::textDim);
        search_.setFont(theme::labelFont(11.0f, false));
        search_.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xfff0f0f4));
        search_.setColour(juce::TextEditor::textColourId, juce::Colours::black);
        search_.setColour(juce::TextEditor::outlineColourId, juce::Colours::black);
        search_.onTextChange = [this] { vst_.setFilter(search_.getText()); samples_.setFilter(search_.getText()); };
        addAndMakeVisible(search_);

        addAndMakeVisible(vst_);
        addChildComponent(samples_);
        vstTab_.setToggleState(true, juce::dontSendNotification);
    }

    void paint(juce::Graphics& g) override {
        g.setColour(theme::panel);
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(4.0f), 6.0f);
        g.setColour(theme::border);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(4.0f), 6.0f, 1.0f);
        g.setColour(theme::text);
        g.setFont(theme::labelFont(12.5f));
        g.drawText("EXPLORER", 18, 12, 120, 22, juce::Justification::centredLeft);
    }

    void resized() override {
        auto r = getLocalBounds().reduced(12, 10);
        r.removeFromTop(30);
        auto tabs = r.removeFromTop(30);
        vstTab_.setBounds(tabs.removeFromLeft(tabs.getWidth() / 2).withTrimmedRight(3));
        samplesTab_.setBounds(tabs.withTrimmedLeft(3));
        r.removeFromTop(8);
        search_.setBounds(r.removeFromTop(28));
        r.removeFromTop(8);
        vst_.setBounds(r);
        samples_.setBounds(r);
    }

private:
    void showTab(int i) { vst_.setVisible(i == 0); samples_.setVisible(i == 1); }

    VstExplorer vst_;
    SampleExplorer samples_;
    juce::TextButton vstTab_, samplesTab_;
    juce::TextEditor search_;
};
