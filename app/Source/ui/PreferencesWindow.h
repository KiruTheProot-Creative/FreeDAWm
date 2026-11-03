#pragma once

#include "../AppState.h"
#include "../Theme.h"
#include "../Widgets.h"

namespace prefsui {

constexpr int kLabelW = 230;
constexpr int kControlW = 300;
constexpr int kRowH = 34;

// ---------------------------------------------------------------------------
// One settings row: label | control | note (or a COMING SOON badge)
// ---------------------------------------------------------------------------
class Row : public juce::Component {
public:
    Row(const juce::String& label, std::unique_ptr<juce::Component> control, const juce::String& note, bool comingSoon, int height = kRowH)
        : label_(label), note_(note), soon_(comingSoon), height_(height), control_(std::move(control)) {
        if (control_) {
            addAndMakeVisible(*control_);
            control_->setEnabled(!comingSoon);
        }
    }

    int preferredHeight() const { return height_; }
    juce::Component* control() { return control_.get(); }

    void paint(juce::Graphics& g) override {
        g.setColour(soon_ ? theme::textDim : theme::text);
        g.setFont(theme::labelFont(12.0f, false));
        g.drawText(label_, 0, 0, kLabelW - 12, std::min(height_, kRowH), juce::Justification::centredLeft, true);

        auto noteArea = getLocalBounds().withLeft(kLabelW + kControlW + 16).withHeight(std::min(height_, kRowH));
        if (soon_) {
            auto badge = noteArea.removeFromLeft(96).reduced(0, 8).toFloat();
            g.setColour(theme::accentDeep);
            g.fillRoundedRectangle(badge, 3.0f);
            g.setColour(theme::readout);
            g.setFont(theme::labelFont(9.5f));
            g.drawText("COMING SOON", badge.toNearestInt(), juce::Justification::centred);
            noteArea.removeFromLeft(8);
        }
        if (note_.isNotEmpty()) {
            g.setColour(theme::textDim);
            g.setFont(theme::labelFont(11.0f, false));
            g.drawFittedText(note_, noteArea, juce::Justification::centredLeft, 2, 0.9f);
        }
    }

    void resized() override {
        if (!control_) return;
        if (height_ > kRowH) control_->setBounds(kLabelW, 4, getWidth() - kLabelW - 10, height_ - 8);       // big custom editors
        else control_->setBounds(kLabelW, 4, kControlW, height_ - 8);
    }

private:
    juce::String label_, note_;
    bool soon_;
    int height_;
    std::unique_ptr<juce::Component> control_;
};

class SectionHeader : public juce::Component {
public:
    explicit SectionHeader(const juce::String& title) : title_(title) {}
    void paint(juce::Graphics& g) override {
        g.setColour(theme::text);
        g.setFont(theme::labelFont(12.5f));
        juce::Path tri;
        tri.addTriangle(0.0f, 14.0f, 9.0f, 14.0f, 4.5f, 20.0f);
        g.setColour(theme::accent);
        g.fillPath(tri);
        g.setColour(theme::text);
        g.drawText(title_.toUpperCase(), 16, 6, getWidth() - 16, 22, juce::Justification::centredLeft);
        g.setColour(theme::accentDeep);
        g.fillRect(0, getHeight() - 4, getWidth(), 1);
    }
private:
    juce::String title_;
};

// ---------------------------------------------------------------------------
// A page: a vertical stack of sections and rows, built with small helper calls
// ---------------------------------------------------------------------------
class Page : public juce::Component {
public:
    explicit Page(Prefs& prefs) : prefs_(prefs) {}

    void section(const juce::String& title) {
        items_.push_back({std::make_unique<SectionHeader>(title), 40, true});
        addAndMakeVisible(*items_.back().comp);
    }

    Row& add(const juce::String& label, std::unique_ptr<juce::Component> control, const juce::String& note = {}, bool soon = false, int height = kRowH) {
        auto row = std::make_unique<Row>(label, std::move(control), note, soon, height);
        auto& ref = *row;
        items_.push_back({std::move(row), height, false});
        addAndMakeVisible(ref);
        return ref;
    }

    void text(const juce::String& paragraph) {
        auto l = std::make_unique<juce::Label>();
        l->setText(paragraph, juce::dontSendNotification);
        l->setFont(theme::labelFont(11.5f, false));
        l->setColour(juce::Label::textColourId, theme::textDim);
        l->setJustificationType(juce::Justification::topLeft);
        items_.push_back({std::move(l), 40, false});
        addAndMakeVisible(*items_.back().comp);
    }

