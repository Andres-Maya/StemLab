#include "Parameter.h"

namespace stemlab
{
Parameter::Parameter (juce::String paramId, juce::String displayName, Kind parameterKind,
                      juce::NormalisableRange<float> valueRange, float defaultVal,
                      juce::String unitSuffix, juce::StringArray choiceNames)
    : id (std::move (paramId)),
      name (std::move (displayName)),
      unit (std::move (unitSuffix)),
      kind (parameterKind),
      range (std::move (valueRange)),
      defaultValue (range.snapToLegalValue (defaultVal)),
      choices (std::move (choiceNames)),
      value (defaultValue)
{
}

std::unique_ptr<Parameter> Parameter::continuous (juce::String id, juce::String name,
                                                  juce::NormalisableRange<float> range,
                                                  float defaultValue, juce::String unit)
{
    return std::unique_ptr<Parameter> (new Parameter (std::move (id), std::move (name), Kind::continuous,
                                                      std::move (range), defaultValue, std::move (unit), {}));
}

std::unique_ptr<Parameter> Parameter::toggle (juce::String id, juce::String name, bool defaultValue)
{
    return std::unique_ptr<Parameter> (new Parameter (std::move (id), std::move (name), Kind::toggle,
                                                      juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f),
                                                      defaultValue ? 1.0f : 0.0f, {}, {}));
}

std::unique_ptr<Parameter> Parameter::choice (juce::String id, juce::String name,
                                              juce::StringArray choices, int defaultIndex)
{
    jassert (! choices.isEmpty());
    const auto maxIndex = static_cast<float> (juce::jmax (1, choices.size() - 1));

    return std::unique_ptr<Parameter> (new Parameter (std::move (id), std::move (name), Kind::choice,
                                                      juce::NormalisableRange<float> (0.0f, maxIndex, 1.0f),
                                                      static_cast<float> (defaultIndex),
                                                      {}, std::move (choices)));
}

juce::NormalisableRange<float> Parameter::frequencyRange (float minHz, float maxHz)
{
    juce::NormalisableRange<float> frequencies (minHz, maxHz, 1.0f);
    frequencies.setSkewForCentre (std::sqrt (minHz * maxHz));
    return frequencies;
}

void Parameter::set (float newValue)
{
    jassert (juce::MessageManager::existsAndIsCurrentThread());

    newValue = range.snapToLegalValue (newValue);

    if (juce::exactlyEqual (value.load (std::memory_order_relaxed), newValue))
        return;

    value.store (newValue, std::memory_order_relaxed);
    listeners.call ([this] (Listener& l) { l.parameterChanged (*this); });
}

juce::String Parameter::toText (float v) const
{
    if (textFormatter != nullptr)
        return textFormatter (v);

    switch (kind)
    {
        case Kind::toggle:
            return v >= 0.5f ? "On" : "Off";

        case Kind::choice:
            return choices[juce::roundToInt (v)];

        case Kind::continuous:
            break;
    }

    if (unit == "Hz" && v >= 1000.0f)
        return juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz";

    const auto magnitude = std::abs (v);
    const int decimals = range.interval >= 1.0f ? 0 : (magnitude >= 100.0f ? 0 : (magnitude >= 10.0f ? 1 : 2));
    auto text = juce::String (v, decimals);

    return unit.isEmpty() ? text : text + " " + unit;
}

juce::var Parameter::toVar() const
{
    switch (kind)
    {
        case Kind::toggle:  return getBool();
        case Kind::choice:  return getIndex();
        case Kind::continuous: break;
    }

    return static_cast<double> (get());
}

void Parameter::fromVar (const juce::var& state)
{
    if (state.isVoid() || state.isUndefined())
        return;

    set (static_cast<float> (state));
}
}
