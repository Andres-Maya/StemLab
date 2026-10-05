#include "MainComponent.h"

#include "AudioSettingsComponent.h"
#include "ExportDialog.h"
#include "StemLabLookAndFeel.h"
#include "Utils/Strings.h"
#include <algorithm>

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
        showSeparationId,
        aboutId,
        splitClipId,
        cutClipId,
        copyClipId,
        pasteClipId,
        deleteClipId,
        addTrackId,
        renameTrackId,
        moveTrackUpId,
        moveTrackDownId,
        zoomInId,
        zoomOutId,
        zoomFitId,
        undoId,
        redoId,
        exportMixId,
        copyTrackId,
        cutTrackId,
        pasteTrackId,
        clearRecentId,
        tourId,
        darkThemeId,
        lightThemeId,
        modelBaseId = 1000,
        recentProjectBaseId = 2000,     // + índice en la lista de recientes
        languageBaseId = 3000           // + índice en Localisation::getLanguages()
    };

    constexpr int maxRecentProjects = 10;

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
MainComponent::MainComponent (AudioEngine& audioEngine, ProjectManager& projectManager, AIProcessManager& aiManager,
                              juce::PropertiesFile* userSettings)
    : engine (audioEngine),
      projects (projectManager),
      ai (aiManager),
      transportBar (engine, projects),
      trackList (engine),
      statusBar (ai, projects),
      settings (userSettings)
{
    recentProjects.setMaxNumberOfItems (maxRecentProjects);

    if (settings != nullptr)
        recentProjects.restoreFromString (settings->getValue ("recentProjects"));

   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (this);
   #else
    menuBar.setModel (this);
    addAndMakeVisible (menuBar);
   #endif

    transportBar.onToStart = [this] { if (! engine.isRecording()) engine.getTransport().setPosition (0); };
    transportBar.onPlayPause = [this] { togglePlayPause(); };
    transportBar.onStop = [this] { stop(); };
    transportBar.onRecord = [this] { toggleRecording(); };
    addAndMakeVisible (transportBar);

    trackList.onSelectionChanged = [this] (std::shared_ptr<AudioTrack> track) { mixer.setTrack (std::move (track)); };
    trackList.onDeleteRequested = [this] (AudioTrack& track) { removeTrack (track); };
    trackList.onClipsEdited = [this] (std::shared_ptr<AudioTrack> track, std::vector<AudioClip> clipsBefore,
                                      const juce::String& actionName)
    {
        projects.clipsEdited (track, std::move (clipsBefore), actionName);
    };
    trackList.onAddTrack = [this] (int insertIndex, const juce::String& folderId) { addTrack (insertIndex, folderId); };
    trackList.onToggleFolder = [this] (const juce::String& folderId)
    {
        if (const auto* folder = projects.findFolder (folderId))
            projects.setFolderExpanded (folderId, ! folder->expanded);
    };
    trackList.onToggleFolderWindow = [this] (const juce::String& folderId) { toggleFolderWindow (folderId); };
    trackList.onDeleteFolderRequested = [this] (const juce::String& folderId) { removeFolder (folderId); };
    trackList.onTrackDropped = [this] (std::shared_ptr<AudioTrack> track, const juce::String& folderId, int mixerIndex)
    {
        projects.moveTrackToFolder (track, folderId, mixerIndex);
    };
    trackList.onTracksReordered = [this] (std::shared_ptr<AudioTrack> track, int fromIndex, int toIndex)
    {
        projects.trackMoved (track, fromIndex, toIndex);
    };
    trackList.onTrackRenamed = [this] (std::shared_ptr<AudioTrack> track, const juce::String& oldName)
    {
        projects.trackRenamed (track, oldName);
        mixer.repaint();
    };
    trackList.onCopyTrack = [this] (std::shared_ptr<AudioTrack> track) { copyTrack (track); };
    trackList.onCutTrack = [this] (std::shared_ptr<AudioTrack> track) { cutTrack (track); };
    trackList.onPasteTrack = [this] (int insertIndex) { pasteTrack (insertIndex); };
    trackList.canPasteTrack = [this] { return trackClipboard != nullptr; };
    trackList.onContextMenu = [this] (std::shared_ptr<AudioTrack> track, juce::uint32 clipId, double seconds)
    {
        showClipMenu (std::move (track), clipId, seconds);
    };
    addAndMakeVisible (trackList);

    addAndMakeVisible (mixer);

    statusBar.onCancel = [this] { ai.cancel(); };
    statusBar.onShowSeparation = [this] { showSeparationWindow(); };
    addAndMakeVisible (statusBar);

    projects.addChangeListener (this);
    updateFolders();        // al volver a crear la ventana (idioma, tema) el proyecto ya puede tener carpetas
    trackList.refresh();

    setWantsKeyboardFocus (true);
    setSize (1280, 820);

    // Revisa una vez por segundo si hay cambios sin guardar (asterisco en el título).
    startTimer (1000);
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

    if (tour != nullptr)
        tour->setBounds (getLocalBounds());
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
    if (key == juce::KeyPress::homeKey)                     { if (! engine.isRecording()) engine.getTransport().setPosition (0); return true; }
    if (key == juce::KeyPress ('z', command, 0))            { undo(); return true; }
    if (key == juce::KeyPress ('z', commandShift, 0))       { redo(); return true; }
    if (key == juce::KeyPress ('y', command, 0))            { redo(); return true; }
    if (key == juce::KeyPress (juce::KeyPress::deleteKey, command, 0)) { deleteSelectedTrack(); return true; }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) { deleteSelection(); return true; }
    if (key == juce::KeyPress ('r'))                        { toggleRecording(); return true; }
    if (key == juce::KeyPress ('s'))                        { splitAtPlayhead(); return true; }
    if (key == juce::KeyPress ('x', command, 0))            { cutSelection(); return true; }
    if (key == juce::KeyPress ('c', command, 0))            { copySelection(); return true; }
    if (key == juce::KeyPress ('v', command, 0))            { paste(); return true; }
    if (key == juce::KeyPress ('t', command, 0))            { addTrack(); return true; }
    if (key == juce::KeyPress::F2Key)                       { trackList.renameSelectedTrack(); return true; }
    if (key == juce::KeyPress (juce::KeyPress::upKey, juce::ModifierKeys (juce::ModifierKeys::altModifier), 0))   { trackList.moveSelectedTrack (-1); return true; }
    if (key == juce::KeyPress (juce::KeyPress::downKey, juce::ModifierKeys (juce::ModifierKeys::altModifier), 0)) { trackList.moveSelectedTrack (1); return true; }
    if (key == juce::KeyPress ('n', command, 0))            { newProject(); return true; }
    if (key == juce::KeyPress ('o', command, 0))            { openProject(); return true; }
    if (key == juce::KeyPress ('s', commandShift, 0))       { saveProjectAs(); return true; }
    if (key == juce::KeyPress ('s', command, 0))            { saveProject(); return true; }
    if (key == juce::KeyPress ('i', command, 0))            { importAudio(); return true; }
    if (key == juce::KeyPress ('e', command, 0))            { exportMix(); return true; }

    return false;
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateFolders();
    trackList.refresh();
    mixer.repaint();        // el nombre de la pista puede haber cambiado (deshacer)
    updateWindowTitle();
    menuItemsChanged();
}

