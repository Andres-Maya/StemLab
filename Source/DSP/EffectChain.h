#pragma once

#include "AudioEffect.h"

namespace stemlab
{
/**
    Cadena de efectos de una pista:

        Gain → Saturación → EQ → Compresor → Limiter

    El orden es fijo por ahora. Cada efecto se puede activar o desactivar; los
    desactivados no consumen CPU.
*/
class EffectChain
{
public:
    EffectChain();

    void prepare (const juce::dsp::ProcessSpec& spec);
    void process (juce::dsp::AudioBlock<float> block) noexcept;
    void reset() noexcept;

    const std::vector<std::unique_ptr<AudioEffect>>& getEffects() const noexcept   { return effects; }
    AudioEffect* findEffect (const juce::String& effectId) const;

    juce::var toVar() const;
    void fromVar (const juce::var& state);

private:
    std::vector<std::unique_ptr<AudioEffect>> effects;

    // Estado "activo" del bloque anterior: al reactivar un efecto se reinicia
    // para que no arrastre envolventes o filtros de hace minutos.
    std::vector<char> wasEnabled;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectChain)
};
}
