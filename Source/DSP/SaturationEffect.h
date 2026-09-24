#pragma once

#include "AudioEffect.h"

#include <array>

namespace stemlab
{
/**
    Saturación por waveshaping.

      Suave   → tanh: redondea los picos, cálido.
      Dura    → recorte duro: agresivo, tipo fuzz.
      Válvula → tanh asimétrica: añade armónicos pares (con filtro anti-DC).

    Pendiente: sobremuestreo (juce::dsp::Oversampling) para reducir el aliasing
    con drive alto. Introduce latencia, así que antes hay que tener compensación
    de latencia entre pistas para que los stems sigan alineados.
*/
class SaturationEffect final : public AudioEffect
{
public:
    SaturationEffect();

    void prepare (const juce::dsp::ProcessSpec& spec) override;
    void process (juce::dsp::AudioBlock<float> block) noexcept override;
    void reset() noexcept override;

private:
    enum class Mode { soft, hard, tube };

    struct DcBlocker
    {
        float process (float x, float coefficient) noexcept
        {
            const auto y = x - x1 + coefficient * y1;
            x1 = x;
            y1 = y;
            return y;
        }

        void reset() noexcept   { x1 = y1 = 0.0f; }

        float x1 = 0.0f, y1 = 0.0f;
    };

    static float shape (float x, Mode mode) noexcept;

    Parameter& drive;
    Parameter& mode;
    Parameter& mix;
    Parameter& output;

    juce::SmoothedValue<float> driveGain, mixAmount, outputGain;
    std::array<DcBlocker, 2> dcBlockers;
    float dcCoefficient = 0.999f;
};
}
