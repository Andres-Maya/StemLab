#include "Tests.h"
#include "TestSupport.h"

#include "Audio/AudioEngine.h"
#include "Project/ProjectManager.h"
#include "UI/StemLabLookAndFeel.h"
#include "UI/WaveformView.h"
#include "Utils/Strings.h"

#include <limits>

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

    void testFreeSpaceHelpers()
    {
        section ("Fragmentos sin solaparse: buscar hueco libre");

        const auto source = makeSource (1000, 0.1f);
        const std::vector<AudioClip> clips { makeClip (source, 0, 0, 100), makeClip (source, 100, 0, 100), makeClip (source, 300, 0, 100) };

        CHECK (ClipEditing::findFreeSpace (clips, 50, 50) == 200, "sobre audio: justo después de los fragmentos seguidos (200)");
        CHECK (ClipEditing::findFreeSpace (clips, 50, 150) == 400, "si no cabe en el hueco, al siguiente donde quepa (400)");
        CHECK (ClipEditing::findFreeSpace (clips, 250, 50) == 250, "en un hueco donde cabe: se queda en el cabezal");
        CHECK (ClipEditing::findFreeSpace (clips, 200, 100) == 200, "cabe justo hasta el siguiente fragmento (200..300)");

        const auto gap = ClipEditing::freeGapAt (clips, 150);
        CHECK (gap.first == 200 && gap.second == 300, "hueco libre desde el cabezal: [200, 300)");

        const auto around = ClipEditing::freeRangeAround (clips, clips[2]);
        CHECK (around.first == 200 && around.second == std::numeric_limits<juce::int64>::max(), "límites del último fragmento: desde 200");

        auto take = makeClip (source, 180, 0, 200);          // 180..380: pisa el final de [100,200) y el principio de [300,400)
        CHECK (ClipEditing::fitIntoFreeSpace (clips, take) && take.timelineStart == 200 && take.getEnd() == 300 && take.sourceOffset == 20,
               "una toma se recorta al hueco: empieza en 200 (desplazando su audio) y acaba en 300");

        auto inside = makeClip (source, 20, 0, 50);           // dentro de audio ya grabado
        CHECK (! ClipEditing::fitIntoFreeSpace (clips, inside), "una toma que cae entera sobre audio no se añade");

        section ("Mover un fragmento delante de otro (y abrir espacio)");

        // A [0,200)  B [200,400)  C [400,600)  D [1000,1200)
        const std::vector<AudioClip> row { makeClip (source, 0, 0, 200), makeClip (source, 200, 0, 200),
                                           makeClip (source, 400, 0, 200), makeClip (source, 1000, 0, 200) };
        const auto a = row[0].id, b = row[1].id, c = row[2].id, d = row[3].id;
        const auto startOf = [] (std::vector<AudioClip> list, juce::uint32 id) { return ClipEditing::find (list, id)->timelineStart; };

        auto moved = ClipEditing::moveWithoutOverlap (row, a, 150);     // centro 250 < centro de B (300)
        CHECK (startOf (moved, a) == 0 && startOf (moved, b) == 200, "sin pasar del centro del vecino: se queda contra él");

        moved = ClipEditing::moveWithoutOverlap (row, a, 250);          // centro 350 > 300: salta delante de B
        CHECK (startOf (moved, b) == 200 && startOf (moved, a) == 400 && startOf (moved, c) == 600 && startOf (moved, d) == 1200,
               "pasado el centro de B, A va delante de B y C y D se apartan 200 a la vez");

        moved = ClipEditing::moveWithoutOverlap (row, c, 50);           // C hacia atrás, centro 150 > centro de A (100)
        CHECK (startOf (moved, a) == 0 && startOf (moved, c) == 200 && startOf (moved, b) == 400 && startOf (moved, d) == 1200,
               "hacia atrás: C se mete entre A y B, y B y D se apartan");

        moved = ClipEditing::moveWithoutOverlap (row, c, 0);            // centro 100: no pasa del centro de A
        CHECK (startOf (moved, c) == 0 && startOf (moved, a) == 200 && startOf (moved, b) == 400,
               "arrastrado del todo al principio, C va primero y los demás se apartan");

        // Con sitio de sobra: A [0,200)  B [200,400)  E [1000,1200)
        const std::vector<AudioClip> roomy { row[0], row[1], makeClip (source, 1000, 0, 200) };
        const auto e = roomy[2].id;
        moved = ClipEditing::moveWithoutOverlap (roomy, a, 500);
        CHECK (startOf (moved, a) == 500 && startOf (moved, b) == 200 && startOf (moved, e) == 1000,
               "si cabe en el hueco de delante, salta ahí sin mover a nadie");
        moved = ClipEditing::moveWithoutOverlap (roomy, a, 900);        // centro 1000 < centro de E (1100)
        CHECK (startOf (moved, a) == 800 && startOf (moved, e) == 1000, "se detiene contra el siguiente (E)");
        moved = ClipEditing::moveWithoutOverlap (roomy, a, 1050);       // centro 1150 > 1100
        CHECK (startOf (moved, a) == 1200 && startOf (moved, e) == 1000, "pasado el centro de E, va detrás de él");
    }

    void testRecordAfterExistingAudio()
    {
        section ("Grabar en una pista con audio: la toma va a continuación, nunca encima");

        AudioEngine engine;     // 44,1 kHz sin dispositivo
        ProjectManager projects (engine);
        const auto rate = engine.getSampleRate();
        const auto seconds = [rate] (double s) { return (juce::int64) (s * rate); };
        const auto& tracks = engine.getMixer().getTracks();

        // Primera toma: 3 s de valor 0,25 desde el principio.
        const auto target = projects.addEmptyTrack ("Grabación"_u8);
        const auto recordings = projects.getProject().getRecordingsDirectory();
        const auto record = [&] (const juce::String& name, juce::int64 start, double length, float value, int latency)
        {
            auto done = false;
            const auto file = writeConstantWav (recordings.getChildFile (name), rate, length, value);
            projects.addRecording ({ file, start, latency, rate }, target, [&] (juce::Result) { done = true; });
            runLoopUntil ([&] { return done; }, 10000);
            return done;
        };

        CHECK (record ("Toma1.wav", 0, 3.0, 0.25f, 0) && target->getClips().size() == 1, "primera toma en la pista");

        // El cabezal está en 1 s, sobre la toma: la grabación empieza en su final (como hace MainComponent).
        const auto start = ClipEditing::findFreeSpace (target->getClips(), seconds (1.0), 1);
        CHECK (start == seconds (3.0), "con el cabezal sobre audio, la grabación empieza al final de ese audio (3 s)");

        // La latencia adelanta la toma 441 muestras: se recorta para no pisar la anterior.
        CHECK (record ("Toma2.wav", start, 1.0, 0.5f, 441), "segunda toma grabada");
        auto clips = target->getClips();
        CHECK (tracks.size() == 1 && clips.size() == 2 && clips[1].timelineStart == seconds (3.0) && clips[1].sourceOffset == 441,
               "va a la misma pista, justo después de la primera (sin solaparse)");

        AudioMixer& mixer = engine.getMixer();
        mixer.prepare (rate, 512);
        CHECK (near (renderSpan (mixer, seconds (1.5), 1).getSample (0, 0), 0.25, 1e-3), "la primera toma sigue sonando entera");
        CHECK (near (renderSpan (mixer, seconds (3.5), 1).getSample (0, 0), 0.5, 1e-3), "y la segunda suena a continuación");
        CHECK (projects.getUndoDescription() == "Grabar fragmento", "grabar un fragmento se puede deshacer");

        // Un fragmento más adelante (en 6 s): una toma que llegue a él se corta ahí.
        auto withLater = target->getClips();
        withLater.push_back (makeClip (makeSource ((int) rate, 0.1f, false, rate), seconds (6.0)));
        projects.editClips (target, withLater, "Pegar fragmento");
        CHECK (record ("Toma3.wav", seconds (5.5), 1.0, 0.75f, 0), "tercera toma, que llegaría al fragmento de 6 s");
        clips = target->getClips();
        CHECK (clips.size() == 4 && clips.back().timelineStart == seconds (5.5) && clips.back().getEnd() == seconds (6.0),
               "se recorta al llegar al fragmento siguiente");

        // Una toma que caería entera sobre audio no se añade.
        CHECK (record ("Toma4.wav", seconds (0.5), 1.0, 0.9f, 0) && target->getClips().size() == 4,
               "una toma entera sobre audio grabado no se añade");

        // Ningún par de fragmentos se solapa.
        clips = target->getClips();
        auto overlaps = false;
        for (size_t i = 0; i < clips.size(); ++i)
            for (size_t j = i + 1; j < clips.size(); ++j)
                overlaps = overlaps || (clips[i].timelineStart < clips[j].getEnd() && clips[j].timelineStart < clips[i].getEnd());
        CHECK (! overlaps, "en la pista no queda ningún fragmento encima de otro");

        projects.undo();
        CHECK (target->getClips().size() == 3, "deshacer quita la última toma");
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
        section ("Arrastrar fragmentos: contra el vecino, por delante de él y abriendo espacio");

        AudioTrack pair ("Dos");
        const auto a = makeClip (makeSource (48000 * 5, 0.3f), 0, 0, 48000 * 2);   // [0, 2) s de un audio de 5 s
        const auto b = makeClip (makeSource (48000 * 2, 0.3f), 48000 * 4);    // [4, 6) s
        pair.setClips ({ a, b });
        WaveformView lane (pair, formats, cache);
        lane.setBounds (0, 0, 1000, 80);
        lane.setVisibleRange (0.0, 10.0);

        const auto startOf = [&pair] (juce::uint32 id) { auto list = pair.getClips(); return ClipEditing::find (list, id)->timelineStart; };
        const auto drag = [&lane] (juce::Point<float> from, std::initializer_list<juce::Point<float>> path)
        {
            lane.mouseDown (mouseEventAt (lane, from, from, false));
            for (const auto& to : path)
                lane.mouseDrag (mouseEventAt (lane, to, from, true));
            lane.mouseUp (mouseEventAt (lane, *(path.end() - 1), from, true));
        };

        // Arrastrar B 2,5 s a la izquierda (su centro no pasa del de A): se detiene al tocar A.
        const juce::Point<float> bCentre (500.0f, 40.0f);
        drag (bCentre, { { 250.0f, 40.0f } });
        CHECK (startOf (b.id) == 48000 * 2 && startOf (a.id) == 0, "mover: se detiene al final del fragmento anterior");

        // Seguir arrastrando B (ahora en [2, 4) s) más a la izquierda: pasa por delante de A,
        // que se aparta. Ida y vuelta en el mismo gesto: A vuelve a su sitio.
        const juce::Point<float> bNow (300.0f, 40.0f);
        drag (bNow, { { 80.0f, 40.0f }, { 290.0f, 40.0f } });
        CHECK (startOf (b.id) == 48000 * 2 && startOf (a.id) == 0, "si se vuelve atrás durante el arrastre, A regresa a su sitio");

        drag (bNow, { { 80.0f, 40.0f } });
        CHECK (startOf (b.id) == 0 && startOf (a.id) == 48000 * 2, "arrastrado lo suficiente, B pasa delante de A y A se aparta");

        // Recortar: A [2, 3) s de un audio de 5 s y B en [0, 2) s; C detrás en [4, 6) s.
        auto layout = pair.getClips();
        ClipEditing::trimEnd (*ClipEditing::find (layout, a.id), 48000 * 3);
        auto c = makeClip (makeSource (48000 * 2, 0.3f), 48000 * 4);
        layout.push_back (c);
        pair.setClips (layout);
        lane.clipsChanged();

        // Alargar A por la derecha: se detiene al llegar a C (4 s), aunque su audio da para más.
        const juce::Point<float> aEnd (298.0f, 40.0f), farRight (700.0f, 40.0f);
        lane.mouseDown (mouseEventAt (lane, aEnd, aEnd, false));
        lane.mouseDrag (mouseEventAt (lane, farRight, aEnd, true));
        lane.mouseUp (mouseEventAt (lane, farRight, aEnd, true));
        auto trimmed = pair.getClips();
        CHECK (ClipEditing::find (trimmed, a.id)->getEnd() == 48000 * 4, "recortar: el borde se detiene al principio del siguiente");
    }

    void testDragAnimation()
    {
        section ("Animación al arrastrar: se levanta, pasa por encima y baja a su sitio");

        StemLabLookAndFeel lookAndFeel;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        {
            juce::AudioFormatManager formats;
            juce::AudioThumbnailCache cache (4);
            AudioTrack track ("Animación"_u8);
            auto tone = std::make_shared<ClipSource>();
            tone->sampleRate = 48000.0;
            tone->audio = makeSine (2, 48000 * 2, 3.0, 48000.0, 0.6f);
            const auto a = makeClip (tone, 0);                    // [0, 2) s
            const auto b = makeClip (tone, 48000 * 4);            // [4, 6) s
            track.setClips ({ a, b });

            WaveformView lane (track, formats, cache);
            lane.setWaveColour (juce::Colour (0xff4fc3f7));
            lane.setBounds (0, 0, 1000, 83);
            lane.setVisibleRange (0.0, 10.0);
            const auto modelStart = [&track] (juce::uint32 id) { auto list = track.getClips(); return ClipEditing::find (list, id)->timelineStart; };

            // Agarrar B y llevarlo encima de A (sin soltar).
            const juce::Point<float> grab (500.0f, 40.0f), overA (150.0f, 40.0f);
            lane.mouseDown (mouseEventAt (lane, grab, grab, false));
            lane.mouseDrag (mouseEventAt (lane, overA, grab, true));
            CHECK (near (lane.getDrawnStart (b.id), 0.5 * 48000, 1.0), "B se dibuja bajo el ratón, encima de A (0,5 s)");
            CHECK (modelStart (b.id) == 48000 * 2, "aunque la pista ya lo tiene en su sitio real (2 s, detrás de A)");

            runLoopUntil ([&] { return lane.getLiftAmount() > 0.99f; }, 1000);
            CHECK (lane.getLiftAmount() > 0.99f, "se levanta mientras se arrastra (" << lane.getLiftAmount() << ")");
            saveSnapshot (lane.createComponentSnapshot (lane.getLocalBounds()), "arrastre-encima.png");

            // Pasar del centro de A: A se aparta deslizándose (no salta).
            const juce::Point<float> pastA (40.0f, 40.0f);
            lane.mouseDrag (mouseEventAt (lane, pastA, grab, true));
            CHECK (modelStart (a.id) == 48000 * 2 && lane.getDrawnStart (a.id) < 48000.0,
                   "A ya está en 2 s en la pista, pero se dibuja deslizándose desde 0");
            runLoopUntil ([&] { return juce::exactlyEqual (lane.getDrawnStart (a.id), 96000.0); }, 1000);
            CHECK (juce::exactlyEqual (lane.getDrawnStart (a.id), 96000.0), "A termina de deslizarse hasta su sitio");
            saveSnapshot (lane.createComponentSnapshot (lane.getLocalBounds()), "arrastre-apartando.png");

            // Soltar: B baja desde el ratón hasta su sitio y deja de estar levantado.
            lane.mouseUp (mouseEventAt (lane, pastA, grab, true));
            CHECK (lane.getLiftAmount() > 0.5f, "justo al soltar sigue levantado: baja con animación");
            runLoopUntil ([&] { return juce::exactlyEqual (lane.getLiftAmount(), 0.0f); }, 1000);
            CHECK (juce::exactlyEqual (lane.getLiftAmount(), 0.0f) && juce::exactlyEqual (lane.getDrawnStart (b.id), 0.0),
                   "al terminar queda apoyado y dibujado en su posición real (0 s)");
            saveSnapshot (lane.createComponentSnapshot (lane.getLocalBounds()), "arrastre-soltado.png");

            // Un clic sin arrastrar no levanta nada.
            const juce::Point<float> click (100.0f, 40.0f);
            lane.mouseDown (mouseEventAt (lane, click, click, false));
            lane.mouseUp (mouseEventAt (lane, click, click, false));
            CHECK (juce::exactlyEqual (lane.getLiftAmount(), 0.0f), "un clic sin arrastrar no levanta el fragmento");
        }

        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }
}

void runEditingTests()
{
    testUndoRedo();
    testFreeSpaceHelpers();
    testRecordAfterExistingAudio();
    testClickOnClipMovesPlayhead();
    testDragAnimation();
}
}
