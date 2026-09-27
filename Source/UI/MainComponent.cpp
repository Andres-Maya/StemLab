#include "MainComponent.h"

#include "AudioSettingsComponent.h"
#include "StemLabLookAndFeel.h"
#include "Utils/Strings.h"

namespace stemlab
{
namespace
{
    enum MenuIds
    {
        newProjectId = 1,
        openProjectId,
        saveProjectId,
        saveProjectAsId,
        importAudioId,
        quitId,
        deleteTrackId,
        showFolderId,
        playPauseId,
        stopId,
        recordId,
        audioSettingsId,
        separateId,
        cancelSeparationId,
        aboutId,
        modelBaseId = 1000
    };

    void addItem (juce::PopupMenu& menu, int id, const juce::String& text,
                  const juce::String& shortcut = {}, bool enabled = true, bool ticked = false)
    {
        juce::PopupMenu::Item item (text);
        item.itemID = id;
        item.shortcutKeyDescription = shortcut;
        item.isEnabled = enabled;
        item.isTicked = ticked;
        menu.addItem (std::move (item));
    }

    juce::File defaultProjectsFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("StemLab");
    }
}

//==============================================================================
MainComponent::MainComponent (AudioEngine& audioEngine, ProjectManager& projectManager, AIProcessManager& aiManager)
    : engine (audioEngine),
      projects (projectManager),
      ai (aiManager),
      transportBar (engine, projects),
      trackList (engine),
      statusBar (ai, projects)
{
   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (this);
   #else
    menuBar.setModel (this);
    addAndMakeVisible (menuBar);
   #endif

    transportBar.onToStart = [this] { engine.getTransport().setPosition (0); };
    transportBar.onPlayPause = [this] { togglePlayPause(); };
    transportBar.onStop = [this] { stop(); };
    transportBar.onRecord = [this] { toggleRecording(); };
    addAndMakeVisible (transportBar);

    trackList.onSelectionChanged = [this] (std::shared_ptr<AudioTrack> track) { mixer.setTrack (std::move (track)); };
    trackList.onDeleteRequested = [this] (AudioTrack& track) { projects.removeTrack (track); };
    addAndMakeVisible (trackList);

    addAndMakeVisible (mixer);

    statusBar.onCancel = [this] { ai.cancel(); };
    addAndMakeVisible (statusBar);

    projects.addChangeListener (this);
    trackList.refresh();

    setWantsKeyboardFocus (true);
    setSize (1280, 820);
}

MainComponent::~MainComponent()
{
    projects.removeChangeListener (this);

   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (nullptr);
   #else
    menuBar.setModel (nullptr);
   #endif
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);
}

void MainComponent::resized()
{
    auto bounds = getLocalBounds();

   #if ! JUCE_MAC
    menuBar.setBounds (bounds.removeFromTop (getLookAndFeel().getDefaultMenuBarHeight()));
   #endif

    transportBar.setBounds (bounds.removeFromTop (60));
    statusBar.setBounds (bounds.removeFromBottom (30));
    mixer.setBounds (bounds.removeFromBottom (MixerView::preferredHeight));
    trackList.setBounds (bounds);
}

void MainComponent::parentHierarchyChanged()
{
    updateWindowTitle();
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const juce::ModifierKeys command (juce::ModifierKeys::commandModifier);
    const juce::ModifierKeys commandShift (juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);

    if (key == juce::KeyPress::spaceKey)                    { togglePlayPause(); return true; }
    if (key == juce::KeyPress::homeKey)                     { engine.getTransport().setPosition (0); return true; }
    if (key == juce::KeyPress::deleteKey)                   { deleteSelectedTrack(); return true; }
    if (key == juce::KeyPress ('r'))                        { toggleRecording(); return true; }
    if (key == juce::KeyPress ('n', command, 0))            { newProject(); return true; }
    if (key == juce::KeyPress ('o', command, 0))            { openProject(); return true; }
    if (key == juce::KeyPress ('s', commandShift, 0))       { saveProjectAs(); return true; }
    if (key == juce::KeyPress ('s', command, 0))            { saveProject(); return true; }
    if (key == juce::KeyPress ('i', command, 0))            { importAudio(); return true; }

    return false;
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    trackList.refresh();
    updateWindowTitle();
    menuItemsChanged();
}

//==============================================================================
juce::StringArray MainComponent::getMenuBarNames()
{
    return juce::StringArray ("Archivo", "Editar", "Proyecto", "Audio", "IA", "Ayuda");
}

