#include "AudioTrack.h"

namespace stemlab
{
AudioTrack::AudioTrack (juce::String trackName, juce::File file, juce::AudioBuffer<float> audioData, double rate)
    : name (std::move (trackName)),
      sourceFile (std::move (file)),
      audio (std::move (audioData)),
      sampleRate (rate),
      volume (Parameter::continuous ("volume", "Volumen", juce::NormalisableRange<float> (-60.0f, 12.0f, 0.1f, 2.0f), 0.0f, "dB")),
      pan (Parameter::continuous ("pan", "Paneo", juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f)),
      mute (Parameter::toggle ("mute", "Mute", false)),
      solo (Parameter::toggle ("solo", "Solo", false))
{
    // Todo el audio de StemLab se normaliza a estéreo al cargarlo.
    jassert (audio.getNumChannels() == 2);

    volume->setTextFormatter ([] (float db) { return db <= -60.0f ? juce::String ("-inf dB") : juce::String (db, 1) + " dB"; });

    pan->setTextFormatter ([] (float value)
    {
        const auto percent = juce::roundToInt (std::abs (value) * 100.0f);
        return percent == 0 ? juce::String ("C") : juce::String (value < 0.0f ? "L " : "R ") + juce::String (percent);
    });
}

juce::var AudioTrack::getState() const
{
    auto* state = new juce::DynamicObject();

    for (const auto* parameter : { volume.get(), pan.get(), mute.get(), solo.get() })
        state->setProperty (parameter->getId(), parameter->toVar());

    state->setProperty ("effects", effects.toVar());
    return juce::var (state);
}

void AudioTrack::applyState (const juce::var& state)
{
    for (auto* parameter : { volume.get(), pan.get(), mute.get(), solo.get() })
        parameter->fromVar (state.getProperty (parameter->getId(), {}));

    effects.fromVar (state.getProperty ("effects", {}));
}

float AudioTrack::getAndResetPeak (int channel) noexcept
{
    return peaks[juce::jlimit (0, 1, channel)].exchange (0.0f, std::memory_order_relaxed);
}

void AudioTrack::prepare (double deviceSampleRate, int maximumBlockSize)
{
    scratch.setSize (2, maximumBlockSize, false, true, false);
    effects.prepare ({ deviceSampleRate, static_cast<juce::uint32> (maximumBlockSize), 2 });

    for (auto* smoothed : { &leftGain, &rightGain })
    {
        smoothed->reset (deviceSampleRate, 0.02);
        smoothed->setCurrentAndTargetValue (0.0f);
    }
}

void AudioTrack::renderAdd (juce::AudioBuffer<float>& destination, int numSamples,
                            juce::int64 timelinePosition, bool audible) noexcept
{
    jassert (numSamples <= scratch.getNumSamples());

    // Volumen + balance. El balance deja el centro a ganancia unidad: así la
    // suma de los stems sin tocar reproduce la mezcla original.
    const auto gain = audible ? juce::Decibels::decibelsToGain (volume->get(), -60.0f) : 0.0f;
    const auto panValue = pan->get();
    const auto halfPi = juce::MathConstants<float>::halfPi;

    leftGain.setTargetValue (gain * (panValue > 0.0f ? std::cos (panValue * halfPi) : 1.0f));
    rightGain.setTargetValue (gain * (panValue < 0.0f ? std::cos (-panValue * halfPi) : 1.0f));

    // Silenciada y con la rampa de salida terminada: no hace falta procesar.
    if (! audible && ! leftGain.isSmoothing() && ! rightGain.isSmoothing())
        return;

    scratch.clear (0, numSamples);

    // Parte del bloque [timelinePosition, timelinePosition + numSamples) que
    // cae dentro del clip [startSample, startSample + longitud).
    const auto offsetInClip = timelinePosition - startSample.load (std::memory_order_relaxed);
    const auto clipLength = static_cast<juce::int64> (audio.getNumSamples());
    const auto first = juce::jlimit<juce::int64> (0, numSamples, -offsetInClip);
    const auto last = juce::jlimit<juce::int64> (0, numSamples, clipLength - offsetInClip);

    if (last > first)
        for (int ch = 0; ch < 2; ++ch)
            scratch.copyFrom (ch, static_cast<int> (first), audio, ch,
                              static_cast<int> (offsetInClip + first), static_cast<int> (last - first));

    effects.process (juce::dsp::AudioBlock<float> (scratch).getSubBlock (0, static_cast<size_t> (numSamples)));

    const auto* inL = scratch.getReadPointer (0);
    const auto* inR = scratch.getReadPointer (1);
    auto* outL = destination.getWritePointer (0);
    auto* outR = destination.getWritePointer (1);
    float peakL = 0.0f, peakR = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto l = inL[i] * leftGain.getNextValue();
        const auto r = inR[i] * rightGain.getNextValue();
        outL[i] += l;
        outR[i] += r;
        peakL = juce::jmax (peakL, std::abs (l));
        peakR = juce::jmax (peakR, std::abs (r));
    }

    peaks[0].store (juce::jmax (peaks[0].load (std::memory_order_relaxed), peakL), std::memory_order_relaxed);
    peaks[1].store (juce::jmax (peaks[1].load (std::memory_order_relaxed), peakR), std::memory_order_relaxed);
}
}
