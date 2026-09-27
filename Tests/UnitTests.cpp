#include "Tests.h"
#include "TestSupport.h"

#include "Audio/AudioEngine.h"
#include "Audio/AudioFileLoader.h"
#include "DSP/CompressorEffect.h"
#include "DSP/EffectChain.h"
#include "DSP/EqualizerEffect.h"
#include "DSP/GainEffect.h"
#include "DSP/LimiterEffect.h"
#include "DSP/SaturationEffect.h"
#include "AI/AudioSeparator.h"
#include "Project/ProjectManager.h"
#include "Utils/Strings.h"

// Pruebas rápidas: no necesitan tarjeta de sonido ni Python.

namespace stemlab::test
{
namespace
{
    void testUtils()
    {
        section ("Utils: Parameter, textos");

        auto p = Parameter::continuous ("x", "X", juce::NormalisableRange<float> (-10.0f, 10.0f, 0.5f), 0.0f, "dB");
        p->set (3.3f);
        CHECK (near (p->get(), 3.5, 1e-6), "set() ajusta al intervalo (3.3 -> 3.5)");
        p->set (99.0f);
        CHECK (near (p->get(), 10.0, 1e-6), "set() limita al rango (99 -> 10)");

        struct Counter : Parameter::Listener { int n = 0; void parameterChanged (Parameter&) override { ++n; } } counter;
        p->addListener (&counter);
        p->set (1.0f);
        p->set (1.0f);
        p->removeListener (&counter);
        CHECK (counter.n == 1, "el listener se avisa solo si el valor cambia");
        CHECK (p->toText (3.5f) == "3.50 dB", "formato continuo: " << p->toText (3.5f));

        auto freq = Parameter::continuous ("f", "F", Parameter::frequencyRange (20.0f, 20000.0f), 1500.0f, "Hz");
        CHECK (freq->toText() == "1.50 kHz", "formato de frecuencia: " << freq->toText());

        auto choice = Parameter::choice ("m", "M", juce::StringArray ("A", "B", "C"), 1);
        CHECK (choice->toText() == "B", "choice muestra el nombre");
        choice->fromVar (juce::var (2));
        CHECK (choice->getIndex() == 2, "choice fromVar");

        auto toggle = Parameter::toggle ("t", "T", true);
        CHECK (toggle->toVar().isBool() && (bool) toggle->toVar(), "toggle se guarda como bool");

        CHECK ("Batería"_u8.length() == 7, "literal _u8 decodifica UTF-8");
        CHECK (formatTime (65.5) == "01:05.500", "formatTime: " << formatTime (65.5));
        CHECK (stemDisplayName ("drums") == "Batería"_u8 && stemDisplayName ("xyz") == "xyz", "nombres de stems");
        CHECK (stemSortOrder ("vocals") < stemSortOrder ("other"), "orden de stems");
    }

