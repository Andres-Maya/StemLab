#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "Audio/AudioTrack.h"

#include <functional>
#include <map>

namespace stemlab
{
/**
    Carril de una pista: dibuja sus clips sobre la línea de tiempo compartida y
    permite editarlos con el ratón.

      - Clic en un clip: lo selecciona.       Clic en zona vacía: mueve el cabezal.
      - Arrastrar el centro: desplaza el clip.
      - Arrastrar un borde: recorta (reduce) el clip por ese lado.
      - Clic derecho: menú de edición.

    Cada archivo de audio tiene un juce::AudioThumbnail (resumen min/max) que
    comparten todos los clips que salen de él.
*/
class WaveformView final : public juce::Component
{
public:
    WaveformView (AudioTrack& track, juce::AudioFormatManager& formatManager, juce::AudioThumbnailCache& cache);

    void setWaveColour (juce::Colour newColour);

    /** Tramo de la línea de tiempo que se ve (zoom y desplazamiento). */
    void setVisibleRange (double startSeconds, double lengthSeconds);
    void setDimmed (bool shouldBeDimmed);
    void setSelectedClip (juce::uint32 clipId);

    /** Volver a leer los clips de la pista (tras una edición o una grabación). */
    void clipsChanged();

    std::function<void (double seconds)> onSeek;
    std::function<void (juce::uint32 clipId)> onClipClicked;                   // 0 = zona vacía
    std::function<void (juce::uint32 clipId, double seconds)> onContextMenu;   // 0 = zona vacía
    std::function<void()> onClipsEdited;                                       // al soltar tras mover o recortar

    /** Rueda del ratón (zoom con Ctrl, desplazamiento con Shift). Devuelve true si la usó;
        si no, la rueda desplaza la lista de pistas en vertical. */
    std::function<bool (int x, const juce::MouseEvent&, const juce::MouseWheelDetails&)> onWheel;

    void paint (juce::Graphics&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    enum class DragMode { none, seek, move, trimStart, trimEnd };

    struct ClipHit
    {
        juce::uint32 clipId = 0;
        DragMode mode = DragMode::seek;
    };

    ClipHit findClipAt (int x) const;
    double secondsForX (int x) const;
    float xForSample (juce::int64 sample, double sampleRate) const;
    juce::AudioThumbnail* thumbnailFor (const ClipSource* source) const;

    AudioTrack& track;
    juce::AudioFormatManager& formatManager;
    juce::AudioThumbnailCache& cache;

    std::vector<AudioClip> clips;   // copia para dibujar y editar
    std::map<const ClipSource*, std::unique_ptr<juce::AudioThumbnail>> thumbnails;
    std::map<const ClipSource*, float> verticalZooms;   // solo visual: amplía el dibujo del audio muy bajo

    juce::Colour waveColour { 0xff4fc3f7 };
    double visibleStart = 0.0;
    double visibleLength = 60.0;
    bool dimmed = false;
    juce::uint32 selectedClip = 0;

    DragMode dragMode = DragMode::none;
    AudioClip dragOriginal;
    int dragStartX = 0;
    bool dragChanged = false;
};
}
