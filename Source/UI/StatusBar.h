#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AI/AIProcessManager.h"
#include "Project/ProjectManager.h"

#include <functional>

namespace stemlab
{
/** Barra inferior: mensajes, progreso de la carga de audio y, durante la
    separación por IA, su estado con el porcentaje, "Ver progreso" (abre la
    ventana de la separación) y "Cancelar". */
class StatusBar final : public juce::Component,
                        private juce::Timer
{
public:
    StatusBar (AIProcessManager& ai, ProjectManager& projects);

    void setMessage (const juce::String& message);

    std::function<void()> onCancel;
    std::function<void()> onShowSeparation;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    AIProcessManager& ai;
    ProjectManager& projects;

    juce::String message;
    juce::Label messageLabel;
    juce::Label projectLabel;
    double progressValue = 0.0;
    juce::ProgressBar progressBar { progressValue };
    juce::TextButton cancelButton;              // "Cancelar"
    juce::TextButton showSeparationButton;      // "Ver progreso"

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StatusBar)
};
}
