#include "LimiterEffect.h"

namespace stemlab
{
LimiterEffect::LimiterEffect()
    : AudioEffect ("limiter", "Limiter", false),
      inputGain (addParameter (Parameter::continuous ("input", "Entrada",
                                                      juce::NormalisableRange<float> (0.0f, 24.0f, 0.1f), 0.0f, "dB"))),
      ceiling   (addParameter (Parameter::continuous ("ceiling", "Techo",
                                                      juce::NormalisableRange<float> (-12.0f, 0.0f, 0.1f), -1.0f, "dB"))),
      release   (addParameter (Parameter::continuous ("release", "Release",
                                                      juce::NormalisableRange<float> (1.0f, 500.0f, 1.0f, 0.4f), 60.0f, "ms")))
{
}

void LimiterEffect::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    inputSmoothed.reset (sampleRate, 0.02);
    inputSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (inputGain.get()));
    lastReleaseMs = -1.0f;
    reset();
}

void LimiterEffect::process (juce::dsp::AudioBlock<float> block) noexcept
{
    inputSmoothed.setTargetValue (juce::Decibels::decibelsToGain (inputGain.get()));
    const auto ceilingGain = juce::Decibels::decibelsToGain (ceiling.get());

    if (const auto releaseMs = release.get(); ! juce::exactlyEqual (releaseMs, lastReleaseMs))
    {
        lastReleaseMs = releaseMs;
        releaseCoefficient = static_cast<float> (std::exp (-1.0 / (releaseMs * 0.001 * sampleRate)));
    }

    const auto numChannels = block.getNumChannels();

    for (size_t i = 0; i < block.getNumSamples(); ++i)
    {
        const auto gainIn = inputSmoothed.getNextValue();
        float peak = 0.0f;

        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            auto& sample = block.getChannelPointer (ch)[i];
            sample *= gainIn;
            peak = juce::jmax (peak, std::abs (sample));
        }

        const auto target = peak > ceilingGain ? ceilingGain / peak : 1.0f;

        // Ataque instantáneo (nunca se supera el techo), release suave.
        envelope = target < envelope ? target
                                     : target + releaseCoefficient * (envelope - target);

        for (size_t ch = 0; ch < numChannels; ++ch)
            block.getChannelPointer (ch)[i] *= envelope;
    }
}

void LimiterEffect::reset() noexcept
{
    envelope = 1.0f;
}
}
