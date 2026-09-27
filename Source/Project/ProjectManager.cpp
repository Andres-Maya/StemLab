#include "ProjectManager.h"

#include "Audio/AudioFileLoader.h"
#include "Utils/Strings.h"

#include <cmath>
#include <map>

namespace stemlab
{
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

    addTracks (std::move (tracks), std::move (onDone));
}

void ProjectManager::addTracks (std::vector<NewTrack> tracks, Callback onDone)
{
    std::vector<TrackRequest> requests;

    for (const auto& track : tracks)
    {
        TrackRequest request;
        request.name = track.name;
        request.copyIntoProject = track.copyIntoProject;
        request.clips.push_back ({ track.file, track.startSeconds, 0.0, -1.0 });
        requests.push_back (std::move (request));
    }

    loadTracks (std::move (requests), std::move (onDone));
}

std::shared_ptr<AudioTrack> ProjectManager::addEmptyTrack (const juce::String& baseName, int insertIndex)
{
    auto track = std::make_shared<AudioTrack> (createTrackName (baseName));
    engine.getMixer().addTrack (track, insertIndex);
    sendChangeMessage();
    return track;
}

void ProjectManager::removeTrack (const AudioTrack& track)
{
    // El shared_ptr devuelto se libera aquí, en el hilo de mensajes. Los
    // archivos de audio se conservan en disco.
    engine.getMixer().removeTrack (&track);
    sendChangeMessage();
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
            for (auto& clip : clips)
                target->addClip (std::move (clip));

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

        mixer.addTrack (std::move (track));
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

    ++generation;
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
