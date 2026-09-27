#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_data_structures/juce_data_structures.h>

#include "AI/AIProcessManager.h"
#include "AI/DemucsSeparator.h"
#include "Application/FileAssociation.h"
#include "Audio/AudioEngine.h"
#include "Project/ProjectManager.h"
#include "Project/Project.h"
#include "UI/MainWindow.h"
#include "UI/StemLabLookAndFeel.h"

namespace stemlab
{
namespace
{
    /** El proyecto que llega por la línea de comandos: al abrir un .stemlab
        con StemLab ("Abrir con", arrastrarlo sobre StemLab.exe...). */
    juce::File projectFileFromCommandLine (const juce::String& commandLine)
    {
        for (const auto& token : juce::StringArray::fromTokens (commandLine, true))
        {
            const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (token.unquoted());

            if (file.existsAsFile() && Project::isProjectFile (file))
                return file;
        }

        return {};
    }
}

/**
    Punto de entrada. Crea los servicios en orden (motor → proyectos → IA →
    ventana) y los destruye en orden inverso: la interfaz desaparece antes que
    los objetos que observa.
*/
class StemLabApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override     { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override              { return false; }

    void initialise (const juce::String& commandLine) override
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "StemLab";
        options.folderName = "StemLab";
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        settings.setStorageParameters (options);

        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        engine = std::make_unique<AudioEngine>();
        engine->setFollowSystemOutput (settings.getUserSettings()->getBoolValue ("followSystemOutput", true));
        engine->getInputGain().set (static_cast<float> (settings.getUserSettings()->getDoubleValue ("inputGain", 18.0)));
        const auto savedDevice = settings.getUserSettings()->getXmlValue ("audioDevice");
        const auto audioError = engine->initialise (savedDevice.get());

        projects = std::make_unique<ProjectManager> (*engine);

        auto separator = std::make_unique<DemucsSeparator> (DemucsSeparator::findDefaultSettings());
        separator->setCurrentModel (settings.getUserSettings()->getValue ("aiModel", "htdemucs"));
        ai = std::make_unique<AIProcessManager> (std::move (separator));

        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *engine, *projects, *ai,
                                                   settings.getUserSettings());

        if (const auto file = projectFileFromCommandLine (commandLine); file != juce::File())
            mainWindow->openProjectFile (file);

        // Los .stemlab con el icono de StemLab y doble clic para abrirlos
        // (solo escribe en el registro del usuario si no estaba ya así).
        FileAssociation::registerProjectFiles (juce::File::getSpecialLocation (juce::File::currentExecutableFile));

        if (audioError.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Audio",
                                                    "No se pudo abrir el dispositivo de audio:\n" + audioError);
    }

    void shutdown() override
    {
        if (auto* userSettings = settings.getUserSettings())
        {
            if (engine != nullptr)
                if (const auto deviceState = engine->getDeviceManager().createStateXml())
                    userSettings->setValue ("audioDevice", deviceState.get());

            if (engine != nullptr)
            {
                userSettings->setValue ("followSystemOutput", engine->isFollowingSystemOutput());
                userSettings->setValue ("inputGain", engine->getInputGain().get());
            }

            if (ai != nullptr)
                userSettings->setValue ("aiModel", ai->getSeparator().getCurrentModel());
        }

        settings.saveIfNeeded();

        mainWindow.reset();
        ai.reset();
        projects.reset();
        engine.reset();

        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        // Cerrar ventana, Archivo > Salir, Alt+F4 o apagado de Windows: si hay
        // cambios sin guardar, la ventana pregunta antes de salir.
        if (mainWindow != nullptr)
            mainWindow->requestQuit();
        else
            quit();
    }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        // Solo se permite una instancia: si se abre un .stemlab con StemLab ya
        // abierto, se abre en esta ventana.
        if (mainWindow != nullptr)
        {
            mainWindow->toFront (true);

            if (const auto file = projectFileFromCommandLine (commandLine); file != juce::File())
                mainWindow->openProjectFile (file);
        }
    }

private:
    StemLabLookAndFeel lookAndFeel;
    juce::ApplicationProperties settings;

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<ProjectManager> projects;
    std::unique_ptr<AIProcessManager> ai;
    std::unique_ptr<MainWindow> mainWindow;
};
}

START_JUCE_APPLICATION (stemlab::StemLabApplication)
