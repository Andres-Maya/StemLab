#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "Audio/AudioTrack.h"

#include <functional>

namespace stemlab
{
/**
    Forma de onda de una pista sobre la línea de tiempo compartida.

    Usa juce::AudioThumbnail (resumen min/max por bloques) a partir del audio
    que ya está en memoria, y se cachea como imagen: redibujar el cabezal de
    reproducción encima no vuelve a pintar la forma de onda.
*/
class WaveformView final : public juce::Component
{
public:
    WaveformView (const AudioTrack& track, juce::AudioFormatManager& formatManager,
                  juce::AudioThumbnailCache& cache);

    void setWaveColour (juce::Colour newColour);
    void setTimelineLength (double seconds);
    void setDimmed (bool shouldBeDimmed);

    /** Clic o arrastre sobre la forma de onda: segundos en la línea de tiempo. */
    std::function<void (double seconds)> onSeek;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    const AudioTrack& track;
    juce::AudioThumbnail thumbnail;
    juce::Colour waveColour { 0xff4fc3f7 };
    double timelineLength = 60.0;
    bool dimmed = false;
};
}