    // ---- typed controls bound to a preference key ------------------------------
    void toggle(const juce::String& label, const juce::String& key, const juce::String& note = {}, bool soon = false) {
        toggleFn(label, [this, key] { return prefs_.getBool(key); }, [this, key](bool v) { prefs_.set(key, v); }, note, soon);
    }

    void toggleFn(const juce::String& label, std::function<bool()> get, std::function<void(bool)> set, const juce::String& note = {}, bool soon = false) {
        auto b = std::make_unique<juce::TextButton>();
        b->setClickingTogglesState(true);
        b->setToggleState(get(), juce::dontSendNotification);
        auto* raw = b.get();
        auto refresh = [raw] { raw->setButtonText(raw->getToggleState() ? "On" : "Off"); };
        refresh();
        b->onClick = [raw, set, refresh] { set(raw->getToggleState()); refresh(); };
        auto holder = std::make_unique<Holder>(std::move(b), 90);
        add(label, std::move(holder), note, soon);
    }

    void choice(const juce::String& label, const juce::String& key, const juce::StringArray& options, const juce::String& note = {}, bool soon = false) {
        auto box = std::make_unique<juce::ComboBox>();
        box->addItemList(options, 1);
        box->setSelectedItemIndex(juce::jlimit(0, options.size() - 1, prefs_.getInt(key)), juce::dontSendNotification);
        auto* raw = box.get();
        box->onChange = [this, key, raw] { prefs_.set(key, raw->getSelectedItemIndex()); };
        add(label, std::move(box), note, soon);
    }

    void slider(const juce::String& label, const juce::String& key, double lo, double hi, double step, const juce::String& suffix,
                const juce::String& note = {}, bool soon = false) {
        auto s = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
        s->setRange(lo, hi, step);
        s->setTextValueSuffix(suffix);
        s->setTextBoxStyle(juce::Slider::TextBoxRight, false, 78, 22);
        s->setValue(prefs_.getDouble(key), juce::dontSendNotification);
        s->setDoubleClickReturnValue(true, static_cast<double>(Prefs::defaultFor(key)));
        auto* raw = s.get();
        s->onDragEnd = [this, key, raw] { prefs_.set(key, raw->getValue()); };
        s->onValueChange = [this, key, raw] { if (!raw->isMouseButtonDown()) prefs_.set(key, raw->getValue()); };
        add(label, std::move(s), note, soon);
    }

    void folder(const juce::String& label, const juce::String& key, std::function<juce::File()> effective, const juce::String& note = {}) {
        add(label, std::make_unique<FolderPicker>(prefs_, key, std::move(effective)), note);
    }

    void button(const juce::String& label, const juce::String& text, std::function<void()> fn, const juce::String& note = {}, bool soon = false) {
        auto b = std::make_unique<juce::TextButton>(text);
        b->onClick = std::move(fn);
        add(label, std::make_unique<Holder>(std::move(b), 180), note, soon);
    }

    void custom(const juce::String& label, std::unique_ptr<juce::Component> comp, int height, const juce::String& note = {}) {
        add(label, std::move(comp), note, false, height);
    }

    int layoutHeight(int width) {
        int y = 8;
        for (auto& it : items_) {
            it.comp->setBounds(0, y, width, it.height);
            y += it.height + (it.header ? 4 : 2);
        }
        return y + 24;
    }

    void resized() override { layoutHeight(getWidth()); }

    // ---- small helpers -----------------------------------------------------------
    /** Puts a control at a fixed width inside the control column. */
    struct Holder : juce::Component {
        Holder(std::unique_ptr<juce::Component> c, int w) : comp(std::move(c)), width(w) { addAndMakeVisible(*comp); }
        void resized() override { comp->setBounds(0, 0, std::min(width, getWidth()), getHeight()); }
        void enablementChanged() override { comp->setEnabled(isEnabled()); }
        std::unique_ptr<juce::Component> comp;
        int width;
    };

