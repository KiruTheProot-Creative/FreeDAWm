#pragma once

#include "Common.h"

namespace theme {

inline const juce::Colour background  { 0xff09090b };   // window / inset readouts
inline const juce::Colour panel       { 0xff131316 };
inline const juce::Colour panelRaised { 0xff202126 };   // button face
inline const juce::Colour panelHover  { 0xff2d2e35 };
inline const juce::Colour border      { 0xff32333a };
inline const juce::Colour text        { 0xffe8e8ec };
inline const juce::Colour textDim     { 0xff8e909b };
inline const juce::Colour accent      { 0xffe02b2b };   // Emulon-style red
inline const juce::Colour accentHi    { 0xffff4a4a };
inline const juce::Colour accentDeep  { 0xff6d0f12 };
inline const juce::Colour readout     { 0xffff3b3b };   // red digital text
inline const juce::Colour play        { 0xff58d68d };
inline const juce::Colour warn        { 0xffff5f5f };
inline const juce::Colour gridBar     { 0xff3a3b44 };
inline const juce::Colour gridBeat    { 0xff26272e };
inline const juce::Colour gridStep    { 0xff1b1c21 };

/** Eight channel colours; a channel's colourIndex picks one. */
inline juce::Colour channelColour(int index) {
    static const juce::Colour colours[] = {
        juce::Colour(0xffe5483f), juce::Colour(0xff4b8fe8), juce::Colour(0xff9b6df0), juce::Colour(0xff3fbf8a),
        juce::Colour(0xffe8a93a), juce::Colour(0xffe0589f), juce::Colour(0xff39b5c0), juce::Colour(0xff9bc94a)};
    return colours[((index % 8) + 8) % 8];
}

/** Small bold capitals with a little extra spacing: the label style used everywhere. */
inline juce::Font labelFont(float size, bool bold = true) {
    return juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain)).withExtraKerningFactor(0.09f);
}

// Layout
constexpr int kTransportHeight = 58;
constexpr int kExplorerWidth   = 270;
constexpr int kTrackHeaderW    = 200;
constexpr int kRulerHeight     = 30;
constexpr int kTrackHeight     = 66;
constexpr int kChannelWidth    = 116;
constexpr int kSceneWidth      = 62;
constexpr int kReturnWidth     = 96;
constexpr int kMasterWidth     = 108;
constexpr int kHeaderHeight    = 40;
constexpr int kSlotHeight      = 28;
constexpr int kStopHeight      = 22;

}  // namespace theme

