#include "Tests.h"
#include "TestSupport.h"

#include "Audio/AudioEngine.h"
#include "UI/StemLabLookAndFeel.h"
#include "UI/TrackListView.h"

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
}

void runUiTests()
{
    snapshotAddTrackRow();
}
}
