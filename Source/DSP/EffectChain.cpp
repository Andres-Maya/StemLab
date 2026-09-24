#include "EffectChain.h"

#include "CompressorEffect.h"
#include "EqualizerEffect.h"
#include "GainEffect.h"
#include "LimiterEffect.h"
#include "SaturationEffect.h"

namespace stemlab
{
EffectChain::EffectChain()
{
    effects.push_back (std::make_unique<GainEffect>());
    effects.push_back (std::make_unique<SaturationEffect>());
    effects.push_back (std::make_unique<EqualizerEffect>());
    effects.push_back (std::make_unique<CompressorEffect>());
    effects.push_back (std::make_unique<LimiterEffect>());

    wasEnabled.resize (effects.size(), 0);
}

void EffectChain::prepare (const juce::dsp::ProcessSpec& spec)
{
    for (size_t i = 0; i < effects.size(); ++i)
    {
        effects[i]->prepare (spec);
        wasEnabled[i] = effects[i]->isEnabled() ? 1 : 0;
    }
}

void EffectChain::process (juce::dsp::AudioBlock<float> block) noexcept
{
    for (size_t i = 0; i < effects.size(); ++i)
    {
        auto& effect = *effects[i];
        const auto enabled = effect.isEnabled();

        if (enabled && wasEnabled[i] == 0)
            effect.reset();

        wasEnabled[i] = enabled ? 1 : 0;

        if (enabled)
            effect.process (block);
    }
}

void EffectChain::reset() noexcept
{
    for (auto& effect : effects)
        effect->reset();
}

AudioEffect* EffectChain::findEffect (const juce::String& effectId) const
{
    for (const auto& effect : effects)
        if (effect->getId() == effectId)
            return effect.get();

    return nullptr;
}

juce::var EffectChain::toVar() const
{
    auto* state = new juce::DynamicObject();

    for (const auto& effect : effects)
        state->setProperty (effect->getId(), effect->toVar());

    return juce::var (state);
}

void EffectChain::fromVar (const juce::var& state)
{
    if (auto* object = state.getDynamicObject())
        for (const auto& effect : effects)
            effect->fromVar (object->getProperty (effect->getId()));
}
}