    void testEffects()
    {
        const double sr = 48000.0;

        section ("DSP: Gain");
        {
            GainEffect gain;
            gain.findParameter ("gain")->set (6.0f);
            juce::AudioBuffer<float> buf (2, 48000);
            for (int ch = 0; ch < 2; ++ch) juce::FloatVectorOperations::fill (buf.getWritePointer (ch), 0.5f, 48000);
            processEffect (gain, buf, sr);
            CHECK (near (buf.getSample (0, 47999), 0.5 * juce::Decibels::decibelsToGain (6.0), 1e-3),
                   "+6 dB sobre 0.5 -> " << buf.getSample (0, 47999));
        }

        section ("DSP: Limiter");
        {
            LimiterEffect limiter;
            limiter.getEnabledParameter().set (1.0f);
            limiter.findParameter ("input")->set (12.0f);
            limiter.findParameter ("ceiling")->set (-1.0f);
            auto buf = makeSine (2, 48000, 1000.0, sr, 0.9f);
            processEffect (limiter, buf, sr);
            const auto ceiling = juce::Decibels::decibelsToGain (-1.0f);
            const auto p = peakFrom (buf, 0);
            CHECK (p <= ceiling + 1e-5f, "nunca supera el techo de -1 dB (pico " << p << " <= " << ceiling << ")");
            CHECK (p > ceiling * 0.95f, "llega al techo (no atenúa de más)");
        }

        section ("DSP: EQ");
        {
            EqualizerEffect eq;
            eq.getEnabledParameter().set (1.0f);
            auto buf = makeSine (2, 48000, 1000.0, sr, 0.5f);
            const auto original = buf;
            processEffect (eq, buf, sr);
            float maxDiff = 0;
            for (int i = 0; i < 48000; ++i) maxDiff = juce::jmax (maxDiff, std::abs (buf.getSample (0, i) - original.getSample (0, i)));
            CHECK (maxDiff < 1e-4f, "EQ plano no altera la señal (dif. máx " << maxDiff << ")");

            auto measure = [&] (double f)
            {
                EqualizerEffect boosted;
                boosted.getEnabledParameter().set (1.0f);
                boosted.findParameter ("lowGain")->set (12.0f);
                boosted.findParameter ("lowFreq")->set (120.0f);
                auto s = makeSine (2, 48000, f, sr, 0.25f);
                const auto in = rmsFrom (s, 24000);
                processEffect (boosted, s, sr);
                return juce::Decibels::gainToDecibels (rmsFrom (s, 24000) / in);
            };
            const auto low = measure (40.0), high = measure (5000.0);
            CHECK (low > 10.0f, "low shelf +12 dB: a 40 Hz sube " << low << " dB");
            CHECK (std::abs (high) < 0.5f, "a 5 kHz no cambia (" << high << " dB)");
        }

        section ("DSP: Saturación");
        {
            SaturationEffect sat;
            sat.getEnabledParameter().set (1.0f);
            sat.findParameter ("drive")->set (24.0f);
            auto buf = makeSine (2, 48000, 220.0, sr, 1.0f);
            processEffect (sat, buf, sr);
            CHECK (peakFrom (buf, 4800) <= 1.0f, "modo Suave acotado a 1 (pico " << peakFrom (buf, 4800) << ")");
            CHECK (rmsFrom (buf, 4800) > 0.8f, "con drive alto la onda se aproxima a cuadrada (RMS " << rmsFrom (buf, 4800) << ")");

            SaturationEffect tube;
            tube.getEnabledParameter().set (1.0f);
            tube.findParameter ("mode")->set (2.0f);
            tube.findParameter ("drive")->set (12.0f);
            auto t = makeSine (2, 96000, 220.0, sr, 0.8f);
            processEffect (tube, t, sr);
            double mean = 0;
            for (int i = 48000; i < 96000; ++i) mean += t.getSample (0, i);
            mean /= 48000.0;
            CHECK (std::abs (mean) < 0.01, "modo Válvula sin DC tras el filtro (media " << mean << ")");
        }

        section ("DSP: Compresor");
        {
            CompressorEffect comp;
            comp.getEnabledParameter().set (1.0f);
            comp.findParameter ("threshold")->set (-30.0f);
            comp.findParameter ("ratio")->set (8.0f);
            auto buf = makeSine (2, 48000, 440.0, sr, 0.9f);
            const auto in = rmsFrom (buf, 9600);
            processEffect (comp, buf, sr);
            const auto reduction = juce::Decibels::gainToDecibels (rmsFrom (buf, 9600) / in);
            CHECK (reduction < -12.0f, "comprime una señal fuerte (" << reduction << " dB)");
        }

        section ("DSP: EffectChain");
        {
            EffectChain chain;
            chain.prepare ({ sr, 512u, 2u });
            auto buf = makeSine (2, 512, 440.0, sr, 0.5f);
            const auto original = buf;
            chain.process (juce::dsp::AudioBlock<float> (buf));
            CHECK (std::abs (buf.getSample (0, 300) - original.getSample (0, 300)) < 1e-5f, "cadena por defecto = identidad");

            chain.findEffect ("saturation")->getEnabledParameter().set (1.0f);
            chain.findEffect ("saturation")->findParameter ("drive")->set (10.0f);
            EffectChain copy;
            copy.fromVar (chain.toVar());
            CHECK (copy.findEffect ("saturation")->isEnabled() && near (copy.findEffect ("saturation")->findParameter ("drive")->get(), 10.0, 1e-4),
                   "serialización de la cadena ida y vuelta");
            CHECK (chain.getEffects().size() == 5, "5 efectos en orden Gain, Sat, EQ, Comp, Limiter");
        }
    }

