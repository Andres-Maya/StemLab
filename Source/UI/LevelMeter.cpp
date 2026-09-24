#include "LevelMeter.h"

#include "StemLabLookAndFeel.h"

namespace stemlab
{
LevelMeter::LevelMeter (PeakReader reader)
    : readPeak (std::move (reader))
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

void LevelMeter::timerCallback()
{
    bool changed = false;

    for (int ch = 0; ch < 2; ++ch)
    {
        const auto peak = readPeak != nullptr ? readPeak (ch) : 0.0f;
        const auto level = juce::jmax (peak, levels[static_cast<size_t> (ch)] * 0.85f);

        if (std::abs (level - levels[static_cast<size_t> (ch)]) > 0.001f)
        {
            levels[static_cast<size_t> (ch)] = level < 0.001f ? 0.0f : level;
            changed = true;
        }
    }

    if (changed)
        repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto vertical = bounds.getHeight() > bounds.getWidth();
    constexpr float gap = 1.0f;

    for (size_t ch = 0; ch < 2; ++ch)
    {
        auto bar = vertical ? bounds.withWidth ((bounds.getWidth() - gap) * 0.5f)
                                    .withX (bounds.getX() + static_cast<float> (ch) * (bounds.getWidth() + gap) * 0.5f)
                            : bounds.withHeight ((bounds.getHeight() - gap) * 0.5f)
                                    .withY (bounds.getY() + static_cast<float> (ch) * (bounds.getHeight() + gap) * 0.5f);

        g.setColour (Palette::panelLight);
        g.fillRect (bar);

        // Escala en dB: -60 dB abajo, 0 dB arriba.
        const auto db = juce::Decibels::gainToDecibels (levels[ch], -60.0f);
        const auto proportion = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);

        if (proportion <= 0.0f)
            continue;

        const auto colour = db > -3.0f ? Palette::record : (db > -12.0f ? Palette::mute : Palette::solo);
        g.setColour (colour);

        if (vertical)
            g.fillRect (bar.removeFromBottom (bar.getHeight() * proportion));
        else
            g.fillRect (bar.removeFromLeft (bar.getWidth() * proportion));
    }
}
}
