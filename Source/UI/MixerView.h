#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Audio/AudioTrack.h"
#include "EffectPanel.h"

#include <memory>
#include <vector>

namespace stemlab
{
/**
    Mezclador de la pista seleccionada:

        [Canal: volumen · paneo · medidor] [Gain] [Saturación] [EQ] [Compresor] [Limiter]
*/
class MixerView final : public juce::Component
{
public:
    static constexpr int preferredHeight = 238;

    MixerView();
    ~MixerView() override;

    void setTrack (std::shared_ptr<AudioTrack> track);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class ChannelStrip;

    std::shared_ptr<AudioTrack> track;

    juce::Component rack;
    juce::Viewport viewport;
    std::unique_ptr<ChannelStrip> strip;
    std::vector<std::unique_ptr<EffectPanel>> panels;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerView)
};
}
