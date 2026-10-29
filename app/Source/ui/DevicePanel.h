#pragma once

#include "../AppState.h"
#include "../Theme.h"
#include "../Widgets.h"

namespace devices {

inline void paintCard(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title, juce::Colour accent) {
    auto r = bounds.toFloat().reduced(2.0f);
    g.setColour(theme::panelRaised);
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(accent);
    g.fillRoundedRectangle(r.removeFromTop(3.0f), 1.5f);
    g.setColour(theme::text);
    g.setFont(juce::Font(juce::FontOptions(12.5f, juce::Font::bold)));
    g.drawText(title, bounds.reduced(12, 0).withHeight(26).translated(0, 3), juce::Justification::centredLeft, true);
}

}  // namespace devices

/** Built-in instrument: a grid of knobs (synth) or pad selector + knobs (drum rack). */
class InstrumentCard : public juce::Component {
public:
    InstrumentCard(AppState& app, fdm::Channel& channel) : channel_(channel) {
        (void)app;
        colour_ = theme::channelColour(channel.colorIndex.load());
        drum_ = channel.instrumentType.load() == static_cast<int>(fdm::InstrumentType::DrumRack);
        auto& params = channel.instrument().params();

        if (drum_) {
            for (int p = 0; p < fdm::DrumRack::kNumPads; ++p) {
                auto b = std::make_unique<juce::TextButton>(fdm::DrumRack::padName(p));
                b->setClickingTogglesState(true);
                b->setRadioGroupId(77);
                b->setColour(juce::TextButton::buttonOnColourId, colour_);
                b->setColour(juce::TextButton::textColourOnId, theme::background);
                b->onClick = [this, p] { showPad(p); };
                b->onStateChange = nullptr;
                addAndMakeVisible(*b);
                padButtons_.push_back(std::move(b));
            }
            for (const char* suffix : {"vol", "tune", "decay", "pan"}) padSuffixes_.push_back(suffix);
            masterKnob_ = std::make_unique<ParamKnob>(*params.find("volume"), "Volume", colour_);
            addAndMakeVisible(*masterKnob_);
            padButtons_[0]->setToggleState(true, juce::dontSendNotification);
            showPad(0);
        } else {
            for (int i = 0; i < params.size(); ++i) {
                auto& p = params.at(i);
                knobs_.push_back(std::make_unique<ParamKnob>(p, shortName(p), colour_));
                addAndMakeVisible(*knobs_.back());
            }
        }
    }

    int preferredWidth() const { return drum_ ? 420 : 12 + 10 * 60 + 12; }

    void paint(juce::Graphics& g) override {
        devices::paintCard(g, getLocalBounds(), juce::String(channel_.instrument().name()), colour_);
    }

    void resized() override {
        auto r = getLocalBounds().reduced(8, 0);
        r.removeFromTop(32);
        r.removeFromBottom(6);
        if (drum_) {
            auto pads = r.removeFromLeft(190);
            const int bw = pads.getWidth() / 2, bh = pads.getHeight() / 4;
            for (int i = 0; i < static_cast<int>(padButtons_.size()); ++i)
                padButtons_[static_cast<size_t>(i)]->setBounds(pads.getX() + (i % 2) * bw, pads.getY() + (i / 2) * bh, bw, bh)
                    ,padButtons_[static_cast<size_t>(i)]->setBounds(padButtons_[static_cast<size_t>(i)]->getBounds().reduced(2));
            r.removeFromLeft(10);
            const int kw = std::min(58, r.getWidth() / 5);
            for (auto& k : padKnobs_) k->setBounds(r.removeFromLeft(kw).withHeight(std::min(r.getHeight(), 84)));
            if (masterKnob_) masterKnob_->setBounds(r.removeFromLeft(kw).withHeight(std::min(r.getHeight(), 84)));
        } else {
            const int kw = 60, kh = std::max(60, r.getHeight() / 2);
            for (int i = 0; i < static_cast<int>(knobs_.size()); ++i)
                knobs_[static_cast<size_t>(i)]->setBounds(r.getX() + (i % 10) * kw, r.getY() + (i / 10) * kh, kw, kh - 2);
        }
    }

private:
    static juce::String shortName(const fdm::Param& p) {
        juce::String n(p.name());
        n = n.replace("Amp ", "A.").replace("Flt ", "F.").replace("Osc ", "O").replace("Sustain", "Sus").replace("Attack", "Atk")
                .replace("Release", "Rel").replace("Decay", "Dec").replace("Resonance", "Reso").replace("Key Track", "Keytrk")
                .replace("Pulse Width", "Pulse").replace("Filter Env", "Env Amt");
        return n;
    }

