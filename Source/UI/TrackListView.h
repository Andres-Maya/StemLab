#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "Audio/AudioEngine.h"
#include "TimeRuler.h"
#include "TrackView.h"

#include <memory>
#include <optional>
#include <vector>

namespace stemlab
{
/**
    Zona de pistas: controles de zoom + regla de tiempo + lista desplazable de
    TrackView + cabezal + barra de desplazamiento horizontal.

    Todas las pistas comparten el mismo tramo visible de la línea de tiempo
    (visibleStart / visibleLength). Zoom: Ctrl + rueda (alrededor del ratón) o
    los botones -, + y Ajustar. Desplazamiento: Shift + rueda, rueda horizontal
    o la barra inferior. Al reproducir, la vista sigue al cabezal.

    Guarda la selección: pista seleccionada y, dentro de ella, el clip
    seleccionado (por su id).
*/
class TrackListView final : public juce::Component,
                            private juce::Timer,
                            private juce::ScrollBar::Listener
{
public:
    explicit TrackListView (AudioEngine& engine);
    ~TrackListView() override;

    /** Sincroniza las filas con las pistas del mezclador (conserva las existentes). */
    void refresh();

    std::shared_ptr<AudioTrack> getSelectedTrack() const   { return selected.lock(); }
    juce::uint32 getSelectedClipId() const noexcept        { return selectedClip; }

    void selectTrack (const std::shared_ptr<AudioTrack>& track);
    void selectClip (const std::shared_ptr<AudioTrack>& track, juce::uint32 clipId);

    // Zoom (también desde el menú / teclado)
    void zoomIn();
    void zoomOut();
    void zoomToFit();

    std::function<void (std::shared_ptr<AudioTrack>)> onSelectionChanged;
    std::function<void (AudioTrack&)> onDeleteRequested;
    std::function<void (std::shared_ptr<AudioTrack>, juce::uint32 clipId, double seconds)> onContextMenu;
    std::function<void()> onClipsEdited;
    std::function<void (int insertIndex)> onAddTrack;       // -1 = al final

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    /** Contenedor de las filas; si no hay pistas muestra una indicación. */
    struct Content final : public juce::Component
    {
        void paint (juce::Graphics&) override;
        bool isEmpty = true;
    };

    /** "+" con su línea arriba del todo cuando todavía no hay pistas. */
    struct EmptyAddButton final : public juce::Component,
                                  public juce::SettableTooltipClient
    {
        static constexpr int height = 26;

        void paint (juce::Graphics&) override;
        bool hitTest (int x, int y) override;
        void mouseEnter (const juce::MouseEvent&) override   { hovered = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override    { hovered = false; repaint(); }
        void mouseUp (const juce::MouseEvent&) override;

        std::function<void()> onClick;
        bool hovered = false;
    };

    /** Capa que dibuja, sobre la pista en la que se graba, la toma en curso a
        medida que llega el audio. */
    struct RecordingLane final : public juce::Component
    {
        RecordingLane()    { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics&) override;

        std::vector<float> peaks;                  // un pico por bin de AudioRecorder::previewBinSize
        std::optional<juce::int64> startSample;    // posición final del clip (puede ser negativa)
        double sampleRate = 48000.0;
        double visibleStart = 0.0;
        double visibleLength = 60.0;
    };

    /** Línea del cabezal, transparente a los clics. */
    struct Playhead final : public juce::Component
    {
        Playhead()    { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics&) override;
        int x = -1;
    };

    void timerCallback() override;
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;

    bool handleWheel (int x, const juce::MouseEvent&, const juce::MouseWheelDetails&);
    void zoomAround (double anchorSeconds, double factor);
    void setVisibleRange (double start, double length);
    void updateTimeline();
    void updateRecordingLane();
    void updateSelectionDisplay();
    void layoutRows();
    void seekTo (double seconds);
    int waveformWidth() const;

    AudioEngine& engine;
    juce::AudioThumbnailCache thumbnailCache { 32 };

    juce::TextButton zoomOutButton { "-" };
    juce::TextButton zoomInButton { "+" };
    juce::TextButton zoomFitButton { "Ajustar" };
    juce::ScrollBar horizontalScroll { false };

    // content va antes que viewport: el viewport se destruye primero y lo suelta.
    Content content;
    EmptyAddButton emptyAddButton;
    RecordingLane recordingLane;
    Playhead playhead;
    TimeRuler ruler;
    juce::Viewport viewport;

    std::vector<std::unique_ptr<TrackView>> rows;
    std::weak_ptr<AudioTrack> selected;
    juce::uint32 selectedClip = 0;

    // Línea de tiempo: duración total (contenido + margen) y tramo visible.
    double totalLength = 30.0;
    double visibleStart = 0.0;
    double visibleLength = 30.0;
    bool fitToWindow = true;            // sin zoom manual: siempre se ve todo
    juce::int64 knownContentLength = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackListView)
};
}
