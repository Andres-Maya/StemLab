#include "TimeRuler.h"

#include "StemLabLookAndFeel.h"

namespace stemlab
{
TimeRuler::TimeRuler()
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void TimeRuler::setTimelineLength (double seconds)
{
    timelineLength = juce::jmax (1.0, seconds);
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
    const auto pixelsPerSecond = width / timelineLength;

    // Intervalo de marcas: el más pequeño que deja al menos ~70 px entre etiquetas.
    double step = 600.0;

    for (const auto candidate : { 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0 })
    {
        if (candidate * pixelsPerSecond >= 70.0)
        {
            step = candidate;
            break;
        }
    }

    g.setFont (juce::FontOptions (11.0f));

    for (double t = 0.0; t <= timelineLength; t += step)
    {
        const auto x = static_cast<float> (t * pixelsPerSecond);
        const auto totalSeconds = juce::roundToInt (t);

        g.setColour (Palette::outline);
        g.drawVerticalLine (juce::roundToInt (x), static_cast<float> (getHeight()) * 0.55f, static_cast<float> (getHeight()));

        g.setColour (Palette::textDim);
        g.drawText (juce::String (totalSeconds / 60) + ":" + juce::String (totalSeconds % 60).paddedLeft ('0', 2),
                    juce::Rectangle<float> (x + 3.0f, 0.0f, 60.0f, static_cast<float> (getHeight()) * 0.7f),
                    juce::Justification::centredLeft, false);
    }

    // Marca del cabezal.
    const auto playheadX = static_cast<float> (playheadSeconds * pixelsPerSecond);
    juce::Path marker;
    marker.addTriangle (playheadX - 5.0f, 0.0f, playheadX + 5.0f, 0.0f, playheadX, 7.0f);
    g.setColour (Palette::accent);
    g.fillPath (marker);

    g.setColour (Palette::outline);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, static_cast<float> (getWidth()));
}

void TimeRuler::mouseDown (const juce::MouseEvent& event)
{
    if (onSeek != nullptr && getWidth() > 0)
        onSeek (juce::jlimit (0.0, timelineLength, static_cast<double> (event.x) / getWidth() * timelineLength));
}

void TimeRuler::mouseDrag (const juce::MouseEvent& event)
{
    mouseDown (event);
}
}
