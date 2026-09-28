#include "ProjectManager.h"

#include "Audio/AudioFileLoader.h"
#include "Utils/Strings.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace stemlab
{
namespace
{
    bool containsTrack (const AudioMixer& mixer, const AudioTrack* track)
    {
        const auto& tracks = mixer.getTracks();
        return std::any_of (tracks.begin(), tracks.end(), [track] (const auto& t) { return t.get() == track; });
    }

    /** Cambio de la lista de clips de una pista: guarda la lista de antes y la
        de después. Los clips comparten el audio (ClipSource), así que copiar
        las listas no duplica muestras. */
    class ClipEditAction final : public juce::UndoableAction
    {
    public:
        ClipEditAction (AudioMixer& m, std::function<void()> changed, std::shared_ptr<AudioTrack> t,
                        std::vector<AudioClip> clipsBefore, std::vector<AudioClip> clipsAfter)
            : mixer (m), onChanged (std::move (changed)), track (std::move (t)),
              before (std::move (clipsBefore)), after (std::move (clipsAfter))
        {
        }

        bool perform() override     { return apply (after); }
        bool undo() override        { return apply (before); }

    private:
        bool apply (const std::vector<AudioClip>& clips)
        {
            // La pista ya no está en el proyecto: el historial deja de ser válido.
            if (! containsTrack (mixer, track.get()))
                return false;

            track->setClips (clips);
            onChanged();
            return true;
        }

        AudioMixer& mixer;
        std::function<void()> onChanged;
        std::shared_ptr<AudioTrack> track;
        std::vector<AudioClip> before, after;
    };

    int indexOfTrack (const AudioMixer& mixer, const AudioTrack* track)
    {
        const auto& tracks = mixer.getTracks();

        for (size_t i = 0; i < tracks.size(); ++i)
            if (tracks[i].get() == track)
                return static_cast<int> (i);

        return -1;
    }

    /** Poner o quitar una pista del proyecto. La acción conserva la pista (con
        sus clips, volumen y efectos) para devolverla a su sitio. */
    class TrackPresenceAction final : public juce::UndoableAction
    {
    public:
        enum class Kind { add, remove };

        TrackPresenceAction (Kind k, AudioMixer& m, std::function<void()> changed,
                             std::shared_ptr<AudioTrack> t, int insertIndex = -1)
            : kind (k), mixer (m), onChanged (std::move (changed)), track (std::move (t)), index (insertIndex)
        {
        }

        bool perform() override     { return kind == Kind::add ? insert() : remove(); }
        bool undo() override        { return kind == Kind::add ? remove() : insert(); }

    private:
        bool insert()
        {
            if (containsTrack (mixer, track.get()))
                return false;

            const auto size = static_cast<int> (mixer.getTracks().size());
            mixer.addTrack (track, index < 0 ? size : juce::jmin (index, size));
            onChanged();
            return true;
        }

        bool remove()
        {
            index = indexOfTrack (mixer, track.get());

            if (index < 0)
                return false;

            track->setArmed (false);
            mixer.removeTrack (track.get());
            onChanged();
            return true;
        }

        Kind kind;
        AudioMixer& mixer;
        std::function<void()> onChanged;
        std::shared_ptr<AudioTrack> track;
        int index;
    };

    /** Cambiar una pista de posición (el orden es solo visual). */
    class MoveTrackAction final : public juce::UndoableAction
    {
    public:
        MoveTrackAction (AudioMixer& m, std::function<void()> changed, std::shared_ptr<AudioTrack> t, int from, int to)
            : mixer (m), onChanged (std::move (changed)), track (std::move (t)), fromIndex (from), toIndex (to)
        {
        }

        bool perform() override     { return moveTo (toIndex); }
        bool undo() override        { return moveTo (fromIndex); }

    private:
        bool moveTo (int newIndex)
        {
            const auto current = indexOfTrack (mixer, track.get());

            if (current < 0)
                return false;

            // Al anotar un arrastre, la pista ya está en su sitio: no se mueve.
            if (current != newIndex)
                mixer.moveTrack (current, newIndex);

            onChanged();
            return true;
        }

        AudioMixer& mixer;
        std::function<void()> onChanged;
        std::shared_ptr<AudioTrack> track;
        int fromIndex, toIndex;
    };

    /** Meter una pista en una carpeta, sacarla o moverla: carpeta y posición. */
    class FolderMoveAction final : public juce::UndoableAction
    {
    public:
        FolderMoveAction (AudioMixer& m, std::function<void()> changed, std::shared_ptr<AudioTrack> t,
                          juce::String toFolder, int toIndex)
            : mixer (m), onChanged (std::move (changed)), track (std::move (t)),
              fromFolder (track->getFolderId()), targetFolder (std::move (toFolder)),
              fromIndex (indexOfTrack (mixer, track.get())), targetIndex (toIndex)
        {
        }

        bool perform() override     { return apply (targetFolder, targetIndex); }
        bool undo() override        { return apply (fromFolder, fromIndex); }

    private:
        bool apply (const juce::String& folder, int index)
        {
            const auto current = indexOfTrack (mixer, track.get());

            if (current < 0)
                return false;

            track->setFolderId (folder);
            index = juce::jlimit (0, static_cast<int> (mixer.getTracks().size()) - 1, index);

            if (current != index)
                mixer.moveTrack (current, index);

            onChanged();
            return true;
        }

        AudioMixer& mixer;
        std::function<void()> onChanged;
        std::shared_ptr<AudioTrack> track;
        juce::String fromFolder, targetFolder;
        int fromIndex, targetIndex;
    };

    /** Poner o quitar una carpeta de la lista (eliminar carpeta). La acción
        conserva la carpeta (nombre, color, stems...) para devolverla a su sitio. */
    class FolderPresenceAction final : public juce::UndoableAction
    {
    public:
        FolderPresenceAction (std::vector<TrackFolder>& f, std::function<void()> changed, TrackFolder removed)
            : folders (f), onChanged (std::move (changed)), folder (std::move (removed))
        {
        }

        bool perform() override
        {
            const auto found = std::find_if (folders.begin(), folders.end(),
                                             [this] (const auto& f) { return f.id == folder.id; });

            if (found == folders.end())
                return false;

            index = static_cast<int> (std::distance (folders.begin(), found));
            folders.erase (found);
            onChanged();
            return true;
        }

        bool undo() override
        {
            folders.insert (folders.begin() + juce::jlimit (0, static_cast<int> (folders.size()), index), folder);
            onChanged();
            return true;
        }

    private:
        std::vector<TrackFolder>& folders;
        std::function<void()> onChanged;
        TrackFolder folder;
        int index = 0;
    };

    /** Cambiar el nombre de una pista. */
    class RenameTrackAction final : public juce::UndoableAction
    {
    public:
        RenameTrackAction (std::function<void()> changed, std::shared_ptr<AudioTrack> t, juce::String before, juce::String after)
            : onChanged (std::move (changed)), track (std::move (t)), oldName (std::move (before)), newName (std::move (after))
        {
        }

        bool perform() override     { track->setName (newName); onChanged(); return true; }
        bool undo() override        { track->setName (oldName); onChanged(); return true; }

    private:
        std::function<void()> onChanged;
        std::shared_ptr<AudioTrack> track;
        juce::String oldName, newName;
    };
}

ProjectManager::ProjectManager (AudioEngine& audioEngine)
    : trashFolder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("StemLab")
                       .getChildFile ("Papelera-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S"))
                       .getNonexistentSibling (false)),
      engine (audioEngine),
      loaderPool (juce::ThreadPoolOptions{}.withThreadName ("StemLab Loader").withNumberOfThreads (1))
{
    engine.onSampleRateChanged = [this] { reloadAllTracks(); };
    newProject();
}