//==============================================================================
juce::StringArray MainComponent::getMenuBarNames()
{
    return juce::StringArray (tr ("Archivo"), tr ("Editar"), tr ("Proyecto"), tr ("Audio"), tr ("IA"), tr ("Ver"), tr ("Ayuda"));
}

juce::PopupMenu MainComponent::getMenuForIndex (int topLevelMenuIndex, const juce::String&)
{
    juce::PopupMenu menu;
    const auto hasTracks = ! engine.getMixer().getTracks().empty();
    const auto aiBusy = ai.isBusy();

    switch (topLevelMenuIndex)
    {
        case 0:
            addItem (menu, newProjectId, tr ("Nuevo proyecto"), "Ctrl+N");
            addItem (menu, openProjectId, tr ("Abrir proyecto..."), "Ctrl+O");
        {
            juce::PopupMenu recent;
            recentProjects.createPopupMenuItems (recent, recentProjectBaseId, true, true);

            if (recent.getNumItems() > 0)
            {
                recent.addSeparator();
                addItem (recent, clearRecentId, tr ("Borrar la lista"));
            }

            menu.addSubMenu (tr ("Abrir reciente"), recent, recent.getNumItems() > 0);
        }
            addItem (menu, saveProjectId, tr ("Guardar proyecto"), "Ctrl+S");
            addItem (menu, saveProjectAsId, tr ("Guardar proyecto como..."), "Ctrl+Shift+S");
            menu.addSeparator();
            addItem (menu, importAudioId, tr ("Importar audio..."), "Ctrl+I");
            addItem (menu, exportMixId, tr ("Exportar mezcla (WAV / MP3)..."), "Ctrl+E", hasTracks);
            menu.addSeparator();
            addItem (menu, quitId, tr ("Salir"));
            break;

        case 1:
        {
            const auto hasTrack = trackList.getSelectedTrack() != nullptr;
            const auto hasClip = trackList.getSelectedClipId() != 0;

            // Los nombres de las acciones de deshacer se guardan en español y se traducen aquí.
            addItem (menu, undoId, projects.canUndo() ? tr ("Deshacer: {0}", tr (projects.getUndoDescription())) : tr ("Deshacer"),
                     "Ctrl+Z", projects.canUndo());
            addItem (menu, redoId, projects.canRedo() ? tr ("Rehacer: {0}", tr (projects.getRedoDescription())) : tr ("Rehacer"),
                     "Ctrl+Y", projects.canRedo());
            menu.addSectionHeader (tr ("Fragmentos"));
            addItem (menu, splitClipId, tr ("Dividir en el cabezal"), "S", hasTrack);
            addItem (menu, cutClipId, tr ("Cortar"), "Ctrl+X", hasClip);
            addItem (menu, copyClipId, tr ("Copiar"), "Ctrl+C", hasClip);
            addItem (menu, pasteClipId, tr ("Pegar en el cabezal"), "Ctrl+V", hasTrack && clipboard.has_value());
            addItem (menu, deleteClipId, tr ("Eliminar fragmento"), tr ("Supr"), hasClip);
            menu.addSectionHeader (tr ("Pistas"));
            addItem (menu, addTrackId, tr ("Añadir pista"), "Ctrl+T");
            addItem (menu, renameTrackId, tr ("Cambiar nombre de la pista"), "F2", hasTrack);
            addItem (menu, moveTrackUpId, tr ("Subir pista"), tr ("Alt+Arriba"), hasTrack);
            addItem (menu, moveTrackDownId, tr ("Bajar pista"), tr ("Alt+Abajo"), hasTrack);
            addItem (menu, copyTrackId, tr ("Copiar pista"), "Ctrl+C", hasTrack);
            addItem (menu, cutTrackId, tr ("Cortar pista"), "Ctrl+X", hasTrack);
            addItem (menu, pasteTrackId, tr ("Pegar pista debajo"), "Ctrl+V", trackClipboard != nullptr);
            addItem (menu, deleteTrackId, tr ("Eliminar pista seleccionada"), "Ctrl+" + tr ("Supr"), hasTrack);
            break;
        }

        case 2:
            menu.addSectionHeader (tr ("Vista"));
            addItem (menu, zoomInId, tr ("Acercar"), tr ("Ctrl + rueda"));
            addItem (menu, zoomOutId, tr ("Alejar"), tr ("Ctrl + rueda"));
            addItem (menu, zoomFitId, tr ("Ver toda la canción"));
            menu.addSeparator();
            addItem (menu, showFolderId, tr ("Mostrar carpeta del proyecto"));
            break;

        case 3:
            addItem (menu, playPauseId, engine.getTransport().isPlaying() ? tr ("Pausa") : tr ("Reproducir"), tr ("Espacio"));
            addItem (menu, stopId, tr ("Detener"));
            addItem (menu, recordId, engine.isRecording() ? tr ("Detener grabación") : tr ("Grabar"), "R");
            menu.addSeparator();
            addItem (menu, audioSettingsId, tr ("Configuración de audio..."));
            break;

        case 4:
        {
            addItem (menu, separateId, tr ("Separar instrumentos"), {}, hasTracks && ! aiBusy);
            addItem (menu, showSeparationId, tr ("Mostrar progreso de la separación"), {}, aiBusy && folderWindows.count (separatingFolderId) > 0);
            addItem (menu, cancelSeparationId, tr ("Cancelar separación"), {}, aiBusy);
            menu.addSeparator();
            menu.addSectionHeader (tr ("Modelo"));

            const auto models = ai.getSeparator().getAvailableModels();
            const auto current = ai.getSeparator().getCurrentModel();

            for (size_t i = 0; i < models.size(); ++i)
                addItem (menu, modelBaseId + static_cast<int> (i), models[i].id + "  -  " + models[i].description,
                         {}, ! aiBusy, models[i].id == current);
            break;
        }

        case 5:
        {
            menu.addSectionHeader (tr ("Tema"));
            addItem (menu, darkThemeId, tr ("Oscuro"), {}, true, Palette::getTheme() == Theme::dark);
            addItem (menu, lightThemeId, tr ("Claro"), {}, true, Palette::getTheme() == Theme::light);
            menu.addSectionHeader (tr ("Idioma"));

            const auto& languages = Localisation::getLanguages();

            for (size_t i = 0; i < languages.size(); ++i)
                addItem (menu, languageBaseId + static_cast<int> (i), juce::String::fromUTF8 (languages[i].name),
                         {}, true, languages[i].language == Localisation::getLanguage());
            break;
        }

        case 6:
            addItem (menu, tourId, tr ("Tutorial"));
            menu.addSeparator();
            addItem (menu, aboutId, tr ("Acerca de StemLab"));
            break;

        default:
            break;
    }

    return menu;
}

