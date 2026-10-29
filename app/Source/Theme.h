#pragma once

#include "Common.h"

namespace theme {

inline const juce::Colour background  { 0xff14161b };
inline const juce::Colour panel       { 0xff1c1f26 };
inline const juce::Colour panelRaised { 0xff272b35 };
inline const juce::Colour panelHover  { 0xff323847 };
inline const juce::Colour border      { 0xff353b4a };
inline const juce::Colour text        { 0xffdde1ea };
inline const juce::Colour textDim     { 0xff8b93a6 };
inline const juce::Colour accent      { 0xffff9a3c };
inline const juce::Colour play        { 0xff58d68d };
inline const juce::Colour warn        { 0xffff5f5f };
inline const juce::Colour gridBar     { 0xff3a4152 };
inline const juce::Colour gridBeat    { 0xff2c3240 };
inline const juce::Colour gridStep    { 0xff232832 };

/** Eight channel colours; a channel's colourIndex picks one. */
inline juce::Colour channelColour(int index) {
    static const juce::Colour colours[] = {
        juce::Colour(0xffff7a59), juce::Colour(0xff4fb0ff), juce::Colour(0xff9b7bff), juce::Colour(0xff4cd6a0),
        juce::Colour(0xffffc84a), juce::Colour(0xffff6fb5), juce::Colour(0xff45d0d6), juce::Colour(0xffb6e35a)};
    return colours[((index % 8) + 8) % 8];
}

// Layout
constexpr int kTransportHeight = 46;
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

        g.setColour(theme::panelRaised);
        g.fillEllipse(juce::Rectangle<float>(0.0f, 0.0f, (arcRadius - thickness) * 2.0f, (arcRadius - thickness) * 2.0f)
                          .withCentre(centre));
        juce::Path pointer;
        const float len = arcRadius - thickness - 2.0f;
        pointer.addRoundedRectangle(-1.2f, -len, 2.4f, len * 0.55f, 1.0f);
        g.setColour(theme::text);
        g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));
    }
};