    struct FolderPicker : juce::Component {
        FolderPicker(Prefs& p, juce::String k, std::function<juce::File()> eff) : prefs(p), key(std::move(k)), effective(std::move(eff)) {
            path.setReadOnly(true);
            path.setFont(theme::labelFont(11.0f, false));
            addAndMakeVisible(path);
            browse.setButtonText("...");
            browse.onClick = [this] {
                chooser = std::make_unique<juce::FileChooser>("Choose a folder", effective());
                chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& fc) {
                    if (fc.getResult().isDirectory()) { prefs.set(key, fc.getResult().getFullPathName()); refresh(); }
                });
            };
            addAndMakeVisible(browse);
            reset.setButtonText("Reset");
            reset.onClick = [this] { prefs.reset(key); refresh(); };
            addAndMakeVisible(reset);
            refresh();
        }
        void refresh() { path.setText(effective().getFullPathName(), false); }
        void resized() override {
            auto r = getLocalBounds();
            reset.setBounds(r.removeFromRight(56));
            r.removeFromRight(3);
            browse.setBounds(r.removeFromRight(30));
            r.removeFromRight(3);
            path.setBounds(r);
        }
        Prefs& prefs;
        juce::String key;
        std::function<juce::File()> effective;
        juce::TextEditor path;
        juce::TextButton browse, reset;
        std::unique_ptr<juce::FileChooser> chooser;
    };

private:
    struct Item { std::unique_ptr<juce::Component> comp; int height; bool header; };
    Prefs& prefs_;
    std::vector<Item> items_;
};

// ---------------------------------------------------------------------------
// Editable list of folders (plugin folders, sample folders)
// ---------------------------------------------------------------------------
class FolderList : public juce::Component, private juce::ListBoxModel {
public:
    FolderList(juce::StringArray fixedFolders, std::function<juce::StringArray()> get, std::function<void(const juce::File&)> addFn,
               std::function<void(const juce::String&)> removeFn)
        : fixed_(std::move(fixedFolders)), get_(std::move(get)), add_(std::move(addFn)), remove_(std::move(removeFn)) {
        list_.setModel(this);
        list_.setRowHeight(24);
        list_.setColour(juce::ListBox::backgroundColourId, theme::background);
        list_.setColour(juce::ListBox::outlineColourId, theme::border);
        list_.setOutlineThickness(1);
        addAndMakeVisible(list_);
        addButton_.setButtonText("Add folder...");
        addButton_.onClick = [this] {
            chooser_ = std::make_unique<juce::FileChooser>("Add a folder");
            chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& fc) {
                if (fc.getResult().isDirectory()) { add_(fc.getResult()); refresh(); }
            });
        };
        removeButton_.setButtonText("Remove");
        removeButton_.onClick = [this] {
            const int row = list_.getSelectedRow();
            if (row >= fixed_.size() && row < getNumRows()) { remove_(items_[row - fixed_.size()]); refresh(); }
        };
        addAndMakeVisible(addButton_);
        addAndMakeVisible(removeButton_);
        refresh();
    }

    void refresh() { items_ = get_(); list_.updateContent(); list_.repaint(); }

    void resized() override {
        auto r = getLocalBounds();
        auto buttons = r.removeFromBottom(28);
        addButton_.setBounds(buttons.removeFromLeft(130));
        buttons.removeFromLeft(4);
        removeButton_.setBounds(buttons.removeFromLeft(90));
        r.removeFromBottom(6);
        list_.setBounds(r);
    }

private:
    int getNumRows() override { return fixed_.size() + items_.size(); }
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override {
        const bool isFixed = row < fixed_.size();
        if (selected && !isFixed) { g.setColour(theme::accentDeep); g.fillRect(0, 0, w, h); }
        g.setColour(isFixed ? theme::textDim : theme::text);
        g.setFont(theme::labelFont(11.5f, false));
        const juce::String path = isFixed ? fixed_[row] : (row - fixed_.size() < items_.size() ? items_[row - fixed_.size()] : juce::String());
        g.drawText(path + (isFixed ? "   (bundled, always scanned)" : ""), 8, 0, w - 12, h, juce::Justification::centredLeft, true);
    }

    juce::StringArray fixed_;
    std::function<juce::StringArray()> get_;
    std::function<void(const juce::File&)> add_;
    std::function<void(const juce::String&)> remove_;
    juce::StringArray items_;
    juce::ListBox list_;
    juce::TextButton addButton_, removeButton_;
    std::unique_ptr<juce::FileChooser> chooser_;
};

