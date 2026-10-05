#include "SaturationEffect.h"

#include "Utils/Strings.h"

namespace stemlab
{
SaturationEffect::SaturationEffect()
    : AudioEffect ("saturation", msg ("Saturación"), false),
      drive (addParameter (Parameter::continuous ("drive", "Drive",
                                                  juce::NormalisableRange<float> (0.0f, 24.0f, 0.1f), 6.0f, "dB"))),
      mode (addParameter (Parameter::choice ("mode", msg ("Tipo"),
                                             juce::StringArray (msg ("Suave"), msg ("Dura"), msg ("Válvula")), 0))),
      mix (addParameter (Parameter::continuous ("mix", msg ("Mezcla"),
                                                juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f, "%"))),
      output (addParameter (Parameter::continuous ("output", msg ("Salida"),
                                                   juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, "dB")))
{
}

void SaturationEffect::prepare (const juce::dsp::ProcessSpec& spec)
{
    for (auto* smoothed : { &driveGain, &mixAmount, &outputGain })
        smoothed->reset (spec.sampleRate, 0.02);

    driveGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (drive.get()));
    mixAmount.setCurrentAndTargetValue (mix.get() * 0.01f);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (output.get()));

    // Filtro paso alto de un polo a ~10 Hz para quitar el DC del modo Válvula.
    dcCoefficient = static_cast<float> (std::exp (-juce::MathConstants<double>::twoPi * 10.0 / spec.sampleRate));
    reset();
}

void SaturationEffect::process (juce::dsp::AudioBlock<float> block) noexcept
{
    driveGain.setTargetValue (juce::Decibels::decibelsToGain (drive.get()));
    mixAmount.setTargetValue (mix.get() * 0.01f);
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (output.get()));

    const auto currentMode = static_cast<Mode> (mode.getIndex());
    const auto numChannels = juce::jmin (block.getNumChannels(), dcBlockers.size());
    const auto numSamples = block.getNumSamples();

    for (size_t i = 0; i < numSamples; ++i)
    {
        const auto d = driveGain.getNextValue();
        const auto m = mixAmount.getNextValue();
        const auto g = outputGain.getNextValue();

        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            auto* data = block.getChannelPointer (ch);
            const auto dry = data[i];
            auto wet = shape (dry * d, currentMode);

            if (currentMode == Mode::tube)
                wet = dcBlockers[ch].process (wet, dcCoefficient);

            data[i] = (dry + m * (wet - dry)) * g;
        }
    }
}

void SaturationEffect::reset() noexcept
{
    for (auto& blocker : dcBlockers)
        blocker.reset();
}

float SaturationEffect::shape (float x, Mode shapeMode) noexcept
{
    switch (shapeMode)
    {
        case Mode::hard:
            return juce::jlimit (-1.0f, 1.0f, x);

        case Mode::tube:
        {
            // tanh desplazada: los semiciclos positivos y negativos saturan
            // distinto. Se resta tanh(bias) para que el silencio siga siendo 0.
            constexpr float bias = 0.25f;
            constexpr float tanhOfBias = 0.24491866f;
            return std::tanh (x + bias) - tanhOfBias;
        }

        case Mode::soft:
            break;
    }

    return std::tanh (x);
}
}
