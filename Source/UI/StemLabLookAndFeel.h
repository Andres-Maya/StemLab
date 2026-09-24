#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace stemlab
{
namespace Palette
{
    inline const juce::Colour background { 0xff121419 };
    inline const juce::Colour panel      { 0xff1b1e25 };
    inline const juce::Colour panelLight { 0xff252932 };
    inline const juce::Colour outline    { 0xff343945 };
    inline const juce::Colour text       { 0xffe4e6eb };
    inline const juce::Colour textDim    { 0xff8b919c };
    inline const juce::Colour accent     { 0xff4fc3f7 };
    inline const juce::Colour mute       { 0xfff0b429 };
    inline const juce::Colour solo       { 0xff5ccb7a };
    inline const juce::Colour record     { 0xffe5484d };
}

/** Color de una pista según su nombre (stems conocidos) o su posición. */
juce::Colour trackColourFor (const juce::String& trackName, int index);

/** Tema oscuro de StemLab. */
class StemLabLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    StemLabLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
};
}