// ---------------------------------------------------------------------------
// MIDI inputs with an on/off switch each
// ---------------------------------------------------------------------------
class MidiInputList : public juce::Component {
public:
    explicit MidiInputList(AppState& app) {
        for (auto& d : juce::MidiInput::getAvailableDevices()) {
            auto b = std::make_unique<juce::TextButton>(d.name);
            b->setClickingTogglesState(true);
            b->setToggleState(app.isMidiInputEnabled(d.identifier), juce::dontSendNotification);
            auto* raw = b.get();
            const auto id = d.identifier;
            b->onClick = [&app, raw, id] { app.setMidiInputEnabled(id, raw->getToggleState()); };
            addAndMakeVisible(*b);
            buttons_.push_back(std::move(b));
        }
        if (buttons_.empty()) {
            empty_.setText("No MIDI inputs found. Connect a controller and reopen Preferences.", juce::dontSendNotification);
            empty_.setColour(juce::Label::textColourId, theme::textDim);
            empty_.setFont(theme::labelFont(11.5f, false));
            addAndMakeVisible(empty_);
        }
    }
    int preferredHeight() const { return std::max(30, static_cast<int>(buttons_.size()) * 30); }
    void resized() override {
        int y = 0;
        for (auto& b : buttons_) { b->setBounds(0, y, std::min(getWidth(), 360), 26); y += 30; }
        empty_.setBounds(getLocalBounds());
    }
private:
    std::vector<std::unique_ptr<juce::TextButton>> buttons_;
    juce::Label empty_;
};

// ---------------------------------------------------------------------------
// Live latency readout
// ---------------------------------------------------------------------------
class LatencyReadout : public juce::Label, private juce::Timer {
public:
    explicit LatencyReadout(AppState& app) : app_(app) {
        setFont(theme::labelFont(11.5f, false));
        setColour(juce::Label::textColourId, theme::readout);
        timerCallback();
        startTimerHz(2);
    }
private:
    void timerCallback() override {
        auto* dev = app_.deviceManager.getCurrentAudioDevice();
        if (dev == nullptr) { setText("No audio device is open.", juce::dontSendNotification); return; }
        const double sr = dev->getCurrentSampleRate();
        const int buf = dev->getCurrentBufferSizeSamples();
        const int out = dev->getOutputLatencyInSamples();
        auto ms = [sr](int samples) { return juce::String(1000.0 * samples / sr, 1) + " ms"; };
        setText("Buffer " + juce::String(buf) + " samples (" + ms(buf) + ")   output " + ms(out) + "   total " + ms(buf + out),
                juce::dontSendNotification);
    }
    AppState& app_;
};

// ---------------------------------------------------------------------------
// Plugins the scanner blocked after they crashed it
// ---------------------------------------------------------------------------
class BlockedPluginList : public juce::Component {
public:
    explicit BlockedPluginList(AppState& app) : app_(app) {
        text_.setMultiLine(true);
        text_.setReadOnly(true);
        text_.setFont(theme::labelFont(11.0f, false));
        addAndMakeVisible(text_);
        clear_.setButtonText("Unblock all");
        clear_.onClick = [this] { app_.pluginHost->knownPlugins.clearBlacklistedFiles(); app_.pluginHost->saveSettings(); refresh(); };
        addAndMakeVisible(clear_);
        refresh();
    }
    void refresh() {
        const auto files = app_.pluginHost->knownPlugins.getBlacklistedFiles();
        text_.setText(files.isEmpty() ? "No plugins are blocked." : files.joinIntoString("\n"), false);
        clear_.setEnabled(!files.isEmpty());
    }
    void resized() override {
        auto r = getLocalBounds();
        clear_.setBounds(r.removeFromBottom(28).removeFromLeft(130));
        r.removeFromBottom(6);
        text_.setBounds(r);
    }
private:
    AppState& app_;
    juce::TextEditor text_;
    juce::TextButton clear_;
};

// ---------------------------------------------------------------------------
// Keyboard shortcuts: click a key, press the new one
// ---------------------------------------------------------------------------
class ShortcutEditor : public juce::Component {
public:
    explicit ShortcutEditor(Prefs& prefs) : prefs_(prefs) {
        for (auto& a : Prefs::actions()) {
            auto row = std::make_unique<KeyRow>(*this, a.id, a.name);
            addAndMakeVisible(*row);
            rows_.push_back(std::move(row));
        }
        resetAll_.setButtonText("Reset all to defaults");
        resetAll_.onClick = [this] { prefs_.resetShortcuts(); refreshAll(); status_.setText({}, juce::dontSendNotification); };
        addAndMakeVisible(resetAll_);
        status_.setFont(theme::labelFont(11.0f, false));
        status_.setColour(juce::Label::textColourId, theme::readout);
        addAndMakeVisible(status_);
    }

