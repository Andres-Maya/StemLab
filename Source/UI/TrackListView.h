#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "Audio/AudioEngine.h"
#include "TimeRuler.h"
#include "TrackView.h"

#include <memory>
#include <vector>

namespace stemlab
{
/**
    Zona de pistas: regla de tiempo + lista desplazable de TrackView + cabezal
    de reproducción. Todas las pistas comparten la misma escala de tiempo
    (de momento "ajustar a la ventana", sin zoom).
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
    void selectTrack (const std::shared_ptr<AudioTrack>& track);

    std::function<void (std::shared_ptr<AudioTrack>)> onSelectionChanged;
    std::function<void (AudioTrack&)> onDeleteRequested;

    void resized() override;

private:
    /** Contenedor de las filas; si no hay pistas muestra una indicación. */
    struct Content final : public juce::Component
    {
        void paint (juce::Graphics&) override;
        bool isEmpty = true;
    };

    /** Línea del cabezal, transparente a los clics. */
    struct Playhead final : public juce::Component
    {
        Playhead()    { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics&) override;
        int x = -1;
    };

    void timerCallback() override;
    void updateTimeline();
    void layoutRows();
    void seekTo (double seconds);

    AudioEngine& engine;
    juce::AudioThumbnailCache thumbnailCache { 16 };

    // content va antes que viewport: el viewport se destruye primero y lo suelta.
    Content content;
    Playhead playhead;
    TimeRuler ruler;
    juce::Viewport viewport;

    std::vector<std::unique_ptr<TrackView>> rows;
    std::weak_ptr<AudioTrack> selected;
    double timelineLength = 60.0;
    juce::int64 knownContentLength = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackListView)
};
}