ProjectManager::~ProjectManager()
{
    engine.onSampleRateChanged = nullptr;
    loaderPool.removeAllJobs (true, 10000);
    emptyTrash();
}

void ProjectManager::setBpm (double bpm)
{
    project.setBpm (bpm);
    sendChangeMessage();
}

//==============================================================================
void ProjectManager::newProject()
{
    ++generation;
    undoManager.clearUndoHistory();
    emptyTrash();
    folders.clear();
    engine.getTransport().stop();
    engine.getMixer().removeAllTracks();
    engine.getMixer().getMasterVolume().resetToDefault();

    // Hasta "Guardar como", la sesión vive en la carpeta temporal del sistema.
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("StemLab")
                            .getChildFile ("Sesion-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S"))
                            .getNonexistentSibling (false);

    project = Project ("Proyecto sin título"_u8, folder, true);

    if (const auto result = project.createFolderStructure(); result.failed())
        DBG ("No se pudo crear la carpeta de la sesion: " << result.getErrorMessage());

    markSaved();
    sendChangeMessage();
}

bool ProjectManager::hasUnsavedChanges() const
{
    // Mientras se carga audio la descripción está incompleta: no se compara.
    if (isLoading())
        return false;

    return ProjectSerializer::toJson (project, describe()) != savedSnapshot;
}

void ProjectManager::markSaved()
{
    savedSnapshot = ProjectSerializer::toJson (project, describe());
}

void ProjectManager::openProject (const juce::File& projectFileOrFolder, Callback onDone)
{
    const auto projectFile = projectFileOrFolder.isDirectory() ? Project::findProjectFileIn (projectFileOrFolder)
                                                               : projectFileOrFolder;

    if (projectFile == juce::File())
    {
        if (onDone != nullptr)
            onDone (juce::Result::fail ("No hay ningún proyecto de StemLab (.stemlab) en la carpeta:\n"_u8
                                        + projectFileOrFolder.getFullPathName()));

        return;
    }

    Project loaded;
    ProjectDocument document;

    if (const auto result = ProjectSerializer::read (projectFile, loaded, document); result.failed())
    {
        if (onDone != nullptr)
            onDone (result);

        return;
    }

    ++generation;
    undoManager.clearUndoHistory();
    emptyTrash();
    engine.getTransport().stop();
    engine.getMixer().removeAllTracks();
    engine.getMixer().getMasterVolume().set (document.masterVolumeDb);

    project = std::move (loaded);
    project.createFolderStructure();
    folders = document.folders;

    sendChangeMessage();

    // Recién abierto = sin cambios (la "foto" se toma cuando termina de cargar).
    loadTracks (requestsFrom (document), [this, onDone = std::move (onDone)] (juce::Result result)
    {
        markSaved();

        if (onDone != nullptr)
            onDone (result);
    });
}