    void testMixerAndTransport()
    {
        section ("Audio: AudioMixer + AudioTrack");

        AudioMixer mixer;
        mixer.prepare (48000.0, 512);

        auto makeTrack = [] (const char* name, float value)
        {
            auto track = std::make_shared<AudioTrack> (name);
            track->setClips ({ makeClip (makeSource (200000, value), 0) });
            return track;
        };

        auto t1 = makeTrack ("A", 0.25f), t2 = makeTrack ("B", 0.25f);
        mixer.addTrack (t1);
        mixer.addTrack (t2);

        juce::AudioBuffer<float> bus (2, 512);
        auto renderAt = [&] (juce::int64 pos, bool playing = true)
        {
            for (int i = 0; i < 8; ++i)
                mixer.render (bus, 512, pos + i * 512, playing);
            return std::pair<float, float> (bus.getSample (0, 511), bus.getSample (1, 511));
        };

        auto [l, r] = renderAt (0);
        CHECK (near (l, 0.5, 1e-4) && near (r, 0.5, 1e-4), "dos pistas se suman a ganancia unidad (" << l << ", " << r << ")");
        CHECK (mixer.getContentLength() == 200000, "duración del contenido");

        t2->getMute().set (1.0f);
        CHECK (near (renderAt (0).first, 0.25, 1e-4), "Mute silencia la pista B");
        t2->getMute().set (0.0f);

        t2->getSolo().set (1.0f);
        CHECK (near (renderAt (0).first, 0.25, 1e-4), "Solo en B deja solo B");
        t2->getSolo().set (0.0f);

        t2->getMute().set (1.0f);
        t1->getPan().set (1.0f);
        auto [pl, pr] = renderAt (0);
        CHECK (near (pl, 0.0, 1e-4) && near (pr, 0.25, 1e-4), "paneo a la derecha (" << pl << ", " << pr << ")");
        t1->getPan().set (0.0f);

        t1->getVolume().set (-60.0f);
        CHECK (near (renderAt (0).first, 0.0, 1e-6), "volumen -60 dB = silencio");
        t1->getVolume().set (0.0f);

        auto moved = t1->getClips();
        ClipEditing::move (moved.front(), 100000);
        t1->setClips (moved);
        mixer.updateContentLength();
        CHECK (near (renderAt (0).first, 0.0, 1e-6) && near (renderAt (100000).first, 0.25, 1e-4), "posición de inicio del clip");
        CHECK (mixer.getContentLength() == 300000, "duración con clip desplazado");
        ClipEditing::move (moved.front(), 0);
        t1->setClips (moved);

        mixer.getMasterVolume().set (-6.0f);
        CHECK (near (renderAt (0).first, 0.25 * juce::Decibels::decibelsToGain (-6.0), 1e-3), "volumen master");
        mixer.getMasterVolume().set (0.0f);

        CHECK (near (renderAt (0, false).first, 0.0, 1e-9), "parado = silencio");

        auto removed = mixer.removeTrack (t2.get());
        CHECK (removed == t2 && mixer.getTracks().size() == 1, "quitar pista");

        section ("Audio: Transport");
        Transport t;
        t.setPosition (1000);
        CHECK (t.getPosition() == 1000, "parado: el salto es inmediato");
        t.play();
        t.setPosition (5000);
        CHECK (t.getPosition() == 1000, "reproduciendo: el salto espera al hilo de audio");
        CHECK (t.beginBlock() == 5000, "beginBlock aplica el salto pendiente");
        t.advance (512);
        CHECK (t.getPosition() == 5512, "advance avanza el cabezal");
        t.stop();
        CHECK (! t.isPlaying() && t.getPosition() == 0, "stop vuelve al inicio");
    }