void MainComponent::menuItemSelected (int menuItemID, int)
{
    if (menuItemID >= recentProjectBaseId && menuItemID < recentProjectBaseId + maxRecentProjects)
    {
        openProjectFile (recentProjects.getFile (menuItemID - recentProjectBaseId));
        return;
    }

    if (menuItemID >= languageBaseId)
    {
        const auto& languages = Localisation::getLanguages();

        if (const auto index = static_cast<size_t> (menuItemID - languageBaseId); index < languages.size())
            changeInterface (languages[index].language, Palette::getTheme());

        return;
    }

    if (menuItemID >= modelBaseId)
    {
        const auto models = ai.getSeparator().getAvailableModels();
        const auto index = static_cast<size_t> (menuItemID - modelBaseId);

        if (index < models.size() && ! ai.isBusy())
        {
            ai.getSeparator().setCurrentModel (models[index].id);
            statusBar.setMessage (tr ("Modelo de separación: {0}", models[index].id));
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
        case exportMixId:           exportMix(); break;
        case quitId:                requestQuit(); break;
        case deleteTrackId:         deleteSelectedTrack(); break;
        case showFolderId:          showProjectFolder(); break;
        case playPauseId:           togglePlayPause(); break;
        case stopId:                stop(); break;
        case recordId:              toggleRecording(); break;
        case audioSettingsId:       showAudioSettings(); break;
        case separateId:            separateInstruments(); break;
        case cancelSeparationId:    ai.cancel(); break;
        case showSeparationId:      showSeparationWindow(); break;
        case aboutId:               showAbout(); break;
        case tourId:                showTour(); break;
        case darkThemeId:           changeInterface (Localisation::getLanguage(), Theme::dark); break;
        case lightThemeId:          changeInterface (Localisation::getLanguage(), Theme::light); break;
        case splitClipId:           splitAtPlayhead(); break;
        case cutClipId:             cutSelectedClip(); break;
        case copyClipId:            copySelectedClip(); break;
        case pasteClipId:           pasteClip (trackList.getSelectedTrack(), engine.getTransport().getPosition()); break;
        case deleteClipId:          deleteSelectedClip(); break;
        case addTrackId:            addTrack(); break;
        case renameTrackId:         trackList.renameSelectedTrack(); break;
        case moveTrackUpId:         trackList.moveSelectedTrack (-1); break;
        case moveTrackDownId:       trackList.moveSelectedTrack (1); break;
        case zoomInId:              trackList.zoomIn(); break;
        case zoomOutId:             trackList.zoomOut(); break;
        case zoomFitId:             trackList.zoomToFit(); break;
        case undoId:                undo(); break;
        case clearRecentId:
            recentProjects.clear();

            if (settings != nullptr)
                settings->setValue ("recentProjects", recentProjects.toString());

            break;

        case copyTrackId:           copyTrack (trackList.getSelectedTrack()); break;
        case cutTrackId:            cutTrack (trackList.getSelectedTrack()); break;
        case pasteTrackId:          pasteTrack(); break;
        case redoId:                redo(); break;
        default:                    break;
    }
}

//==============================================================================
bool MainComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& path : files)
    {
        const juce::File file (path);

        if (Project::isProjectFile (file) || projects.canImport (file))
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

        if (Project::isProjectFile (file))
        {
            openProjectFile (file);
            return;
        }

        if (projects.canImport (file))
            audioFiles.add (file);
    }

    if (! audioFiles.isEmpty())
    {
        statusBar.setMessage (tr ("Importando..."));
        projects.importAudio (audioFiles, resultHandler (tr ("Audio importado.")));
    }
}

//==============================================================================
void MainComponent::newProject()
{
    if (! ensureIdle (tr ("No se puede crear un proyecto mientras hay una separación, una carga de audio o una grabación en curso.")))
        return;

    askToSaveChanges ([this]
    {
        projects.newProject();
        statusBar.setMessage (tr ("Proyecto nuevo."));
    });
}

void MainComponent::openProject()
{
    if (! ensureIdle (tr ("No se puede abrir un proyecto mientras hay una separación, una carga de audio o una grabación en curso.")))
        return;

    askToSaveChanges ([this]
    {
        const auto folder = defaultProjectsFolder();
        fileChooser = std::make_unique<juce::FileChooser> (tr ("Abrir proyecto de StemLab"),
                                                           folder.isDirectory() ? folder : juce::File(),
                                                           "*" + juce::String (Project::fileExtension) + ";project.json");

        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& chooser)
        {
            const auto file = chooser.getResult();

            if (file != juce::File())
                loadProject (file);
        });
    });
}

void MainComponent::openProjectFile (const juce::File& file)
{
    if (! ensureIdle (tr ("No se puede abrir un proyecto mientras hay una separación, una carga de audio o una grabación en curso.")))
        return;

    askToSaveChanges ([this, file] { loadProject (file); });
}

void MainComponent::loadProject (const juce::File& file)
{
    statusBar.setMessage (tr ("Abriendo {0}...", file.getFileName()));
    const auto previous = projects.getProject().getProjectFile();

    projects.openProject (file, [safe = juce::Component::SafePointer<MainComponent> (this), file, previous] (juce::Result result)
    {
        if (safe == nullptr)
            return;

        // Se abrió (aunque faltara algún audio, que se avisa): va a "Abrir reciente".
        // Si no existe o no es un proyecto, se quita de la lista.
        const auto opened = result.wasOk() || safe->projects.getProject().getProjectFile() != previous;

        if (opened)
            safe->rememberProject();
        else if (! file.exists())
            safe->recentProjects.removeFile (file);

        safe->reportResult (result, tr ("Proyecto abierto: {0}", safe->projects.getProject().getProjectFile().getFullPathName()));
    });
}

void MainComponent::rememberProject()
{
    const auto& project = projects.getProject();

    if (project.isTemporary() || ! project.getProjectFile().existsAsFile())
        return;

    recentProjects.addFile (project.getProjectFile());

    if (settings != nullptr)
        settings->setValue ("recentProjects", recentProjects.toString());
}

void MainComponent::saveProject()
{
    if (projects.getProject().isTemporary())
    {
        saveProjectAs();
        return;
    }

    reportResult (projects.save(), tr ("Proyecto guardado."));
}

void MainComponent::saveProjectAs (std::function<void()> onSaved)
{
    // Guardar como copia la carpeta de la sesión: no puede coincidir con una
    // separación o una carga que están escribiendo o leyendo en ella.
    if (! ensureIdle (tr ("No se puede guardar el proyecto mientras hay una separación, una carga de audio o una grabación en curso.")))
        return;

    // Se elige un archivo .stemlab; el proyecto va en una carpeta con su nombre
    // (MiCancion/MiCancion.stemlab + audio/, stems/, recordings/, exports/).
    const auto& project = projects.getProject();
    const auto folder = project.isTemporary() ? defaultProjectsFolder() : project.getDirectory().getParentDirectory();
    const auto suggestedName = project.isTemporary() ? tr ("MiProyecto") : project.getName();
    const juce::String extension (Project::fileExtension);
    folder.createDirectory();

    fileChooser = std::make_unique<juce::FileChooser> (tr ("Guardar proyecto de StemLab"),
                                                       folder.getChildFile (suggestedName + extension), "*" + extension);

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, onSaved = std::move (onSaved)] (const juce::FileChooser& chooser)
    {
        const auto chosen = chooser.getResult();

        if (chosen == juce::File())
            return;

        const auto result = projects.saveAs (ProjectManager::folderForSaveAs (chosen));
        reportResult (result, tr ("Proyecto guardado: {0}", projects.getProject().getProjectFile().getFullPathName()));

        if (result.wasOk())
            rememberProject();

        // Se aplaza: la acción puede abrir otro FileChooser, y este no se puede
        // destruir mientras se ejecuta su propio callback.
        if (result.wasOk() && onSaved != nullptr)
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), onSaved]
            {
                if (safe != nullptr)
                    onSaved();
            });
    });
}

