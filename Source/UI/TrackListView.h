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
    Zona de pistas: regla de tiempo + lista desplazable de TrackView + cabezal +
    barra de desplazamiento horizontal.

    - Zoom: Ctrl + rueda (alrededor del ratón). Desplazamiento: Shift + rueda,
      rueda horizontal o la barra inferior. Al reproducir, la vista sigue al cabezal.
    - Al pasar el ratón por una pista aparece un "+" en la esquina derecha de su
      borde inferior: añade una pista justo debajo.
    - Las pistas se reordenan arrastrando su cabecera.

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

    /** Abre el editor del nombre de la pista seleccionada (F2). */
    void renameSelectedTrack();

    /** Mueve la pista seleccionada una posición (-1 arriba, +1 abajo). */
    void moveSelectedTrack (int direction);

    // Zoom (menú Proyecto > Vista y Ctrl + rueda)
    void zoomIn();
    void zoomOut();
    void zoomToFit();

    std::function<void (std::shared_ptr<AudioTrack>)> onSelectionChanged;
    std::function<void (AudioTrack&)> onDeleteRequested;
    std::function<void (std::shared_ptr<AudioTrack>, juce::uint32 clipId, double seconds)> onContextMenu;
    /** Un clip se movió o recortó con el ratón (ya aplicado a la pista). */
    std::function<void (std::shared_ptr<AudioTrack>, std::vector<AudioClip> clipsBefore, const juce::String& actionName)> onClipsEdited;
    /** La pista ya se movió en el mezclador (arrastre, Alt+flechas o menú). */
    std::function<void (std::shared_ptr<AudioTrack>, int fromIndex, int toIndex)> onTracksReordered;
    std::function<void (std::shared_ptr<AudioTrack>, const juce::String& oldName)> onTrackRenamed;
    std::function<void (int insertIndex)> onAddTrack;       // -1 = al final

    // Menú de la cabecera: copiar, cortar y pegar pistas.
    std::function<void (std::shared_ptr<AudioTrack>)> onCopyTrack;
    std::function<void (std::shared_ptr<AudioTrack>)> onCutTrack;
    std::function<void (int insertIndex)> onPasteTrack;
    std::function<bool()> canPasteTrack;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    /** Contenedor de las filas; si no hay pistas muestra una indicación. */
    struct Content final : public juce::Component
    {
        void paint (juce::Graphics&) override;
        bool isEmpty = true;
    };

    /** "+" en un círculo centrado sobre una línea, en su extremo derecho. La
        línea ocupa solo la zona de cabeceras. Se usa sobre el borde inferior
        de la pista bajo el ratón, y arriba del todo cuando no hay pistas. */
    struct AddTrackButton final : public juce::Component,
                                  public juce::SettableTooltipClient
    {
        static constexpr float radius = 9.0f;
        static constexpr int height = 26;

        juce::Point<float> getCircleCentre() const;
        bool hitTest (int x, int y) override;
        void paint (juce::Graphics&) override;
        void mouseEnter (const juce::MouseEvent&) override   { hovered = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override    { hovered = false; repaint(); }
        void mouseUp (const juce::MouseEvent&) override;

        std::function<void()> onClick;
        juce::Colour colour;
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

    int indexOf (const TrackView& view) const;
    void showTrackMenu (TrackView& view);
    void reorderDrag (TrackView& view, int parentY, int grabY);
    void reorderEnd (TrackView& view);
    void moveTrack (int fromIndex, int toIndex);
    void updateAddButton();

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
    juce::ScrollBar horizontalScroll { false };

    // content va antes que viewport: el viewport se destruye primero y lo suelta.
    Content content;
    AddTrackButton emptyAddButton;      // sin pistas: arriba del todo
    AddTrackButton addBelowButton;      // con pistas: en el borde de la pista bajo el ratón
    RecordingLane recordingLane;
    Playhead playhead;
    TimeRuler ruler;
    juce::Viewport viewport;

    std::vector<std::unique_ptr<TrackView>> rows;
    std::weak_ptr<AudioTrack> selected;
    juce::uint32 selectedClip = 0;

    int addBelowRow = -1;               // pista bajo la que está el "+"
    int draggingRow = -1;               // pista que se está arrastrando
    int dropRow = -1;                   // posición donde caería

    // Línea de tiempo: duración total (contenido + margen) y tramo visible.
    double totalLength = 30.0;
    double visibleStart = 0.0;
    double visibleLength = 30.0;
    bool fitToWindow = true;            // sin zoom manual: siempre se ve todo
    juce::int64 knownContentLength = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackListView)
};
}
