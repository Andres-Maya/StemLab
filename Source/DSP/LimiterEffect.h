#pragma once

#include "AudioEffect.h"

namespace stemlab
{
/**
    Limitador de picos con techo garantizado.

    Detección estéreo enlazada, ataque instantáneo y release exponencial.
    Sin lookahead: es simple y no añade latencia, a cambio de algo de
    distorsión en transitorios muy rápidos.
*/
class LimiterEffect final : public AudioEffect
{
public:
    LimiterEffect();

    void prepare (const juce::dsp::ProcessSpec& spec) override;
    void process (juce::dsp::AudioBlock<float> block) noexcept override;
    void reset() noexcept override;

private:
    Parameter& inputGain;
    Parameter& ceiling;
    Parameter& release;

    double sampleRate = 44100.0;
    juce::SmoothedValue<float> inputSmoothed;
    float envelope = 1.0f;
    float lastReleaseMs = -1.0f;
    float releaseCoefficient = 0.0f;
};
}