juce::Result ProjectManager::save()
{
    if (project.isTemporary())
        return juce::Result::fail ("El proyecto aún no tiene carpeta: usa \"Guardar como\"."_u8);

    if (const auto result = project.createFolderStructure(); result.failed())
        return result;

    syncProjectFiles();
    const auto result = ProjectSerializer::write (project, describe());

    if (result.wasOk())
        markSaved();

    return result;
}

juce::Result ProjectManager::saveAs (const juce::File& newFolder)
{
    const auto oldFolder = project.getDirectory();

    if (newFolder != oldFolder)
    {
        if (newFolder.existsAsFile())
            return juce::Result::fail ("Ya existe un archivo con ese nombre.");

        const auto isOtherProject = Project::findProjectFileIn (newFolder).existsAsFile();

        if (newFolder.isDirectory() && ! isOtherProject
            && newFolder.getNumberOfChildFiles (juce::File::findFilesAndDirectories) > 0)
            return juce::Result::fail ("La carpeta ya existe y no está vacía:\n"_u8 + newFolder.getFullPathName());

        if (const auto result = newFolder.createDirectory(); result.failed())
            return result;

        for (const auto& subfolder : Project::getSubfolderNames())
        {
            const auto source = oldFolder.getChildFile (subfolder);

            if (source.isDirectory() && ! source.copyDirectoryTo (newFolder.getChildFile (subfolder)))
                return juce::Result::fail ("No se pudo copiar la carpeta " + subfolder);
        }

        // Los clips (también los del historial) apuntan ahora a las copias.
        const auto moved = [&] (const juce::File& file)
        {
            return newFolder.getChildFile (file.getRelativePathFrom (oldFolder));
        };

        for (const auto& source : getLiveSources())
            if (source->file.isAChildOf (oldFolder))
                source->file = moved (source->file);

        for (auto& folder : folders)
            if (folder.sourceFile.isAChildOf (oldFolder))
                folder.sourceFile = moved (folder.sourceFile);

        for (auto& [trashed, original] : trashOrigins)
            if (original.isAChildOf (oldFolder))
                original = moved (original);

        for (auto& reserved : reservedFiles)
            if (reserved.isAChildOf (oldFolder))
                reserved = moved (reserved);

        // La sesión temporal ya está copiada: se elimina para no llenar el disco.
        if (project.isTemporary())
            oldFolder.deleteRecursively();
    }

    const auto previousFile = project.getProjectFile();

    project.setDirectory (newFolder);
    project.setProjectFile (Project::projectFileFor (newFolder));
    project.setName (newFolder.getFileName());
    project.setTemporary (false);

    if (const auto result = project.createFolderStructure(); result.failed())
        return result;

    syncProjectFiles();
    const auto result = ProjectSerializer::write (project, describe());

    if (result.wasOk())
    {
        markSaved();

        // Un proyecto antiguo guardado en su misma carpeta pasa a ser .stemlab:
        // se quita el project.json viejo para que no haya dos versiones.
        if (previousFile.getFileName() == "project.json" && previousFile.getParentDirectory() == newFolder)
            previousFile.deleteFile();
    }

    sendChangeMessage();
    return result;
}

//==============================================================================
juce::File ProjectManager::folderForSaveAs (const juce::File& chosenFile)
{
    const auto name = chosenFile.getFileNameWithoutExtension();
    const auto parent = chosenFile.getParentDirectory();

    return parent.getFileName() == name ? parent : parent.getChildFile (name);
}

bool ProjectManager::canImport (const juce::File& file) const
{
    return file.existsAsFile()
        && engine.getFormatManager().findFormatForFileExtension (file.getFileExtension()) != nullptr;
}

void ProjectManager::importAudio (const juce::Array<juce::File>& files, Callback onDone)
{
    std::vector<NewTrack> tracks;

    for (const auto& file : files)
        tracks.push_back ({ file.getFileNameWithoutExtension(), file, 0.0, true });

    addTracks (std::move (tracks), std::move (onDone), "Importar audio");
}

void ProjectManager::addTracks (std::vector<NewTrack> tracks, Callback onDone, const juce::String& undoName)
{
    std::vector<TrackRequest> requests;

    for (const auto& track : tracks)
    {
        TrackRequest request;
        request.name = track.name;
        request.copyIntoProject = track.copyIntoProject;
        request.undoName = undoName;
        request.folderId = track.folderId;
        request.stemGroup = track.stemGroup;
        request.stemId = track.stemId;
        request.clips.push_back ({ track.file, track.startSeconds, 0.0, -1.0 });
        requests.push_back (std::move (request));
    }

    loadTracks (std::move (requests), std::move (onDone));
}

std::shared_ptr<AudioTrack> ProjectManager::addEmptyTrack (const juce::String& baseName, int insertIndex,
                                                           const juce::String& folderId)
{
    auto track = std::make_shared<AudioTrack> (createTrackName (baseName));
    track->setFolderId (folderId);
    performUndoable (std::make_unique<TrackPresenceAction> (TrackPresenceAction::Kind::add, engine.getMixer(),
                                                            [this] { notifyTracksEdited(); }, track, insertIndex),
                     "Añadir pista"_u8);
    return track;
}

