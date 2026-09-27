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

    void setSelected (bool shouldBeSelected);
    void setSelectedClip (juce::uint32 clipId)                      { waveform.setSelectedClip (clipId); }
    void setVisibleRange (double startSeconds, double lengthSeconds) { waveform.setVisibleRange (startSeconds, lengthSeconds); }

    /** La última pista muestra siempre su "+"; las demás solo con el ratón encima. */
    void setIsLast (bool shouldBeLast);

    /** Volver a leer clips y estado de grabación de la pista. */
    void trackChanged();

    std::function<void (TrackView&)> onSelect;
    std::function<void (TrackView&)> onDelete;
    std::function<void (double seconds)> onSeek;
    std::function<void (TrackView&, juce::uint32 clipId)> onClipClicked;
    std::function<void (TrackView&, juce::uint32 clipId, double seconds)> onContextMenu;
    std::function<void()> onClipsEdited;
    std::function<void (TrackView&)> onAddBelow;
    std::function<bool (int x, const juce::MouseEvent&, const juce::MouseWheelDetails&)> onWheel;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    /** "+" en un círculo pegado al borde inferior de la cabecera, con una línea
        que llega hasta donde empiezan los clips: añade una pista debajo. */
    struct AddBelowButton final : public juce::Component,
                                  public juce::SettableTooltipClient
    {
        static constexpr int height = 18;
        static constexpr float radius = 8.0f;

        juce::Point<float> getCircleCentre() const;
        bool hitTest (int x, int y) override;
        void paint (juce::Graphics&) override;
        void mouseEnter (const juce::MouseEvent&) override   { hovered = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override    { hovered = false; repaint(); }
        void mouseUp (const juce::MouseEvent&) override;

        std::function<void()> onClick;
        juce::Colour colour;        // color de la pista
        bool hovered = false;
    };

    /** Detecta si el ratón está sobre la fila (o sobre cualquiera de sus hijos). */
    struct HoverWatcher final : public juce::MouseListener
    {
        explicit HoverWatcher (TrackView& v) : view (v) {}
        void mouseEnter (const juce::MouseEvent&) override   { view.updateAddButton(); }
        void mouseExit (const juce::MouseEvent&) override    { view.updateAddButton(); }
        TrackView& view;
    };

    void parameterChanged (Parameter&) override;
    void updateAddButton();

    std::shared_ptr<AudioTrack> track;
    juce::Colour colour;
    bool selected = false;
    bool isLast = false;

    juce::Label nameLabel;
    juce::TextButton muteButton { "M" };
    juce::TextButton soloButton { "S" };
    IconButton deleteButton;
    juce::Slider volumeSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider panSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    LevelMeter meter;
    WaveformView waveform;
    AddBelowButton addBelowButton;
    HoverWatcher hoverWatcher { *this };

    SliderAttachment volumeAttachment;
    SliderAttachment panAttachment;
    ButtonAttachment muteAttachment;
    ButtonAttachment soloAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackView)
};
}
