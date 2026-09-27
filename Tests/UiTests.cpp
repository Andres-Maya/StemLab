#include "Tests.h"
#include "TestSupport.h"

#include "Audio/AudioEngine.h"
#include "UI/StemLabLookAndFeel.h"
#include "UI/SeparationWindow.h"
#include "UI/TrackListView.h"
#include "AI/DemucsSeparator.h"
#include "Utils/Strings.h"

// Interfaz sin dispositivo de audio: añadir, reordenar y hacer zoom. Guarda
// capturas PNG en la carpeta de salida para revisarlas a ojo.

namespace stemlab::test
{
namespace
{
    void snapshotAddTrackRow()
    {
        section ("UI: + de pistas, reordenar, renombrar y zoom");

        AudioEngine engine;
        StemLabLookAndFeel lookAndFeel;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        {
            TrackListView view (engine);
            int added = 0;
            int reordered = 0;
            view.onAddTrack = [&] (int insertIndex)
            {
                ++added;
                engine.getMixer().addTrack (std::make_shared<AudioTrack> ("Pista " + juce::String (added)), insertIndex);
                view.refresh();
            };
            view.onTracksReordered = [&] (std::shared_ptr<AudioTrack>, int, int) { ++reordered; };
            view.setSize (900, 330);
            view.setVisible (true);
            view.refresh();
            saveSnapshot (view.createComponentSnapshot (view.getLocalBounds()), "addrow-empty.png");

            view.onAddTrack (-1);                     /* Pista 1 */
            view.onAddTrack (-1);                     /* Pista 2 */
            view.onAddTrack (1);                      /* Pista 3, con el + de la Pista 1: queda en medio */
            const auto& tracks = engine.getMixer().getTracks();
            CHECK (tracks.size() == 3 && tracks[1]->getName() == "Pista 3",
                   "el + de una pista inserta la nueva justo debajo de ella");

            /* Reordenar: seleccionar la Pista 1 y bajarla dos veces. */
            view.selectTrack (tracks[0]);
            view.moveSelectedTrack (1);
            view.moveSelectedTrack (1);
            CHECK (tracks[0]->getName() == "Pista 3" && tracks[1]->getName() == "Pista 2" && tracks[2]->getName() == "Pista 1"
                       && reordered == 2 && view.getSelectedTrack() == tracks[2],
                   "mover pistas cambia el orden y conserva la selección");
            view.moveSelectedTrack (1);
            CHECK (tracks[2]->getName() == "Pista 1" && reordered == 2, "no se sale de la lista");

            /* Un clip de 60 s para ver el zoom. */
            auto source = std::make_shared<ClipSource>();
            source->sampleRate = engine.getSampleRate();
            source->audio = makeSine (2, (int) (60 * source->sampleRate), 2.0, source->sampleRate, 0.5f);
            tracks[0]->setClips ({ { AudioClip::createId(), source, 0, 0, source->getLength() } });
            engine.getMixer().updateContentLength();
            view.refresh();
            saveSnapshot (view.createComponentSnapshot (view.getLocalBounds()), "addrow-tracks.png");

            view.zoomIn();
            view.zoomIn();
            view.zoomIn();
            saveSnapshot (view.createComponentSnapshot (view.getLocalBounds()), "zoom-in.png");
            view.zoomToFit();
            CHECK (true, "zoom sin errores (capturas addrow-tracks.png y zoom-in.png)");
        }

        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }
    void testSeparationWindow()
    {
        section ("Ventana de la separación: esfera y pistas que van apareciendo");

        CHECK (near (SeparationView::appearanceThreshold (0, 4), 0.2, 1e-9) && near (SeparationView::appearanceThreshold (3, 4), 0.8, 1e-9),
               "con 4 pistas aparecen al 20, 40, 60 y 80 %");

        DemucsSeparator separator (DemucsSeparator::findDefaultSettings());
        CHECK (separator.getExpectedStems() == juce::StringArray ("vocals", "drums", "bass", "other"), "htdemucs: 4 pistas");
        separator.setCurrentModel ("htdemucs_6s");
        CHECK (separator.getExpectedStems().size() == 6 && separator.getExpectedStems().contains ("guitar"), "htdemucs_6s: 6 pistas");

        StemLabLookAndFeel lookAndFeel;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        {
            std::vector<SeparationView::Stem> stems;

            for (const auto* id : { "vocals", "drums", "bass", "other" })
                stems.push_back ({ stemDisplayName (id), trackColourFor (stemDisplayName (id), 0) });

            SeparationView view ("Mi canción"_u8, trackColourFor ("Mi canción"_u8, 0), stems);
            double progress = -1.0;
            auto cancelled = false;
            view.getProgress = [&] { return progress; };
            view.getStatus = [] { return juce::String ("Separando instrumentos (cpu)..."); };
            view.onCancel = [&] { cancelled = true; };

            const auto runFor = [&] (double seconds) { for (double t = 0.0; t < seconds; t += 1.0 / 60.0) view.advance (1.0 / 60.0); };

            runFor (1.0);
            CHECK (view.getNumVisibleStems() == 0, "sin porcentaje todavía (cargando el modelo): solo la esfera");
            saveSnapshot (view.createComponentSnapshot (view.getLocalBounds()), "separacion-0.png");

            progress = 0.1;
            runFor (0.5);
            CHECK (view.getNumVisibleStems() == 0, "al 10 %: ninguna pista todavía");

            progress = 0.25;
            runFor (0.5);
            CHECK (view.getNumVisibleStems() == 1, "al 25 %: aparece la primera (Voz)");

            progress = 0.65;
            runFor (1.2);
            CHECK (view.getNumVisibleStems() == 3, "al 65 %: tres pistas");
            saveSnapshot (view.createComponentSnapshot (view.getLocalBounds()), "separacion-65.png");

            view.setFinished (true);
            runFor (1.2);
            CHECK (view.getNumVisibleStems() == 4, "al terminar bien aparecen todas");
            saveSnapshot (view.createComponentSnapshot (view.getLocalBounds()), "separacion-fin.png");

            if (auto* button = dynamic_cast<juce::Button*> (view.getChildComponent (0)))
            {
                CHECK (! button->isVisible(), "al terminar ya no se puede cancelar");
            }

            SeparationView other ("Otra", juce::Colours::orange, stems);
            other.getProgress = [] { return 0.5; };
            other.onCancel = [&] { cancelled = true; };
            other.advance (0.1);
            if (auto* button = dynamic_cast<juce::Button*> (other.getChildComponent (0)))
                button->triggerClick();
            runLoopUntil ([&] { return cancelled; }, 1000);
            CHECK (cancelled, "el botón Cancelar avisa para cancelar la separación");
        }

        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }
}

void runUiTests()
{
    testSeparationWindow();

    section ("Colores de las pistas");
    CHECK (trackColourFor ("Grabación 1"_u8, 0) != Palette::record && trackColourFor ("Grabación 3"_u8, 5) == trackColourFor ("Grabación 1"_u8, 0),
           "las pistas de grabación tienen su color propio, que no es el rojo de 'grabando'");
    CHECK (trackColourFor ("Grabación 1"_u8, 0) != Palette::accent, "ni el del cabezal");

    snapshotAddTrackRow();
}
}
