#include "WaveformView.h"

#include "StemLabLookAndFeel.h"

namespace stemlab
{
WaveformView::WaveformView (const AudioTrack& audioTrack, juce::AudioFormatManager& formatManager,
                            juce::AudioThumbnailCache& cache)
    : track (audioTrack),
      thumbnail (512, formatManager, cache)
{
    const auto& audio = track.getAudio();
    thumbnail.reset (audio.getNumChannels(), track.getSampleRate(), audio.getNumSamples());
    thumbnail.addBlock (0, audio, 0, audio.getNumSamples());

    setOpaque (true);
    setBufferedToImage (true);
}

void WaveformView::setWaveColour (juce::Colour newColour)
{
    waveColour = newColour;
    repaint();
}

void WaveformView::setTimelineLength (double seconds)
{
    if (! juce::exactlyEqual (timelineLength, seconds))
    {
        timelineLength = juce::jmax (1.0, seconds);
        repaint();
    }
}

void WaveformView::setDimmed (bool shouldBeDimmed)
{
    if (dimmed != shouldBeDimmed)
    {
        dimmed = shouldBeDimmed;
        repaint();
    }
}

void WaveformView::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);

    const auto pixelsPerSecond = static_cast<double> (getWidth()) / timelineLength;
    const auto startSeconds = static_cast<double> (track.getStartSample()) / track.getSampleRate();

    const auto clip = juce::Rectangle<double> (startSeconds * pixelsPerSecond, 3.0,
                                               track.getLengthInSeconds() * pixelsPerSecond,
                                               static_cast<double> (getHeight()) - 6.0).toFloat();

    g.setColour (waveColour.withAlpha (dimmed ? 0.05f : 0.12f));
    g.fillRoundedRectangle (clip, 4.0f);

    g.setColour (dimmed ? waveColour.withAlpha (0.3f) : waveColour);
    thumbnail.drawChannels (g, clip.reduced (0.0f, 2.0f).toNearestInt(), 0.0, thumbnail.getTotalLength(), 1.0f);
}

void WaveformView::mouseDown (const juce::MouseEvent& event)
{
    if (onSeek != nullptr && getWidth() > 0)
        onSeek (juce::jmax (0.0, static_cast<double> (event.x) / getWidth() * timelineLength));
}

void WaveformView::mouseDrag (const juce::MouseEvent& event)
{
    mouseDown (event);
}
}
