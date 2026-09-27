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
    Fila de una pista:  [nombre · ● · M · S · × · volumen · paneo · medidor] [clips]
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
    void setTimelineLength (double seconds);

    /** Volver a leer clips y estado de grabación de la pista. */
    void trackChanged();

    std::function<void (TrackView&)> onSelect;
    std::function<void (TrackView&)> onDelete;
    std::function<void (TrackView&)> onArm;
    std::function<void (double seconds)> onSeek;
    std::function<void (TrackView&, juce::uint32 clipId)> onClipClicked;
    std::function<void (TrackView&, juce::uint32 clipId, double seconds)> onContextMenu;
    std::function<void()> onClipsEdited;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void parameterChanged (Parameter&) override;

    std::shared_ptr<AudioTrack> track;
    juce::Colour colour;
    bool selected = false;

    juce::Label nameLabel;
    IconButton armButton;
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