juce::PopupMenu MainComponent::getMenuForIndex (int topLevelMenuIndex, const juce::String&)
{
    juce::PopupMenu menu;
    const auto hasTracks = ! engine.getMixer().getTracks().empty();
    const auto aiBusy = ai.isBusy();

    switch (topLevelMenuIndex)
    {
        case 0:
            addItem (menu, newProjectId, "Nuevo proyecto", "Ctrl+N");
            addItem (menu, openProjectId, "Abrir proyecto...", "Ctrl+O");
            addItem (menu, saveProjectId, "Guardar proyecto", "Ctrl+S");
            addItem (menu, saveProjectAsId, "Guardar proyecto como...", "Ctrl+Shift+S");
            menu.addSeparator();
            addItem (menu, importAudioId, "Importar audio...", "Ctrl+I");
            menu.addSeparator();
            addItem (menu, quitId, "Salir");
            break;

        case 1:
            addItem (menu, deleteTrackId, "Eliminar pista seleccionada", "Supr", trackList.getSelectedTrack() != nullptr);
            break;

        case 2:
            addItem (menu, showFolderId, "Mostrar carpeta del proyecto");
            break;

        case 3:
            addItem (menu, playPauseId, engine.getTransport().isPlaying() ? "Pausa" : "Reproducir", "Espacio");
            addItem (menu, stopId, "Detener");
            addItem (menu, recordId, engine.isRecording() ? "Detener grabación"_u8 : juce::String ("Grabar"), "R");
            menu.addSeparator();
            addItem (menu, audioSettingsId, "Configuración de audio..."_u8);
            break;

        case 4:
        {
            addItem (menu, separateId, "Separar instrumentos", {}, hasTracks && ! aiBusy);
            addItem (menu, cancelSeparationId, "Cancelar separación"_u8, {}, aiBusy);
            menu.addSeparator();
            menu.addSectionHeader ("Modelo");

            const auto models = ai.getSeparator().getAvailableModels();
            const auto current = ai.getSeparator().getCurrentModel();

            for (size_t i = 0; i < models.size(); ++i)
                addItem (menu, modelBaseId + static_cast<int> (i), models[i].id + "  -  " + models[i].description,
                         {}, ! aiBusy, models[i].id == current);
            break;
        }

        case 5:
            addItem (menu, aboutId, "Acerca de StemLab");
            break;

        default:
            break;
    }

    return menu;
}

void MainComponent::menuItemSelected (int menuItemID, int)
{
    if (menuItemID >= modelBaseId)
    {
        const auto models = ai.getSeparator().getAvailableModels();
        const auto index = static_cast<size_t> (menuItemID - modelBaseId);

        if (index < models.size() && ! ai.isBusy())
        {
            ai.getSeparator().setCurrentModel (models[index].id);
            statusBar.setMessage ("Modelo de separación: "_u8 + models[index].id);
        }

        return;
    }

    switch (menuItemID)
    {
        case newProjectId:          newProject(); break;
        case openProjectId:         openProject(); break;
        case saveProjectId:         saveProject(); break;
        case saveProjectAsId:       saveProjectAs(); break;
        case importAudioId:         importAudio(); break;
        case quitId:                juce::JUCEApplication::getInstance()->systemRequestedQuit(); break;
        case deleteTrackId:         deleteSelectedTrack(); break;
        case showFolderId:          showProjectFolder(); break;
        case playPauseId:           togglePlayPause(); break;
        case stopId:                stop(); break;
        case recordId:              toggleRecording(); break;
        case audioSettingsId:       showAudioSettings(); break;
        case separateId:            separateInstruments(); break;
        case cancelSeparationId:    ai.cancel(); break;
        case aboutId:               showAbout(); break;
        default:                    break;
    }
}

//==============================================================================
bool MainComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& path : files)
    {
        const juce::File file (path);

        if (file.getFileName() == "project.json" || projects.canImport (file))
            return true;
    }

    return false;
}

void MainComponent::filesDropped (const juce::StringArray& files, int, int)
{
    juce::Array<juce::File> audioFiles;

    for (const auto& path : files)
    {
        const juce::File file (path);

        if (file.getFileName() == "project.json")
        {
            if (ensureIdle ("abrir un proyecto"))
                confirmDiscard ([this, file] { projects.openProject (file, resultHandler ("Proyecto abierto.")); });

            return;
        }

        if (projects.canImport (file))
            audioFiles.add (file);
    }

    if (! audioFiles.isEmpty())
    {
        statusBar.setMessage ("Importando...");
        projects.importAudio (audioFiles, resultHandler ("Audio importado."));
    }
}

