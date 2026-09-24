#pragma once

#include "AudioEffect.h"
#include "Biquad.h"

#include <array>

namespace stemlab
{
/** Ecualizador de 3 bandas: low shelf, campana (peak) y high shelf. */
class EqualizerEffect final : public AudioEffect
{
public:
    EqualizerEffect();

    void prepare (const juce::dsp::ProcessSpec& spec) override;
    void process (juce::dsp::AudioBlock<float> block) noexcept override;
    void reset() noexcept override;

private:
    struct ChannelFilters
    {
        Biquad low, mid, high;
    };

    void updateCoefficients() noexcept;

    Parameter& lowGain;
    Parameter& lowFreq;
    Parameter& midGain;
    Parameter& midFreq;
    Parameter& midQ;
    Parameter& highGain;
    Parameter& highFreq;

    double sampleRate = 44100.0;
    std::array<ChannelFilters, 2> channels;
    std::array<float, 7> lastValues {};
};
}
