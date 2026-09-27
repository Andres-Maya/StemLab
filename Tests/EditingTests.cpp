#include "Tests.h"
#include "TestSupport.h"

#include "Audio/AudioEngine.h"
#include "Project/ProjectManager.h"
#include "UI/WaveformView.h"
#include "Utils/Strings.h"

// Edición de fragmentos: deshacer/rehacer, grabar encima de audio ya grabado
// y colocar el cabezal con un clic sobre un clip.

namespace stemlab::test
{
namespace
{
    std::vector<juce::uint32> idsOf (const std::vector<AudioClip>& clips)
    {
        std::vector<juce::uint32> ids;

        for (const auto& clip : clips)
            ids.push_back (clip.id);

        return ids;
    }

    void testUndoRedo()
    {
        section ("Deshacer / rehacer la edición de fragmentos");

        AudioEngine engine;     // sin dispositivo: 44,1 kHz
        ProjectManager projects (engine);
        const auto track = projects.addEmptyTrack ("Pista");
        CHECK (projects.canUndo() && projects.getUndoDescription() == "Añadir pista"_u8 && ! projects.canRedo(),
               "añadir una pista es el primer paso del historial");

        // Añadir un fragmento de 1 s (como Pegar).
        const auto clip = makeClip (makeSource (44100, 0.25f, false, 44100.0), 0);
        projects.editClips (track, { clip }, "Pegar fragmento");
        CHECK (track->getClips().size() == 1 && projects.canUndo() && projects.getUndoDescription() == "Pegar fragmento",
               "editar deja un paso en el historial con su nombre");
        CHECK (engine.getMixer().getContentLength() == 44100, "la duración del proyecto se actualiza al editar");

        // Dividir.
        auto clips = track->getClips();
        const auto right = ClipEditing::split (clips, clip.id, 22050);
        projects.editClips (track, clips, "Dividir fragmento");
        const auto afterSplit = idsOf (track->getClips());
        CHECK (afterSplit.size() == 2, "dividir: 2 fragmentos");

        CHECK (projects.undo() && track->getClips().size() == 1 && track->getClips().front().length == 44100
                   && track->getClips().front().id == clip.id,
               "Ctrl+Z deshace la división (vuelve el fragmento original, con su id)");
        CHECK (projects.canRedo() && projects.getRedoDescription() == "Dividir fragmento", "se puede rehacer");
        CHECK (projects.redo() && idsOf (track->getClips()) == afterSplit, "Ctrl+Y rehace la división (mismos fragmentos)");

        // Mover con el ratón: la pista ya tiene la posición final; se anota la lista de antes.
        const auto beforeDrag = track->getClips();
        auto dragged = beforeDrag;
        ClipEditing::move (*ClipEditing::find (dragged, right), 88200);
        track->setClips (dragged);
        projects.clipsEdited (track, beforeDrag, "Mover fragmento");
        CHECK (ClipEditing::find (dragged, right)->timelineStart == 88200 && projects.getUndoDescription() == "Mover fragmento",
               "arrastrar un fragmento queda en el historial");
        CHECK (engine.getMixer().getContentLength() == 88200 + 22050, "la duración sigue al fragmento movido");

        projects.undo();
        auto restored = track->getClips();
        CHECK (ClipEditing::find (restored, right) != nullptr && ClipEditing::find (restored, right)->timelineStart == 22050
                   && engine.getMixer().getContentLength() == 44100,
               "deshacer el arrastre devuelve el fragmento a su sitio");

        // Una edición nueva descarta lo que se podía rehacer.
        clips = track->getClips();
        ClipEditing::remove (clips, right);
        projects.editClips (track, clips, "Eliminar fragmento");
        CHECK (! projects.canRedo() && track->getClips().size() == 1, "una edición nueva borra el 'rehacer'");

        // Varios pasos hacia atrás.
        projects.undo();
        projects.undo();
        projects.undo();
        CHECK (track->getClips().empty(), "deshacer varias veces vuelve al principio (pista vacía)");
        CHECK (projects.getUndoDescription() == "Añadir pista"_u8, "solo queda deshacer la creación de la pista");

        section ("Deshacer: eliminar pista y cambios sin guardar");

        projects.redo();                              // vuelve el fragmento
        const auto other = projects.addEmptyTrack ("Pista");
        const auto folder = outputFolder().getChildFile ("ProyectoDeshacer");
        folder.deleteRecursively();
        projects.saveAs (folder);
        CHECK (! projects.hasUnsavedChanges(), "guardado: sin cambios");

        projects.removeTrack (*track);
        const auto& tracks = engine.getMixer().getTracks();
        CHECK (tracks.size() == 1 && tracks.front() == other && projects.hasUnsavedChanges(), "eliminar la pista");
        CHECK (projects.getUndoDescription() == "Eliminar pista", "eliminar una pista se puede deshacer");

        projects.undo();
        CHECK (tracks.size() == 2 && tracks.front() == track && track->getClips().size() == 1,
               "deshacer devuelve la pista a su posición, con sus fragmentos");
        CHECK (! projects.hasUnsavedChanges(), "tras deshacer, el proyecto vuelve a estar como se guardó");

        projects.undo();                              // la creación de "other"
        projects.undo();                              // el fragmento de antes también se deshace
        CHECK (track->getClips().empty() && tracks.size() == 1, "el historial anterior a la eliminación sigue funcionando");
        projects.redo();
        projects.redo();
        projects.redo();
        CHECK (tracks.size() == 1 && tracks.front() == other, "rehacer vuelve a eliminar la pista");

        projects.newProject();
        CHECK (! projects.canUndo() && ! projects.canRedo(), "Nuevo proyecto vacía el historial");
        folder.deleteRecursively();
    }