    void testClips()
    {
        section ("Edición de clips: dividir, recortar, mover, eliminar");

        const auto ramp = makeSource (48000, 0.0f, true);     /* muestra i = i / 48000 */
        std::vector<AudioClip> clips { makeClip (ramp, 0) };
        const auto originalId = clips.front().id;

        const auto rightId = ClipEditing::split (clips, originalId, 24000);
        CHECK (rightId != 0 && clips.size() == 2, "dividir crea dos fragmentos");
        CHECK (clips[0].length == 24000 && clips[1].timelineStart == 24000 && clips[1].sourceOffset == 24000
                   && clips[1].length == 24000 && clips[1].source == ramp,
               "las dos mitades comparten el audio y encajan sin huecos");
        CHECK (ClipEditing::split (clips, originalId, 10) == 0, "no divide pegado al borde");

        auto* right = ClipEditing::find (clips, rightId);
        ClipEditing::trimStart (*right, 30000);
        CHECK (right->timelineStart == 30000 && right->sourceOffset == 30000 && right->length == 18000, "recortar el principio (reducir)");
        ClipEditing::trimStart (*right, 20000);
        CHECK (right->timelineStart == 20000 && right->sourceOffset == 20000, "el recorte es reversible: se recupera audio");
        ClipEditing::trimStart (*right, -5000);
        CHECK (right->timelineStart == 0 && right->sourceOffset == 0, "no se recupera más allá del principio del archivo");
        ClipEditing::trimStart (*right, 24000);

        ClipEditing::trimEnd (*right, 40000);
        CHECK (right->getEnd() == 40000 && right->length == 16000, "recortar el final");
        ClipEditing::trimEnd (*right, 999999);
        CHECK (right->sourceOffset + right->length == 48000, "no se alarga más allá del final del archivo");
        ClipEditing::trimEnd (*right, 40000);

        ClipEditing::move (*right, -100);
        CHECK (right->timelineStart == 0, "no se mueve antes del 0");
        ClipEditing::move (*right, 60000);

        /* Render real: [0,24000) rampa, hueco en silencio, y la mitad derecha movida a 60000. */
        AudioMixer mixer;
        mixer.prepare (48000.0, 512);
        auto track = std::make_shared<AudioTrack> ("Edit");
        track->setClips (clips);
        mixer.addTrack (track);
        CHECK (mixer.getContentLength() == 60000 + 16000, "la duración sigue a los clips (" << mixer.getContentLength() << ")");

        const auto a = renderSpan (mixer, 10000, 1);
        const auto gap = renderSpan (mixer, 45000, 1);
        const auto b = renderSpan (mixer, 70000, 1);
        CHECK (near (a.getSample (0, 0), 10000.0 / 48000.0, 1e-4), "primer fragmento suena en su sitio");
        CHECK (near (gap.getSample (0, 0), 0.0, 1e-9), "el hueco entre fragmentos es silencio");
        CHECK (near (b.getSample (0, 0), (24000.0 + 10000.0) / 48000.0, 1e-4),
               "el fragmento movido reproduce su audio original (" << b.getSample (0, 0) << ")");

        CHECK (ClipEditing::remove (clips, rightId) && clips.size() == 1, "eliminar el fragmento cortado");
        track->setClips (clips);
        mixer.updateContentLength();
        CHECK (near (renderSpan (mixer, 70000, 1).getSample (0, 0), 0.0, 1e-9) && mixer.getContentLength() == 24000,
               "tras eliminarlo ya no suena y la duración se acorta");

        section ("Clips solapados y fundidos");
        auto layered = std::make_shared<AudioTrack> ("Capas");
        layered->setClips ({ makeClip (makeSource (48000, 0.25f), 0), makeClip (makeSource (10000, 0.5f), 20000) });
        AudioMixer mixer2;
        mixer2.prepare (48000.0, 512);
        mixer2.addTrack (layered);
        CHECK (near (renderSpan (mixer2, 25000, 1).getSample (0, 0), 0.5, 1e-4), "donde se solapan suena el fragmento de encima (no la suma)");
        CHECK (near (renderSpan (mixer2, 35000, 1).getSample (0, 0), 0.25, 1e-4), "fuera del solape vuelve el de abajo");

        auto edge = std::make_shared<AudioTrack> ("Borde");
        edge->setClips ({ makeClip (makeSource (10000, 0.8f), 0) });
        AudioMixer mixer3;
        mixer3.prepare (48000.0, 512);
        mixer3.addTrack (edge);
        const auto around = renderSpan (mixer3, 9800, 400);
        float maxStep = 0;
        for (int i = 1; i < 400; ++i) maxStep = juce::jmax (maxStep, std::abs (around.getSample (0, i) - around.getSample (0, i - 1)));
        CHECK (maxStep < 0.05f, "fundido en el borde: sin saltos bruscos (salto máx " << maxStep << " con señal 0.8)");
    }

