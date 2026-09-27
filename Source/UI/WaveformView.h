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

      - Clic: mueve el cabezal ahí (también sobre un clip, que además queda
        seleccionado). Lo que se grabe o pegue va a continuación del audio que
        haya en el cabezal.
      - Arrastrar el centro: desplaza el clip.
      - Arrastrar un borde: recorta (reduce) el clip por ese lado.
      En una pista los fragmentos no se solapan. Al mover, un clip se detiene
      contra su vecino hasta que su centro pasa del centro del vecino; entonces
      salta al otro lado, y si ahí no cabe, los de delante se apartan. Al
      recortar, el borde se detiene en el vecino.
      - Clic derecho: menú de edición.

    Animación al mover: el clip que se arrastra se "levanta" (sube un poco, con
    sombra) y sigue al ratón por encima de los demás, que se oscurecen debajo;
    los que se apartan o vuelven se deslizan, y al soltar el clip baja hasta su
    sitio. Es solo visual: la pista ya tiene la posición final en todo momento.

    Cada archivo de audio tiene un juce::AudioThumbnail (resumen min/max) que
    comparten todos los clips que salen de él.
*/
class WaveformView final : public juce::Component,
                           private juce::Timer
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
    /** Al soltar tras mover o recortar: la lista de clips de antes de empezar
        y el nombre de la edición (para deshacerla). */
    std::function<void (std::vector<AudioClip> clipsBefore, const juce::String& actionName)> onClipsEdited;

    /** Rueda del ratón (zoom con Ctrl, desplazamiento con Shift). Devuelve true si la usó;
        si no, la rueda desplaza la lista de pistas en vertical. */
    std::function<bool (int x, const juce::MouseEvent&, const juce::MouseWheelDetails&)> onWheel;

    /** Estado de la animación (para las pruebas): dónde se dibuja el clip, en
        muestras, y cuánto está levantado el que se arrastra (0 = apoyado). */
    double getDrawnStart (juce::uint32 clipId) const;
    float getLiftAmount() const noexcept    { return lift; }

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
    float xForPosition (double sample, double sampleRate) const;

    void drawClip (juce::Graphics&, const AudioClip&, double drawnStart, float liftAmount) const;
    double shownStartOf (const AudioClip&) const;

    /** Tras cambiar los clips: los que ya se veían se deslizan a su sitio nuevo. */
    void syncShownStarts();
    void startAnimation();
    void timerCallback() override;
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
    std::vector<AudioClip> clipsBeforeDrag;
    int dragStartX = 0;
    double clickSeconds = 0.0;
    bool dragChanged = false;

    // Animación (solo visual). Posiciones en muestras de la línea de tiempo.
    std::map<juce::uint32, double> shownStarts;     // dónde se dibuja cada clip; se acerca a su posición real
    juce::uint32 floatingClip = 0;                  // clip que se arrastra: sigue al ratón, sin topes
    double floatingStart = 0.0;
    juce::uint32 topClip = 0;                       // se dibuja encima (arrastrándose o bajando al soltar)
    float lift = 0.0f;                              // 0 = apoyado, 1 = levantado
    double lastFrameMs = 0.0;
};
}
