#pragma once

#include "AudioEffect.h"

namespace stemlab
{
/** Ganancia de entrada de la pista (primer eslabón de la cadena). */
class GainEffect final : public AudioEffect
{
public:
    GainEffect();

    void prepare (const juce::dsp::ProcessSpec& spec) override;
    void process (juce::dsp::AudioBlock<float> block) noexcept override;
    void reset() noexcept override;

private:
    Parameter& gainDb;
    juce::dsp::Gain<float> gain;
};
}
