#include "AudioEffect.h"

namespace stemlab
{
AudioEffect::AudioEffect (juce::String effectId, juce::String displayName, bool enabledByDefault)
    : id (std::move (effectId)),
      name (std::move (displayName)),
      enabled (Parameter::toggle ("enabled", "Activo", enabledByDefault))
{
}

Parameter& AudioEffect::addParameter (std::unique_ptr<Parameter> parameter)
{
    jassert (parameter != nullptr && findParameter (parameter->getId()) == nullptr);

    parameters.push_back (std::move (parameter));
    return *parameters.back();
}

Parameter* AudioEffect::findParameter (const juce::String& parameterId) const
{
    for (const auto& parameter : parameters)
        if (parameter->getId() == parameterId)
            return parameter.get();

    return nullptr;
}

juce::var AudioEffect::toVar() const
{
    auto* state = new juce::DynamicObject();
    state->setProperty (enabled->getId(), enabled->toVar());

    for (const auto& parameter : parameters)
        state->setProperty (parameter->getId(), parameter->toVar());

    return juce::var (state);
}

void AudioEffect::fromVar (const juce::var& state)
{
    if (auto* object = state.getDynamicObject())
    {
        enabled->fromVar (object->getProperty (enabled->getId()));

        for (const auto& parameter : parameters)
            parameter->fromVar (object->getProperty (parameter->getId()));
    }
}
}
