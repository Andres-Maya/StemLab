#pragma once

#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <memory>

namespace stemlab
{
/**
    Parámetro compartido entre la interfaz y el hilo de audio.

    - El valor vive en un std::atomic<float>: el hilo de audio lo lee con get()
      sin locks ni reservas de memoria.
    - Solo el hilo de mensajes lo modifica con set(), y en ese momento avisa a
      sus listeners (sliders, botones...) para que se mantengan sincronizados.

    Lo usan tanto los efectos DSP como las pistas (volumen, paneo, mute, solo),
    de modo que la UI y la serialización del proyecto funcionan igual para todos.
*/
class Parameter
{
public:
    enum class Kind
    {
        continuous,
        toggle,
        choice
    };

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void parameterChanged (Parameter&) = 0;
    };

    static std::unique_ptr<Parameter> continuous (juce::String id, juce::String name,
                                                  juce::NormalisableRange<float> range,
                                                  float defaultValue, juce::String unit = {});

    static std::unique_ptr<Parameter> toggle (juce::String id, juce::String name, bool defaultValue);

    static std::unique_ptr<Parameter> choice (juce::String id, juce::String name,
                                              juce::StringArray choices, int defaultIndex);

    /** Rango de frecuencias con el centro geométrico en la mitad del control. */
    static juce::NormalisableRange<float> frequencyRange (float minHz, float maxHz);

    //==========================================================================
    // Lectura: segura desde cualquier hilo, incluido el de audio.
    float get() const noexcept      { return value.load (std::memory_order_relaxed); }
    bool getBool() const noexcept   { return get() >= 0.5f; }
    int getIndex() const noexcept   { return juce::roundToInt (get()); }

    //==========================================================================
    // Escritura y listeners: solo desde el hilo de mensajes.
    void set (float newValue);
    void resetToDefault()           { set (defaultValue); }

    void addListener (Listener* listener)      { listeners.add (listener); }
    void removeListener (Listener* listener)   { listeners.remove (listener); }

    /** Permite personalizar cómo se muestra el valor (p. ej. "L 30" en el paneo). */
    void setTextFormatter (std::function<juce::String (float)> formatter)   { textFormatter = std::move (formatter); }

    juce::String toText (float valueToFormat) const;
    juce::String toText() const     { return toText (get()); }

    juce::var toVar() const;
    void fromVar (const juce::var& state);

    //==========================================================================
    const juce::String& getId() const noexcept                   { return id; }
    const juce::String& getName() const noexcept                 { return name; }
    const juce::String& getUnit() const noexcept                 { return unit; }
    Kind getKind() const noexcept                                { return kind; }
    const juce::NormalisableRange<float>& getRange() const noexcept { return range; }
    float getDefault() const noexcept                            { return defaultValue; }
    const juce::StringArray& getChoices() const noexcept         { return choices; }

private:
    Parameter (juce::String id, juce::String name, Kind kind, juce::NormalisableRange<float> range,
               float defaultValue, juce::String unit, juce::StringArray choices);

    const juce::String id, name, unit;
    const Kind kind;
    const juce::NormalisableRange<float> range;
    const float defaultValue;
    const juce::StringArray choices;

    std::atomic<float> value;
    juce::ListenerList<Listener> listeners;
    std::function<juce::String (float)> textFormatter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Parameter)
};
}
