#include "IconButton.h"

namespace stemlab
{
IconButton::IconButton (const juce::String& name, Icon initialIcon)
    : juce::Button (name), icon (initialIcon)
{
    setTooltip (name);
}

void IconButton::setIcon (Icon newIcon)
{
    if (icon != newIcon)
    {
        icon = newIcon;
        repaint();
    }
}

void IconButton::setActiveColour (juce::Colour newColour)
{
    activeColour = newColour;
    repaint();
}

void IconButton::paintButton (juce::Graphics& g, bool isHighlighted, bool isDown)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const auto on = getToggleState();

    auto background = on ? activeColour.withAlpha (0.25f) : Palette::panelLight;

    if (isDown)
        background = background.brighter (0.15f);
    else if (isHighlighted)
        background = background.brighter (0.08f);

    g.setColour (background);
    g.fillRoundedRectangle (bounds, 5.0f);

    const auto size = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.42f;
    const auto area = juce::Rectangle<float> (size, size).withCentre (bounds.getCentre());
    const auto iconColour = on || icon == Icon::record ? activeColour : Palette::text;

    g.setColour (iconColour.withAlpha (isEnabled() ? 1.0f : 0.35f));

    juce::Path path;

    switch (icon)
    {
        case Icon::play:
            path.addTriangle (area.getX(), area.getY(), area.getX(), area.getBottom(),
                              area.getRight(), area.getCentreY());
            break;

        case Icon::pause:
            path.addRectangle (area.withWidth (area.getWidth() * 0.35f));
            path.addRectangle (area.withTrimmedLeft (area.getWidth() * 0.65f));
            break;

        case Icon::stop:
            path.addRectangle (area.reduced (area.getWidth() * 0.05f));
            break;

        case Icon::record:
            path.addEllipse (area);
            break;

        case Icon::toStart:
            path.addRectangle (area.withWidth (area.getWidth() * 0.18f));
            path.addTriangle (area.getRight(), area.getY(), area.getRight(), area.getBottom(),
                              area.getX() + area.getWidth() * 0.2f, area.getCentreY());
            break;

        case Icon::close:
        {
            const auto cross = area.reduced (area.getWidth() * 0.15f);
            g.drawLine ({ cross.getTopLeft(), cross.getBottomRight() }, 1.8f);
            g.drawLine ({ cross.getTopRight(), cross.getBottomLeft() }, 1.8f);
            return;
        }
    }

    g.fillPath (path);
}
}
