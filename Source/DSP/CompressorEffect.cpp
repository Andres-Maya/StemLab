#include "CompressorEffect.h"

#include "Utils/Localisation.h"      // msg(): los nombres se traducen al mostrarlos

namespace stemlab
{
CompressorEffect::CompressorEffect()
    : AudioEffect ("compressor", msg ("Compresor"), false),
      threshold (addParameter (Parameter::continuous ("threshold", msg ("Umbral"),
                                                      juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -18.0f, "dB"))),
      ratio     (addParameter (Parameter::continuous ("ratio", "Ratio",
                                                      juce::NormalisableRange<float> (1.0f, 20.0f, 0.1f, 0.5f), 4.0f, ":1"))),
      attack    (addParameter (Parameter::continuous ("attack", msg ("Ataque"),
                                                      juce::NormalisableRange<float> (0.1f, 200.0f, 0.1f, 0.4f), 10.0f, "ms"))),
      release   (addParameter (Parameter::continuous ("release", "Release",
                                                      juce::NormalisableRange<float> (5.0f, 1000.0f, 1.0f, 0.4f), 120.0f, "ms"))),
      makeup    (addParameter (Parameter::continuous ("makeup", "Makeup",
                                                      juce::NormalisableRange<float> (0.0f, 24.0f, 0.1f), 0.0f, "dB")))
{
    makeupGain.setRampDurationSeconds (0.02);
}

void CompressorEffect::prepare (const juce::dsp::ProcessSpec& spec)
{
    updateSettings();
    compressor.prepare (spec);
    makeupGain.prepare (spec);
}

void CompressorEffect::process (juce::dsp::AudioBlock<float> block) noexcept
{
    updateSettings();

    const juce::dsp::ProcessContextReplacing<float> context (block);
    compressor.process (context);
    makeupGain.process (context);
}

void CompressorEffect::reset() noexcept
{
    compressor.reset();
    makeupGain.reset();
}

void CompressorEffect::updateSettings() noexcept
{
    // Los setters de juce::dsp::Compressor solo recalculan unas pocas
    // constantes: es seguro llamarlos en cada bloque.
    compressor.setThreshold (threshold.get());
    compressor.setRatio (ratio.get());
    compressor.setAttack (attack.get());
    compressor.setRelease (release.get());
    makeupGain.setGainDecibels (makeup.get());
}
}