void MainComponent::importAudio()
{
    fileChooser = std::make_unique<juce::FileChooser> (tr ("Importar audio"),
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

        statusBar.setMessage (tr ("Importando..."));
        projects.importAudio (files, resultHandler (tr ("Audio importado.")));
    });
}

void MainComponent::exportMix()
{
    // La grabación en curso y el audio que se está cargando aún no son clips:
    // no saldrían en el archivo.
    if (! ensureIdle (tr ("No se puede exportar la mezcla mientras hay una separación, una carga de audio o una grabación en curso.")))
        return;

    if (engine.getMixer().getContentLength() <= 0)
    {
        showError (tr ("Exportar mezcla"), tr ("No hay nada que exportar: importa o graba audio primero."));
        return;
    }

    // Un proyecto guardado exporta a su carpeta exports/; una sesión sin
    // guardar, a Documentos/StemLab (la carpeta temporal se borra sola).
    const auto& project = projects.getProject();
    const auto folder = project.isTemporary() ? defaultProjectsFolder() : project.getExportsDirectory();
    const auto name = project.isTemporary() ? tr ("Mezcla") : project.getName();

    ExportDialog::show (this, engine, fileChooser, folder, name,
                        [safe = juce::Component::SafePointer<MainComponent> (this)] (const ExportResult& result, const juce::File& file)
    {
        if (safe == nullptr)
            return;

        if (result.cancelled)
        {
            safe->statusBar.setMessage (tr ("Exportación cancelada."));
            return;
        }

        if (result.status.failed())
        {
            safe->showError (tr ("Exportar mezcla"), result.status.getErrorMessage());
            return;
        }

        // Por encima de 0 dBFS, WAV entero y MP3 recortan: se avisa (el WAV de
        // 32 bits coma flotante lo conserva).
        if (result.clipped)
            safe->showError (tr ("Mezcla exportada con recorte"),
                             tr ("La mezcla llega a +{0} dBFS y se ha recortado al guardarla.\n\n"
                                 "Baja el volumen master o de las pistas (o activa el Limiter) y vuelve a exportar.",
                                 juce::String (juce::Decibels::gainToDecibels (result.peak), 1))
                             + "\n\n" + file.getFullPathName());

        safe->statusBar.setMessage (tr ("Mezcla exportada ({0}): {1}", formatTime (result.seconds), file.getFullPathName()));
    });
}

void MainComponent::deleteSelection()
{
    if (trackList.getSelectedClipId() != 0)
        deleteSelectedClip();
    else if (const auto track = trackList.getSelectedTrack())
        removeTrack (*track);       // pide confirmación
    else
        statusBar.setMessage (tr ("Selecciona un fragmento o una pista para eliminarlo."));
}

void MainComponent::deleteSelectedTrack()
{
    if (const auto track = trackList.getSelectedTrack())
        removeTrack (*track);
    else
        statusBar.setMessage (tr ("Selecciona primero la pista que quieres eliminar."));
}

void MainComponent::removeTrack (AudioTrack& track)
{
    if (engine.isRecording() && track.isArmed())
    {
        showError (tr ("Eliminar pista"), tr ("No se puede eliminar la pista mientras se graba en ella."));
        return;
    }

    // Se guarda una referencia débil: si la pista desaparece mientras el
    // diálogo está abierto, no se hace nada.
    std::weak_ptr<AudioTrack> weakTrack;

    for (const auto& t : engine.getMixer().getTracks())
        if (t.get() == &track)
            weakTrack = t;

    const auto message = tr ("¿Estás seguro de que quieres eliminar esta pista?\n\n\"{0}\"\n\n"
                             "Sus fragmentos se quitarán del proyecto y, al guardar, su audio saldrá de la carpeta del proyecto. "
                             "Puedes recuperarla con Editar > Deshacer (Ctrl+Z).", track.getName());

    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, tr ("Eliminar pista"), message,
                                        tr ("Eliminar"), tr ("Cancelar"), this,
                                        juce::ModalCallbackFunction::create (
                                            [safe = juce::Component::SafePointer<MainComponent> (this), weakTrack] (int result)
                                        {
                                            const auto target = weakTrack.lock();

                                            if (result == 0 || safe == nullptr || target == nullptr)
                                                return;

                                            safe->projects.removeTrack (*target);
                                            safe->statusBar.setMessage (tr ("Pista \"{0}\" eliminada. Ctrl+Z la recupera.", target->getName()));
                                        }));
}

void MainComponent::removeFolder (const juce::String& folderId)
{
    const auto* folder = projects.findFolder (folderId);

    if (folder == nullptr)
        return;

    // Mientras se cargan sus pistas todavía no están dentro.
    if (folderId == loadingStemsFolderId)
    {
        showError (tr ("Eliminar carpeta"), tr ("Espera a que terminen de cargarse las pistas de la separación."));
        return;
    }

    const auto tracks = projects.getFolderTracks (folderId);

    if (engine.isRecording() && std::any_of (tracks.begin(), tracks.end(), [] (const auto& t) { return t->isArmed(); }))
    {
        showError (tr ("Eliminar carpeta"), tr ("No se puede eliminar la carpeta mientras se graba en una de sus pistas."));
        return;
    }

    const auto name = folder->name;
    const auto count = static_cast<int> (tracks.size());
    const auto message = tr ("¿Estás seguro de que quieres eliminar esta carpeta?\n\n\"{0}\" ({1})\n\n"
                             "Se eliminarán la carpeta y las pistas que tiene dentro; al guardar, su audio saldrá "
                             "de la carpeta del proyecto. Puedes recuperarla con Editar > Deshacer (Ctrl+Z).",
                             name, count == 1 ? tr ("1 pista") : tr ("{0} pistas", count));

    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, tr ("Eliminar carpeta"), message,
                                        tr ("Eliminar"), tr ("Cancelar"), this,
                                        juce::ModalCallbackFunction::create (
                                            [safe = juce::Component::SafePointer<MainComponent> (this), folderId, name] (int result)
                                        {
                                            if (result == 0 || safe == nullptr)
                                                return;

                                            safe->projects.removeFolder (folderId);
                                            safe->statusBar.setMessage (tr ("Carpeta \"{0}\" eliminada. Ctrl+Z la recupera.", name));
                                        }));
}

int MainComponent::indexBelowSelectedTrack() const
{
    const auto& tracks = engine.getMixer().getTracks();
    const auto selectedTrack = trackList.getSelectedTrack();

    for (size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i] == selectedTrack)
            return static_cast<int> (i) + 1;

    return -1;
}

void MainComponent::addTrack (int insertIndex, const juce::String& folderId)
{
    // Sin posición (Ctrl+T, menú): justo debajo de la pista seleccionada.
    if (insertIndex < 0)
        insertIndex = indexBelowSelectedTrack();

    const auto track = projects.addEmptyTrack (tr ("Pista"), insertIndex, folderId);
    trackList.refresh();
    trackList.selectTrack (track);
    statusBar.setMessage (tr ("Pista añadida y seleccionada: pulsa R o el botón rojo para grabar en ella (Ctrl+Z la quita)."));
}

