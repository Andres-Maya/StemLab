#pragma once

#include <juce_events/juce_events.h>

#include "Audio/AudioEngine.h"
#include "ProjectSerializer.h"

#include <functional>
#include <map>
#include <vector>

namespace stemlab
{
/**
    Coordina el proyecto abierto con el motor de audio: crear, abrir, guardar,
    importar audio, crear pistas y convertir grabaciones o stems en clips.

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

    //==========================================================================
    // Pistas
    bool canImport (const juce::File& file) const;
    void importAudio (const juce::Array<juce::File>& files, Callback onDone);
    void addTracks (std::vector<NewTrack> tracks, Callback onDone);

    /** Crea una pista vacía (por ejemplo, para grabar en ella) en la posición
        indicada, o al final si insertIndex < 0. */
    std::shared_ptr<AudioTrack> addEmptyTrack (const juce::String& baseName, int insertIndex = -1);
    void removeTrack (const AudioTrack& track);

    /** Pista en la que se está grabando (solo una; resalta su franja y la vista
        previa en directo). */
    std::shared_ptr<AudioTrack> getArmedTrack() const;
    void setArmedTrack (const std::shared_ptr<AudioTrack>& track);

    /** Añade la grabación como un clip nuevo de la pista indicada (o de una
        pista nueva si ya no existe). */
    void addRecording (const RecordingInfo& recording, std::weak_ptr<AudioTrack> target, Callback onDone);

    /** Avisar tras editar clips (actualiza la duración y la interfaz). */
    void notifyTracksEdited();

    juce::String createTrackName (const juce::String& baseName) const;
    juce::File createRecordingFile() const;
    juce::File createStemsFolderFor (const AudioTrack& track) const;

private:
    struct ClipRequest
    {
        juce::File file;
        double startSeconds = 0.0;
        double offsetSeconds = 0.0;
        double lengthSeconds = -1.0;
    };

    struct TrackRequest
    {
        juce::String name;
        juce::var state;
        std::vector<ClipRequest> clips;
        std::weak_ptr<AudioTrack> target;   // si sigue existiendo, los clips se añaden a ella
        bool keepIfEmpty = false;           // proyecto abierto: conservar la pista aunque falte el audio
        bool copyIntoProject = false;
        bool armed = false;
    };

    using SourceMap = std::map<juce::String, std::shared_ptr<ClipSource>>;

    void loadTracks (std::vector<TrackRequest> requests, Callback onDone);
    void finishLoading (const std::vector<TrackRequest>& requests, const SourceMap& sources,
                        const juce::StringArray& errors, double decodedSampleRate,
                        int loadGeneration, const Callback& onDone);
    void reloadAllTracks();
    std::vector<TrackRequest> requestsFrom (const ProjectDocument& document) const;
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
