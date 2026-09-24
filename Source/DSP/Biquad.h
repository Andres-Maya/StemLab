#pragma once

#include <array>

namespace stemlab
{
/**
    Filtro biquad (transposed direct form II) para un canal.

    Los coeficientes se calculan con juce::dsp::IIR::ArrayCoefficients, que
    devuelve un std::array sin reservar memoria, así que se pueden recalcular
    desde el hilo de audio cuando cambia un parámetro.
*/
struct Biquad
{
    /** Recibe { b0, b1, b2, a0, a1, a2 } y normaliza por a0. */
    void setCoefficients (const std::array<float, 6>& c) noexcept
    {
        const auto a0Inverse = 1.0f / c[3];
        b0 = c[0] * a0Inverse;
        b1 = c[1] * a0Inverse;
        b2 = c[2] * a0Inverse;
        a1 = c[4] * a0Inverse;
        a2 = c[5] * a0Inverse;
    }

    float process (float x) noexcept
    {
        const auto y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void reset() noexcept   { z1 = z2 = 0.0f; }

    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;
};
}
