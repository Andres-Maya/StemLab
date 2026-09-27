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
        splitClipId,
        cutClipId,
        copyClipId,
        pasteClipId,
        deleteClipId,
        addTrackId,
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
    trackList.onDeleteRequested = [this] (AudioTrack& track) { removeTrack (track); };
    trackList.onClipsEdited = [this] { projects.notifyTracksEdited(); };
    trackList.onAddTrack = [this] { addTrack(); };
    trackList.onContextMenu = [this] (std::shared_ptr<AudioTrack> track, juce::uint32 clipId, double seconds)
    {
        showClipMenu (std::move (track), clipId, seconds);
    };
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
    if (key == juce::KeyPress (juce::KeyPress::deleteKey, command, 0)) { deleteSelectedTrack(); return true; }
    if (key == juce::KeyPress::deleteKey)                   { deleteSelectedClip(); return true; }
    if (key == juce::KeyPress ('r'))                        { toggleRecording(); return true; }
    if (key == juce::KeyPress ('s'))                        { splitAtPlayhead(); return true; }
    if (key == juce::KeyPress ('x', command, 0))            { cutSelectedClip(); return true; }
    if (key == juce::KeyPress ('c', command, 0))            { copySelectedClip(); return true; }
    if (key == juce::KeyPress ('v', command, 0))            { pasteClip (trackList.getSelectedTrack(), engine.getTransport().getPosition()); return true; }
    if (key == juce::KeyPress ('t', command, 0))            { addTrack(); return true; }
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
        {
            const auto hasTrack = trackList.getSelectedTrack() != nullptr;
            const auto hasClip = trackList.getSelectedClipId() != 0;

            menu.addSectionHeader ("Fragmentos");
            addItem (menu, splitClipId, "Dividir en el cabezal", "S", hasTrack);
            addItem (menu, cutClipId, "Cortar", "Ctrl+X", hasClip);
            addItem (menu, copyClipId, "Copiar", "Ctrl+C", hasClip);
            addItem (menu, pasteClipId, "Pegar en el cabezal", "Ctrl+V", hasTrack && clipboard.has_value());
            addItem (menu, deleteClipId, "Eliminar fragmento", "Supr", hasClip);
            menu.addSectionHeader ("Pistas");
            addItem (menu, addTrackId, "Añadir pista"_u8, "Ctrl+T");
            addItem (menu, deleteTrackId, "Eliminar pista seleccionada", "Ctrl+Supr", hasTrack);
            break;
        }

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
        case splitClipId:           splitAtPlayhead(); break;
        case cutClipId:             cutSelectedClip(); break;
        case copyClipId:            copySelectedClip(); break;
        case pasteClipId:           pasteClip (trackList.getSelectedTrack(), engine.getTransport().getPosition()); break;
        case deleteClipId:          deleteSelectedClip(); break;
        case addTrackId:            addTrack(); break;
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
        removeTrack (*track);
    else
        statusBar.setMessage ("Selecciona primero la pista que quieres eliminar.");
}

void MainComponent::removeTrack (AudioTrack& track)
{
    if (engine.isRecording() && track.isArmed())
    {
        showError ("Eliminar pista", "No se puede eliminar la pista mientras se graba en ella."_u8);
        return;
    }

    // Se guarda una referencia débil: si la pista desaparece mientras el
    // diálogo está abierto, no se hace nada.
    std::weak_ptr<AudioTrack> weakTrack;

    for (const auto& t : engine.getMixer().getTracks())
        if (t.get() == &track)
            weakTrack = t;

    const auto message = "¿Estás seguro de que quieres eliminar esta pista?\n\n\""_u8 + track.getName() + "\"\n\n"
                       + "Sus fragmentos se quitarán del proyecto; los archivos de audio se conservan en disco."_u8;

    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Eliminar pista", message,
                                        "Eliminar", "Cancelar", this,
                                        juce::ModalCallbackFunction::create (
                                            [safe = juce::Component::SafePointer<MainComponent> (this), weakTrack] (int result)
                                        {
                                            const auto target = weakTrack.lock();

                                            if (result == 0 || safe == nullptr || target == nullptr)
                                                return;

                                            safe->projects.removeTrack (*target);
                                            safe->statusBar.setMessage ("Pista \"" + target->getName() + "\" eliminada.");
                                        }));
}

