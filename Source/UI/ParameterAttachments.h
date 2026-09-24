#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Utils/Parameter.h"

namespace stemlab
{
/**
    Enlaces bidireccionales control ↔ Parameter.

    El control escribe en el parámetro al moverse, y el parámetro actualiza el
    control cuando cambia desde otro sitio (otro control, cargar un proyecto).
    Deben destruirse antes que el control y que el parámetro.
*/
class ParameterAttachment : protected Parameter::Listener
{
public:
    explicit ParameterAttachment (Parameter& p) : parameter (p)   { parameter.addListener (this); }
    ~ParameterAttachment() override                                { parameter.removeListener (this); }

protected:
    Parameter& parameter;

    JUCE_DECLARE_NON_COPYABLE (ParameterAttachment)
};

class SliderAttachment final : public ParameterAttachment
{
public:
    SliderAttachment (Parameter&, juce::Slider&);
    ~SliderAttachment() override;

private:
    void parameterChanged (Parameter&) override;
    juce::Slider& slider;
};

class ButtonAttachment final : public ParameterAttachment
{
public:
    ButtonAttachment (Parameter&, juce::Button&);
    ~ButtonAttachment() override;

private:
    void parameterChanged (Parameter&) override;
    juce::Button& button;
};

class ComboBoxAttachment final : public ParameterAttachment
{
public:
    ComboBoxAttachment (Parameter&, juce::ComboBox&);
    ~ComboBoxAttachment() override;

private:
    void parameterChanged (Parameter&) override;
    juce::ComboBox& comboBox;
};
}
