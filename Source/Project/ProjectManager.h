#pragma once

#include <juce_events/juce_events.h>

#include "Audio/AudioEngine.h"
#include "ProjectSerializer.h"

#include <functional>
#include <vector>

namespace stemlab
{
/**
    Coordina el proyecto abierto con el motor de audio: crear, abrir, guardar,
    importar audio y convertir grabaciones o stems en pistas.

    Todas las funciones públicas son del hilo de mensajes. La decodificación
    de audio ocurre en un hilo de trabajo (loaderPool) y el resultado vuelve al
    hilo de mensajes para crear las pistas. Emite un cambio (ChangeBroadcaster)
    cada vez que cambian las pistas o los datos del proyecto.
*/
class ProjectManager final : public juce::ChangeBroadcaster
{
public:
    using Callback = std::function<void (juce::Result)>;

    struct NewTrack
    {
        juce::String name;
        juce::File file;
        double startSeconds = 0.0;
        bool copyIntoProject = false;   // copiar a audio/ (archivos importados)
    };

    explicit ProjectManager (AudioEngine& engine);
    ~ProjectManager() override;

    const Project& getProject() const noexcept      { return project; }
    void setBpm (double bpm);
    bool isLoading() const noexcept                 { return pendingLoads > 0; }

    void newProject();
    void openProject (const juce::File& projectFileOrFolder, Callback onDone);
    juce::Result save();
    juce::Result saveAs (const juce::File& newFolder);

    bool canImport (const juce::File& file) const;
    void importAudio (const juce::Array<juce::File>& files, Callback onDone);
    void addTracks (std::vector<NewTrack> tracks, Callback onDone);
    void addRecording (const RecordingInfo& recording, Callback onDone);
    void removeTrack (const AudioTrack& track);

    juce::File createRecordingFile() const;
    juce::File createStemsFolderFor (const AudioTrack& track) const;

private:
    struct LoadRequest
    {
        juce::String name;
        juce::File file;
        double startSeconds = 0.0;
        juce::var state;
        bool copyIntoProject = false;
    };

    struct DecodedTrack
    {
        LoadRequest request;
        juce::File file;
        juce::AudioBuffer<float> audio;
        juce::int64 startSample = 0;
    };

    void loadTracks (std::vector<LoadRequest> requests, Callback onDone);
    void finishLoading (std::vector<DecodedTrack>& decoded, const juce::StringArray& errors,
                        double decodedSampleRate, int loadGeneration, const Callback& onDone);
    void reloadAllTracks();
    ProjectDocument describe() const;

    AudioEngine& engine;
    Project project;

    // Se incrementa al cambiar de proyecto: las cargas en curso de un proyecto
    // anterior se descartan al terminar.
    int generation = 0;
    int pendingLoads = 0;

    juce::ThreadPool loaderPool;

    JUCE_DECLARE_WEAK_REFERENCEABLE (ProjectManager)
    JUCE_DECLARE_NON_COPYABLE (ProjectManager)
};
}