    int preferredHeight() const { return static_cast<int>(rows_.size()) * 30 + 44; }

    void resized() override {
        int y = 0;
        for (auto& r : rows_) { r->setBounds(0, y, getWidth(), 28); y += 30; }
        resetAll_.setBounds(0, y + 8, 190, 28);
        status_.setBounds(200, y + 8, getWidth() - 200, 28);
    }

    void refreshAll() { for (auto& r : rows_) r->refresh(); }
    void report(const juce::String& s) { status_.setText(s, juce::dontSendNotification); }
    Prefs& prefs() { return prefs_; }

private:
    struct KeyButton : juce::TextButton {
        std::function<void(const juce::KeyPress&)> onKey;
        std::function<void()> onCancel;
        bool capturing = false;
        KeyButton() { setWantsKeyboardFocus(true); }
        bool keyPressed(const juce::KeyPress& k) override {
            if (!capturing) return false;
            capturing = false;
            if (k == juce::KeyPress::escapeKey) { if (onCancel) onCancel(); }
            else if (onKey) onKey(k == juce::KeyPress::backspaceKey ? juce::KeyPress() : k);
            return true;
        }
        void focusLost(FocusChangeType) override { if (capturing) { capturing = false; if (onCancel) onCancel(); } }
    };

    struct KeyRow : juce::Component {
        KeyRow(ShortcutEditor& o, juce::String i, juce::String n) : owner(o), id(std::move(i)), name(std::move(n)) {
            key.onClick = [this] {
                key.capturing = true;
                key.setButtonText("Press a key (Esc cancels, Backspace clears)");
                key.grabKeyboardFocus();
            };
            key.onKey = [this](const juce::KeyPress& k) {
                const auto clashed = owner.prefs().setShortcut(id, k);
                owner.report(clashed.isNotEmpty() ? "\"" + clashed + "\" lost its shortcut, it was the same key." : juce::String());
                owner.refreshAll();
            };
            key.onCancel = [this] { refresh(); };
            reset.setButtonText("Default");
            reset.onClick = [this] {
                for (auto& a : Prefs::actions())
                    if (id == a.id) owner.prefs().setShortcut(id, juce::KeyPress::createFromDescription(a.defaultKey));
                owner.refreshAll();
            };
            addAndMakeVisible(key);
            addAndMakeVisible(reset);
            refresh();
        }
        void refresh() {
            const auto k = owner.prefs().shortcut(id);
            key.setButtonText(k.isValid() ? k.getTextDescriptionWithIcons() : juce::String("(none)"));
        }
        void paint(juce::Graphics& g) override {
            g.setColour(theme::text);
            g.setFont(theme::labelFont(11.5f, false));
            g.drawText(name, 0, 0, 200, getHeight(), juce::Justification::centredLeft);
        }
        void resized() override {
            key.setBounds(205, 1, std::min(230, getWidth() - 295), getHeight() - 2);
            reset.setBounds(key.getRight() + 6, 1, 80, getHeight() - 2);
        }
        ShortcutEditor& owner;
        juce::String id, name;
        KeyButton key;
        juce::TextButton reset;
    };

    Prefs& prefs_;
    std::vector<std::unique_ptr<KeyRow>> rows_;
    juce::TextButton resetAll_;
    juce::Label status_;
};

}  // namespace prefsui

// ---------------------------------------------------------------------------
// The Preferences panel: tab list on the left, the selected page on the right
// ---------------------------------------------------------------------------
class PreferencesPanel : public juce::Component {
public:
    static const juce::StringArray& tabNames() {
        static const juce::StringArray names {"General", "Audio", "MIDI", "Plugins", "Library", "Recording", "Export", "Editing", "Appearance", "Shortcuts"};
        return names;
    }