std::shared_ptr<AudioTrack> ProjectManager::pasteTrack (const AudioTrack& copyFrom, int insertIndex)
{
    auto track = copyFrom.createCopy (createCopyName (copyFrom.getName()));
    performUndoable (std::make_unique<TrackPresenceAction> (TrackPresenceAction::Kind::add, engine.getMixer(),
                                                            [this] { notifyTracksEdited(); }, track, insertIndex),
                     "Pegar pista");
    return track;
}

void ProjectManager::removeTrack (const AudioTrack& track, const juce::String& actionName)
{
    // La pista queda guardada en el historial (en el hilo de mensajes) para
    // poder recuperarla. Su audio sale de la carpeta del proyecto al guardar.
    for (const auto& t : engine.getMixer().getTracks())
    {
        if (t.get() == &track)
        {
            performUndoable (std::make_unique<TrackPresenceAction> (TrackPresenceAction::Kind::remove, engine.getMixer(),
                                                                    [this] { notifyTracksEdited(); }, t),
                             actionName);
            return;
        }
    }
}

void ProjectManager::trackMoved (const std::shared_ptr<AudioTrack>& track, int fromIndex, int toIndex)
{
    if (track != nullptr && fromIndex != toIndex)
        performUndoable (std::make_unique<MoveTrackAction> (engine.getMixer(), [this] { notifyTracksEdited(); },
                                                            track, fromIndex, toIndex),
                         "Mover pista");
}

const TrackFolder* ProjectManager::findFolder (const juce::String& folderId) const
{
    for (const auto& folder : folders)
        if (folder.id == folderId)
            return &folder;

    return nullptr;
}

void ProjectManager::addFolder (TrackFolder folder)
{
    folders.push_back (std::move (folder));
    sendChangeMessage();
}

void ProjectManager::setFolderExpanded (const juce::String& folderId, bool expanded)
{
    for (auto& folder : folders)
    {
        if (folder.id == folderId && folder.expanded != expanded)
        {
            folder.expanded = expanded;
            sendChangeMessage();
        }
    }
}

std::vector<std::shared_ptr<AudioTrack>> ProjectManager::getFolderTracks (const juce::String& folderId) const
{
    std::vector<std::shared_ptr<AudioTrack>> result;

    for (const auto& track : engine.getMixer().getTracks())
        if (folderId.isNotEmpty() && track->getFolderId() == folderId)
            result.push_back (track);

    return result;
}

std::vector<std::shared_ptr<AudioTrack>> ProjectManager::getStemTracks (const juce::String& folderId) const
{
    std::vector<std::shared_ptr<AudioTrack>> result;

    for (const auto& track : engine.getMixer().getTracks())
        if (folderId.isNotEmpty() && track->getStemGroup() == folderId)
            result.push_back (track);

    return result;
}

void ProjectManager::moveTrackToFolder (const std::shared_ptr<AudioTrack>& track, const juce::String& folderId, int mixerIndex)
{
    if (track == nullptr || ! containsTrack (engine.getMixer(), track.get()))
        return;

    const auto sameFolder = track->getFolderId() == folderId;

    if (sameFolder && indexOfTrack (engine.getMixer(), track.get()) == mixerIndex)
        return;

    const auto name = sameFolder ? juce::String ("Mover pista")
                    : folderId.isEmpty() ? juce::String ("Sacar de la carpeta")
                    : track->getFolderId().isEmpty() ? juce::String ("Meter en la carpeta")
                    : juce::String ("Mover a otra carpeta");

    performUndoable (std::make_unique<FolderMoveAction> (engine.getMixer(), [this] { notifyTracksEdited(); },
                                                         track, folderId, mixerIndex),
                     name);
}

void ProjectManager::removeFolder (const juce::String& folderId)
{
    const auto* folder = findFolder (folderId);

    if (folder == nullptr)
        return;

    // Las pistas y la carpeta, en una sola transacción.
    undoManager.beginNewTransaction ("Eliminar carpeta");

    for (const auto& track : getFolderTracks (folderId))
        undoManager.perform (new TrackPresenceAction (TrackPresenceAction::Kind::remove, engine.getMixer(),
                                                      [this] { notifyTracksEdited(); }, track));

    undoManager.perform (new FolderPresenceAction (folders, [this] { notifyTracksEdited(); }, *folder));
}

void ProjectManager::trackRenamed (const std::shared_ptr<AudioTrack>& track, const juce::String& oldName)
{
    if (track != nullptr && track->getName() != oldName)
        performUndoable (std::make_unique<RenameTrackAction> ([this] { notifyTracksEdited(); }, track,
                                                              oldName, track->getName()),
                         "Cambiar nombre de la pista");
}

std::shared_ptr<AudioTrack> ProjectManager::getArmedTrack() const
{
    for (const auto& track : engine.getMixer().getTracks())
        if (track->isArmed())
            return track;

    return nullptr;
}

void ProjectManager::setArmedTrack (const std::shared_ptr<AudioTrack>& armedTrack)
{
    for (const auto& track : engine.getMixer().getTracks())
        track->setArmed (track == armedTrack);

    sendChangeMessage();
}