//==============================================================================
void MainComponent::newProject()
{
    if (! ensureIdle ("crear un proyecto"))
        return;

    confirmDiscard ([this]
    {
        projects.newProject();
        statusBar.setMessage ("Proyecto nuevo.");
    });
}

void MainComponent::openProject()
{
    if (! ensureIdle ("abrir un proyecto"))
        return;

    confirmDiscard ([this]
    {
        const auto folder = defaultProjectsFolder();
        fileChooser = std::make_unique<juce::FileChooser> ("Abrir proyecto (project.json)",
                                                           folder.isDirectory() ? folder : juce::File(),
                                                           "project.json");

        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& chooser)
        {
            const auto file = chooser.getResult();

            if (file == juce::File())
                return;

            statusBar.setMessage ("Abriendo proyecto...");
            projects.openProject (file, resultHandler ("Proyecto abierto."));
        });
    });
}

void MainComponent::saveProject()
{
    if (projects.getProject().isTemporary())
    {
        saveProjectAs();
        return;
    }

    reportResult (projects.save(), "Proyecto guardado.");
}

void MainComponent::saveProjectAs()
{
    // Guardar como copia la carpeta de la sesión: no puede coincidir con una
    // separación o una carga que están escribiendo o leyendo en ella.
    if (! ensureIdle ("guardar el proyecto"))
        return;

    const auto folder = defaultProjectsFolder();
    folder.createDirectory();

    const auto& project = projects.getProject();
    const auto suggestedName = project.isTemporary() ? juce::String ("MiProyecto") : project.getName();

    fileChooser = std::make_unique<juce::FileChooser> ("Guardar proyecto como (nombre de la carpeta)",
                                                       folder.getChildFile (suggestedName), juce::String());

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& chooser)
    {
        const auto target = chooser.getResult();

        if (target == juce::File())
            return;

        reportResult (projects.saveAs (target), "Proyecto guardado en " + target.getFullPathName());
    });
}

void MainComponent::importAudio()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Importar audio",
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                       engine.getFormatManager().getWildcardForAllFormats());

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this] (const juce::FileChooser& chooser)
    {
        const auto files = chooser.getResults();

        if (files.isEmpty())
            return;

        statusBar.setMessage ("Importando...");
        projects.importAudio (files, resultHandler ("Audio importado."));
    });
}

void MainComponent::deleteSelectedTrack()
{
    if (const auto track = trackList.getSelectedTrack())
        projects.removeTrack (*track);
}

void MainComponent::showProjectFolder()
{
    const auto folder = projects.getProject().getDirectory();
    folder.createDirectory();
    folder.startAsProcess();
}

void MainComponent::showAudioSettings()
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new AudioSettingsComponent (engine));
    options.dialogTitle = "Configuración de audio"_u8;
    options.dialogBackgroundColour = Palette::panel;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

//==============================================================================
void MainComponent::togglePlayPause()
{
    if (engine.isRecording())
    {
        finishRecording();
        return;
    }

    engine.getTransport().togglePlayPause();
}

void MainComponent::stop()
{
    if (engine.isRecording())
        finishRecording();

    engine.getTransport().stop();
}

void MainComponent::toggleRecording()
{
    if (engine.isRecording())
    {
        finishRecording();
        return;
    }

    const auto result = engine.startRecording (projects.createRecordingFile());

    if (result.failed())
        showError ("No se puede grabar", result.getErrorMessage());
    else
        statusBar.setMessage ("Grabando... pulsa R o Detener para terminar.");
}

void MainComponent::finishRecording()
{
    const auto recording = engine.stopRecording();
    engine.getTransport().pause();

    statusBar.setMessage ("Procesando la grabación..."_u8);
    projects.addRecording (recording, resultHandler ("Grabación añadida como pista nueva."_u8));
}

