#include "TimeRuler.h"

#include "StemLabLookAndFeel.h"

#include <cmath>

namespace stemlab
{
TimeRuler::TimeRuler()
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void TimeRuler::setVisibleRange (double startSeconds, double lengthSeconds)
{
    visibleStart = juce::jmax (0.0, startSeconds);
    visibleLength = juce::jmax (0.01, lengthSeconds);
    repaint();
}

void TimeRuler::setPlayheadSeconds (double seconds)
{
    if (! juce::exactlyEqual (playheadSeconds, seconds))
    {
        playheadSeconds = seconds;
        repaint();
    }
}

void TimeRuler::paint (juce::Graphics& g)
{
    g.fillAll (Palette::panel);

    const auto width = static_cast<double> (getWidth());
    const auto pixelsPerSecond = width / visibleLength;

    // Intervalo de marcas: el más pequeño que deja al menos ~70 px entre etiquetas.
    double step = 600.0;

    for (const auto candidate : { 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0 })
    {
        if (candidate * pixelsPerSecond >= 70.0)
        {
            step = candidate;
            break;
        }
    }

    g.setFont (juce::FontOptions (11.0f));

    const auto firstTick = std::ceil (visibleStart / step) * step;

    for (auto t = firstTick; t <= visibleStart + visibleLength; t += step)
    {
        const auto x = static_cast<float> ((t - visibleStart) * pixelsPerSecond);
        const auto minutes = static_cast<int> (t / 60.0);
        const auto seconds = t - minutes * 60.0;

        // Con zoom alto se muestran décimas: 0:01.5
        const auto secondsText = step < 1.0 ? juce::String (seconds, 1).paddedLeft ('0', 4)
                                            : juce::String (juce::roundToInt (seconds)).paddedLeft ('0', 2);

        g.setColour (Palette::outline);
        g.drawVerticalLine (juce::roundToInt (x), static_cast<float> (getHeight()) * 0.55f, static_cast<float> (getHeight()));

        g.setColour (Palette::textDim);
        g.drawText (juce::String (minutes) + ":" + secondsText,
                    juce::Rectangle<float> (x + 3.0f, 0.0f, 60.0f, static_cast<float> (getHeight()) * 0.7f),
                    juce::Justification::centredLeft, false);
    }

    // Marca del cabezal (solo si está dentro de lo visible).
    const auto playheadX = static_cast<float> ((playheadSeconds - visibleStart) * pixelsPerSecond);

    if (playheadX >= -5.0f && playheadX <= static_cast<float> (getWidth()) + 5.0f)
    {
        juce::Path marker;
        marker.addTriangle (playheadX - 5.0f, 0.0f, playheadX + 5.0f, 0.0f, playheadX, 7.0f);
        g.setColour (Palette::accent);
        g.fillPath (marker);
    }

    g.setColour (Palette::outline);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, static_cast<float> (getWidth()));
}

void TimeRuler::mouseDown (const juce::MouseEvent& event)
{
    if (onSeek != nullptr && getWidth() > 0)
        onSeek (juce::jmax (0.0, visibleStart + static_cast<double> (event.x) / getWidth() * visibleLength));
}

void TimeRuler::mouseDrag (const juce::MouseEvent& event)
{
    mouseDown (event);
}

void TimeRuler::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (onWheel == nullptr || ! onWheel (event.x, event, wheel))
        Component::mouseWheelMove (event, wheel);
}
}
