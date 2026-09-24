#pragma once

#include <juce_dsp/juce_dsp.h>

#include "Utils/Parameter.h"

#include <memory>
#include <vector>

namespace stemlab
{
/**
    Interfaz común de todos los efectos.

    El motor solo conoce esta interfaz, así que añadir un efecto nuevo (Reverb,
    Delay...) consiste en crear una subclase y registrarla en EffectChain, sin
    tocar AudioEngine ni AudioMixer.

    Reglas de hilos:
      - prepare(): hilo de mensajes o con el audio parado. Aquí se reserva memoria.
      - process() / reset(): hilo de audio. Prohibido reservar memoria, usar
        locks bloqueantes o hacer E/S.
      - Los parámetros son Parameter (atómicos): la UI los cambia y process()
        los lee al principio de cada bloque.
*/
class AudioEffect
{
public:
    AudioEffect (juce::String effectId, juce::String displayName, bool enabledByDefault);
    virtual ~AudioEffect() = default;

    virtual void prepare (const juce::dsp::ProcessSpec& spec) = 0;
    virtual void process (juce::dsp::AudioBlock<float> block) noexcept = 0;
    virtual void reset() noexcept = 0;

    const juce::String& getId() const noexcept      { return id; }
    const juce::String& getName() const noexcept    { return name; }

    Parameter& getEnabledParameter() noexcept       { return *enabled; }
    bool isEnabled() const noexcept                 { return enabled->getBool(); }

    /** Parámetros propios del efecto (sin incluir "enabled"). */
    const std::vector<std::unique_ptr<Parameter>>& getParameters() const noexcept   { return parameters; }
    Parameter* findParameter (const juce::String& parameterId) const;

    juce::var toVar() const;
    void fromVar (const juce::var& state);

protected:
    Parameter& addParameter (std::unique_ptr<Parameter> parameter);

private:
    const juce::String id, name;
    std::unique_ptr<Parameter> enabled;
    std::vector<std::unique_ptr<Parameter>> parameters;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEffect)
};
}
