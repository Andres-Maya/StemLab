#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "Audio/AudioTrack.h"
#include "IconButton.h"
#include "LevelMeter.h"
#include "ParameterAttachments.h"
#include "WaveformView.h"

#include <functional>
#include <memory>

namespace stemlab
{
/**
    Fila de una pista:  [nombre · M · S · × · volumen · paneo · medidor] [clips]

    - Arrastrar la cabecera (nombre o zona vacía) arriba/abajo: mueve la pista.
    - Doble clic en el nombre (o F2): cambiar el nombre.
    - Clic derecho en la cabecera: menú de la pista.
    Mientras se graba en la pista, su franja de color se pone roja.
*/
class TrackView final : public juce::Component,
                        private Parameter::Listener
{
public:
    static constexpr int headerWidth = 250;
    static constexpr int preferredHeight = 84;

    TrackView (std::shared_ptr<AudioTrack> track, juce::Colour colour,
               juce::AudioFormatManager& formatManager, juce::AudioThumbnailCache& cache);
    ~TrackView() override;

    AudioTrack& getTrack() noexcept                                 { return *track; }
    const std::shared_ptr<AudioTrack>& getTrackPointer() const noexcept { return track; }
    juce::Colour getColour() const noexcept                         { return colour; }

    void setSelected (bool shouldBeSelected);
    void setSelectedClip (juce::uint32 clipId)                      { waveform.setSelectedClip (clipId); }
    void setVisibleRange (double startSeconds, double lengthSeconds) { waveform.setVisibleRange (startSeconds, lengthSeconds); }

    /** Volver a leer clips, nombre y estado de grabación de la pista. */
    void trackChanged();

    /** Abre el editor del nombre (como un doble clic). */
    void startRename();

    std::function<void (TrackView&)> onSelect;
    std::function<void (TrackView&)> onDelete;
    std::function<void (TrackView&)> onRenamed;
    std::function<void (TrackView&)> onHeaderMenu;
    std::function<void (double seconds)> onSeek;
    std::function<void (TrackView&, juce::uint32 clipId)> onClipClicked;
    std::function<void (TrackView&, juce::uint32 clipId, double seconds)> onContextMenu;
    std::function<void (TrackView&, std::vector<AudioClip> clipsBefore, const juce::String& actionName)> onClipsEdited;
    std::function<bool (int x, const juce::MouseEvent&, const juce::MouseWheelDetails&)> onWheel;

    /** Arrastre de la cabecera para reordenar: posición vertical del ratón en el
        contenedor de las pistas y punto donde se agarró la fila. */
    std::function<void (TrackView&, int parentY, int grabY)> onReorderDrag;
    std::function<void (TrackView&)> onReorderEnd;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void parameterChanged (Parameter&) override;

    std::shared_ptr<AudioTrack> track;
    juce::Colour colour;
    bool selected = false;

    int grabY = 0;
    bool reordering = false;

    juce::Label nameLabel;
    juce::TextButton muteButton { "M" };
    juce::TextButton soloButton { "S" };
    IconButton deleteButton;
    juce::Slider volumeSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider panSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    LevelMeter meter;
    WaveformView waveform;

    SliderAttachment volumeAttachment;
    SliderAttachment panAttachment;
    ButtonAttachment muteAttachment;
    ButtonAttachment soloAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackView)
};
}