void MainComponent::addTrack()
{
    const auto track = projects.addEmptyTrack ("Pista");
    trackList.refresh();
    trackList.selectTrack (track);
    statusBar.setMessage ("Pista añadida y seleccionada: pulsa R o el botón rojo para grabar en ella."_u8);
}

//==============================================================================
namespace
{
    /** Clip sobre el que actúa "Dividir": el seleccionado si contiene la
        posición; si no, el de más arriba que la contenga. */
    const AudioClip* clipAt (const std::vector<AudioClip>& clips, juce::uint32 preferredId, juce::int64 position)
    {
        for (const auto& clip : clips)
            if (clip.id == preferredId && clip.contains (position))
                return &clip;

        for (auto it = clips.rbegin(); it != clips.rend(); ++it)
            if (it->contains (position))
                return &*it;

        return nullptr;
    }
}

void MainComponent::splitAtPlayhead()
{
    const auto track = trackList.getSelectedTrack();

    if (track == nullptr)
    {
        statusBar.setMessage ("Selecciona una pista para dividir.");
        return;
    }

    auto clips = track->getClips();
    const auto position = engine.getTransport().getPosition();
    const auto* target = clipAt (clips, trackList.getSelectedClipId(), position);

    if (target == nullptr)
    {
        statusBar.setMessage ("El cabezal no está sobre ningún fragmento de la pista seleccionada."_u8);
        return;
    }

    const auto rightHalf = ClipEditing::split (clips, target->id, position);

    if (rightHalf == 0)
    {
        statusBar.setMessage ("Demasiado cerca del borde del fragmento para dividir.");
        return;
    }

    track->setClips (std::move (clips));
    trackList.selectClip (track, rightHalf);
    projects.notifyTracksEdited();
    statusBar.setMessage ("Fragmento dividido en el cabezal.");
}

void MainComponent::copySelectedClip()
{
    const auto track = trackList.getSelectedTrack();

    if (track == nullptr)
        return;

    auto clips = track->getClips();

    if (const auto* clip = ClipEditing::find (clips, trackList.getSelectedClipId()))
    {
        clipboard = *clip;
        statusBar.setMessage ("Fragmento copiado. Ctrl+V lo pega en el cabezal.");
    }
}

void MainComponent::cutSelectedClip()
{
    const auto track = trackList.getSelectedTrack();

    if (track == nullptr)
        return;

    auto clips = track->getClips();

    if (const auto* clip = ClipEditing::find (clips, trackList.getSelectedClipId()))
    {
        clipboard = *clip;
        ClipEditing::remove (clips, clip->id);
        track->setClips (std::move (clips));
        trackList.selectClip (track, 0);
        projects.notifyTracksEdited();
        statusBar.setMessage ("Fragmento cortado. Ctrl+V lo pega en el cabezal.");
    }
}

void MainComponent::pasteClip (std::shared_ptr<AudioTrack> track, juce::int64 position)
{
    if (! clipboard.has_value())
    {
        statusBar.setMessage ("No hay nada copiado.");
        return;
    }

    if (track == nullptr)
    {
        statusBar.setMessage ("Selecciona la pista donde pegar.");
        return;
    }

    auto clip = *clipboard;
    clip.id = AudioClip::createId();
    clip.timelineStart = juce::jmax<juce::int64> (0, position);

    track->addClip (clip);
    trackList.selectClip (track, clip.id);
    projects.notifyTracksEdited();
    statusBar.setMessage ("Fragmento pegado.");
}

