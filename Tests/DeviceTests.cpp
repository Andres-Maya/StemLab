#include "Tests.h"
#include "TestSupport.h"

#include "Audio/AudioEngine.h"
#include "Project/ProjectManager.h"
#include "UI/StemLabLookAndFeel.h"
#include "UI/TrackListView.h"
#include "Utils/Strings.h"

// Pruebas con la tarjeta de sonido real (--device). La prueba acústica
// (--acoustic) además reproduce ruido por los altavoces y lo graba.

namespace stemlab::test
{
namespace
{
    void snapshotRecordingLane()
    {
        section ("Grabación real: pausar y seguir grabando en la MISMA pista");

        AudioEngine engine;
        engine.initialise (nullptr);
        ProjectManager projects (engine);
        StemLabLookAndFeel lookAndFeel;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        {
            TrackListView view (engine);
            view.setSize (900, 260);
            view.setVisible (true);

            /* Como MainComponent: sin pista armada, se crea una y se arma. */
            const auto target = projects.addEmptyTrack ("Grabación"_u8);
            projects.setArmedTrack (target);
            view.refresh();

            auto recordFor = [&] (int ms, const char* snapshotName)
            {
                const auto result = engine.startRecording (projects.createRecordingFile());
                runLoopUntil ([] { return false; }, ms);
                if (snapshotName != nullptr)
                    saveSnapshot (view.createComponentSnapshot (view.getLocalBounds()), snapshotName);
                const auto info = engine.stopRecording();
                engine.getTransport().pause();
                bool done = false;
                projects.addRecording (info, target, [&] (juce::Result) { done = true; });
                runLoopUntil ([&] { return done; }, 10000);
                view.refresh();
                return result;
            };

            engine.getTransport().stop();   /* desde 0:00, como en la captura del usuario */
            CHECK (recordFor (2000, "lane.png").wasOk(), "primera toma grabada");
            const auto afterFirst = engine.getTransport().getPosition();
            CHECK (recordFor (2000, "lane2.png").wasOk(), "segunda toma tras pausar (R, R)");

            const auto clips = target->getClips();
            CHECK (engine.getMixer().getTracks().size() == 1 && clips.size() == 2,
                   "las dos tomas quedan en la misma pista como 2 fragmentos (" << (int) clips.size() << ")");
            if (clips.size() == 2)
                CHECK (clips[1].timelineStart >= clips[0].getEnd() - 4800 && clips[1].timelineStart <= afterFirst,
                       "el segundo fragmento va a continuación del primero (" << (int) clips[0].getEnd() << " -> " << (int) clips[1].timelineStart << ")");

            /* Vista previa durante la 1ª toma (la pista aún no tiene fragmentos): se
               dibuja con el color de la pista, no en rojo. */
            const auto lane = juce::ImageFileFormat::loadFrom (outputFolder().getChildFile ("lane.png"));
            const auto trackColour = trackColourFor (target->getName(), 0);
            int trackPixels = 0, redPixels = 0;
            for (int y = 30; y < lane.getHeight(); ++y)
                for (int x = TrackView::headerWidth + 2; x < lane.getWidth(); ++x)
                {
                    const auto c = lane.getPixelAt (x, y);
                    if (std::abs (c.getRed() - trackColour.getRed()) < 24 && std::abs (c.getGreen() - trackColour.getGreen()) < 24
                        && std::abs (c.getBlue() - trackColour.getBlue()) < 24) ++trackPixels;
                    if (c.getRed() > 180 && c.getGreen() < 110 && c.getBlue() < 110) ++redPixels;
                }
            CHECK (trackPixels > 50, "la toma en curso se dibuja en directo con el color de la pista (" << trackPixels << " píxeles)");
            CHECK (redPixels == 0, "y nada de la toma se ve en rojo (" << redPixels << " píxeles rojos)");

            /* Tercera toma con el cabezal en mitad de la primera: como en MainComponent,
               la grabación empieza después del audio que ya hay (nunca encima). */
            const auto firstTake = target->getClips().front();
            const auto middle = firstTake.timelineStart + firstTake.length / 2;
            const auto secondEnd = target->getClips().back().getEnd();
            engine.getTransport().setPosition (ClipEditing::findFreeSpace (target->getClips(), middle, 1));
            CHECK (recordFor (1000, "lane3.png").wasOk(), "tercera toma, con el cabezal sobre la primera");
            const auto layered = target->getClips();
            auto overlaps = false;
            for (size_t i = 0; i < layered.size(); ++i)
                for (size_t j = i + 1; j < layered.size(); ++j)
                    overlaps = overlaps || (layered[i].timelineStart < layered[j].getEnd() && layered[j].timelineStart < layered[i].getEnd());
            CHECK (engine.getMixer().getTracks().size() == 1 && layered.size() == 3 && layered.back().timelineStart == secondEnd && ! overlaps,
                   "queda en la misma pista, justo después de las tomas anteriores (" << (int) secondEnd << " -> "
                       << (int) layered.back().timelineStart << ") y sin solaparse");

            /* Edición visual: dividir la primera toma y separar las mitades. */
            auto edited = target->getClips();
            juce::int64 lastEnd = 0;
            for (const auto& clip : edited) lastEnd = juce::jmax (lastEnd, clip.getEnd());
            const auto half = ClipEditing::split (edited, edited.front().id, edited.front().timelineStart + edited.front().length / 4);
            ClipEditing::move (*ClipEditing::find (edited, half), lastEnd + 48000);
            target->setClips (edited);
            projects.notifyTracksEdited();
            runLoopUntil ([] { return false; }, 300);
            view.refresh();
            view.selectClip (target, half);
            saveSnapshot (view.createComponentSnapshot (view.getLocalBounds()), "clips.png");
            CHECK (target->getClips().size() == 4, "dividir + mover deja 4 fragmentos visibles (clips.png)");
        }

        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void diagnosePlayback()
    {
        section ("Audio: recuperación del dispositivo (fallo del cabezal congelado)");
        AudioEngine engine;
        engine.initialise (nullptr);
        auto& dm = engine.getDeviceManager();

        auto advances = [&]
        {
            engine.getTransport().stop();
            engine.getTransport().play();
            runLoopUntil ([] { return false; }, 800);
            const auto pos = engine.getTransport().getPosition();
            engine.getTransport().stop();
            return pos > 0;
        };

        CHECK (engine.isDeviceRunning() && advances(), "al inicio el audio funciona y el cabezal avanza");
        CHECK (engine.getCurrentOutputName().isNotEmpty(), "muestra la salida actual: " << engine.getCurrentOutputName());

        /* Escenario 1: el dispositivo se cierra (como cuando falla un cambio de salida). */
        dm.closeAudioDevice();
        CHECK (! engine.isDeviceRunning() && engine.getCurrentOutputName().isEmpty(), "dispositivo cerrado -> se detecta 'sin audio'");
        runLoopUntil ([&] { return engine.isDeviceRunning(); }, 6000);
        CHECK (engine.isDeviceRunning() && advances(), "se reabre solo y el cabezal vuelve a avanzar");

        /* Escenario 2: la entrada guardada desaparece (micrófono interno desactivado al conectar un headset). */
        auto setup = dm.getAudioDeviceSetup();
        setup.inputDeviceName = "Microfono que ya no existe";
        const auto error = dm.setAudioDeviceSetup (setup, true);
        std::cout << "  (JUCE al abrir con entrada inexistente: '" << error << "', abierto=" << engine.isDeviceRunning() << ")\n";
        runLoopUntil ([&] { return engine.isDeviceRunning(); }, 6000);
        CHECK (engine.isDeviceRunning() && advances() && dm.getAudioDeviceSetup().inputDeviceName.isNotEmpty(), "se recupera con la entrada anterior: '" << dm.getAudioDeviceSetup().inputDeviceName << "'");
        CHECK (engine.isFollowingSystemOutput(), "la opción de seguir la salida de Windows sigue activa");
    }

    void testSystemOutput()
    {
        section ("Audio: seguir la salida predeterminada de Windows");

        AudioEngine engine;
        const auto error = engine.initialise (nullptr);
        CHECK (error.isEmpty(), "abre el dispositivo de audio real " << error);

        const auto systemOutput = engine.getSystemDefaultOutputName();
        auto current = engine.getDeviceManager().getAudioDeviceSetup().outputDeviceName;
        CHECK (engine.isFollowingSystemOutput(), "activado por defecto");
        CHECK (systemOutput.isNotEmpty() && current == systemOutput,
               "usa la salida predeterminada: '" << current << "' (Windows: '" << systemOutput << "')");

        // Simula que el usuario elige manualmente "sin salida" en el selector.
        auto setup = engine.getDeviceManager().getAudioDeviceSetup();
        setup.outputDeviceName = {};
        engine.getDeviceManager().setAudioDeviceSetup (setup, true);
        runLoopUntil ([&] { return ! engine.isFollowingSystemOutput(); }, 3000);
        CHECK (! engine.isFollowingSystemOutput() && engine.getDeviceManager().getAudioDeviceSetup().outputDeviceName.isEmpty(),
               "elegir otra salida a mano desactiva la opción y se respeta");

        engine.setFollowSystemOutput (true);
        current = engine.getDeviceManager().getAudioDeviceSetup().outputDeviceName;
        CHECK (engine.isFollowingSystemOutput() && current == systemOutput, "reactivarla vuelve a la salida de Windows: '" << current << "'");
    }

    void acousticNoiseTest()
    {
        section ("Grabación real: ruido constante por los altavoces -> micrófono (modo RAW)");

        AudioEngine engine;
        engine.initialise (nullptr);
        CHECK (engine.getInputGain().get() > 17.0f, "ganancia de entrada por defecto: " << engine.getInputGain().toText());

        /* Ruido rosa suave y constante durante 12 s. */
        const auto rate = engine.getSampleRate();
        auto source = std::make_shared<ClipSource>();
        source->sampleRate = rate;
        source->audio.setSize (2, (int) (12 * rate));
        juce::Random random (1234);
        float b0 = 0, b1 = 0, b2 = 0;
        for (int i = 0; i < source->audio.getNumSamples(); ++i)
        {
            const auto white = random.nextFloat() * 2.0f - 1.0f;
            b0 = 0.99765f * b0 + white * 0.0990460f;
            b1 = 0.96300f * b1 + white * 0.2965164f;
            b2 = 0.57000f * b2 + white * 1.0526913f;
            const auto pink = (b0 + b1 + b2 + white * 0.1848f) * 0.08f;
            source->audio.setSample (0, i, pink);
            source->audio.setSample (1, i, pink);
        }

        auto track = std::make_shared<AudioTrack> ("Ruido");
        track->setClips ({ { AudioClip::createId(), source, 0, 0, source->getLength() } });
        engine.getMixer().addTrack (track);

        const auto file = outputFolder().getChildFile ("noise-rec.wav");
        engine.getTransport().stop();
        const auto started = engine.startRecording (file);
        CHECK (started.wasOk(), "graba mientras suena el ruido " << started.getErrorMessage());
        runLoopUntil ([] { return false; }, 10000);
        engine.stopRecording();
        engine.getTransport().stop();

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr) { CHECK (false, "no se pudo leer la grabación"); return; }

        juce::AudioBuffer<float> recorded ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&recorded, 0, recorded.getNumSamples(), 0, true, true);

        std::cout << "  nivel por segundo (RMS dBFS):";
        std::vector<float> levels;
        for (int second = 0; second + 1 <= recorded.getNumSamples() / (int) rate; ++second)
        {
            const auto rms = recorded.getRMSLevel (0, second * (int) rate, (int) rate);
            levels.push_back (juce::Decibels::gainToDecibels (rms, -100.0f));
            std::cout << " " << juce::String (levels.back(), 1);
        }
        std::cout << "\n";

        if (levels.size() >= 8)
        {
            const auto early = (levels[1] + levels[2]) * 0.5f;
            const auto late = (levels[levels.size() - 2] + levels[levels.size() - 1]) * 0.5f;
            CHECK (late > early - 4.0f, "el ruido constante NO se atenúa con el tiempo (de " << early << " a " << late << " dBFS)");
            CHECK (early > -45.0f, "nivel grabado razonable con la ganancia por defecto (" << early << " dBFS)");
        }
    }
}

void runDeviceTests()
{
    snapshotRecordingLane();
    diagnosePlayback();
    testSystemOutput();
}

void runAcousticTest()
{
    acousticNoiseTest();
}
}
