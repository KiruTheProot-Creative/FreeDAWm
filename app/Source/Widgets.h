#pragma once

#include "Theme.h"

// ---------------------------------------------------------------------------
// Value formatting for engine parameters
// ---------------------------------------------------------------------------
inline juce::String formatParamValue(const fdm::Param& p, float v) {
    const juce::String id(p.id());

    if (id.endsWith(".wave")) {
        static const char* names[] = {"Saw", "Square", "Tri", "Sine"};
        return names[juce::jlimit(0, 3, static_cast<int>(std::lround(v)))];
    }
    if (id == "cutoff")
        return v >= 1000.0f ? juce::String(v / 1000.0f, 2) + " kHz" : juce::String(juce::roundToInt(v)) + " Hz";
    if (id.endsWith(".a") || id.endsWith(".d") || id.endsWith(".r"))
        return v < 1.0f ? juce::String(juce::roundToInt(v * 1000.0f)) + " ms" : juce::String(v, 2) + " s";
    if (id.endsWith(".s") || id == "env.amount" || id == "keytrack" || id == "osc2.level" || id == "resonance")
        return juce::String(juce::roundToInt(v * 100.0f)) + "%";
    if (id == "volume" || id.endsWith(".vol"))
        return juce::String(v, 1) + " dB";
    if (id == "osc2.semi" || id.endsWith(".tune"))
        return (v > 0.0f ? "+" : "") + juce::String(juce::roundToInt(v)) + " st";
    if (id == "osc2.fine")
        return (v > 0.0f ? "+" : "") + juce::String(juce::roundToInt(v)) + " ct";
    if (id.endsWith(".pan")) {
        const int pct = juce::roundToInt(std::abs(v) * 100.0f);
        return pct == 0 ? juce::String("C") : juce::String(pct) + (v < 0.0f ? " L" : " R");
    }
    if (id.endsWith(".decay"))
        return juce::String(v, 2) + "x";
    if (id == "pulse")
        return juce::String(juce::roundToInt(v * 100.0f)) + "%";
    return juce::String(v, 2);
}

inline float paramFromNormalised(const fdm::Param& p, double n) {
    n = juce::jlimit(0.0, 1.0, n);
    float v = p.isLogarithmic() ? p.min() * std::pow(p.max() / p.min(), static_cast<float>(n))
                                : p.min() + static_cast<float>(n) * (p.max() - p.min());
    if (p.isStepped()) v = std::round(v);
    return v;
}

inline double normalisedFromParamValue(const fdm::Param& p, float v) {
    return p.isLogarithmic() ? std::log(v / p.min()) / std::log(p.max() / p.min())
                             : (v - p.min()) / (p.max() - p.min());
}

// ---------------------------------------------------------------------------
// Icon button
// ---------------------------------------------------------------------------
class IconButton : public juce::Button {
public:
    enum class Icon { Play, Stop, Plus, Cross, Power, Edit };

    explicit IconButton(Icon icon, juce::Colour onColour = theme::accent)
        : juce::Button("icon"), icon_(icon), onColour_(onColour) {}