//==============================================================================
void MainComponent::copySelection()
{
    if (trackList.getSelectedClipId() != 0)
        copySelectedClip();
    else if (const auto track = trackList.getSelectedTrack())
        copyTrack (track);
    else
        statusBar.setMessage (tr ("Selecciona un fragmento o una pista (clic en su cabecera) para copiarlo."));
}

void MainComponent::cutSelection()
{
    if (trackList.getSelectedClipId() != 0)
        cutSelectedClip();
    else if (const auto track = trackList.getSelectedTrack())
        cutTrack (track);
    else
        statusBar.setMessage (tr ("Selecciona un fragmento o una pista (clic en su cabecera) para cortarlo."));
}

void MainComponent::paste()
{
    // Se pega lo último que se copió: una pista o un fragmento.
    if (trackClipboard != nullptr)
        pasteTrack();
    else
        pasteClip (trackList.getSelectedTrack(), engine.getTransport().getPosition());
}

void MainComponent::copyTrack (const std::shared_ptr<AudioTrack>& track)
{
    if (track == nullptr)
    {
        statusBar.setMessage (tr ("Selecciona la pista que quieres copiar."));
        return;
    }

    // Una copia en el momento de copiar: lo que se edite después en la pista
    // original no cambia lo que se pegará.
    trackClipboard = track->createCopy (track->getName());
    clipboard.reset();
    statusBar.setMessage (tr ("Pista \"{0}\" copiada. Ctrl+V la pega debajo de la pista seleccionada.", track->getName()));
}

void MainComponent::cutTrack (const std::shared_ptr<AudioTrack>& track)
{
    if (track == nullptr)
    {
        statusBar.setMessage (tr ("Selecciona la pista que quieres cortar."));
        return;
    }

    if (engine.isRecording() && track->isArmed())
    {
        showError (tr ("Cortar pista"), tr ("No se puede cortar la pista mientras se graba en ella."));
        return;
    }

    copyTrack (track);
    projects.removeTrack (*track, msg ("Cortar pista"));
    statusBar.setMessage (tr ("Pista \"{0}\" cortada. Ctrl+V la pega; Ctrl+Z la devuelve a su sitio.", track->getName()));
}

void MainComponent::pasteTrack (int insertIndex)
{
    if (trackClipboard == nullptr)
    {
        statusBar.setMessage (tr ("No hay ninguna pista copiada."));
        return;
    }

    if (insertIndex < 0)
        insertIndex = indexBelowSelectedTrack();

    const auto track = projects.pasteTrack (*trackClipboard, insertIndex);
    trackList.refresh();
    trackList.selectTrack (track);
    statusBar.setMessage (tr ("Pista pegada: \"{0}\".", track->getName()));
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

void MainComponent::undo()
{
    // Durante una grabación, la pista en la que se graba recibirá la toma al
    // terminar: se deshace después, para no mezclar las dos cosas.
    if (engine.isRecording())
    {
        statusBar.setMessage (tr ("Termina la grabación antes de deshacer."));
        return;
    }

    const auto description = projects.getUndoDescription();

    if (projects.undo())
        statusBar.setMessage (tr ("Deshecho: {0}", tr (description)));
    else
        statusBar.setMessage (tr ("No hay nada que deshacer."));
}

void MainComponent::redo()
{
    if (engine.isRecording())
    {
        statusBar.setMessage (tr ("Termina la grabación antes de rehacer."));
        return;
    }

    const auto description = projects.getRedoDescription();

    if (projects.redo())
        statusBar.setMessage (tr ("Rehecho: {0}", tr (description)));
    else
        statusBar.setMessage (tr ("No hay nada que rehacer."));
}

void MainComponent::splitAtPlayhead()
{
    const auto track = trackList.getSelectedTrack();

    if (track == nullptr)
    {
        statusBar.setMessage (tr ("Selecciona una pista para dividir."));
        return;
    }

    auto clips = track->getClips();
    const auto position = engine.getTransport().getPosition();
    const auto* target = clipAt (clips, trackList.getSelectedClipId(), position);

    if (target == nullptr)
    {
        statusBar.setMessage (tr ("El cabezal no está sobre ningún fragmento de la pista seleccionada."));
        return;
    }

    const auto rightHalf = ClipEditing::split (clips, target->id, position);

    if (rightHalf == 0)
    {
        statusBar.setMessage (tr ("Demasiado cerca del borde del fragmento para dividir."));
        return;
    }

    projects.editClips (track, std::move (clips), msg ("Dividir fragmento"));
    trackList.selectClip (track, rightHalf);
    statusBar.setMessage (tr ("Fragmento dividido en el cabezal."));
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
        trackClipboard.reset();
        statusBar.setMessage (tr ("Fragmento copiado. Ctrl+V lo pega en el cabezal."));
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
        trackClipboard.reset();
        ClipEditing::remove (clips, clip->id);
        projects.editClips (track, std::move (clips), msg ("Cortar fragmento"));
        trackList.selectClip (track, 0);
        statusBar.setMessage (tr ("Fragmento cortado. Ctrl+V lo pega en el cabezal."));
    }
}

void MainComponent::pasteClip (std::shared_ptr<AudioTrack> track, juce::int64 position)
{
    if (! clipboard.has_value())
    {
        statusBar.setMessage (tr ("No hay nada copiado."));
        return;
    }

    if (track == nullptr)
    {
        statusBar.setMessage (tr ("Selecciona la pista donde pegar."));
        return;
    }

    auto clips = track->getClips();
    auto clip = *clipboard;
    clip.id = AudioClip::createId();

    // Nunca encima de otro audio de la pista: si el cabezal está sobre un
    // fragmento, se pega justo después (en el primer hueco donde quepa).
    position = std::max<juce::int64> (0, position);
    clip.timelineStart = ClipEditing::findFreeSpace (clips, position, clip.length);

    clips.push_back (clip);
    projects.editClips (track, std::move (clips), msg ("Pegar fragmento"));
    trackList.selectClip (track, clip.id);
    statusBar.setMessage (clip.timelineStart == position
                              ? tr ("Fragmento pegado.")
                              : tr ("Fragmento pegado a continuación del audio que había en {0}, en {1}.",
                                    formatTime ((double) position / engine.getSampleRate()),
                                    formatTime ((double) clip.timelineStart / engine.getSampleRate())));
}

void MainComponent::deleteSelectedClip()
{
    const auto track = trackList.getSelectedTrack();
    const auto clipId = trackList.getSelectedClipId();

    if (track == nullptr || clipId == 0)
    {
        statusBar.setMessage (tr ("Selecciona un fragmento (clic sobre él) para eliminarlo."));
        return;
    }

    auto clips = track->getClips();

    if (ClipEditing::remove (clips, clipId))
    {
        projects.editClips (track, std::move (clips), msg ("Eliminar fragmento"));
        trackList.selectClip (track, 0);
        statusBar.setMessage (tr ("Fragmento eliminado. Ctrl+Z lo recupera."));
    }
}