void ProjectManager::addRecording (const RecordingInfo& recording, std::weak_ptr<AudioTrack> target, Callback onDone)
{
    if (recording.timelineStart < 0 || recording.sampleRate <= 0.0)
    {
        recording.file.deleteFile();

        if (onDone != nullptr)
            onDone (juce::Result::fail ("No se grabó audio."_u8));

        return;
    }

    // Compensación de latencia: lo que se grabó en el instante T corresponde a
    // lo que sonaba en T - (latencia de entrada + latencia de salida).
    const auto startSeconds = static_cast<double> (recording.timelineStart - recording.latencySamples)
                            / recording.sampleRate;

    TrackRequest request;
    request.name = createTrackName ("Grabación"_u8);
    request.target = std::move (target);
    request.clips.push_back ({ recording.file, startSeconds, 0.0, -1.0 });

    std::vector<TrackRequest> requests;
    requests.push_back (std::move (request));
    loadTracks (std::move (requests), std::move (onDone));
}

void ProjectManager::notifyTracksEdited()
{
    engine.getMixer().updateContentLength();
    sendChangeMessage();
}

//==============================================================================
void ProjectManager::performUndoable (std::unique_ptr<juce::UndoableAction> action, const juce::String& actionName)
{
    undoManager.beginNewTransaction (actionName);
    undoManager.perform (action.release());
}

void ProjectManager::editClips (const std::shared_ptr<AudioTrack>& track, std::vector<AudioClip> newClips,
                                const juce::String& actionName)
{
    if (track == nullptr)
        return;

    performUndoable (std::make_unique<ClipEditAction> (engine.getMixer(), [this] { notifyTracksEdited(); }, track,
                                                       track->getClips(), std::move (newClips)),
                     actionName);
}

void ProjectManager::clipsEdited (const std::shared_ptr<AudioTrack>& track, std::vector<AudioClip> clipsBefore,
                                  const juce::String& actionName)
{
    if (track == nullptr)
        return;

    // perform() vuelve a aplicar la lista actual: no cambia nada, solo avisa.
    performUndoable (std::make_unique<ClipEditAction> (engine.getMixer(), [this] { notifyTracksEdited(); }, track,
                                                       std::move (clipsBefore), track->getClips()),
                     actionName);
}

bool ProjectManager::undo()
{
    return undoManager.undo();
}

bool ProjectManager::redo()
{
    return undoManager.redo();
}

juce::String ProjectManager::createTrackName (const juce::String& baseName) const
{
    const auto& tracks = engine.getMixer().getTracks();

    for (int number = 1;; ++number)
    {
        const auto candidate = baseName + " " + juce::String (number);
        const auto taken = std::any_of (tracks.begin(), tracks.end(),
                                        [&] (const auto& t) { return t->getName() == candidate; });

        if (! taken)
            return candidate;
    }
}

juce::String ProjectManager::createCopyName (const juce::String& name) const
{
    const auto& tracks = engine.getMixer().getTracks();
    const auto isTaken = [&] (const juce::String& candidate)
    {
        return std::any_of (tracks.begin(), tracks.end(), [&] (const auto& t) { return t->getName() == candidate; });
    };

    if (! isTaken (name))
        return name;

    for (int number = 1;; ++number)
    {
        const auto candidate = name + (number == 1 ? juce::String (" (copia)") : " (copia " + juce::String (number) + ")");

        if (! isTaken (candidate))
            return candidate;
    }
}

juce::File ProjectManager::createRecordingFile()
{
    const auto file = project.getRecordingsDirectory().getNonexistentChildFile ("Grabacion", ".wav", false);
    reservedFiles.addIfNotAlreadyThere (file);
    return file;
}

juce::File ProjectManager::createStemsFolderFor (const AudioTrack& track)
{
    auto baseName = juce::File::createLegalFileName (track.getSourceFile().getFileNameWithoutExtension());

    if (baseName.isEmpty())
        baseName = "stems";

    const auto folder = project.getStemsDirectory().getChildFile (baseName).getNonexistentSibling (false);
    reservedFiles.addIfNotAlreadyThere (folder);
    return folder;
}

//==============================================================================
void ProjectManager::loadTracks (std::vector<TrackRequest> requests, Callback onDone)
{
    if (requests.empty())
    {
        if (onDone != nullptr)
            onDone (juce::Result::ok());

        return;
    }

    ++pendingLoads;
    sendChangeMessage();

    const auto targetRate = engine.getSampleRate();
    const auto audioFolder = project.getAudioDirectory();
    const auto loadGeneration = generation;
    auto* formats = &engine.getFormatManager();
    juce::WeakReference<ProjectManager> weakThis (this);

    loaderPool.addJob ([weakThis, requests = std::move (requests), targetRate, audioFolder,
                        loadGeneration, formats, onDone = std::move (onDone)]
    {
        // Cada archivo se decodifica una sola vez aunque lo usen varios clips.
        auto sources = std::make_shared<SourceMap>();
        juce::StringArray errors;

        for (const auto& request : requests)
        {
            for (const auto& clip : request.clips)
            {
                const auto key = clip.file.getFullPathName();

                if (sources->count (key) > 0)
                    continue;

                auto file = clip.file;

                if (request.copyIntoProject && ! file.isAChildOf (audioFolder))
                {
                    const auto copy = audioFolder.getChildFile (file.getFileName()).getNonexistentSibling();

                    if (audioFolder.createDirectory().failed() || ! file.copyFileTo (copy))
                    {
                        errors.add (file.getFileName() + ": no se pudo copiar al proyecto.");
                        continue;
                    }

                    file = copy;
                }

                auto source = std::make_shared<ClipSource>();
                source->file = file;
                source->sampleRate = targetRate;

                if (const auto result = AudioFileLoader::load (*formats, file, targetRate, source->audio); result.failed())
                {
                    errors.add (file.getFileName() + ": " + result.getErrorMessage());
                    continue;
                }

                (*sources)[key] = std::move (source);
            }
        }

        juce::MessageManager::callAsync ([weakThis, requests, sources, errors, targetRate, loadGeneration, onDone]
        {
            if (auto* self = weakThis.get())
                self->finishLoading (requests, *sources, errors, targetRate, loadGeneration, onDone);
        });
    });
}

