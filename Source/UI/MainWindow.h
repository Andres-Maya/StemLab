#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace stemlab
{
class AudioEngine;
class ProjectManager;
class AIProcessManager;

/** Ventana de escritorio que contiene MainComponent. */
class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow (const juce::String& name, AudioEngine& engine, ProjectManager& projects, AIProcessManager& ai);

    void closeButtonPressed() override;

    /** Cerrar la aplicación preguntando antes si hay cambios sin guardar. */
    void requestQuit();

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};
}