    void testRecordOverExistingAudio()
    {
        section ("Grabar encima de audio ya grabado en la misma pista");

        AudioEngine engine;     // 44,1 kHz sin dispositivo
        ProjectManager projects (engine);
        const auto rate = engine.getSampleRate();

        // Primera toma: 3 s de valor 0,25 desde el principio.
        const auto target = projects.addEmptyTrack ("Grabación"_u8);
        const auto first = writeConstantWav (projects.getProject().getRecordingsDirectory().getChildFile ("Toma1.wav"),
                                             rate, 3.0, 0.25f);
        bool done = false;
        projects.addRecording ({ first, 0, 0, rate }, target, [&] (juce::Result) { done = true; });
        runLoopUntil ([&] { return done; }, 10000);
        CHECK (done && target->getClips().size() == 1, "primera toma en la pista");

        // Segunda toma sobre el mismo tramo: el cabezal se colocó en 1 s (encima del audio dibujado).
        const auto second = writeConstantWav (projects.getProject().getRecordingsDirectory().getChildFile ("Toma2.wav"),
                                              rate, 1.0, 0.5f);
        done = false;
        projects.addRecording ({ second, (juce::int64) rate, 0, rate }, target, [&] (juce::Result) { done = true; });
        runLoopUntil ([&] { return done; }, 10000);

        const auto clips = target->getClips();
        CHECK (done && engine.getMixer().getTracks().size() == 1 && clips.size() == 2,
               "la segunda toma va a la misma pista (no crea otra)");

        if (clips.size() == 2)
        {
            CHECK (clips[1].timelineStart == (juce::int64) rate && clips[1].length == (juce::int64) rate,
                   "la toma nueva empieza donde estaba el cabezal, sobre el audio existente");
            CHECK (clips.back().source->file == second, "la toma nueva queda la última: se dibuja y suena encima");
        }

        AudioMixer& mixer = engine.getMixer();
        mixer.prepare (rate, 512);
        CHECK (near (renderSpan (mixer, (juce::int64) (1.5 * rate), 1).getSample (0, 0), 0.5, 1e-3),
               "en el tramo solapado suena la toma nueva (no la suma)");
        CHECK (near (renderSpan (mixer, (juce::int64) (2.5 * rate), 1).getSample (0, 0), 0.25, 1e-3),
               "después de la toma nueva vuelve la primera");
        CHECK (projects.getUndoDescription() == "Grabar fragmento", "grabar un fragmento se puede deshacer");

        projects.undo();
        CHECK (target->getClips().size() == 1
                   && near (renderSpan (mixer, (juce::int64) (1.5 * rate), 1).getSample (0, 0), 0.25, 1e-3),
               "deshacer quita la toma y vuelve a sonar la de debajo");
    }

