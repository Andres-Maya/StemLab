#include "ProjectManager.h"

#include "Audio/AudioFileLoader.h"
#include "Utils/Strings.h"

#include <algorithm>
#include <cmath>
#include <map>

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
    : engine (audioEngine),
      loaderPool (juce::ThreadPoolOptions{}.withThreadName ("StemLab Loader").withNumberOfThreads (1))
{
    engine.onSampleRateChanged = [this] { reloadAllTracks(); };
    newProject();
}

ProjectManager::~ProjectManager()
{
    engine.onSampleRateChanged = nullptr;
    loaderPool.removeAllJobs (true, 10000);
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
    const auto projectFile = projectFileOrFolder.isDirectory() ? projectFileOrFolder.getChildFile ("project.json")
                                                               : projectFileOrFolder;
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
    engine.getTransport().stop();
    engine.getMixer().removeAllTracks();
    engine.getMixer().getMasterVolume().set (document.masterVolumeDb);

    project = std::move (loaded);
    project.createFolderStructure();

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

        const auto isOtherProject = newFolder.getChildFile ("project.json").existsAsFile();

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

        // Los clips apuntan ahora a las copias.
        for (const auto& track : engine.getMixer().getTracks())
            for (const auto& source : track->getSources())
                if (source->file.isAChildOf (oldFolder))
                    source->file = newFolder.getChildFile (source->file.getRelativePathFrom (oldFolder));

        // La sesión temporal ya está copiada: se elimina para no llenar el disco.
        if (project.isTemporary())
            oldFolder.deleteRecursively();
    }

    project.setDirectory (newFolder);
    project.setName (newFolder.getFileName());
    project.setTemporary (false);

    if (const auto result = project.createFolderStructure(); result.failed())
        return result;

    const auto result = ProjectSerializer::write (project, describe());

    if (result.wasOk())
        markSaved();

    sendChangeMessage();
    return result;
}

//==============================================================================
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
        request.clips.push_back ({ track.file, track.startSeconds, 0.0, -1.0 });
        requests.push_back (std::move (request));
    }

    loadTracks (std::move (requests), std::move (onDone));
}

std::shared_ptr<AudioTrack> ProjectManager::addEmptyTrack (const juce::String& baseName, int insertIndex)
{
    auto track = std::make_shared<AudioTrack> (createTrackName (baseName));
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
    // poder recuperarla. Los archivos de audio se conservan en disco.
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

juce::File ProjectManager::createRecordingFile() const
{
    return project.getRecordingsDirectory().getNonexistentChildFile ("Grabacion", ".wav", false);
}

juce::File ProjectManager::createStemsFolderFor (const AudioTrack& track) const
{
    auto baseName = juce::File::createLegalFileName (track.getSourceFile().getFileNameWithoutExtension());

    if (baseName.isEmpty())
        baseName = "stems";

    return project.getStemsDirectory().getChildFile (baseName).getNonexistentSibling (false);
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
            // Va al final de la lista: queda (y suena) encima de lo que ya
            // hubiera en la pista. Se puede deshacer como cualquier edición.
            auto updated = target->getClips();

            for (auto& clip : clips)
                updated.push_back (std::move (clip));

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
}