void ProjectManager::finishLoading (const std::vector<TrackRequest>& requests, const SourceMap& sources,
                                    const juce::StringArray& errors, double decodedSampleRate,
                                    int loadGeneration, const Callback& onDone)
{
    pendingLoads = juce::jmax (0, pendingLoads - 1);

    // Mientras se decodificaba se abrió o creó otro proyecto: descartar.
    if (loadGeneration != generation)
    {
        sendChangeMessage();
        return;
    }

    auto& mixer = engine.getMixer();
    auto startedTransaction = false;

    // El audio ya está en el proyecto: deja de estar reservado y guardar lo
    // tiene en cuenta (también sus archivos en disco).
    loadedSources.erase (std::remove_if (loadedSources.begin(), loadedSources.end(),
                                         [] (const auto& weak) { return weak.expired(); }),
                         loadedSources.end());

    for (const auto& [path, source] : sources)
    {
        loadedSources.push_back (source);
        reservedFiles.removeIf ([&] (const juce::File& reserved)
        {
            return source->file == reserved || source->file.isAChildOf (reserved);
        });
    }

    for (const auto& request : requests)
    {
        std::vector<AudioClip> clips;

        for (const auto& clipRequest : request.clips)
        {
            const auto found = sources.find (clipRequest.file.getFullPathName());

            if (found == sources.end())
                continue;

            const auto& source = found->second;
            const auto sourceLength = source->getLength();

            auto start = static_cast<juce::int64> (std::llround (clipRequest.startSeconds * decodedSampleRate));
            auto offset = juce::jlimit<juce::int64> (0, sourceLength, std::llround (clipRequest.offsetSeconds * decodedSampleRate));
            auto length = clipRequest.lengthSeconds < 0.0 ? sourceLength - offset
                                                          : static_cast<juce::int64> (std::llround (clipRequest.lengthSeconds * decodedSampleRate));
            length = juce::jmin (length, sourceLength - offset);

            // Un clip que empezaría antes del 0 (grabación con compensación de
            // latencia) se recorta por el principio.
            if (start < 0)
            {
                offset -= start;
                length += start;
                start = 0;
            }

            if (length < ClipEditing::minimumLength)
                continue;

            clips.push_back ({ AudioClip::createId(), source, start, offset, length });
        }

        // ¿Añadir a una pista existente (grabación sobre la pista seleccionada)?
        auto target = request.target.lock();
        const auto& tracks = mixer.getTracks();

        if (target != nullptr && std::find (tracks.begin(), tracks.end(), target) != tracks.end())
        {
            // Los fragmentos de una pista no se solapan: la toma se recorta al
            // hueco libre donde empieza (la compensación de latencia la
            // adelanta unos milisegundos sobre el audio anterior, y si llega al
            // fragmento siguiente se corta ahí). Se puede deshacer.
            auto updated = target->getClips();
            const auto before = updated.size();

            for (auto& clip : clips)
                if (ClipEditing::fitIntoFreeSpace (updated, clip))
                    updated.push_back (std::move (clip));

            if (updated.size() > before)
                editClips (target, std::move (updated), "Grabar fragmento");

            continue;
        }

        if (clips.empty() && ! request.keepIfEmpty)
            continue;

        auto track = std::make_shared<AudioTrack> (request.name);
        track->setClips (std::move (clips));
        track->setArmed (request.armed);

        if (! request.state.isVoid())
            track->applyState (request.state);

        if (request.folderId.isNotEmpty())
            track->setFolderId (request.folderId);

        if (request.stemGroup.isNotEmpty())
            track->setStem (request.stemGroup, request.stemId);

        if (request.armed)
            for (const auto& other : tracks)
                other->setArmed (false);

        // Importar o separar: todas las pistas de esta carga son un solo paso
        // del historial. Abrir o recargar un proyecto no entra en él.
        if (request.undoName.isNotEmpty())
        {
            if (! startedTransaction)
                undoManager.beginNewTransaction (request.undoName);

            startedTransaction = true;
            undoManager.perform (new TrackPresenceAction (TrackPresenceAction::Kind::add, mixer,
                                                          [this] { notifyTracksEdited(); }, std::move (track)));
        }
        else
        {
            mixer.addTrack (std::move (track));
        }
    }

    mixer.updateContentLength();
    sendChangeMessage();

    // El dispositivo cambió de frecuencia mientras se decodificaba.
    if (! sources.empty() && std::abs (decodedSampleRate - engine.getSampleRate()) > 0.5)
        reloadAllTracks();

    if (onDone != nullptr)
        onDone (errors.isEmpty() ? juce::Result::ok() : juce::Result::fail (errors.joinIntoString ("\n")));
}