    void testClickOnClipMovesPlayhead()
    {
        section ("Clic sobre un fragmento: selecciona y coloca el cabezal");

        juce::AudioFormatManager formats;
        juce::AudioThumbnailCache cache (4);
        AudioTrack track ("Pista");
        const auto clip = makeClip (makeSource (48000 * 10, 0.3f), 0);   // 10 s a 48 kHz
        track.setClips ({ clip });

        WaveformView waveform (track, formats, cache);
        waveform.setBounds (0, 0, 1000, 80);
        waveform.setVisibleRange (0.0, 10.0);                  // 100 píxeles por segundo

        juce::uint32 clicked = 0;
        double seekedTo = -1.0;
        int edits = 0;
        juce::String editName;
        std::vector<AudioClip> clipsBefore;

        waveform.onClipClicked = [&] (juce::uint32 id) { clicked = id; };
        waveform.onSeek = [&] (double seconds) { seekedTo = seconds; };
        waveform.onClipsEdited = [&] (std::vector<AudioClip> before, const juce::String& name)
        {
            ++edits;
            editName = name;
            clipsBefore = std::move (before);
        };

        // Clic (sin arrastrar) en mitad del audio dibujado, a los 4 s.
        const juce::Point<float> at (400.0f, 40.0f);
        waveform.mouseDown (mouseEventAt (waveform, at, at, false));
        waveform.mouseUp (mouseEventAt (waveform, at, at, false));
        CHECK (clicked == clip.id, "el clic selecciona el fragmento");
        CHECK (near (seekedTo, 4.0, 0.011), "y lleva el cabezal a ese punto (" << seekedTo << " s): se puede grabar encima");
        CHECK (edits == 0 && track.getClips().front().timelineStart == 0, "un clic no mueve el fragmento");

        // Arrastrar el fragmento 1 s a la derecha: lo mueve, no toca el cabezal y anota la edición.
        seekedTo = -1.0;
        const juce::Point<float> to (500.0f, 40.0f);
        waveform.mouseDown (mouseEventAt (waveform, at, at, false));
        waveform.mouseDrag (mouseEventAt (waveform, to, at, true));
        waveform.mouseUp (mouseEventAt (waveform, to, at, true));
        CHECK (near ((double) track.getClips().front().timelineStart, 48000.0, 480.0),
               "arrastrar mueve el fragmento (" << (int) track.getClips().front().timelineStart << ")");
        CHECK (seekedTo < 0.0, "arrastrar no mueve el cabezal");
        CHECK (edits == 1 && editName == "Mover fragmento" && clipsBefore.size() == 1 && clipsBefore.front().timelineStart == 0,
               "al soltar avisa con la lista de antes (para deshacer)");

        // Clic en una zona vacía (antes del fragmento movido): solo mueve el cabezal.
        clicked = 12345;
        const juce::Point<float> empty (50.0f, 40.0f);
        waveform.mouseDown (mouseEventAt (waveform, empty, empty, false));
        waveform.mouseUp (mouseEventAt (waveform, empty, empty, false));
        CHECK (clicked == 0 && near (seekedTo, 0.5, 0.011), "clic en zona vacía: deselecciona y coloca el cabezal");
    }
}

void runEditingTests()
{
    testUndoRedo();
    testRecordOverExistingAudio();
    testClickOnClipMovesPlayhead();
}
}