    void showPad(int pad) {
        padKnobs_.clear();
        auto& params = channel_.instrument().params();
        const char* labels[] = {"Level", "Tune", "Decay", "Pan"};
        for (size_t i = 0; i < padSuffixes_.size(); ++i) {
            auto* p = params.find("pad" + std::to_string(pad) + "." + padSuffixes_[i]);
            if (p == nullptr) continue;
            padKnobs_.push_back(std::make_unique<ParamKnob>(*p, labels[i], colour_));
            addAndMakeVisible(*padKnobs_.back());
        }
        resized();
    }

    fdm::Channel& channel_;
    juce::Colour colour_;
    bool drum_ = false;
    std::vector<std::unique_ptr<ParamKnob>> knobs_, padKnobs_;
    std::vector<std::unique_ptr<juce::TextButton>> padButtons_;
    std::vector<std::string> padSuffixes_;
    std::unique_ptr<ParamKnob> masterKnob_;
};

/** One FX slot: empty (click to add), or a loaded VST3 with bypass / edit / remove. */
class FxCard : public juce::Component {
public:
    FxCard(AppState& app, fdm::Strip& strip, int slot) : app_(app), strip_(strip), slot_(slot) {
        effect_ = app_.effectAt(strip_, slot_);
        if (effect_ != nullptr) {
            power_ = std::make_unique<IconButton>(IconButton::Icon::Power, theme::play);
            power_->setClickingTogglesState(true);
            power_->setToggleState(!strip_.fx[static_cast<size_t>(slot_)].bypass.load(), juce::dontSendNotification);
            power_->onClick = [this] { strip_.fx[static_cast<size_t>(slot_)].bypass.store(!power_->getToggleState()); };
            edit_ = std::make_unique<IconButton>(IconButton::Icon::Edit);
            edit_->onClick = [this] { app_.showEffectEditor(strip_, slot_); };
            remove_ = std::make_unique<IconButton>(IconButton::Icon::Cross, theme::warn);
            remove_->onClick = [this] {
                const int slot = slot_;
                juce::MessageManager::callAsync([&app = app_, &strip = strip_, slot] { app.clearEffect(strip, slot); });
            };
            for (auto* b : {power_.get(), edit_.get(), remove_.get()}) addAndMakeVisible(b);
        }
    }

    void paint(juce::Graphics& g) override {
        if (effect_ != nullptr) {
            devices::paintCard(g, getLocalBounds(), juce::String(effect_->name()), theme::accent);
            g.setColour(theme::textDim);
            g.setFont(juce::Font(juce::FontOptions(11.0f)));
            g.drawFittedText("VST3 effect\nLatency " + juce::String(effect_->latencySamples()) + " samples",
                             getLocalBounds().reduced(14, 0).withTrimmedTop(40).withTrimmedBottom(40), juce::Justification::topLeft, 3);
        } else {
            auto r = getLocalBounds().toFloat().reduced(3.0f);
            g.setColour(theme::border);
            const float dashes[] = {5.0f, 4.0f};
            juce::Path p;
            p.addRoundedRectangle(r, 6.0f);
            juce::Path dashed;
            juce::PathStrokeType(1.2f).createDashedStroke(dashed, p, dashes, 2);
            g.fillPath(dashed);
            g.setColour(theme::textDim);
            g.setFont(juce::Font(juce::FontOptions(12.0f)));
            g.drawText("FX " + juce::String(slot_ + 1), r.toNearestInt().withTrimmedBottom(20), juce::Justification::centred);
            g.drawText("+ Add effect", r.toNearestInt().withTrimmedTop(20), juce::Justification::centred);
        }
    }

