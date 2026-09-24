#include "ProjectManager.h"

#include "Audio/AudioFileLoader.h"
#include "Utils/Strings.h"

#include <cmath>

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

    sendChangeMessage();
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

    std::vector<LoadRequest> requests;

    for (const auto& track : document.tracks)
        requests.push_back ({ track.name, track.file, track.startSeconds, track.state, false });

    sendChangeMessage();
    loadTracks (std::move (requests), std::move (onDone));
}

juce::Result ProjectManager::save()
{
    if (project.isTemporary())
        return juce::Result::fail ("El proyecto aún no tiene carpeta: usa \"Guardar como\"."_u8);

    if (const auto result = project.createFolderStructure(); result.failed())
        return result;

    return ProjectSerializer::write (project, describe());
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

        // Las pistas apuntan ahora a las copias.
        for (const auto& track : engine.getMixer().getTracks())
            if (track->getSourceFile().isAChildOf (oldFolder))
                track->setSourceFile (newFolder.getChildFile (track->getSourceFile().getRelativePathFrom (oldFolder)));

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
    std::vector<LoadRequest> requests;

    for (const auto& track : tracks)
        requests.push_back ({ track.name, track.file, track.startSeconds, {}, track.copyIntoProject });

    loadTracks (std::move (requests), std::move (onDone));
}

void ProjectManager::addRecording (const RecordingInfo& recording, Callback onDone)
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

    int number = 1;

    for (const auto& track : engine.getMixer().getTracks())
        if (track->getSourceFile().isAChildOf (project.getRecordingsDirectory()))
            ++number;

    std::vector<NewTrack> tracks;
    tracks.push_back ({ "Grabación "_u8 + juce::String (number), recording.file, startSeconds, false });
    addTracks (std::move (tracks), std::move (onDone));
}

void ProjectManager::removeTrack (const AudioTrack& track)
{
    // El shared_ptr devuelto se libera aquí, en el hilo de mensajes. El archivo
    // de audio se conserva en disco.
    engine.getMixer().removeTrack (&track);
    sendChangeMessage();
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
void ProjectManager::loadTracks (std::vector<LoadRequest> requests, Callback onDone)
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
        auto decoded = std::make_shared<std::vector<DecodedTrack>>();
        juce::StringArray errors;

        for (const auto& request : requests)
        {
            auto file = request.file;

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

            DecodedTrack track;
            track.request = request;
            track.file = file;

            if (const auto result = AudioFileLoader::load (*formats, file, targetRate, track.audio); result.failed())
            {
                errors.add (file.getFileName() + ": " + result.getErrorMessage());
                continue;
            }

            // Una grabación con compensación de latencia puede empezar antes
            // del 0: se recorta su principio en vez de mover la pista.
            track.startSample = static_cast<juce::int64> (std::llround (request.startSeconds * targetRate));

            if (track.startSample < 0)
            {
                const auto trim = static_cast<int> (juce::jmin<juce::int64> (-track.startSample, track.audio.getNumSamples()));
                juce::AudioBuffer<float> trimmed (2, track.audio.getNumSamples() - trim);

                for (int ch = 0; ch < 2; ++ch)
                    trimmed.copyFrom (ch, 0, track.audio, ch, trim, trimmed.getNumSamples());

                track.audio = std::move (trimmed);
                track.startSample = 0;
            }

            if (track.audio.getNumSamples() == 0)
            {
                errors.add (file.getFileName() + ": no contiene audio.");
                continue;
            }

            decoded->push_back (std::move (track));
        }

        juce::MessageManager::callAsync ([weakThis, decoded, errors, targetRate, loadGeneration, onDone]
        {
            if (auto* self = weakThis.get())
                self->finishLoading (*decoded, errors, targetRate, loadGeneration, onDone);
        });
    });
}

void ProjectManager::finishLoading (std::vector<DecodedTrack>& decoded, const juce::StringArray& errors,
                                    double decodedSampleRate, int loadGeneration, const Callback& onDone)
{
    pendingLoads = juce::jmax (0, pendingLoads - 1);

    // Mientras se decodificaba se abrió o creó otro proyecto: descartar.
    if (loadGeneration != generation)
    {
        sendChangeMessage();
        return;
    }

    for (auto& item : decoded)
    {
        auto track = std::make_shared<AudioTrack> (item.request.name, item.file, std::move (item.audio), decodedSampleRate);
        track->setStartSample (item.startSample);

        if (! item.request.state.isVoid())
            track->applyState (item.request.state);

        engine.getMixer().addTrack (std::move (track));
    }

    sendChangeMessage();

    // El dispositivo cambió de frecuencia mientras se decodificaba.
    if (! decoded.empty() && std::abs (decodedSampleRate - engine.getSampleRate()) > 0.5)
        reloadAllTracks();

    if (onDone != nullptr)
        onDone (errors.isEmpty() ? juce::Result::ok() : juce::Result::fail (errors.joinIntoString ("\n")));
}

void ProjectManager::reloadAllTracks()
{
    const auto document = describe();

    ++generation;
    engine.getMixer().removeAllTracks();

    std::vector<LoadRequest> requests;

    for (const auto& track : document.tracks)
        requests.push_back ({ track.name, track.file, track.startSeconds, track.state, false });

    sendChangeMessage();
    loadTracks (std::move (requests), nullptr);
}

ProjectDocument ProjectManager::describe() const
{
    ProjectDocument document;
    const auto& mixer = engine.getMixer();
    document.masterVolumeDb = mixer.getMasterVolume().get();

    for (const auto& track : mixer.getTracks())
        document.tracks.push_back ({ track->getName(), track->getSourceFile(),
                                     static_cast<double> (track->getStartSample()) / track->getSampleRate(),
                                     track->getState() });

    return document;
}
}
