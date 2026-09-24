#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "StemLabLookAndFeel.h"

namespace stemlab
{
/** Botón con icono vectorial (no depende de que la fuente tenga ▶ ⏹ ⏺). */
class IconButton final : public juce::Button
{
public:
    enum class Icon { toStart, play, pause, stop, record, close };

    IconButton (const juce::String& name, Icon icon);

    void setIcon (Icon newIcon);
    void setActiveColour (juce::Colour newColour);

    void paintButton (juce::Graphics&, bool isHighlighted, bool isDown) override;

private:
    Icon icon;
    juce::Colour activeColour { Palette::accent };
};
}
