#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

#include "Audio/AudioEngine.h"
#include "ProjectSerializer.h"
#include "TrackFolder.h"

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

        // Pistas de una separación: carpeta en la que se muestran y su origen.
        juce::String folderId;
        juce::String stemGroup;
        juce::String stemId;
    };

    explicit ProjectManager (AudioEngine& engine);
    ~ProjectManager() override;

    const Project& getProject() const noexcept      { return project; }
    void setBpm (double bpm);
    bool isLoading() const noexcept                 { return pendingLoads > 0; }

    /** ¿Hay cambios sin guardar? Compara el estado actual (pistas, clips,
        volúmenes, efectos, nombres, orden, BPM...) con el del último
        guardado, apertura o proyecto nuevo. */
    bool hasUnsavedChanges() const;

    /** Proyecto vacío en una sesión temporal. La sesión anterior sin guardar
        (si la había) se borra, igual que al abrir otro proyecto o al cerrar. */
    void newProject();

    /** Abre un .stemlab (o un project.json antiguo), o la carpeta que lo contiene. */
    void openProject (const juce::File& projectFileOrFolder, Callback onDone);

    /** Escribe el .stemlab y deja la carpeta del proyecto como se ve en el
        programa (ver syncProjectFiles). */
    juce::Result save();

    /** Guarda el proyecto en una carpeta propia: newFolder/<nombre>.stemlab más
        audio/, stems/, recordings/ y exports/ (se copian desde la carpeta anterior
        y después se sincronizan como al guardar). */
    juce::Result saveAs (const juce::File& newFolder);

    /** Carpeta de "Guardar como" para el archivo elegido en el diálogo:
        C:/Musica/MiCancion.stemlab -> C:/Musica/MiCancion/ (si ya se eligió
        dentro de esa carpeta, la misma). */
    static juce::File folderForSaveAs (const juce::File& chosenFile);

    //==========================================================================
    // Pistas
    bool canImport (const juce::File& file) const;
    void importAudio (const juce::Array<juce::File>& files, Callback onDone);

    /** Carga los archivos como pistas nuevas. Al terminar, todas juntas son un
        solo paso del historial (undoName: "Importar audio", "Separar instrumentos"...). */
    void addTracks (std::vector<NewTrack> tracks, Callback onDone,
                    const juce::String& undoName = juce::String::fromUTF8 ("Añadir pistas"));

    // Todas estas operaciones con pistas se pueden deshacer (Ctrl+Z).

    /** Crea una pista vacía (por ejemplo, para grabar en ella) en la posición
        indicada, o al final si insertIndex < 0. */
    std::shared_ptr<AudioTrack> addEmptyTrack (const juce::String& baseName, int insertIndex = -1,
                                               const juce::String& folderId = {});

    /** Inserta una copia de la pista (Pegar pista) en la posición indicada, o
        al final si insertIndex < 0. Si el nombre ya existe se añade "(copia)". */
    std::shared_ptr<AudioTrack> pasteTrack (const AudioTrack& copyFrom, int insertIndex = -1);

    /** Quita la pista del proyecto. */
    void removeTrack (const AudioTrack& track, const juce::String& actionName = "Eliminar pista");

    /** Anota un cambio de orden que ya se aplicó en el mezclador (arrastrar la cabecera). */
    void trackMoved (const std::shared_ptr<AudioTrack>& track, int fromIndex, int toIndex);

    /** Anota un cambio de nombre que ya se aplicó a la pista. */
    void trackRenamed (const std::shared_ptr<AudioTrack>& track, const juce::String& oldName);

    //==========================================================================
    // Carpetas de pistas (las crea la separación por IA)

    const std::vector<TrackFolder>& getFolders() const noexcept    { return folders; }
    const TrackFolder* findFolder (const juce::String& folderId) const;
    void addFolder (TrackFolder folder);
    void setFolderExpanded (const juce::String& folderId, bool expanded);

    /** Pistas que se muestran en la carpeta, en orden. */
    std::vector<std::shared_ptr<AudioTrack>> getFolderTracks (const juce::String& folderId) const;

    /** Pistas que generó la separación de esa carpeta (estén dentro o no). */
    std::vector<std::shared_ptr<AudioTrack>> getStemTracks (const juce::String& folderId) const;

    /** Meter la pista en una carpeta, sacarla (folderId vacío) o cambiarla de
        sitio, y dejarla en esa posición del mezclador. Se puede deshacer. */
    void moveTrackToFolder (const std::shared_ptr<AudioTrack>& track, const juce::String& folderId, int mixerIndex);

    /** Elimina la carpeta con las pistas que tiene dentro (las que se sacaron
        de ella se quedan). Es un solo paso del historial: Ctrl+Z la devuelve
        entera. Su audio sale de la carpeta del proyecto al guardar. */
    void removeFolder (const juce::String& folderId);

    /** Pista en la que se está grabando (solo una; resalta su franja y la vista
        previa en directo). */
    std::shared_ptr<AudioTrack> getArmedTrack() const;
    void setArmedTrack (const std::shared_ptr<AudioTrack>& track);

    /** Añade la grabación como un clip nuevo de la pista indicada (o de una
        pista nueva si ya no existe), recortada al hueco libre donde empieza:
        nunca tapa el audio que ya tiene la pista. */
    void addRecording (const RecordingInfo& recording, std::weak_ptr<AudioTrack> target, Callback onDone);

    /** Avisar tras editar clips (actualiza la duración y la interfaz). */
    void notifyTracksEdited();

    //==========================================================================
    // Edición de fragmentos con deshacer / rehacer (Ctrl+Z / Ctrl+Y)

    /** Sustituye los clips de la pista y anota el cambio en el historial. */
    void editClips (const std::shared_ptr<AudioTrack>& track, std::vector<AudioClip> newClips,
                    const juce::String& actionName);

    /** Anota una edición que ya se aplicó a la pista (arrastrar con el ratón
        aplica cada movimiento al momento para oírlo): clipsBefore es la lista
        de antes de empezar. */
    void clipsEdited (const std::shared_ptr<AudioTrack>& track, std::vector<AudioClip> clipsBefore,
                      const juce::String& actionName);

    bool canUndo() const                            { return undoManager.canUndo(); }
    bool canRedo() const                            { return undoManager.canRedo(); }
    juce::String getUndoDescription() const         { return undoManager.getUndoDescription(); }
    juce::String getRedoDescription() const         { return undoManager.getRedoDescription(); }
    bool undo();
    bool redo();

    juce::String createTrackName (const juce::String& baseName) const;

    /** El nombre tal cual si está libre; si no, "nombre (copia)", "nombre (copia 2)"... */
    juce::String createCopyName (const juce::String& name) const;

    /** Archivo para una grabación nueva y carpeta para los stems de una
        separación. Quedan reservados (guardar no los toca mientras se
        escriben) hasta que su audio se carga en una pista. */
    juce::File createRecordingFile();
    juce::File createStemsFolderFor (const AudioTrack& track);

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
        juce::String undoName;              // vacío: no entra en el historial (abrir, recargar)
        juce::String folderId, stemGroup, stemId;
    };

    using SourceMap = std::map<juce::String, std::shared_ptr<ClipSource>>;

    void loadTracks (std::vector<TrackRequest> requests, Callback onDone);
    void finishLoading (const std::vector<TrackRequest>& requests, const SourceMap& sources,
                        const juce::StringArray& errors, double decodedSampleRate,
                        int loadGeneration, const Callback& onDone);
    void reloadAllTracks();
    std::vector<TrackRequest> requestsFrom (const ProjectDocument& document) const;
    ProjectDocument describe() const;

    /** Añade al historial una acción ya creada, como una transacción propia. */
    void performUndoable (std::unique_ptr<juce::UndoableAction> action, const juce::String& actionName);

    /** Toma la "foto" del estado actual como referencia de "sin cambios". */
    void markSaved();
    juce::String savedSnapshot;

    //==========================================================================
    // Carpeta del proyecto en disco

    /** Al guardar, la carpeta del proyecto refleja lo que hay en el programa:
          - el audio que ya no usa ninguna pista (pistas o fragmentos eliminados,
            carpetas sin pistas) se quita de audio/, stems/ y recordings/;
          - el audio de una pista que está en una carpeta va a stems/<carpeta>/,
            y el que se sacó de una carpeta, a audio/ (las grabaciones se
            quedan siempre en recordings/);
          - el audio que está fuera del proyecto se copia dentro;
          - las subcarpetas que quedan vacías se borran.
        Lo quitado va a una papelera temporal: si se deshace (Ctrl+Z), el
        archivo vuelve a su sitio en ese momento. */
    void syncProjectFiles();

    /** Carpeta en disco de cada carpeta de pistas: la de sus stems
        (stems/<canción>/) o, si no tiene, una nueva con su nombre. */
    std::map<juce::String, juce::File> folderDirectories() const;

    bool moveAudioFile (const juce::File& file, const juce::File& destinationFolder, bool keepOriginal);
    void moveToTrash (const juce::File& file);
    void restoreFromTrash (const std::shared_ptr<ClipSource>& source);
    void emptyTrash();

    /** Cambia la ruta de todos los ClipSource vivos (pistas, historial, ventanas de ondas). */
    void renameSources (const juce::File& from, const juce::File& to);
    std::vector<std::shared_ptr<ClipSource>> getLiveSources();

    bool isReserved (const juce::File& file) const;

    /** Borra la carpeta de la sesión sin guardar (%TEMP%/StemLab/Sesion-...)
        al dejarla: nadie la va a volver a abrir. */
    void discardTemporarySession();

    std::vector<std::weak_ptr<ClipSource>> loadedSources;
    juce::Array<juce::File> reservedFiles;
    const juce::File trashFolder;
    std::map<juce::String, juce::File> trashOrigins;    // archivo en la papelera -> ruta original

    AudioEngine& engine;
    Project project;
    std::vector<TrackFolder> folders;

    // Se incrementa al cambiar de proyecto: las cargas en curso de un proyecto
    // anterior se descartan al terminar.
    int generation = 0;
    int pendingLoads = 0;

    juce::ThreadPool loaderPool;

    // Historial de ediciones. Guarda las listas de clips (no el audio, que se
    // comparte), así que cada paso ocupa poco. Se vacía al cambiar de proyecto.
    juce::UndoManager undoManager { 1000, 50 };

    JUCE_DECLARE_WEAK_REFERENCEABLE (ProjectManager)
    JUCE_DECLARE_NON_COPYABLE (ProjectManager)
};
}
