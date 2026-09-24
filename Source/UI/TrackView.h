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
    Fila de una pista:  [nombre · M · S · volumen · paneo · medidor] [forma de onda]
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
    void setTimelineLength (double seconds);

    std::function<void (TrackView&)> onSelect;
    std::function<void (TrackView&)> onDelete;
    std::function<void (double seconds)> onSeek;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void parameterChanged (Parameter&) override;

    std::shared_ptr<AudioTrack> track;
    juce::Colour colour;
    bool selected = false;

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