    void testLoaderAndRecorder()
    {
        section ("Audio: AudioFileLoader");

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        const auto wav = writeToneWav (outputFolder().getChildFile ("tone44k.wav"), 44100.0, 1, 1.0, 441.0);

        juce::AudioBuffer<float> loaded;
        auto result = AudioFileLoader::load (formats, wav, 48000.0, loaded);
        CHECK (result.wasOk(), "carga y remuestrea 44.1 kHz -> 48 kHz");
        CHECK (loaded.getNumChannels() == 2 && std::abs (loaded.getNumSamples() - 48000) <= 1, "estéreo, 48000 muestras (" << loaded.getNumSamples() << ")");

        int crossings = 0;
        for (int i = 1; i < loaded.getNumSamples(); ++i)
            if ((loaded.getSample (0, i - 1) < 0) != (loaded.getSample (0, i) < 0)) ++crossings;
        CHECK (std::abs (crossings - 882) <= 4, "la frecuencia se conserva (" << crossings << " cruces por cero, esperado ~882)");
        CHECK (std::abs (loaded.getSample (0, 20000) - loaded.getSample (1, 20000)) < 1e-6f, "mono duplicado en ambos canales");

        result = AudioFileLoader::load (formats, outputFolder().getChildFile ("no-existe.wav"), 48000.0, loaded);
        CHECK (result.failed(), "error claro si el archivo no existe: " << result.getErrorMessage());

        section ("Audio: AudioRecorder");
        AudioRecorder recorder;
        const auto recFile = outputFolder().getChildFile ("rec-test.wav");
        CHECK (recorder.start (recFile, 48000.0, 2).wasOk(), "empieza a grabar");
        auto input = makeSine (2, 480, 440.0, 48000.0, 0.3f);
        for (int block = 0; block < 100; ++block)
        {
            recorder.write (input.getArrayOfReadPointers(), 2, 480, 1000 + block * 480);
            juce::Thread::sleep (10);   // ritmo real: 480 muestras a 48 kHz = 10 ms
        }
        CHECK (recorder.getStartPosition() == 1000, "guarda la posición del primer sample");

        {
            std::vector<float> peaks (1000);
            const auto n = recorder.readPreviewPeaks (peaks.data(), 1000);
            float maxPeak = 0, minPeak = 1;
            for (int i = 0; i < n; ++i) { maxPeak = juce::jmax (maxPeak, peaks[(size_t) i]); minPeak = juce::jmin (minPeak, peaks[(size_t) i]); }
            CHECK (n == 48000 / AudioRecorder::previewBinSize, "vista previa en directo: " << n << " picos por segundo de grabación");
            CHECK (near (maxPeak, 0.3, 0.01) && minPeak > 0.25f, "los picos reflejan la señal (máx " << maxPeak << ", mín " << minPeak << ")");
            CHECK (recorder.readPreviewPeaks (peaks.data(), 1000) == 0, "cada pico se entrega una sola vez");
        }
        recorder.stop();

        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (recFile));
        CHECK (reader != nullptr && reader->lengthInSamples == 48000 && reader->numChannels == 2 && reader->bitsPerSample == 24,
               "WAV 24 bits estéreo de 48000 muestras");
    }