    explicit PreferencesPanel(AppState& app) : app_(app) {
        for (int i = 0; i < tabNames().size(); ++i) {
            auto b = std::make_unique<juce::TextButton>(tabNames()[i]);
            b->setClickingTogglesState(true);
            b->setRadioGroupId(91);
            b->onClick = [this, i] { showTab(i); };
            addAndMakeVisible(*b);
            tabs_.push_back(std::move(b));
        }
        viewport_.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport_);
        setSize(1060, 720);
        showTab(0);
    }

    void showTab(int index) {
        index_ = juce::jlimit(0, tabNames().size() - 1, index);
        tabs_[static_cast<size_t>(index_)]->setToggleState(true, juce::dontSendNotification);
        page_ = std::make_unique<prefsui::Page>(*app_.prefs);
        build(*page_, index_);
        viewport_.setViewedComponent(page_.get(), false);
        resized();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(theme::background);
        auto side = getLocalBounds().removeFromLeft(kSideW).toFloat().reduced(8.0f);
        g.setColour(theme::panel);
        g.fillRoundedRectangle(side, 6.0f);
        g.setColour(theme::border);
        g.drawRoundedRectangle(side, 6.0f, 1.0f);
        g.setColour(theme::text);
        g.setFont(theme::labelFont(12.5f));
        g.drawText("PREFERENCES", 22, 18, kSideW - 30, 20, juce::Justification::centredLeft);
        g.setColour(theme::text);
        g.setFont(theme::labelFont(18.0f));
        g.drawText(tabNames()[index_].toUpperCase(), kSideW + 16, 14, 400, 28, juce::Justification::centredLeft);
        g.setColour(theme::accent);
        g.fillRect(kSideW + 16, 46, 60, 2);
    }

    void resized() override {
        auto r = getLocalBounds();
        auto side = r.removeFromLeft(kSideW).reduced(18, 0);
        side.removeFromTop(50);
        for (auto& t : tabs_) { t->setBounds(side.removeFromTop(32)); side.removeFromTop(5); }
        r.removeFromTop(56);
        viewport_.setBounds(r.reduced(16, 0).withTrimmedBottom(10));
        if (page_) {
            const int w = viewport_.getMaximumVisibleWidth();
            page_->setSize(w, page_->layoutHeight(w));
        }
    }