void MainComponent::deleteSelectedClip()
{
    const auto track = trackList.getSelectedTrack();
    const auto clipId = trackList.getSelectedClipId();

    if (track == nullptr || clipId == 0)
    {
        statusBar.setMessage ("Selecciona un fragmento (clic sobre él) para eliminarlo."_u8);
        return;
    }

    auto clips = track->getClips();

    if (ClipEditing::remove (clips, clipId))
    {
        track->setClips (std::move (clips));
        trackList.selectClip (track, 0);
        projects.notifyTracksEdited();
        statusBar.setMessage ("Fragmento eliminado.");
    }
}

void MainComponent::showClipMenu (std::shared_ptr<AudioTrack> track, juce::uint32 clipId, double seconds)
{
    const auto position = static_cast<juce::int64> (seconds * engine.getSampleRate());
    const auto hasClip = clipId != 0;

    juce::PopupMenu menu;
    addItem (menu, splitClipId, "Dividir en el cabezal", "S");
    addItem (menu, cutClipId, "Cortar", "Ctrl+X", hasClip);
    addItem (menu, copyClipId, "Copiar", "Ctrl+C", hasClip);
    addItem (menu, pasteClipId, "Pegar aquí"_u8, {}, clipboard.has_value());
    addItem (menu, deleteClipId, "Eliminar fragmento", "Supr", hasClip);

    menu.showMenuAsync (juce::PopupMenu::Options(),
                        [safe = juce::Component::SafePointer<MainComponent> (this), track, position] (int result)
    {
        if (safe == nullptr || result == 0)
            return;

        if (result == pasteClipId)
            safe->pasteClip (track, position);   // "Pegar aquí": donde se hizo clic
        else
            safe->menuItemSelected (result, 0);
    });
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

    // Se graba en la pista seleccionada. Si no hay ninguna, se crea una nueva
    // y queda seleccionada, así el siguiente R sigue grabando en ella.
    auto target = trackList.getSelectedTrack();

    if (target == nullptr)
    {
        target = projects.addEmptyTrack ("Grabación"_u8);
        trackList.refresh();
        trackList.selectTrack (target);
    }

    const auto result = engine.startRecording (projects.createRecordingFile());

    if (result.failed())
    {
        showError ("No se puede grabar", result.getErrorMessage());
        return;
    }

    // "Armada" solo mientras se graba: pinta la franja roja y la vista previa
    // en directo sobre esa pista.
    recordingTarget = target;
    projects.setArmedTrack (target);
    statusBar.setMessage ("Grabando en \"" + target->getName() + "\"... pulsa R para pausar y R para seguir en la misma pista.");
}

void MainComponent::finishRecording()
{
    const auto recording = engine.stopRecording();
    engine.getTransport().pause();
    projects.setArmedTrack (nullptr);

    // La grabación se añade como un fragmento más de la misma pista (que sigue
    // seleccionada); al volver a pulsar R se sigue grabando en ella, justo después.
    statusBar.setMessage ("Procesando la grabación..."_u8);
    projects.addRecording (recording, recordingTarget,
                           resultHandler ("Fragmento grabado. Pulsa R para seguir grabando en la misma pista."_u8));
}

void MainComponent::separateInstruments()
{
    if (ai.isBusy())
        return;

    auto source = trackList.getSelectedTrack();

    if (source == nullptr && ! engine.getMixer().getTracks().empty())
        source = engine.getMixer().getTracks().front();

    if (source == nullptr || ! source->hasClips())
    {
        showError ("Separar instrumentos", "Primero importa una canción (Archivo > Importar audio...) "
                                           "y selecciona su pista."_u8);
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

    // Los stems se alinean con el archivo original: su muestra 0 va donde
    // estaría la muestra 0 del primer clip de la pista.
    double startSeconds = 0.0;

    if (source != nullptr)
    {
        const auto clips = source->getClips();

        if (! clips.empty() && clips.front().source != nullptr)
            startSeconds = static_cast<double> (clips.front().timelineStart - clips.front().sourceOffset)
                         / clips.front().source->sampleRate;
    }

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
