#pragma once

#include "AudioEffect.h"

namespace stemlab
{
/** Compresor (juce::dsp::Compressor) con ganancia de compensación. */
class CompressorEffect final : public AudioEffect
{
public:
    CompressorEffect();

    void prepare (const juce::dsp::ProcessSpec& spec) override;
    void process (juce::dsp::AudioBlock<float> block) noexcept override;
    void reset() noexcept override;

private:
    void updateSettings() noexcept;

    Parameter& threshold;
    Parameter& ratio;
    Parameter& attack;
    Parameter& release;
    Parameter& makeup;

    juce::dsp::Compressor<float> compressor;
    juce::dsp::Gain<float> makeupGain;
};
}
