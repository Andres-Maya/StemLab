#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "StemLabLookAndFeel.h"
#include "Utils/Localisation.h"

namespace stemlab
{
class AudioEngine;
class ProjectManager;
class AIProcessManager;
class MainComponent;

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

    /** Cambia el idioma y el tema: los guarda en los ajustes y vuelve a crear
        la interfaz (los componentes toman sus textos y colores al construirse).
        El proyecto, el motor de audio y la pista seleccionada no cambian.
        showTour: abre el tutorial en la interfaz nueva. */
    void setInterface (Language language, Theme theme, bool showTour = false);

private:
    MainComponent* createContent();

    AudioEngine& engine;
    ProjectManager& projects;
    AIProcessManager& ai;
    juce::PropertiesFile* settings = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};
}