    void paintButton(juce::Graphics& g, bool over, bool down) override {
        auto r = getLocalBounds().toFloat().reduced(1.0f);
        const bool on = getToggleState();
        g.setColour(on ? onColour_ : (down || over ? theme::panelHover : theme::panelRaised));
        g.fillRoundedRectangle(r, 4.0f);

        const auto c = r.getCentre();
        const float s = juce::jmin(r.getWidth(), r.getHeight()) * 0.30f;
        g.setColour(on ? theme::background : theme::text);

        switch (icon_) {
            case Icon::Play: {
                juce::Path p;
                p.addTriangle(c.x - s * 0.8f, c.y - s, c.x - s * 0.8f, c.y + s, c.x + s, c.y);
                g.fillPath(p);
                break;
            }
            case Icon::Stop:
                g.fillRoundedRectangle(c.x - s * 0.9f, c.y - s * 0.9f, s * 1.8f, s * 1.8f, 1.5f);
                break;
            case Icon::Plus:
                g.fillRect(c.x - s, c.y - 1.2f, s * 2.0f, 2.4f);
                g.fillRect(c.x - 1.2f, c.y - s, 2.4f, s * 2.0f);
                break;
            case Icon::Cross:
                g.drawLine(c.x - s * 0.8f, c.y - s * 0.8f, c.x + s * 0.8f, c.y + s * 0.8f, 2.0f);
                g.drawLine(c.x - s * 0.8f, c.y + s * 0.8f, c.x + s * 0.8f, c.y - s * 0.8f, 2.0f);
                break;
            case Icon::Power: {
                juce::Path arc;
                arc.addCentredArc(c.x, c.y + 0.5f, s, s, 0.0f, 0.65f, juce::MathConstants<float>::twoPi - 0.65f, true);
                g.strokePath(arc, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.drawLine(c.x, c.y - s * 1.1f, c.x, c.y - 0.5f, 2.0f);
                break;
            }
            case Icon::Edit:
                g.drawRoundedRectangle(c.x - s, c.y - s * 0.8f, s * 2.0f, s * 1.6f, 2.0f, 1.6f);
                g.fillRect(c.x - s, c.y - s * 0.8f, s * 2.0f, 3.0f);
                break;
        }
    }

private:
    Icon icon_;
    juce::Colour onColour_;
};

// ---------------------------------------------------------------------------
// A knob bound to an engine Param, with a caption underneath
// ---------------------------------------------------------------------------
class ParamKnob : public juce::Component {
public:
    ParamKnob(fdm::Param& param, const juce::String& caption, juce::Colour colour = theme::accent)
        : param_(param) {
        slider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider_.setRange(0.0, 1.0, 0.0);
        slider_.setValue(normalisedFromParamValue(param, param.get()), juce::dontSendNotification);
        slider_.setDoubleClickReturnValue(true, normalisedFromParamValue(param, param.defaultValue()));
        slider_.setColour(juce::Slider::rotarySliderFillColourId, colour);
        slider_.setPopupDisplayEnabled(true, true, nullptr);
        slider_.setMouseDragSensitivity(180);
        slider_.textFromValueFunction = [this](double n) { return formatParamValue(param_, paramFromNormalised(param_, n)); };
        slider_.onValueChange = [this] {
            param_.set(paramFromNormalised(param_, slider_.getValue()));
            if (param_.isStepped())
                slider_.setValue(normalisedFromParamValue(param_, param_.get()), juce::dontSendNotification);
            valueLabel_.setText(formatParamValue(param_, param_.get()), juce::dontSendNotification);
        };
        addAndMakeVisible(slider_);

        caption_.setText(caption, juce::dontSendNotification);
        caption_.setJustificationType(juce::Justification::centred);
        caption_.setFont(juce::Font(juce::FontOptions(11.0f)));
        caption_.setColour(juce::Label::textColourId, theme::textDim);
        caption_.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(caption_);

        valueLabel_.setText(formatParamValue(param_, param_.get()), juce::dontSendNotification);
        valueLabel_.setJustificationType(juce::Justification::centred);
        valueLabel_.setFont(juce::Font(juce::FontOptions(10.5f)));
        valueLabel_.setColour(juce::Label::textColourId, theme::text);
        valueLabel_.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(valueLabel_);
    }

    void resized() override {
        auto r = getLocalBounds();
        caption_.setBounds(r.removeFromBottom(13));
        valueLabel_.setBounds(r.removeFromBottom(12));
        slider_.setBounds(r);
    }

private:
    fdm::Param& param_;
    juce::Slider slider_;
    juce::Label caption_, valueLabel_;
};

// ---------------------------------------------------------------------------
// Vertical fader with a stereo level meter, bound to a mixer strip
// ---------------------------------------------------------------------------
class Fader : public juce::Component, private juce::Timer {
public:
    explicit Fader(fdm::Strip& strip) : strip_(strip) {
        range_.setSkewForCentre(-20.0);
        startTimerHz(30);
    }

    std::function<void()> onChange;

