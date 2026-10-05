#include "ParameterAttachments.h"

#include "Utils/Strings.h"

namespace stemlab
{
SliderAttachment::SliderAttachment (Parameter& p, juce::Slider& s)
    : ParameterAttachment (p), slider (s)
{
    const auto& range = parameter.getRange();
    slider.setNormalisableRange (juce::NormalisableRange<double> (static_cast<double> (range.start),
                                                                  static_cast<double> (range.end),
                                                                  static_cast<double> (range.interval),
                                                                  static_cast<double> (range.skew),
                                                                  range.symmetricSkew));

    slider.textFromValueFunction = [this] (double value) { return parameter.toText (static_cast<float> (value)); };
    slider.setDoubleClickReturnValue (true, parameter.getDefault());
    slider.setValue (parameter.get(), juce::dontSendNotification);
    slider.updateText();

    slider.onValueChange = [this] { parameter.set (static_cast<float> (slider.getValue())); };
}

SliderAttachment::~SliderAttachment()
{
    slider.onValueChange = nullptr;
    slider.textFromValueFunction = nullptr;
}

void SliderAttachment::parameterChanged (Parameter&)
{
    slider.setValue (parameter.get(), juce::dontSendNotification);
}

//==============================================================================
ButtonAttachment::ButtonAttachment (Parameter& p, juce::Button& b)
    : ParameterAttachment (p), button (b)
{
    button.setClickingTogglesState (true);
    button.setToggleState (parameter.getBool(), juce::dontSendNotification);
    button.onClick = [this] { parameter.set (button.getToggleState() ? 1.0f : 0.0f); };
}

ButtonAttachment::~ButtonAttachment()
{
    button.onClick = nullptr;
}

void ButtonAttachment::parameterChanged (Parameter&)
{
    button.setToggleState (parameter.getBool(), juce::dontSendNotification);
}

//==============================================================================
ComboBoxAttachment::ComboBoxAttachment (Parameter& p, juce::ComboBox& c)
    : ParameterAttachment (p), comboBox (c)
{
    comboBox.clear (juce::dontSendNotification);

    // Las opciones ("Suave", "Dura"...) están en español: se traducen al mostrarlas.
    for (int i = 0; i < parameter.getChoices().size(); ++i)
        comboBox.addItem (tr (parameter.getChoices()[i]), i + 1);

    comboBox.setSelectedId (parameter.getIndex() + 1, juce::dontSendNotification);
    comboBox.onChange = [this] { parameter.set (static_cast<float> (comboBox.getSelectedId() - 1)); };
}

ComboBoxAttachment::~ComboBoxAttachment()
{
    comboBox.onChange = nullptr;
}

void ComboBoxAttachment::parameterChanged (Parameter&)
{
    comboBox.setSelectedId (parameter.getIndex() + 1, juce::dontSendNotification);
}
}