void MainComponent::separateInstruments()
{
    if (ai.isBusy())
        return;

    auto source = trackList.getSelectedTrack();

    if (source == nullptr && ! engine.getMixer().getTracks().empty())
        source = engine.getMixer().getTracks().front();

    if (source == nullptr)
    {
        showError ("Separar instrumentos", "Primero importa una canción (Archivo > Importar audio...)."_u8);
        return;
    }

    SeparationRequest request { source->getSourceFile(), projects.createStemsFolderFor (*source) };
    std::weak_ptr<AudioTrack> weakSource = source;

    const auto started = ai.start (std::move (request),
                                   [safe = juce::Component::SafePointer<MainComponent> (this), weakSource]
                                   (const SeparationResult& result)
    {
        if (safe != nullptr)
            safe->separationFinished (result, weakSource.lock());
    });

    if (started)
        statusBar.setMessage ("Separando \"" + source->getName() + "\" con " + ai.getSeparator().getName() + "...");
}

void MainComponent::separationFinished (const SeparationResult& result, std::shared_ptr<AudioTrack> source)
{
    if (result.cancelled)
    {
        statusBar.setMessage ("Separación cancelada."_u8);
        return;
    }

    if (result.status.failed())
    {
        showError ("Error en la separación"_u8, result.status.getErrorMessage());
        return;
    }

    // Los stems se colocan donde empieza la pista original.
    const auto startSeconds = source != nullptr
                            ? static_cast<double> (source->getStartSample()) / source->getSampleRate()
                            : 0.0;

    std::vector<ProjectManager::NewTrack> tracks;

    for (const auto& stem : result.stems)
        tracks.push_back ({ stemDisplayName (stem.name), stem.file, startSeconds, false });

    const auto numStems = static_cast<int> (tracks.size());
    std::weak_ptr<AudioTrack> weakSource = source;

    projects.addTracks (std::move (tracks),
                        [safe = juce::Component::SafePointer<MainComponent> (this), weakSource, numStems] (juce::Result r)
    {
        if (safe == nullptr)
            return;

        if (r.failed())
        {
            safe->reportResult (r, {});
            return;
        }

        // La suma de los stems ya reproduce la canción: se silencia el original
        // para no oírlo dos veces (sigue disponible para comparar).
        if (const auto original = weakSource.lock())
            original->getMute().set (1.0f);

        safe->statusBar.setMessage ("Separación completada: "_u8 + juce::String (numStems)
                                    + " pistas. La pista original se ha silenciado.");
    });
}

void MainComponent::showAbout()
{
    const auto message = "StemLab " + juce::JUCEApplication::getInstance()->getApplicationVersion() + "\n\n"
                       + "Mini-DAW para separar, editar y mezclar instrumentos.\n\n"
                       + juce::SystemStats::getJUCEVersion() + "\n"
                       + "Separación de fuentes: Demucs (Python + PyTorch)"_u8;

    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Acerca de StemLab", message, "OK", this);
}

//==============================================================================
bool MainComponent::ensureIdle (const juce::String& action)
{
    if (ai.isBusy() || projects.isLoading() || engine.isRecording())
    {
        showError ("Espera un momento",
                   "No se puede " + action + " mientras hay una separación, una carga de audio "
                   "o una grabación en curso."_u8);
        return false;
    }

    return true;
}

void MainComponent::confirmDiscard (std::function<void()> action)
{
    if (engine.getMixer().getTracks().empty() || ! projects.getProject().isTemporary())
    {
        action();
        return;
    }

    // Sesión sin guardar con pistas: pedir confirmación antes de descartarla.
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "StemLab",
                                        "La sesión actual no está guardada y se descartará.\n¿Continuar?"_u8,
                                        "Continuar", "Cancelar", this,
                                        juce::ModalCallbackFunction::create (
                                            [safe = juce::Component::SafePointer<MainComponent> (this),
                                             action = std::move (action)] (int buttonIndex)
                                        {
                                            if (buttonIndex != 0 && safe != nullptr)
                                                action();
                                        }));
}

ProjectManager::Callback MainComponent::resultHandler (const juce::String& successMessage)
{
    return [safe = juce::Component::SafePointer<MainComponent> (this), successMessage] (juce::Result result)
    {
        if (safe != nullptr)
            safe->reportResult (result, successMessage);
    };
}

void MainComponent::reportResult (const juce::Result& result, const juce::String& successMessage)
{
    if (result.wasOk())
        statusBar.setMessage (successMessage);
    else
        showError ("StemLab", result.getErrorMessage());
}

void MainComponent::showError (const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, title, message, "OK", this);
}

void MainComponent::updateWindowTitle()
{
    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
    {
        const auto& project = projects.getProject();
        window->setName ("StemLab - " + project.getName()
                         + (project.isTemporary() ? " (sin guardar)"_u8 : juce::String()));
    }
}
}