    void testUnsavedChanges()
    {
        section ("Proyecto: detección de cambios sin guardar");

        AudioEngine engine;
        ProjectManager projects (engine);
        CHECK (! projects.hasUnsavedChanges(), "proyecto nuevo: sin cambios");

        const auto track = projects.addEmptyTrack ("Pista");
        CHECK (projects.hasUnsavedChanges(), "añadir una pista es un cambio");

        const auto folder = outputFolder().getChildFile ("ProyectoCambios");
        folder.deleteRecursively();
        CHECK (projects.saveAs (folder).wasOk() && ! projects.hasUnsavedChanges(), "tras Guardar como: sin cambios");

        track->getVolume().set (-3.0f);
        CHECK (projects.hasUnsavedChanges(), "mover un volumen es un cambio");
        CHECK (projects.save().wasOk() && ! projects.hasUnsavedChanges(), "tras Guardar: sin cambios");

        track->getEffects().findEffect ("saturation")->getEnabledParameter().set (1.0f);
        CHECK (projects.hasUnsavedChanges(), "activar un efecto es un cambio");
        track->getEffects().findEffect ("saturation")->getEnabledParameter().set (0.0f);
        CHECK (! projects.hasUnsavedChanges(), "deshacerlo a mano vuelve a 'sin cambios'");

        track->setName ("Voz principal");
        CHECK (projects.hasUnsavedChanges(), "cambiar el nombre de una pista es un cambio");
        projects.save();

        projects.addEmptyTrack ("Pista");
        projects.save();
        engine.getMixer().moveTrack (0, 1);
        CHECK (projects.hasUnsavedChanges(), "reordenar pistas es un cambio");
        projects.save();

        projects.setBpm (140.0);
        CHECK (projects.hasUnsavedChanges(), "cambiar el BPM es un cambio");
        projects.save();

        track->setArmed (true);
        CHECK (! projects.hasUnsavedChanges(), "marcar la pista de grabación no cuenta como cambio");
        track->setArmed (false);

        bool done = false;
        projects.openProject (folder, [&] (juce::Result) { done = true; });
        runLoopUntil ([&] { return done; }, 10000);
        CHECK (done && ! projects.hasUnsavedChanges(), "recién abierto: sin cambios");

        projects.newProject();
        folder.deleteRecursively();
    }