    void resized() override {
        if (effect_ == nullptr) return;
        auto r = getLocalBounds().reduced(10, 0);
        auto row = r.removeFromBottom(36).reduced(0, 4);
        const int w = row.getWidth() / 3;
        power_->setBounds(row.removeFromLeft(w).reduced(3, 0));
        edit_->setBounds(row.removeFromLeft(w).reduced(3, 0));
        remove_->setBounds(row.reduced(3, 0));
    }

    void mouseDown(const juce::MouseEvent&) override {
        if (effect_ != nullptr) return;
        showAddMenu();
    }

private:
    void showAddMenu() {
        auto effects = app_.pluginHost->availableEffects();
        juce::PopupMenu m;
        if (effects.isEmpty()) {
            m.addItem(-1, "No VST3 effects found yet", false);
        } else {
            for (int i = 0; i < effects.size(); ++i) m.addItem(i + 1, effects[i].name + (effects[i].manufacturerName.isNotEmpty() ? "   (" + effects[i].manufacturerName + ")" : ""));
        }
        m.addSeparator();
        m.addItem(1000, "Scan for plugins...");

        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this, effects, safe = juce::Component::SafePointer<FxCard>(this)](int id) {
            if (safe == nullptr || id <= 0) return;
            if (id == 1000) {
                app_.pluginHost->scanAsync([this](int) { app_.structureHasChanged(); });
                return;
            }
            const auto error = app_.loadEffect(strip_, slot_, effects[id - 1]);
            if (error.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Couldn't load plugin", error);
        });
    }

    AppState& app_;
    fdm::Strip& strip_;
    int slot_;
    Vst3Effect* effect_ = nullptr;
    std::unique_ptr<IconButton> power_, edit_, remove_;
};

/** The "Device" tab: instrument (channels only) followed by the four FX slots. */
class DevicePanel : public juce::Component, private AppState::Listener, private juce::AsyncUpdater {
public:
    explicit DevicePanel(AppState& app) : app_(app) {
        viewport_.setViewedComponent(&content_, false);
        viewport_.setScrollBarsShown(false, true);
        addAndMakeVisible(viewport_);
        addAndMakeVisible(title_);
        title_.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
        app_.addListener(this);
        rebuild();
    }

    ~DevicePanel() override { app_.removeListener(this); cancelPendingUpdate(); }

    void paint(juce::Graphics& g) override { g.fillAll(theme::panel); }

    void resized() override {
        auto r = getLocalBounds().reduced(8, 6);
        title_.setBounds(r.removeFromTop(24));
        r.removeFromTop(4);
        viewport_.setBounds(r);
        layoutCards();
    }

private:
    void selectionChanged() override { triggerAsyncUpdate(); }
    void structureChanged() override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override { rebuild(); }

    void rebuild() {
        instrument_.reset();
        fx_.clear();

        auto* strip = app_.selectedStrip();
        if (strip == nullptr) return;
        title_.setText(juce::String(strip->name), juce::dontSendNotification);

        if (auto* ch = app_.selectedChannel()) {
            instrument_ = std::make_unique<InstrumentCard>(app_, *ch);
            content_.addAndMakeVisible(*instrument_);
        }
        for (int s = 0; s < fdm::kMaxFxSlots; ++s) {
            fx_.push_back(std::make_unique<FxCard>(app_, *strip, s));
            content_.addAndMakeVisible(*fx_.back());
        }
        layoutCards();
    }

    void layoutCards() {
        const int h = std::max(120, viewport_.getMaximumVisibleHeight());
        int x = 0;
        if (instrument_) { const int w = instrument_->preferredWidth(); instrument_->setBounds(x, 0, w, h); x += w + 6; }
        for (auto& f : fx_) { f->setBounds(x, 0, 170, h); x += 176; }
        content_.setSize(std::max(x, viewport_.getMaximumVisibleWidth()), h);
    }

    struct Content : juce::Component {};

    AppState& app_;
    juce::Label title_;
    juce::Viewport viewport_;
    Content content_;
    std::unique_ptr<InstrumentCard> instrument_;
    std::vector<std::unique_ptr<FxCard>> fx_;
};
