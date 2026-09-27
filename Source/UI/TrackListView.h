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
    Zona de pistas: botones de pista + regla de tiempo + lista desplazable de
    TrackView + cabezal. Todas las pistas comparten la misma escala de tiempo
    (de momento "ajustar a la ventana", sin zoom).

    Guarda la selección: pista seleccionada y, dentro de ella, el clip
    seleccionado (por su id).
*/
class TrackListView final : public juce::Component,
                            private juce::Timer
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

    std::function<void (std::shared_ptr<AudioTrack>)> onSelectionChanged;
    std::function<void (AudioTrack&)> onDeleteRequested;
    std::function<void (std::shared_ptr<AudioTrack>)> onArmRequested;
    std::function<void (std::shared_ptr<AudioTrack>, juce::uint32 clipId, double seconds)> onContextMenu;
    std::function<void()> onClipsEdited;
    std::function<void()> onAddTrack;
    std::function<void()> onRemoveTrack;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    /** Contenedor de las filas; si no hay pistas muestra una indicación. */
    struct Content final : public juce::Component
    {
        void paint (juce::Graphics&) override;
        bool isEmpty = true;
    };

    /** Capa que dibuja, sobre la pista armada, la grabación en curso a medida
        que llega el audio. */
    struct RecordingLane final : public juce::Component
    {
        RecordingLane()    { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics&) override;

        std::vector<float> peaks;                  // un pico por bin de AudioRecorder::previewBinSize
        std::optional<juce::int64> startSample;    // posición final del clip (puede ser negativa)
        double sampleRate = 48000.0;
        double timelineLength = 60.0;
    };

    /** Línea del cabezal, transparente a los clics. */
    struct Playhead final : public juce::Component
    {
        Playhead()    { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics&) override;
        int x = -1;
    };

    void timerCallback() override;
    void updateRecordingLane();
    void updateSelectionDisplay();
    void setTimelineLength (double seconds);
    void updateTimeline();
    void layoutRows();
    void seekTo (double seconds);

    AudioEngine& engine;
    juce::AudioThumbnailCache thumbnailCache { 32 };

    juce::TextButton addTrackButton { "+ Pista" };
    juce::TextButton removeTrackButton { "- Pista" };

    // content va antes que viewport: el viewport se destruye primero y lo suelta.
    Content content;
    RecordingLane recordingLane;
    Playhead playhead;
    TimeRuler ruler;
    juce::Viewport viewport;

    std::vector<std::unique_ptr<TrackView>> rows;
    std::weak_ptr<AudioTrack> selected;
    juce::uint32 selectedClip = 0;
    double timelineLength = 60.0;
    juce::int64 knownContentLength = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackListView)
};
}