/** Dark theme with rotary knobs drawn as arcs. */
class FdmLookAndFeel : public juce::LookAndFeel_V4 {
public:
    FdmLookAndFeel() {
        setColourScheme(juce::LookAndFeel_V4::ColourScheme(
            theme::background, theme::panelRaised, theme::panelRaised, theme::border, theme::text,
            theme::accent, theme::background, theme::accent, theme::text));

        setColour(juce::ResizableWindow::backgroundColourId, theme::background);
        setColour(juce::Slider::thumbColourId, theme::accent);
        setColour(juce::Slider::rotarySliderFillColourId, theme::accent);
        setColour(juce::Slider::rotarySliderOutlineColourId, theme::border);
        setColour(juce::Slider::trackColourId, theme::accent);
        setColour(juce::Slider::backgroundColourId, theme::panelRaised);
        setColour(juce::Slider::textBoxTextColourId, theme::text);
        setColour(juce::Slider::textBoxBackgroundColourId, theme::panelRaised);
        setColour(juce::Slider::textBoxOutlineColourId, theme::border);
        setColour(juce::TextButton::buttonColourId, theme::panelRaised);
        setColour(juce::TextButton::buttonOnColourId, theme::accent);
        setColour(juce::TextButton::textColourOffId, theme::text);
        setColour(juce::TextButton::textColourOnId, theme::background);
        setColour(juce::ComboBox::backgroundColourId, theme::panelRaised);
        setColour(juce::ComboBox::outlineColourId, theme::border);
        setColour(juce::ComboBox::textColourId, theme::text);
        setColour(juce::ComboBox::arrowColourId, theme::textDim);
        setColour(juce::PopupMenu::backgroundColourId, theme::panelRaised);
        setColour(juce::PopupMenu::textColourId, theme::text);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, theme::accent);
        setColour(juce::PopupMenu::highlightedTextColourId, theme::background);
        setColour(juce::Label::textColourId, theme::text);
        setColour(juce::TextEditor::backgroundColourId, theme::background);
        setColour(juce::TextEditor::textColourId, theme::text);
        setColour(juce::TextEditor::outlineColourId, theme::border);
        setColour(juce::TextEditor::focusedOutlineColourId, theme::accent);
        setColour(juce::ScrollBar::thumbColourId, theme::border.brighter(0.2f));
        setColour(juce::AlertWindow::backgroundColourId, theme::panel);
        setColour(juce::AlertWindow::textColourId, theme::text);
        setColour(juce::AlertWindow::outlineColourId, theme::border);
    }

    // ---- glossy dark buttons with spaced capitals (Emulon X look) ------------------
    static juce::Font labelFont(float size, bool bold = true) { return theme::labelFont(size, bold); }

    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override {
        auto r = b.getLocalBounds().toFloat().reduced(0.5f);
        const bool on = b.getToggleState();
        auto top = on ? theme::accentHi.darker(0.1f) : theme::panelRaised.brighter(0.18f);
        auto bot = on ? theme::accentDeep.brighter(0.35f) : theme::panelRaised.darker(0.35f);
        if (over && !on) { top = top.brighter(0.12f); bot = bot.brighter(0.1f); }
        if (down) { std::swap(top, bot); }
        if (!b.isEnabled()) { top = top.withMultipliedSaturation(0.2f).darker(0.4f); bot = bot.darker(0.4f); }
        g.setGradientFill(juce::ColourGradient(top, 0.0f, r.getY(), bot, 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle(r, 5.0f);
        g.setColour(on ? theme::accentHi.withAlpha(0.8f) : juce::Colours::black.withAlpha(0.7f));
        g.drawRoundedRectangle(r, 5.0f, 1.0f);
        g.setColour(juce::Colours::white.withAlpha(on ? 0.22f : 0.07f));
        g.drawHorizontalLine(static_cast<int>(r.getY()) + 1, r.getX() + 4.0f, r.getRight() - 4.0f);
    }

    juce::Font getTextButtonFont(juce::TextButton&, int height) override { return labelFont(juce::jmin(12.0f, height * 0.42f)); }

    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override {
        g.setFont(getTextButtonFont(b, b.getHeight()));
        g.setColour(b.findColour(b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId)
                        .withMultipliedAlpha(b.isEnabled() ? 1.0f : 0.45f));
        g.drawFittedText(b.getButtonText().toUpperCase(), b.getLocalBounds().reduced(4, 1), juce::Justification::centred, 1, 0.9f);
    }

    void drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override {
        auto r = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)).reduced(0.5f);
        g.setGradientFill(juce::ColourGradient(theme::panelRaised.brighter(0.15f), 0.0f, r.getY(), theme::panelRaised.darker(0.35f), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle(r, 5.0f);
        g.setColour(juce::Colours::black.withAlpha(0.7f));
        g.drawRoundedRectangle(r, 5.0f, 1.0f);
        juce::Path arrow;
        const float cx = static_cast<float>(w) - 14.0f, cy = static_cast<float>(h) * 0.5f;
        arrow.addTriangle(cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
        g.setColour(box.isEnabled() ? theme::accentHi : theme::textDim);
        g.fillPath(arrow);
    }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return labelFont(11.5f); }
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override {
        label.setBounds(8, 1, box.getWidth() - 26, box.getHeight() - 2);
        label.setFont(getComboBoxFont(box));
    }
    juce::Font getPopupMenuFont() override { return labelFont(12.5f, false); }

    // ---- horizontal sliders: dark groove with a red fill -----------------------------
    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                          juce::Slider::SliderStyle style, juce::Slider& s) override {
        if (style != juce::Slider::LinearHorizontal) { LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, pos, 0, 0, style, s); return; }
        auto groove = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y) + h * 0.5f - 3.0f, static_cast<float>(w), 6.0f);
        g.setColour(theme::background);
        g.fillRoundedRectangle(groove, 3.0f);
        g.setColour(juce::Colours::black);
        g.drawRoundedRectangle(groove, 3.0f, 1.0f);
        const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0 && std::abs(s.getMinimum() + s.getMaximum()) < 1.0e-6;
        const float from = bipolar ? groove.getCentreX() : groove.getX();
        auto fill = juce::Rectangle<float>(juce::jmin(from, pos), groove.getY(), std::abs(pos - from), groove.getHeight());
        g.setColour(s.findColour(juce::Slider::trackColourId));
        g.fillRoundedRectangle(fill, 3.0f);
        auto thumb = juce::Rectangle<float>(8.0f, 14.0f).withCentre({pos, groove.getCentreY()});
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xffd8d8de), 0.0f, thumb.getY(), juce::Colour(0xff8c8e98), 0.0f, thumb.getBottom(), false));
        g.fillRoundedRectangle(thumb, 2.0f);
        g.setColour(theme::accentDeep);
        g.fillRect(thumb.getX() + 1.5f, thumb.getCentreY() - 0.5f, thumb.getWidth() - 3.0f, 1.0f);
    }

    // ---- tabs ---------------------------------------------------------------------------
    int getTabButtonBestWidth(juce::TabBarButton& b, int) override {
        juce::GlyphArrangement ga;
        ga.addLineOfText(labelFont(12.0f), b.getButtonText().toUpperCase(), 0.0f, 0.0f);
        return static_cast<int>(std::ceil(ga.getBoundingBox(0, -1, true).getWidth())) + 28;
    }
    void drawTabButton(juce::TabBarButton& b, juce::Graphics& g, bool over, bool) override {
        auto r = b.getLocalBounds().toFloat().reduced(1.0f, 2.0f);
        const bool on = b.isFrontTab();
        g.setGradientFill(juce::ColourGradient(on ? theme::accentHi.darker(0.15f) : theme::panelRaised.brighter(over ? 0.3f : 0.15f), 0.0f, r.getY(),
                                               on ? theme::accentDeep.brighter(0.3f) : theme::panelRaised.darker(0.35f), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle(r, 5.0f);
        g.setColour(juce::Colours::black.withAlpha(0.7f));
        g.drawRoundedRectangle(r, 5.0f, 1.0f);
        g.setFont(labelFont(12.0f));
        g.setColour(on ? theme::text : theme::textDim);
        g.drawText(b.getButtonText().toUpperCase(), b.getLocalBounds(), juce::Justification::centred);
    }
    void drawTabbedButtonBarBackground(juce::TabbedButtonBar&, juce::Graphics& g) override { g.fillAll(theme::panel); }
    void drawTabAreaBehindFrontButton(juce::TabbedButtonBar&, juce::Graphics&, int, int) override {}

    int getDefaultScrollbarWidth() override { return 10; }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                          float startAngle, float endAngle, juce::Slider& slider) override {
        const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                                   static_cast<float>(width), static_cast<float>(height)).reduced(2.0f);
        const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float angle = startAngle + sliderPos * (endAngle - startAngle);
        const float arcRadius = radius - 2.0f;
        const float thickness = juce::jmax(2.5f, radius * 0.16f);

        juce::Path track;
        track.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
        g.setColour(slider.findColour(juce::Slider::rotarySliderOutlineColourId));
        g.strokePath(track, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Bipolar controls (pan, tune) fill from the middle; others fill from the left.
        const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0
                             && std::abs(slider.getMinimum() + slider.getMaximum()) < 1.0e-6;
        const float fillFrom = bipolar ? startAngle + 0.5f * (endAngle - startAngle) : startAngle;
        juce::Path value;
        value.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, fillFrom, angle, true);
        g.setColour(slider.isEnabled() ? slider.findColour(juce::Slider::rotarySliderFillColourId) : theme::textDim);
        g.strokePath(value, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        auto body = juce::Rectangle<float>(0.0f, 0.0f, (arcRadius - thickness) * 2.0f, (arcRadius - thickness) * 2.0f).withCentre(centre);
        g.setGradientFill(juce::ColourGradient(theme::panelRaised.brighter(0.25f), 0.0f, body.getY(), theme::background, 0.0f, body.getBottom(), false));
        g.fillEllipse(body);
        g.setColour(juce::Colours::black);
        g.drawEllipse(body, 1.0f);
        juce::Path pointer;
        const float len = arcRadius - thickness - 2.0f;
        pointer.addRoundedRectangle(-1.2f, -len, 2.4f, len * 0.55f, 1.0f);
        g.setColour(theme::accentHi);
        g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));
    }
};