private:
    static constexpr int kSideW = 190;

    // ---- the pages --------------------------------------------------------------------
    void build(prefsui::Page& p, int tab) {
        auto& prefs = *app_.prefs;
        switch (tab) {
            case 0: {   // General
                p.section("Startup");
                p.choice("On startup, open", "startup", {"The last project", "The demo project", "An empty project"});
                p.section("Projects");
                p.folder("Project folder", "projectFolder", [&prefs] { return prefs.projectFolder(); }, "Where Open and Save start.");
                p.choice("Autosave", "autosaveMinutes", {"Off", "Every minute", "Every 2 minutes", "Every 3 minutes", "Every 4 minutes", "Every 5 minutes",
                                                         "Every 6 minutes", "Every 7 minutes", "Every 8 minutes", "Every 9 minutes", "Every 10 minutes"},
                         "Saved as <name>.autosave.fdm next to the project.");
                p.choice("Backups to keep", "backups", {"None", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10"},
                         "Each Save keeps the previous version in a Backups folder.");
                p.section("Undo");
                p.slider("Undo history", "undoSteps", 10, 1000, 10, " steps", "Undo/redo isn't built yet.", true);
                p.section("Updates & crash reports");
                p.toggle("Check for updates on startup", "checkUpdates", "Needs an update server.", true);
                p.toggle("Send crash reports", "crashReports", "Needs a report server.", true);
                break;
            }
            case 1: {   // Audio
                p.section("Device");
                auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(app_.deviceManager, 0, 2, 2, 2, false, false, true, false);
                p.custom("Driver, devices, rate, buffer", std::move(selector), 330);
                p.button("Test tone", "Play 440 Hz for 1 second", [this] { app_.playTestTone(); }, "Checks that sound reaches your speakers.");
                p.section("Latency");
                p.custom("Current latency", std::make_unique<prefsui::LatencyReadout>(app_), prefsui::kRowH);
                p.slider("Manual offset", "latencyOffsetMs", -50.0, 50.0, 0.1, " ms", "Lines up recordings once recording exists.", true);
                p.section("Performance");
                p.choice("CPU meter", "cpuMeter", {"Percentage", "Bar", "Hidden"}, "Shown at the right of the toolbar.");
                p.toggle("Multicore processing", "multicore", "Tracks are processed on one thread for now.", true);
                break;
            }
            case 2: {   // MIDI
                p.section("Inputs");
                auto list = std::make_unique<prefsui::MidiInputList>(app_);
                const int h = list->preferredHeight();
                p.custom("Devices", std::move(list), h + 8, h <= 30 ? juce::String() : juce::String());
                p.text("Notes from every enabled input play the selected MIDI track.");
                p.section("Outputs & sync");
                p.toggle("Send MIDI clock", "midiClockSend", {}, true);
                p.toggle("Follow incoming MIDI clock", "midiClockReceive", {}, true);
                p.section("Computer keyboard");
                p.toggle("Play notes with the keyboard", "keyboardPiano", "A W S E D F T G Y H U J K play one octave.");
                p.choice("Starting octave", "keyboardOctave", {"C-1", "C0", "C1", "C2", "C3", "C4", "C5", "C6"}, "Z and X move it while you play.");
                p.slider("Velocity", "keyboardVelocity", 1, 127, 1, {});
                break;
            }
            case 3: {   // Plugins
                p.section("Folders");
                juce::StringArray bundled;
                for (auto& f : app_.pluginHost->bundledFolders()) bundled.add(f.getFullPathName());
                if (bundled.isEmpty()) bundled.add(app_.pluginHost->primaryBundledFolder().getFullPathName());
                auto& host = *app_.pluginHost;
                p.custom("Plugin folders", std::make_unique<prefsui::FolderList>(bundled, [&host] { return host.extraFolders(); },
                                                                               [&host](const juce::File& f) { host.addFolder(f); },
                                                                               [&host](const juce::String& s) { host.removeFolder(s); }), 190);
                p.toggleFn("Also scan system VST3 folders", [&host] { return host.useSystemFolders(); }, [&host](bool v) { host.setUseSystemFolders(v); },
                           "The standard VST3 folders of your OS.");
                p.section("Scanning");
                p.button("Scan now", "Rescan all folders", [this] { app_.pluginHost->scanAsync([this](int) { app_.structureHasChanged(); }); });
                p.toggle("Scan on startup", "scanOnStartup", "Otherwise only when the bundled folder has new plugins.");
                p.custom("Blocked plugins", std::make_unique<prefsui::BlockedPluginList>(app_), 120, "Plugins that crashed the scanner are skipped.");
                p.section("Plugin windows");
                p.toggle("Keep plugin windows on top", "pluginWindowsOnTop");
                p.toggle("Open editor on add", "openEditorOnLoad", "Shows the plugin window as soon as an effect is loaded.");
                p.section("Formats");
                p.toggleFn("VST3", [] { return true; }, [](bool) {}, "Always on.");
                p.toggleFn("VST2", [&host] { return host.vst2Enabled(); }, [this, &host](bool v) { host.setVst2Enabled(v); app_.structureHasChanged(); },
                           "FreeDAWm's own VST2 host (no Steinberg SDK). 64-bit plugins only. Rescan after turning it on.");
                break;
            }
            case 4: {   // Library
                p.section("Sample folders");
                p.custom("Folders", std::make_unique<prefsui::FolderList>(juce::StringArray(), [this] { return app_.sampleFolders(); },
                                                                         [this](const juce::File& f) { app_.addSampleFolder(f); },
                                                                         [this](const juce::String& s) { app_.removeSampleFolder(s); }), 190,
                         {});
                p.text("These folders appear in the Samples explorer.");
                p.section("Preview");
                p.slider("Preview volume", "previewGainDb", -40.0, 6.0, 0.5, " dB");
                p.toggle("Preview when a file is clicked", "autoPreview");
                p.toggle("Loop the preview", "loopPreview");
                break;
            }
            case 5: {   // Recording
                p.text("Recording isn't built yet. These settings are saved and will apply once it is.");
                p.section("Files");
                p.choice("File format", "recordFormat", {"WAV", "AIFF", "FLAC"}, {}, true);
                p.choice("Bit depth", "recordBitDepth", {"16-bit", "24-bit", "32-bit float"}, {}, true);
                p.section("Count-in");
                p.choice("Count-in", "countInBars", {"None", "1 bar", "2 bars", "4 bars"}, {}, true);
                break;
            }
            case 6: {   // Export
                p.section("Format");
                p.choice("File type", "exportFormatType", {"WAV"}, "More formats later.");
                p.choice("Bit depth", "exportBitDepth", {"16-bit", "24-bit", "32-bit float"});
                p.choice("Sample rate", "exportSampleRate", {"Same as audio device", "44.1 kHz", "48 kHz", "88.2 kHz", "96 kHz"});
                p.toggle("Dither (16-bit only)", "exportDither", "Adds a tiny amount of noise to hide rounding distortion.");
                p.section("Rendering");
                p.slider("Tail after the last clip", "exportTail", 0.0, 10.0, 0.1, " s", "Lets reverbs and releases ring out.");
                p.toggle("Normalize", "exportNormalize");
                p.slider("Normalize to", "exportNormalizeDb", -12.0, 0.0, 0.1, " dBFS");
                p.section("Destination");
                p.folder("Export folder", "exportFolder", [&prefs] { return prefs.exportFolder(); }, "Defaults to the project folder.");
                p.toggle("Show export options every time", "exportAsk", "Off: File > Export goes straight to the file name.");
                break;
            }
            case 7: {   // Editing
                p.section("Timeline");
                p.choice("Snap", "timelineSnap", {"Adaptive (follows zoom)", "Bar", "Beat", "1/8", "1/16", "Off"}, "Hold Alt while dragging for 1/16.");
                p.toggle("Follow the playhead", "followPlayhead", "Scrolls the timeline while playing.");
                p.choice("Mouse wheel", "wheelZoom", {"Ctrl + wheel zooms", "Wheel zooms, Shift + wheel scrolls"});
                p.section("MIDI clips");
                p.choice("New clip length", "defaultClipBars", {"1 bar", "2 bars", "3 bars", "4 bars", "5 bars", "6 bars", "7 bars", "8 bars"});
                p.slider("Default velocity", "defaultVelocity", 1, 127, 1, {}, "For new notes in the piano roll.");
                p.choice("Name new clips", "clipNaming", {"Clip 1, Clip 2, ...", "After the track", "Track name + number"});
                p.section("Audio clips");
                p.slider("Edge fade", "audioFadeMs", 0.0, 50.0, 0.1, " ms", "Short fades at clip edges prevent clicks.");
                p.choice("When the tempo changes", "tempoFollow", {"Keep the original speed", "Stretch to the new tempo"}, "Stretching isn't built yet.", true);
                break;
            }
            case 8: {   // Appearance
                p.section("Theme");
                p.choice("Accent colour", "accent", theme::accentNames(), "Applies after a restart.");
                p.choice("Contrast", "contrast", {"Normal", "High"}, "Applies after a restart.");
                p.section("Size");
                p.choice("UI scale", "uiScale", {"80%", "90%", "100%", "110%", "125%", "150%", "175%", "200%"});
                p.choice("Track height", "trackHeight", {"Small", "Medium", "Large"});
                p.section("Meters");
                p.toggle("Peak hold", "peakHold", "Mixer meters keep the peak for 1.5 s.");
                p.choice("Meter colours", "meterColours", {"Green, amber, red", "Accent colour"});
                p.section("Help");
                p.toggle("Tooltips", "tooltips");
                break;
            }
            case 9: {   // Shortcuts
                p.section("Keyboard shortcuts");
                auto editor = std::make_unique<prefsui::ShortcutEditor>(prefs);
                const int h = editor->preferredHeight();
                p.text("Click a shortcut, then press the new key. Esc cancels, Backspace clears.");
                p.custom("Actions", std::move(editor), h + 8);
                break;
            }
            default: break;
        }
    }

    AppState& app_;
    std::vector<std::unique_ptr<juce::TextButton>> tabs_;
    juce::Viewport viewport_;
    std::unique_ptr<prefsui::Page> page_;
    int index_ = 0;
};

class PreferencesWindow : public juce::DocumentWindow {
public:
    explicit PreferencesWindow(AppState& app)
        : juce::DocumentWindow("Preferences", theme::background, juce::DocumentWindow::closeButton) {
        setUsingNativeTitleBar(true);
        setContentOwned(new PreferencesPanel(app), true);
        setResizable(true, false);
        setResizeLimits(860, 520, 2000, 1600);
        centreWithSize(getWidth(), getHeight());
    }
    void closeButtonPressed() override { setVisible(false); }
    void showTab(int i) { if (auto* p = dynamic_cast<PreferencesPanel*>(getContentComponent())) p->showTab(i); }
};
