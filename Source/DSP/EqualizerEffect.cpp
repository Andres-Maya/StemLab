#include "EqualizerEffect.h"

#include "Utils/Localisation.h"      // msg(): los nombres se traducen al mostrarlos

namespace stemlab
{
namespace
{
    juce::NormalisableRange<float> eqGainRange()
    {
        return juce::NormalisableRange<float> (-18.0f, 18.0f, 0.1f);
    }
}

EqualizerEffect::EqualizerEffect()
    : AudioEffect ("eq", "EQ", false),
      lowGain  (addParameter (Parameter::continuous ("lowGain",  msg ("Graves"),    eqGainRange(), 0.0f, "dB"))),
      lowFreq  (addParameter (Parameter::continuous ("lowFreq",  msg ("Frec. G"),   Parameter::frequencyRange (20.0f, 1000.0f), 120.0f, "Hz"))),
      midGain  (addParameter (Parameter::continuous ("midGain",  msg ("Medios"),    eqGainRange(), 0.0f, "dB"))),
      midFreq  (addParameter (Parameter::continuous ("midFreq",  msg ("Frec. M"),   Parameter::frequencyRange (100.0f, 10000.0f), 1000.0f, "Hz"))),
      midQ     (addParameter (Parameter::continuous ("midQ",     "Q",         juce::NormalisableRange<float> (0.2f, 10.0f, 0.01f, 0.4f), 0.7f))),
      highGain (addParameter (Parameter::continuous ("highGain", msg ("Agudos"),    eqGainRange(), 0.0f, "dB"))),
      highFreq (addParameter (Parameter::continuous ("highFreq", msg ("Frec. A"),   Parameter::frequencyRange (1000.0f, 20000.0f), 8000.0f, "Hz")))
{
}

void EqualizerEffect::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    updateCoefficients();
    reset();
}

void EqualizerEffect::process (juce::dsp::AudioBlock<float> block) noexcept
{
    const std::array<float, 7> values { lowGain.get(), lowFreq.get(), midGain.get(), midFreq.get(),
                                        midQ.get(), highGain.get(), highFreq.get() };

    // Recalcular solo si algún parámetro cambió desde el bloque anterior.
    if (values != lastValues)
        updateCoefficients();

    const auto numChannels = juce::jmin (block.getNumChannels(), channels.size());

    for (size_t ch = 0; ch < numChannels; ++ch)
    {
        auto* data = block.getChannelPointer (ch);
        auto& filters = channels[ch];

        for (size_t i = 0; i < block.getNumSamples(); ++i)
            data[i] = filters.high.process (filters.mid.process (filters.low.process (data[i])));
    }
}

void EqualizerEffect::reset() noexcept
{
    for (auto& filters : channels)
    {
        filters.low.reset();
        filters.mid.reset();
        filters.high.reset();
    }
}

void EqualizerEffect::updateCoefficients() noexcept
{
    using Coefficients = juce::dsp::IIR::ArrayCoefficients<float>;

    lastValues = { lowGain.get(), lowFreq.get(), midGain.get(), midFreq.get(),
                   midQ.get(), highGain.get(), highFreq.get() };

    // Mantener las frecuencias por debajo de Nyquist aunque el dispositivo
    // funcione a una frecuencia de muestreo baja.
    const auto nyquistLimit = static_cast<float> (sampleRate * 0.45);
    constexpr float shelfQ = 0.707f;

    const auto low = Coefficients::makeLowShelf (sampleRate, juce::jmin (lowFreq.get(), nyquistLimit), shelfQ,
                                                 juce::Decibels::decibelsToGain (lowGain.get()));
    const auto mid = Coefficients::makePeakFilter (sampleRate, juce::jmin (midFreq.get(), nyquistLimit), midQ.get(),
                                                   juce::Decibels::decibelsToGain (midGain.get()));
    const auto high = Coefficients::makeHighShelf (sampleRate, juce::jmin (highFreq.get(), nyquistLimit), shelfQ,
                                                   juce::Decibels::decibelsToGain (highGain.get()));

    for (auto& filters : channels)
    {
        filters.low.setCoefficients (low);
        filters.mid.setCoefficients (mid);
        filters.high.setCoefficients (high);
    }
}
}