void ProjectManager::reloadAllTracks()
{
    // La recarga (por cambio de sample rate) no es un cambio del usuario: si no
    // había nada sin guardar, tras recargar tampoco debe haberlo.
    const auto wasClean = ! hasUnsavedChanges();
    const auto document = describe();
    auto requests = requestsFrom (document);

    // Conservar qué pista estaba armada.
    const auto& tracks = engine.getMixer().getTracks();

    for (size_t i = 0; i < requests.size() && i < tracks.size(); ++i)
        requests[i].armed = tracks[i]->isArmed();

    // Las pistas se vuelven a crear (y los clips se miden en muestras de la
    // frecuencia anterior): el historial ya no sirve.
    ++generation;
    undoManager.clearUndoHistory();
    emptyTrash();
    engine.getMixer().removeAllTracks();
    sendChangeMessage();
    loadTracks (std::move (requests), [this, wasClean] (juce::Result)
    {
        if (wasClean)
            markSaved();
    });
}

std::vector<ProjectManager::TrackRequest> ProjectManager::requestsFrom (const ProjectDocument& document) const
{
    std::vector<TrackRequest> requests;

    for (const auto& track : document.tracks)
    {
        TrackRequest request;
        request.name = track.name;
        request.state = track.state;
        request.keepIfEmpty = true;

        for (const auto& clip : track.clips)
            request.clips.push_back ({ clip.file, clip.startSeconds, clip.offsetSeconds, clip.lengthSeconds });

        requests.push_back (std::move (request));
    }

    return requests;
}

ProjectDocument ProjectManager::describe() const
{
    ProjectDocument document;
    const auto& mixer = engine.getMixer();
    document.masterVolumeDb = mixer.getMasterVolume().get();

    // Solo las carpetas que aún tienen pistas (dentro, o generadas por ellas).
    for (const auto& folder : folders)
        if (! getFolderTracks (folder.id).empty() || ! getStemTracks (folder.id).empty())
            document.folders.push_back (folder);

    for (const auto& track : mixer.getTracks())
    {
        TrackDescription description;
        description.name = track->getName();
        description.state = track->getState();

        for (const auto& clip : track->getClips())
        {
            if (clip.source == nullptr)
                continue;

            const auto rate = clip.source->sampleRate;
            description.clips.push_back ({ clip.source->file,
                                           static_cast<double> (clip.timelineStart) / rate,
                                           static_cast<double> (clip.sourceOffset) / rate,
                                           static_cast<double> (clip.length) / rate });
        }

        document.tracks.push_back (std::move (description));
    }

    return document;
}

//==============================================================================
void ProjectManager::syncProjectFiles()
{
    // Mientras se carga audio (importar, separar, grabar) sus archivos aún no
    // están en ninguna pista: no se toca nada hasta el siguiente guardado.
    if (isLoading())
        return;

    const auto root = project.getDirectory();
    const auto audioFolder = project.getAudioDirectory();
    const auto stemsFolder = project.getStemsDirectory();
    const auto recordingsFolder = project.getRecordingsDirectory();
    const juce::Array<juce::File> audioSubfolders { audioFolder, stemsFolder, recordingsFolder };
    const auto& tracks = engine.getMixer().getTracks();

    // 1. Lo que se quitó al guardar y ha vuelto con Deshacer, a su sitio.
    for (const auto& track : tracks)
        for (const auto& source : track->getSources())
            restoreFromTrash (source);

    // 2. El audio que ya no usa ninguna pista sale del proyecto.
    std::set<juce::String> used;

    for (const auto& track : tracks)
        for (const auto& source : track->getSources())
            used.insert (source->file.getFullPathName());

    const auto audioFiles = engine.getFormatManager().getWildcardForAllFormats();

    for (const auto& subfolder : audioSubfolders)
        for (const auto& file : subfolder.findChildFiles (juce::File::findFiles, true, audioFiles))
            if (used.count (file.getFullPathName()) == 0 && ! isReserved (file))
                moveToTrash (file);

    // 3. Cada archivo, donde está su pista: en la carpeta en disco de su
    //    carpeta de pistas o, fuera de ellas, en audio/. Las grabaciones se
    //    quedan en recordings/, y un archivo que usan pistas de sitios
    //    distintos (pistas pegadas), donde está.
    const auto folderPaths = folderDirectories();
    std::map<juce::String, juce::File> destinations;
    std::set<juce::String> shared;

    for (const auto& track : tracks)
    {
        const auto found = folderPaths.find (track->getFolderId());
        const auto destination = found != folderPaths.end() ? found->second : audioFolder;

        for (const auto& source : track->getSources())
        {
            const auto path = source->file.getFullPathName();

            if (const auto [it, added] = destinations.emplace (path, destination); ! added && it->second != destination)
                shared.insert (path);
        }
    }

    for (const auto& [path, destination] : destinations)
    {
        const juce::File file (path);

        if (shared.count (path) > 0 || ! file.existsAsFile() || file.getParentDirectory() == destination
            || file.isAChildOf (recordingsFolder))
            continue;

        if (! file.isAChildOf (root))
            moveAudioFile (file, destination, true);        // de fuera del proyecto: se copia dentro
        else if (destination != audioFolder || file.isAChildOf (stemsFolder))
            moveAudioFile (file, destination, false);       // sale de stems/ o entra en una carpeta
    }

    // 4. Las subcarpetas vacías (de una carpeta de pistas eliminada) se borran.
    for (const auto& subfolder : audioSubfolders)
    {
        auto children = subfolder.findChildFiles (juce::File::findDirectories, true);

        // Primero las más profundas, para que sus padres queden vacíos.
        std::sort (children.begin(), children.end(), [] (const juce::File& a, const juce::File& b)
        {
            return a.getFullPathName().length() > b.getFullPathName().length();
        });

        for (const auto& child : children)
            if (! isReserved (child) && child.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0)
                child.deleteFile();
    }
}

