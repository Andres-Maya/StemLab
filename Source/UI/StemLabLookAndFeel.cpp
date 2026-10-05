#include "StemLabLookAndFeel.h"

#include <iterator>

namespace stemlab
{
namespace Palette
{
    namespace
    {
        Theme currentTheme = Theme::dark;
    }

    Theme getTheme()    { return currentTheme; }

    void setTheme (Theme theme)
    {
        currentTheme = theme;
        const auto light = theme == Theme::light;

        // El claro, sin blancos puros y con los colores más oscuros que en el
        // tema oscuro: sobre un fondo claro, los tonos vivos distraen.
        background = juce::Colour (light ? 0xffe8ebf0 : 0xff121419);
        panel      = juce::Colour (light ? 0xfff4f5f8 : 0xff1b1e25);
        panelLight = juce::Colour (light ? 0xffdbe0e8 : 0xff252932);
        outline    = juce::Colour (light ? 0xffb9c1ce : 0xff343945);
        text       = juce::Colour (light ? 0xff1b1f27 : 0xffe4e6eb);
        textDim    = juce::Colour (light ? 0xff566070 : 0xff8b919c);
        accent     = juce::Colour (light ? 0xff0a6aa6 : 0xff4fc3f7);
        mute       = juce::Colour (light ? 0xffc48a0a : 0xfff0b429);
        solo       = juce::Colour (light ? 0xff3a9d5b : 0xff5ccb7a);
        highlight  = juce::Colour (light ? 0xff1b1f27 : 0xffffffff);
    }

    juce::Colour onBackground (juce::Colour colour)
    {
        return currentTheme == Theme::light && ! colour.isTransparent() ? colour.darker (0.75f) : colour;
    }

    juce::Colour emphasised (juce::Colour colour, float amount)
    {
        return currentTheme == Theme::light ? colour.darker (amount * 0.6f) : colour.brighter (amount);
    }
}

juce::Colour trackColourFor (const juce::String& trackName, int index)
{
    // Con el nombre en cualquier idioma de la interfaz (ver Utils/Localisation.h).
    struct NamedColour
    {
        const char* spanish;
        const char* english;
        juce::uint32 argb;
    };

    static const NamedColour named[] {
        { "Voz",       "Vocals",    0xffff6b9d },
        { "Batería",   "Drums",     0xffffa94d },
        { "Bajo",      "Bass",      0xffb197fc },
        { "Guitarra",  "Guitar",    0xffff6b6b },
        { "Piano",     "Piano",     0xffffd43b },
        { "Otros",     "Other",     0xff38d9a9 },
        { "Grabación", "Recording", 0xff748ffc },     // índigo: el rojo queda solo para "grabando"
    };

    for (const auto& entry : named)
        if (trackName.startsWith (juce::String::fromUTF8 (entry.spanish)) || trackName.startsWith (entry.english))
            return juce::Colour (entry.argb);

    static const juce::uint32 fallback[] { 0xff4fc3f7, 0xff94d82d, 0xfff783ac, 0xff74c0fc, 0xffffc078, 0xff63e6be };
    return juce::Colour (fallback[static_cast<size_t> (juce::jmax (0, index)) % std::size (fallback)]);
}

StemLabLookAndFeel::StemLabLookAndFeel()
{
    applyTheme();
}

void StemLabLookAndFeel::applyTheme()
{
    setColourScheme (juce::LookAndFeel_V4::ColourScheme (Palette::background, Palette::panel, Palette::panel,
                                                         Palette::outline, Palette::text, Palette::panelLight,
                                                         Palette::background, Palette::accent, Palette::text));

    setColour (juce::ResizableWindow::backgroundColourId, Palette::background);

    setColour (juce::Label::textColourId, Palette::text);

    setColour (juce::TextButton::buttonColourId, Palette::panelLight);
    setColour (juce::TextButton::buttonOnColourId, Palette::accent);
    setColour (juce::TextButton::textColourOffId, Palette::text);
    setColour (juce::TextButton::textColourOnId, Palette::background);
    setColour (juce::ToggleButton::tickColourId, Palette::accent);

    setColour (juce::Slider::thumbColourId, Palette::text);
    setColour (juce::Slider::trackColourId, Palette::accent);
    setColour (juce::Slider::backgroundColourId, Palette::outline);
    setColour (juce::Slider::rotarySliderFillColourId, Palette::accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, Palette::outline);
    setColour (juce::Slider::textBoxTextColourId, Palette::textDim);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

    setColour (juce::ComboBox::backgroundColourId, Palette::panelLight);
    setColour (juce::ComboBox::outlineColourId, Palette::outline);
    setColour (juce::PopupMenu::backgroundColourId, Palette::panel);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::accent.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, Palette::text);

    setColour (juce::ProgressBar::backgroundColourId, Palette::panelLight);
    setColour (juce::ProgressBar::foregroundColourId, Palette::accent);

    setColour (juce::TooltipWindow::backgroundColourId, Palette::panelLight);
    setColour (juce::TooltipWindow::textColourId, Palette::text);
    setColour (juce::TooltipWindow::outlineColourId, Palette::outline);
}

void StemLabLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                           juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto lineWidth = juce::jmax (2.0f, radius * 0.16f);
    const auto arcRadius = radius - lineWidth * 0.5f;
    const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // Controles bipolares (paneo, ganancias de EQ): el arco nace en el cero.
    const auto min = slider.getMinimum();
    const auto max = slider.getMaximum();
    auto fromAngle = rotaryStartAngle;

    if (min < 0.0 && max > 0.0)
        fromAngle = rotaryStartAngle + static_cast<float> (slider.valueToProportionOfLength (0.0))
                                         * (rotaryEndAngle - rotaryStartAngle);

    const auto enabled = slider.isEnabled();
    const juce::PathStrokeType stroke (lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (slider.findColour (juce::Slider::rotarySliderOutlineColourId));
    g.strokePath (track, stroke);

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                         juce::jmin (fromAngle, angle), juce::jmax (fromAngle, angle), true);
    g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId).withAlpha (enabled ? 1.0f : 0.4f));
    g.strokePath (value, stroke);

    const auto pointerLength = arcRadius * 0.6f;
    const juce::Point<float> tip (centre.x + pointerLength * std::sin (angle),
                                  centre.y - pointerLength * std::cos (angle));
    g.setColour (Palette::text.withAlpha (enabled ? 0.9f : 0.4f));
    g.drawLine ({ centre, tip }, lineWidth * 0.8f);
}
}