void MainComponent::showClipMenu (std::shared_ptr<AudioTrack> track, juce::uint32 clipId, double seconds)
{
    const auto position = static_cast<juce::int64> (seconds * engine.getSampleRate());
    const auto hasClip = clipId != 0;

    juce::PopupMenu menu;
    addItem (menu, splitClipId, tr ("Dividir en el cabezal"), "S");
    addItem (menu, cutClipId, tr ("Cortar"), "Ctrl+X", hasClip);
    addItem (menu, copyClipId, tr ("Copiar"), "Ctrl+C", hasClip);
    addItem (menu, pasteClipId, tr ("Pegar aquí"), {}, clipboard.has_value());
    addItem (menu, deleteClipId, tr ("Eliminar fragmento"), tr ("Supr"), hasClip);

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
    options.dialogTitle = tr ("Configuración de audio");
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

    // Sin audio no hay nada que reproducir (el cabezal no se movería).
    if (! engine.getTransport().isPlaying() && engine.getMixer().getContentLength() <= 0)
    {
        statusBar.setMessage (tr ("No hay audio que reproducir: importa una canción o graba primero."));
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
        target = projects.addEmptyTrack (tr ("Grabación"));
        trackList.refresh();
        trackList.selectTrack (target);
    }

    // En una pista los fragmentos no se solapan: si el cabezal está sobre
    // audio grabado, la toma empieza justo después (para grabar encima se usa
    // otra pista).
    const auto playhead = engine.getTransport().getPosition();
    const auto start = ClipEditing::findFreeSpace (target->getClips(), playhead, 1);

    if (start != playhead)
        engine.getTransport().setPosition (start);

    const auto result = engine.startRecording (projects.createRecordingFile());

    if (result.failed())
    {
        showError (tr ("No se puede grabar"), result.getErrorMessage());
        return;
    }

    // "Armada" solo mientras se graba: pinta la franja roja y la vista previa
    // en directo sobre esa pista.
    recordingTarget = target;
    projects.setArmedTrack (target);
    statusBar.setMessage (start != playhead
                              ? tr ("Grabando en \"{0}\" a continuación del audio que ya tiene, desde {1}... "
                                    "pulsa R para pausar y R para seguir en la misma pista.",
                                    target->getName(), formatTime ((double) start / engine.getSampleRate()))
                              : tr ("Grabando en \"{0}\"... pulsa R para pausar y R para seguir en la misma pista.", target->getName()));
}

void MainComponent::finishRecording()
{
    const auto recording = engine.stopRecording();
    engine.getTransport().pause();
    projects.setArmedTrack (nullptr);

    // La grabación se añade como un fragmento más de la misma pista (que sigue
    // seleccionada); al volver a pulsar R se sigue grabando en ella, justo después.
    statusBar.setMessage (tr ("Procesando la grabación..."));
    projects.addRecording (recording, recordingTarget,
                           resultHandler (tr ("Fragmento grabado. Pulsa R para seguir grabando en la misma pista.")));
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
        showError (tr ("Separar instrumentos"), tr ("Primero importa una canción (Archivo > Importar audio...) "
                                                    "y selecciona su pista."));
        return;
    }

    SeparationRequest request { source->getSourceFile(), projects.createStemsFolderFor (*source) };
    std::weak_ptr<AudioTrack> weakSource = source;

    // La separación, su carpeta de pistas y su ventana de ondas comparten un id.
    const auto folderId = juce::Uuid().toString();
    const auto expected = ai.getSeparator().getExpectedStems();

    const auto started = ai.start (std::move (request),
                                   [safe = juce::Component::SafePointer<MainComponent> (this), weakSource, folderId, expected]
                                   (const SeparationResult& result)
    {
        if (safe != nullptr)
            safe->separationFinished (result, weakSource.lock(), folderId, expected);
    });

    if (! started)
        return;

    statusBar.setMessage (tr ("Separando \"{0}\" con {1}...", source->getName(), ai.getSeparator().getName()));
    separatingFolderId = folderId;

    // Ventana con la animación: el anillo del color de la pista original y,
    // según avanza, la onda de cada pista que se va a generar.
    const auto& tracks = engine.getMixer().getTracks();
    const auto index = static_cast<int> (std::distance (tracks.begin(), std::find (tracks.begin(), tracks.end(), source)));
    auto& window = createFolderWindow (folderId, source->getName(), trackColourFor (source->getName(), index), expected);
    auto& view = window.getView();

    // El anillo de frecuencias dibuja la propia canción.
    if (const auto clips = source->getClips(); ! clips.empty() && clips.front().source != nullptr)
        view.setSourceAudio (clips.front().source, clips.front().sourceOffset, clips.front().length);

    view.getProgress = [this] { return ai.getProgress(); };
    view.getStatus = [this] { return ai.getStatus(); };
    view.onCancel = [this] { ai.cancel(); };
    window.setName (tr ("Separando instrumentos"));
    window.present();
}

void MainComponent::showSeparationWindow()
{
    if (const auto found = folderWindows.find (separatingFolderId); found != folderWindows.end())
        found->second->present();
}

//==============================================================================
SeparationWindow& MainComponent::createFolderWindow (const juce::String& folderId, const juce::String& name,
                                                     juce::Colour colour, const juce::StringArray& stemIds)
{
    std::vector<SeparationView::Stem> stems;

    for (const auto& id : stemIds)
    {
        const auto stemName = stemDisplayName (id);
        stems.push_back ({ stemName, trackColourFor (stemName, 0) });
    }

    auto window = std::make_unique<SeparationWindow> (name, colour, std::move (stems));
    window->setName (tr ("Ondas: {0}", name));

    // Cada onda sigue a su pista: si se elimina desaparece; si se deshace, vuelve.
    window->getView().isStemPresent = [this, folderId, stemIds] (int index)
    {
        return juce::isPositiveAndBelow (index, stemIds.size()) && isStemPresent (folderId, stemIds[index]);
    };

    // Cerrarla con la X actualiza el botón "Ondas" de la carpeta.
    window->onVisibilityChanged = [safe = juce::Component::SafePointer<MainComponent> (this)]
    {
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->updateFolders(); });
    };

    auto& result = *window;
    folderWindows[folderId] = std::move (window);
    return result;
}

bool MainComponent::isStemPresent (const juce::String& folderId, const juce::String& stemId) const
{
    // Mientras separa o carga las pistas todavía no existen: se ven todas.
    if (folderId == separatingFolderId || folderId == loadingStemsFolderId)
        return true;

    const auto& tracks = engine.getMixer().getTracks();
    return std::any_of (tracks.begin(), tracks.end(), [&] (const auto& track)
    {
        return track->getStemGroup() == folderId && track->getStemId() == stemId;
    });
}

void MainComponent::toggleFolderWindow (const juce::String& folderId)
{
    auto found = folderWindows.find (folderId);

    if (found != folderWindows.end() && found->second->isVisible())
    {
        found->second->setVisible (false);
        updateFolders();
        return;
    }

    if (found == folderWindows.end())
    {
        const auto* folder = projects.findFolder (folderId);

        if (folder == nullptr || projects.getStemTracks (folderId).empty())
            return;

        auto& window = createFolderWindow (folderId, folder->name, folder->colour, folder->stems);
        window.getView().setFinished (true);

        // El anillo, con el audio de la canción separada (si sigue en el proyecto).
        for (const auto& track : engine.getMixer().getTracks())
        {
            for (const auto& clip : track->getClips())
            {
                if (clip.source != nullptr && clip.source->file == folder->sourceFile)
                {
                    const auto rate = clip.source->sampleRate;
                    const auto length = folder->sourceLengthSeconds < 0.0 ? clip.source->getLength()
                                                                           : std::llround (folder->sourceLengthSeconds * rate);
                    window.getView().setSourceAudio (clip.source, std::llround (folder->sourceStartSeconds * rate), length);
                    break;
                }
            }
        }

        found = folderWindows.find (folderId);
    }

    found->second->present();
    updateFolders();
}