std::map<juce::String, juce::File> ProjectManager::folderDirectories() const
{
    const auto stemsFolder = project.getStemsDirectory();
    std::map<juce::String, juce::File> result;
    juce::Array<juce::File> taken;

    // La de sus stems: la carpeta donde los dejó la separación.
    for (const auto& folder : folders)
    {
        auto candidates = getStemTracks (folder.id);

        for (const auto& track : getFolderTracks (folder.id))
            candidates.push_back (track);

        for (const auto& track : candidates)
        {
            for (const auto& source : track->getSources())
            {
                const auto parent = source->file.getParentDirectory();

                if (result.count (folder.id) == 0 && parent.isAChildOf (stemsFolder) && ! taken.contains (parent))
                {
                    result[folder.id] = parent;
                    taken.add (parent);
                }
            }
        }
    }

    // Una carpeta sin stems en disco (solo tiene pistas que se metieron en
    // ella): stems/<nombre>/, sin coincidir con la de otra.
    for (const auto& folder : folders)
    {
        if (result.count (folder.id) > 0 || getFolderTracks (folder.id).empty())
            continue;

        auto name = juce::File::createLegalFileName (folder.name);

        if (name.isEmpty())
            name = "Carpeta";

        auto directory = stemsFolder.getChildFile (name);

        for (int number = 2; taken.contains (directory) || isReserved (directory); ++number)
            directory = stemsFolder.getChildFile (name + " (" + juce::String (number) + ")");

        result[folder.id] = directory;
        taken.add (directory);
    }

    return result;
}

bool ProjectManager::moveAudioFile (const juce::File& file, const juce::File& destinationFolder, bool keepOriginal)
{
    auto target = destinationFolder.getChildFile (file.getFileName());

    if (target.exists())
        target = target.getNonexistentSibling();

    if (destinationFolder.createDirectory().failed()
        || ! (keepOriginal ? file.copyFileTo (target) : file.moveFileTo (target)))
    {
        DBG ("No se pudo mover " << file.getFullPathName() << " a " << target.getFullPathName());
        return false;
    }

    renameSources (file, target);

    for (auto& folder : folders)
        if (folder.sourceFile == file)
            folder.sourceFile = target;

    return true;
}

void ProjectManager::moveToTrash (const juce::File& file)
{
    // Cada archivo en su propia subcarpeta, con su nombre (si vuelve con
    // Deshacer, la separación usa ese nombre para la carpeta de stems).
    const auto slot = trashFolder.getChildFile (juce::String (static_cast<int> (trashOrigins.size()) + 1))
                                 .getNonexistentSibling (false);
    const auto target = slot.getChildFile (file.getFileName());

    if (slot.createDirectory().failed() || ! file.moveFileTo (target))
    {
        DBG ("No se pudo quitar " << file.getFullPathName() << " del proyecto");
        return;
    }

    // Los clips del historial tienen su audio en memoria; su ruta apunta a la
    // papelera hasta que vuelvan a una pista y se guarde.
    trashOrigins[target.getFullPathName()] = file;
    renameSources (file, target);
}

void ProjectManager::restoreFromTrash (const std::shared_ptr<ClipSource>& source)
{
    const auto found = trashOrigins.find (source->file.getFullPathName());

    if (found == trashOrigins.end() || ! source->file.existsAsFile())
        return;

    const auto trashed = source->file;
    const auto original = found->second;
    auto target = original;

    // Otro archivo ocupó su nombre mientras estaba en la papelera.
    if (target.exists())
        target = target.getNonexistentSibling();

    if (target.getParentDirectory().createDirectory().failed() || ! trashed.moveFileTo (target))
        return;

    trashOrigins.erase (found);
    renameSources (trashed, target);

    for (auto& folder : folders)
        if (folder.sourceFile == original)
            folder.sourceFile = target;
}

void ProjectManager::emptyTrash()
{
    trashOrigins.clear();
    trashFolder.deleteRecursively();
}

void ProjectManager::renameSources (const juce::File& from, const juce::File& to)
{
    for (const auto& source : getLiveSources())
        if (source->file == from)
            source->file = to;
}

std::vector<std::shared_ptr<ClipSource>> ProjectManager::getLiveSources()
{
    std::vector<std::shared_ptr<ClipSource>> live;

    for (const auto& weak : loadedSources)
        if (auto source = weak.lock())
            live.push_back (std::move (source));

    // Y el audio de las pistas que no pasó por la carga (por si acaso).
    for (const auto& track : engine.getMixer().getTracks())
        for (auto& source : track->getSources())
            if (std::find (live.begin(), live.end(), source) == live.end())
                live.push_back (std::move (source));

    return live;
}

bool ProjectManager::isReserved (const juce::File& file) const
{
    return std::any_of (reservedFiles.begin(), reservedFiles.end(), [&] (const juce::File& reserved)
    {
        return file == reserved || file.isAChildOf (reserved) || reserved.isAChildOf (file);
    });
}
}