    void testProjects()
    {
        section ("Proyecto: ProjectManager (carga asíncrona, guardar, abrir)");

        AudioEngine engine;     /* sin abrir dispositivo: 44.1 kHz por defecto */
        ProjectManager projects (engine);
        CHECK (projects.getProject().isTemporary() && projects.getProject().getAudioDirectory().isDirectory(),
               "sesión temporal con estructura de carpetas");

        const auto wav = writeToneWav (outputFolder().getChildFile ("import-me.wav"), 44100.0, 2, 2.0, 220.0);

        bool done = false;
        juce::Result loadResult = juce::Result::ok();
        std::vector<ProjectManager::NewTrack> newTracks;
        newTracks.push_back ({ "Tono", wav, 0.5, true });
        projects.addTracks (std::move (newTracks), [&] (juce::Result r) { loadResult = r; done = true; });
        CHECK (projects.isLoading(), "la carga ocurre en segundo plano");
        runLoopUntil ([&] { return done; }, 15000);

        const auto& tracks = engine.getMixer().getTracks();
        CHECK (done && loadResult.wasOk() && tracks.size() == 1, "pista importada");
        if (tracks.size() != 1) return;

        auto track = tracks.front();
        CHECK (track->getSourceFile().isAChildOf (projects.getProject().getAudioDirectory()), "el archivo se copia a audio/");
        CHECK (track->getClips().size() == 1 && track->getClips().front().timelineStart == 22050, "un clip que empieza a 0,5 s");

        /* Editar: dividir en 1,0 s y mover la mitad derecha a 3,0 s. */
        auto clips = track->getClips();
        const auto rightId = ClipEditing::split (clips, clips.front().id, 44100);
        ClipEditing::move (*ClipEditing::find (clips, rightId), 3 * 44100);
        track->setClips (clips);
        projects.notifyTracksEdited();

        track->getVolume().set (-6.0f);
        track->getMute().set (1.0f);
        track->getEffects().findEffect ("eq")->getEnabledParameter().set (1.0f);
        projects.setBpm (98.0);
        const auto empty = projects.addEmptyTrack ("Pista");
        CHECK (empty->getName() == "Pista 1" && projects.addEmptyTrack ("Pista")->getName() == "Pista 2", "pistas vacías con nombres únicos");
        projects.setArmedTrack (empty);
        CHECK (empty->isArmed() && projects.getArmedTrack() == empty && ! track->isArmed(), "solo una pista armada a la vez");

        const auto folder = outputFolder().getChildFile ("ProyectoPrueba");
        folder.deleteRecursively();
        const auto saved = projects.saveAs (folder);
        CHECK (saved.wasOk() && folder.getChildFile ("project.json").existsAsFile(), "Guardar como crea project.json");
        CHECK (track->getSourceFile().isAChildOf (folder), "los clips apuntan a la carpeta nueva");

        const auto json = folder.getChildFile ("project.json").loadFileAsString();
        CHECK (json.contains ("\"audio/import-me.wav\"") && json.contains ("\"clips\"") && json.contains ("\"version\": 2"),
               "JSON v2 con clips y rutas relativas");

        projects.newProject();
        CHECK (engine.getMixer().getTracks().empty(), "Nuevo proyecto vacía las pistas");

        done = false;
        projects.openProject (folder, [&] (juce::Result r) { loadResult = r; done = true; });
        runLoopUntil ([&] { return done; }, 15000);

        CHECK (done && loadResult.wasOk() && engine.getMixer().getTracks().size() == 3, "Abrir proyecto recarga las 3 pistas (incluidas las vacías)");
        if (engine.getMixer().getTracks().size() == 3)
        {
            auto reopened = engine.getMixer().getTracks().front();
            const auto reClips = reopened->getClips();
            CHECK (reClips.size() == 2
                       && reClips[0].timelineStart == 22050 && reClips[0].length == 44100 - 22050
                       && reClips[1].timelineStart == 3 * 44100 && reClips[1].sourceOffset == 44100 - 22050
                       && reClips[0].source == reClips[1].source,
                   "se restauran los 2 fragmentos (posición, recorte) y comparten un solo audio decodificado");
            CHECK (near (reopened->getVolume().get(), -6.0, 1e-4) && reopened->getMute().getBool()
                       && reopened->getEffects().findEffect ("eq")->isEnabled(),
                   "se restauran volumen, mute y efectos");
            CHECK (near (projects.getProject().getBpm(), 98.0, 1e-9) && projects.getProject().getName() == "ProyectoPrueba",
                   "se restauran nombre y BPM");
        }

        /* Grabación como fragmento de la pista armada, con compensación de latencia. */
        const auto target = engine.getMixer().getTracks()[1];
        const auto recording = writeToneWav (projects.getProject().getRecordingsDirectory().getChildFile ("Grabacion.wav"), 44100.0, 2, 1.0, 330.0);
        RecordingInfo info { recording, 100, 441, 44100.0 };
        done = false;
        projects.addRecording (info, target, [&] (juce::Result r) { loadResult = r; done = true; });
        runLoopUntil ([&] { return done; }, 15000);
        const auto targetClips = target->getClips();
        CHECK (done && loadResult.wasOk() && engine.getMixer().getTracks().size() == 3 && targetClips.size() == 1,
               "la grabación se añade como fragmento de la pista elegida (no crea otra)");
        if (targetClips.size() == 1)
            CHECK (targetClips[0].timelineStart == 0 && targetClips[0].sourceOffset == 341 && targetClips[0].length == 44100 - 341,
                   "latencia compensada: empieza en 0 y recorta 341 muestras");

        /* Compatibilidad con proyectos v1 y archivos que faltan. */
        const auto v1Folder = outputFolder().getChildFile ("ProyectoV1");
        v1Folder.deleteRecursively();
        v1Folder.createDirectory();
        wav.copyFileTo (v1Folder.getChildFile ("tono.wav"));
        v1Folder.getChildFile ("project.json").replaceWithText (
            "{ \"format\": \"stemlab-project\", \"version\": 1, \"name\": \"Viejo\", \"bpm\": 100, \"masterVolume\": 0,"
            "  \"tracks\": [ { \"name\": \"Tono\", \"file\": \"tono.wav\", \"start\": 1.0, \"state\": {} },"
            "              { \"name\": \"Perdida\", \"file\": \"no-existe.wav\", \"start\": 0.0, \"state\": {} } ] }");
        done = false;
        projects.openProject (v1Folder, [&] (juce::Result r) { loadResult = r; done = true; });
        runLoopUntil ([&] { return done; }, 15000);
        const auto& v1Tracks = engine.getMixer().getTracks();
        CHECK (done && v1Tracks.size() == 2 && v1Tracks[0]->getClips().size() == 1
                   && v1Tracks[0]->getClips().front().timelineStart == 44100,
               "abre proyectos v1 (un archivo por pista)");
        CHECK (v1Tracks.size() == 2 && ! v1Tracks[1]->hasClips() && loadResult.failed(),
               "una pista cuyo archivo falta se conserva vacía y se avisa del error");

        projects.newProject();
        folder.deleteRecursively();
        v1Folder.deleteRecursively();
    }
}

void runUnitTests()
{
    testUtils();
    testEffects();
    testMixerAndTransport();
    testClips();
    testLoaderAndRecorder();
    testProjects();
    testUnsavedChanges();
}
}