bool MainComponent::isFolderWindowOpen (const juce::String& folderId) const
{
    const auto found = folderWindows.find (folderId);
    return found != folderWindows.end() && found->second->isVisible();
}

void MainComponent::updateFolders()
{
    // Las ventanas de carpetas que ya no existen o sin pistas de su separación
    // se cierran (la de la separación en curso, no).
    for (auto it = folderWindows.begin(); it != folderWindows.end();)
    {
        const auto& id = it->first;
        const auto keep = id == separatingFolderId || id == loadingStemsFolderId
                       || (projects.findFolder (id) != nullptr && ! projects.getStemTracks (id).empty());
        it = keep ? std::next (it) : folderWindows.erase (it);
    }

    std::vector<TrackListView::FolderInfo> infos;

    for (const auto& folder : projects.getFolders())
        infos.push_back ({ folder.id, folder.name, folder.colour, folder.expanded,
                           ! projects.getStemTracks (folder.id).empty(), isFolderWindowOpen (folder.id) });

    trackList.setFolders (std::move (infos));
}

void MainComponent::separationFinished (const SeparationResult& result, std::shared_ptr<AudioTrack> source,
                                        const juce::String& folderId, const juce::StringArray& expectedStems)
{
    separatingFolderId.clear();
    const auto window = folderWindows.find (folderId);

    // Bien: la ventana se queda (con todas las ondas) y ya pertenece a la
    // carpeta de las pistas nuevas. Cancelada o con error: se cierra.
    if (window != folderWindows.end())
    {
        if (result.status.wasOk() && ! result.cancelled)
        {
            window->second->getView().setFinished (true);

            if (source != nullptr)
                window->second->setName (tr ("Ondas: {0}", source->getName()));
        }
        else
        {
            folderWindows.erase (window);
        }
    }

    if (result.cancelled)
    {
        statusBar.setMessage (tr ("Separación cancelada."));
        return;
    }

    if (result.status.failed())
    {
        showError (tr ("Error en la separación"), result.status.getErrorMessage());
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

    // Carpeta con las pistas nuevas (antes de cargarlas, para que aparezcan
    // ya dentro). Guarda lo que necesita su ventana de ondas.
    TrackFolder folder;
    folder.id = folderId;
    folder.name = source != nullptr ? source->getName() : tr ("Separación");
    folder.stems = expectedStems;

    if (source != nullptr)
    {
        const auto& all = engine.getMixer().getTracks();
        const auto index = static_cast<int> (std::distance (all.begin(), std::find (all.begin(), all.end(), source)));
        folder.colour = trackColourFor (source->getName(), index);

        if (const auto clips = source->getClips(); ! clips.empty() && clips.front().source != nullptr)
        {
            const auto rate = clips.front().source->sampleRate;
            folder.sourceFile = clips.front().source->file;
            folder.sourceStartSeconds = static_cast<double> (clips.front().sourceOffset) / rate;
            folder.sourceLengthSeconds = static_cast<double> (clips.front().length) / rate;
        }
    }

    projects.addFolder (std::move (folder));
    loadingStemsFolderId = folderId;

    std::vector<ProjectManager::NewTrack> tracks;

    for (const auto& stem : result.stems)
    {
        ProjectManager::NewTrack track { stemDisplayName (stem.name), stem.file, startSeconds, false };
        track.folderId = folderId;
        track.stemGroup = folderId;
        track.stemId = stem.name;
        tracks.push_back (std::move (track));
    }

    const auto numStems = static_cast<int> (tracks.size());
    std::weak_ptr<AudioTrack> weakSource = source;

    projects.addTracks (std::move (tracks),
                        [safe = juce::Component::SafePointer<MainComponent> (this), weakSource, numStems] (juce::Result r)
    {
        if (safe == nullptr)
            return;

        safe->loadingStemsFolderId.clear();
        safe->updateFolders();

        if (r.failed())
        {
            safe->reportResult (r, {});
            return;
        }

        // La suma de los stems ya reproduce la canción: se silencia el original
        // para no oírlo dos veces (sigue disponible para comparar).
        if (const auto original = weakSource.lock())
            original->getMute().set (1.0f);

        safe->statusBar.setMessage (tr ("Separación completada: {0} pistas. La pista original se ha silenciado.", numStems));
    }, msg ("Separar instrumentos"));
}

void MainComponent::showAbout()
{
    const auto message = "StemLab " + juce::JUCEApplication::getInstance()->getApplicationVersion() + "\n\n"
                       + tr ("Mini-DAW para separar, editar y mezclar instrumentos.") + "\n\n"
                       + juce::SystemStats::getJUCEVersion() + "\n"
                       + tr ("Separación de fuentes: Demucs (Python + PyTorch)");

    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Acerca de StemLab"), message, "OK", this);
}

//==============================================================================
// Idioma, tema y tutorial

void MainComponent::changeInterface (Language language, Theme theme, bool reopenTour)
{
    if (language == Localisation::getLanguage() && theme == Palette::getTheme())
        return;

    // Toda la interfaz se vuelve a crear con el idioma y el tema nuevos: mejor
    // no hacerlo con una separación, una carga o una grabación a medias.
    if (! ensureIdle (tr ("No se puede cambiar el idioma ni el tema mientras hay una separación, una carga de audio o una grabación en curso.")))
        return;

    if (onInterfaceChange != nullptr)
        onInterfaceChange (language, theme, reopenTour);    // MainWindow: destruye este componente (más tarde)
}

void MainComponent::showTour()
{
    // Las zonas se calculan al mostrar cada paso: siguen a la ventana si cambia de tamaño.
    const auto inTransport = [this] (juce::Rectangle<int> (TransportBar::* area)() const)
    {
        return [this, area] { return (transportBar.*area)().translated (transportBar.getX(), transportBar.getY()); };
    };

    std::vector<TourOverlay::Step> steps;

    steps.push_back ({ nullptr,
                       tr ("Te damos la bienvenida a StemLab"),
                       tr ("StemLab es un pequeño estudio de grabación: separa una canción en instrumentos con IA, "
                           "graba encima, edita cada pista, mézclalas y exporta el resultado.\n\n"
                           "Este recorrido te enseña cada parte en un minuto. Antes, elige cómo quieres verlo:"),
                       true });

    steps.push_back ({ [this] { return menuBar.getBounds(); },
                       tr ("Los menús"),
                       tr ("Aquí está todo lo que StemLab sabe hacer. Archivo: proyectos, importar audio y exportar la mezcla. "
                           "Editar: deshacer, cortar, copiar, pegar y gestionar las pistas. Proyecto: acercar y alejar. "
                           "Audio: tarjeta de sonido. Ver: tema e idioma.") });

    steps.push_back ({ inTransport (&TransportBar::getButtonsArea),
                       tr ("El transporte"),
                       tr ("Ir al inicio, reproducir o pausar (Espacio), detener y grabar (R).\n\n"
                           "El botón rojo graba con el micrófono en la pista seleccionada.") });

    steps.push_back ({ inTransport (&TransportBar::getTimeArea),
                       tr ("Tiempo y tempo"),
                       tr ("La posición del cabezal y la duración total del proyecto. Al lado está el tempo en BPM: "
                           "doble clic para cambiarlo.") });

    steps.push_back ({ inTransport (&TransportBar::getInputArea),
                       tr ("Nivel de entrada"),
                       tr ("La ganancia del micrófono y su medidor. Ajústala antes de grabar para que, al cantar o tocar, "
                           "el medidor se quede en verde o amarillo.") });

    steps.push_back ({ inTransport (&TransportBar::getMasterArea),
                       tr ("Volumen master"),
                       tr ("El volumen general de todo lo que suena, con su medidor. "
                           "Es también el volumen con el que se exporta la mezcla.") });

    steps.push_back ({ [this] { return trackList.getBounds().withTrimmedTop (TrackListView::rulerHeight); },
                       tr ("Las pistas"),
                       tr ("Cada fila es una pista. Pulsa + (o Ctrl+T) para añadir una, o arrastra una canción hasta aquí "
                           "para importarla.\n\n"
                           "A la izquierda está su cabecera: nombre, silenciar (M), solo (S), volumen y paneo. A la derecha, "
                           "su audio: haz clic para mover el cabezal, arrastra un fragmento para moverlo, estira sus bordes "
                           "para recortarlo y usa el clic derecho para dividir, copiar o pegar.") });

    steps.push_back ({ [this] { return trackList.getBounds().withHeight (TrackListView::rulerHeight); },
                       tr ("La línea de tiempo"),
                       tr ("Haz clic en la regla para saltar a ese momento. Ctrl + rueda acerca o aleja; "
                           "Shift + rueda desplaza a los lados.") });

    steps.push_back ({ [this] { return getMenuBounds (4); },
                       tr ("Separar instrumentos con IA"),
                       tr ("Importa una canción y elige IA > Separar instrumentos: StemLab la divide en voz, batería, bajo y "
                           "otros, cada uno en su propia pista dentro de una carpeta.\n\n"
                           "En este menú también eliges el modelo de separación.") });

    steps.push_back ({ [this] { return mixer.getBounds(); },
                       tr ("El mezclador"),
                       tr ("El canal de la pista seleccionada: volumen, paneo y sus efectos (ganancia, saturación, "
                           "ecualizador, compresor y limitador).\n\n"
                           "Activa cada efecto con su casilla y gira los controles; un doble clic los devuelve a su valor inicial.") });

    steps.push_back ({ [this] { return statusBar.getBounds(); },
                       tr ("La barra de estado"),
                       tr ("Aquí StemLab te cuenta qué acaba de pasar y qué puedes hacer después. Durante una separación "
                           "muestra el progreso; a la derecha, dónde está guardado el proyecto.") });

    steps.push_back ({ [this] { return getMenuBounds (5).getUnion (getMenuBounds (6)); },
                       tr ("Tema, idioma y ayuda"),
                       tr ("En el menú Ver cambias entre el tema claro y el oscuro, y de idioma.\n\n"
                           "Puedes repetir este tutorial cuando quieras en Ayuda > Tutorial.") });

    tour = std::make_unique<TourOverlay> (std::move (steps));
    tour->onClose = [safe = juce::Component::SafePointer<MainComponent> (this)]
    {
        // El tutorial se está cerrando desde uno de sus botones: se destruye después.
        juce::MessageManager::callAsync ([safe]
        {
            if (safe == nullptr)
                return;

            safe->tour.reset();
            safe->grabKeyboardFocus();

            if (safe->settings != nullptr)
                safe->settings->setValue ("tutorialSeen", true);
        });
    };
    tour->onLanguageChosen = [this] (Language language) { changeInterface (language, Palette::getTheme(), true); };
    tour->onThemeChosen = [this] (Theme theme) { changeInterface (Localisation::getLanguage(), theme, true); };

    addAndMakeVisible (*tour);
    tour->setBounds (getLocalBounds());
    tour->toFront (false);
    tour->grabKeyboardFocus();
}

juce::Rectangle<int> MainComponent::getMenuBounds (int menuIndex) const
{
    // Cada menú de la barra es un componente hijo suyo, en orden.
    if (auto* item = menuBar.getChildComponent (menuIndex))
        return item->getBounds().translated (menuBar.getX(), menuBar.getY());

    return menuBar.getBounds();
}

//==============================================================================
bool MainComponent::ensureIdle (const juce::String& message)
{
    if (ai.isBusy() || projects.isLoading() || engine.isRecording())
    {
        showError (tr ("Espera un momento"), message);
        return false;
    }

    return true;
}

void MainComponent::askToSaveChanges (std::function<void()> continueAction)
{
    if (! projects.hasUnsavedChanges())
    {
        continueAction();
        return;
    }

    // Evita abrir el diálogo dos veces (p. ej. pulsar la X repetidamente).
    if (unsavedChangesDialogOpen)
        return;

    unsavedChangesDialogOpen = true;

    const auto message = tr ("El proyecto \"{0}\" tiene cambios sin guardar.\n\n"
                             "¿Quieres guardarlos antes de continuar?", projects.getProject().getName());

    juce::AlertWindow::showYesNoCancelBox (juce::MessageBoxIconType::QuestionIcon, tr ("Cambios sin guardar"), message,
                                           tr ("Guardar"), tr ("No guardar"), tr ("Cancelar"), this,
                                           juce::ModalCallbackFunction::create (
                                               [safe = juce::Component::SafePointer<MainComponent> (this),
                                                continueAction = std::move (continueAction)] (int result)
                                           {
                                               if (safe == nullptr)
                                                   return;

                                               safe->unsavedChangesDialogOpen = false;

                                               if (result == 1)            // Guardar
                                                   safe->saveThen (continueAction);
                                               else if (result == 2)       // No guardar
                                                   continueAction();
                                               // 0: Cancelar (o Esc): no se hace nada.
                                           }));
}

void MainComponent::saveThen (std::function<void()> action)
{
    // Proyecto que nunca se guardó: primero hay que elegir dónde (Guardar como).
    // Si se cancela ese diálogo, tampoco se continúa.
    if (projects.getProject().isTemporary())
    {
        saveProjectAs (std::move (action));
        return;
    }

    const auto result = projects.save();
    reportResult (result, tr ("Proyecto guardado."));

    if (result.wasOk())
        action();
}

void MainComponent::requestQuit()
{
    // Una grabación en curso se termina y se añade al proyecto antes de preguntar.
    if (engine.isRecording())
        finishRecording();

    // Mientras se carga audio (por ejemplo, esa grabación) todavía no se puede
    // saber qué cambió: se espera un momento y se vuelve a intentar.
    if (projects.isLoading())
    {
        juce::Timer::callAfterDelay (200, [safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safe != nullptr)
                safe->requestQuit();
        });
        return;
    }

    askToSaveChanges ([] { juce::JUCEApplication::quit(); });
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
        const auto title = "StemLab - " + project.getName()
                         + (projects.hasUnsavedChanges() ? juce::String (" *") : juce::String())
                         + (project.isTemporary() ? " " + tr ("(sin guardar)") : juce::String());

        if (window->getName() != title)
            window->setName (title);
    }
}
}
