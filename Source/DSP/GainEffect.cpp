#include "GainEffect.h"

namespace stemlab
{
GainEffect::GainEffect()
    : AudioEffect ("gain", "Gain", true),
      gainDb (addParameter (Parameter::continuous ("gain", "Ganancia",
                                                   juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f),
                                                   0.0f, "dB")))
{
    gain.setRampDurationSeconds (0.02);
}

void GainEffect::prepare (const juce::dsp::ProcessSpec& spec)
{
    gain.setGainDecibels (gainDb.get());
    gain.prepare (spec);
}

void GainEffect::process (juce::dsp::AudioBlock<float> block) noexcept
{
    gain.setGainDecibels (gainDb.get());
    gain.process (juce::dsp::ProcessContextReplacing<float> (block));
}

void GainEffect::reset() noexcept
{
    gain.reset();
}
}
