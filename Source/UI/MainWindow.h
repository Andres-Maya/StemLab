#pragma once

#include <juce_data_structures/juce_data_structures.h>
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
    MainWindow (const juce::String& name, AudioEngine& engine, ProjectManager& projects, AIProcessManager& ai,
                juce::PropertiesFile* settings);

    void closeButtonPressed() override;

    /** Cerrar la aplicación preguntando antes si hay cambios sin guardar. */
    void requestQuit();

    /** Abrir un proyecto (desde la línea de comandos, "Abrir con"...). */
    void openProjectFile (const juce::File& file);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};
}
