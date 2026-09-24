#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "DSP/AudioEffect.h"
#include "ParameterAttachments.h"

#include <memory>
#include <vector>

namespace stemlab
{
/**
    Panel genérico de un efecto: se construye a partir de sus Parameter, así
    que un efecto nuevo aparece en el mezclador sin escribir UI específica.
*/
class EffectPanel final : public juce::Component,
                          private Parameter::Listener
{
public:
    explicit EffectPanel (AudioEffect& effect);
    ~EffectPanel() override;

    int getPreferredWidth() const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Control
    {
        std::unique_ptr<juce::Component> editor;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<ParameterAttachment> attachment;   // se destruye antes que editor
    };

    void parameterChanged (Parameter&) override;
    void updateEnablement();
    int getNumColumns() const;

    AudioEffect& effect;
    juce::ToggleButton enableButton;
    std::unique_ptr<ButtonAttachment> enableAttachment;
    std::vector<Control> controls;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectPanel)
};
}