    void paint(juce::Graphics& g) override {
        auto b = getLocalBounds().toFloat();
        const float meterW = 6.0f;
        auto meterArea = b.removeFromRight(meterW * 2.0f + 4.0f).reduced(0.0f, 4.0f);
        auto faderArea = b.reduced(0.0f, 4.0f);

        // Meter
        for (int ch = 0; ch < 2; ++ch) {
            auto bar = meterArea.removeFromLeft(meterW);
            meterArea.removeFromLeft(2.0f);
            g.setColour(theme::background);
            g.fillRoundedRectangle(bar, 2.0f);
            const float lin = ch == 0 ? dispL_ : dispR_;
            const float pos = lin <= 0.0001f ? 0.0f : static_cast<float>(posForDb(fdm::gainToDb(lin)));
            auto fill = bar.withTop(bar.getBottom() - bar.getHeight() * pos);
            g.setColour(lin > 1.0f ? theme::warn : (lin > 0.7f ? theme::accent : theme::play));
            g.fillRoundedRectangle(fill, 2.0f);
        }

        // 0 dB tick and groove
        const float travelTop = faderArea.getY() + kThumbH * 0.5f;
        const float travelBot = faderArea.getBottom() - kThumbH * 0.5f;
        const float cx = faderArea.getCentreX();
        const float zeroY = travelBot - static_cast<float>(posForDb(0.0)) * (travelBot - travelTop);
        g.setColour(theme::border);
        g.fillRoundedRectangle(cx - 1.5f, travelTop, 3.0f, travelBot - travelTop, 1.5f);
        g.setColour(theme::textDim);
        g.fillRect(cx - 12.0f, zeroY - 0.5f, 24.0f, 1.0f);

        // Thumb
        const double db = strip_.volumeDb.load();
        const float y = travelBot - static_cast<float>(posForDb(db)) * (travelBot - travelTop);
        auto thumb = juce::Rectangle<float>(cx - 11.0f, y - kThumbH * 0.5f, 22.0f, kThumbH);
        g.setColour(isMouseOverOrDragging() ? theme::text : theme::text.withAlpha(0.85f));
        g.fillRoundedRectangle(thumb, 3.0f);
        g.setColour(theme::background);
        g.fillRect(thumb.getX() + 3.0f, thumb.getCentreY() - 0.5f, thumb.getWidth() - 6.0f, 1.0f);
    }

    void mouseDown(const juce::MouseEvent& e) override { setFromMouse(e); }
    void mouseDrag(const juce::MouseEvent& e) override { setFromMouse(e); }
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void mouseDoubleClick(const juce::MouseEvent&) override { setDb(0.0); }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override {
        const double step = e.mods.isShiftDown() ? 0.1 : 0.5;
        setDb(juce::jlimit(-70.0, 6.0, static_cast<double>(strip_.volumeDb.load()) + (w.deltaY > 0 ? step : -step)));
    }

private:
    static constexpr float kThumbH = 16.0f;

    double posForDb(double db) const {
        return db <= -69.5 ? 0.0 : range_.convertTo0to1(juce::jlimit(-70.0, 6.0, db));
    }

    void setDb(double db) {
        strip_.volumeDb.store(db <= -69.0 ? -90.0f : static_cast<float>(juce::jlimit(-70.0, 6.0, db)));
        repaint();
        if (onChange) onChange();
    }

    void setFromMouse(const juce::MouseEvent& e) {
        const float top = 4.0f + kThumbH * 0.5f;
        const float bot = static_cast<float>(getHeight()) - 4.0f - kThumbH * 0.5f;
        const double pos = juce::jlimit(0.0, 1.0, static_cast<double>((bot - e.position.y) / (bot - top)));
        setDb(pos < 0.004 ? -90.0 : range_.convertFrom0to1(pos));
    }

    void timerCallback() override {
        const float l = strip_.takePeakL(), r = strip_.takePeakR();
        const float nl = juce::jmax(l, dispL_ * 0.84f), nr = juce::jmax(r, dispR_ * 0.84f);
        const bool changed = std::abs(nl - dispL_) > 0.0005f || std::abs(nr - dispR_) > 0.0005f;
        dispL_ = nl < 0.0003f ? 0.0f : nl;
        dispR_ = nr < 0.0003f ? 0.0f : nr;
        if (changed) repaint();
    }

    fdm::Strip& strip_;
    juce::NormalisableRange<double> range_ { -70.0, 6.0 };
    float dispL_ = 0.0f, dispR_ = 0.0f;
};

inline juce::String dbText(float db) {
    return db <= -69.0f ? juce::String("-inf") : juce::String(db, 1);
}
