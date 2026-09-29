#include "Tests.h"
#include "TestSupport.h"

#include "AI/AIProcessManager.h"
#include "AI/DemucsSeparator.h"
#include "Audio/AudioEngine.h"
#include "Project/ProjectManager.h"
#include "UI/MainComponent.h"
#include "UI/StemLabLookAndFeel.h"
#include "UI/TrackListView.h"
#include "UI/TrackView.h"
#include "Utils/Strings.h"

// Pistas: deshacer añadir / eliminar / mover / renombrar / importar, y copiar
// y pegar pistas enteras (también con las teclas reales de la ventana).

namespace stemlab::test
{
namespace
{
    template <typename ComponentType>
    ComponentType* findChild (juce::Component& parent)
    {
        for (auto* child : parent.getChildren())
        {
            if (auto* found = dynamic_cast<ComponentType*> (child))
                return found;

            if (auto* found = findChild<ComponentType> (*child))
                return found;
        }

        return nullptr;
    }

    void testTrackUndo()
    {
        section ("Deshacer: añadir, eliminar, mover y renombrar pistas");

        AudioEngine engine;
        ProjectManager projects (engine);
        const auto& tracks = engine.getMixer().getTracks();

        // El caso del error: crear una pista, eliminarla, crear otra y Ctrl+Z.
        const auto first = projects.addEmptyTrack ("Pista");
        projects.removeTrack (*first);
        const auto second = projects.addEmptyTrack ("Pista");
        CHECK (tracks.size() == 1 && tracks.front() == second, "crear, eliminar y crear otra: queda solo la nueva");

        projects.undo();
        CHECK (tracks.empty(), "Ctrl+Z quita la pista recién creada (no recupera la eliminada)");
        projects.undo();
        CHECK (tracks.size() == 1 && tracks.front() == first, "el siguiente Ctrl+Z recupera la pista eliminada");
        projects.undo();
        CHECK (tracks.empty() && ! projects.canUndo(), "y el siguiente deshace su creación: historial agotado");

        projects.redo();
        projects.redo();
        projects.redo();
        CHECK (tracks.size() == 1 && tracks.front() == second, "rehacer los tres pasos vuelve al mismo estado");

        // Insertar en una posición concreta y deshacerlo.
        const auto third = projects.addEmptyTrack ("Pista", 0);
        CHECK (tracks.size() == 2 && tracks.front() == third, "añadir encima de otra pista");
        projects.undo();
        projects.redo();
        CHECK (tracks.size() == 2 && tracks.front() == third, "rehacer la vuelve a poner en la misma posición");

        // Mover (ya aplicado en el mezclador, como al arrastrar la cabecera).
        const auto fourth = projects.addEmptyTrack ("Pista");
        engine.getMixer().moveTrack (0, 2);
        projects.trackMoved (third, 0, 2);
        CHECK (tracks[2] == third && projects.getUndoDescription() == "Mover pista", "mover una pista entra en el historial");
        projects.undo();
        CHECK (tracks[0] == third && tracks[1] == second && tracks[2] == fourth, "deshacer la devuelve a su posición");
        projects.redo();
        CHECK (tracks[2] == third, "rehacer la vuelve a mover");

        // Renombrar (ya aplicado, como al editar el nombre).
        const auto oldName = second->getName();
        second->setName ("Voz");
        projects.trackRenamed (second, oldName);
        projects.undo();
        CHECK (second->getName() == oldName, "deshacer el cambio de nombre: '" << second->getName() << "'");
        projects.redo();
        CHECK (second->getName() == "Voz", "rehacerlo");

        projects.trackRenamed (second, "Voz");      // el nombre no cambió
        projects.undo();
        CHECK (second->getName() == oldName, "un nombre que no cambia no crea un paso vacío");
        projects.redo();

        section ("Deshacer: importar audio");

        const auto wav = writeToneWav (outputFolder().getChildFile ("importar-deshacer.wav"), 44100.0, 2, 1.0, 220.0);
        const auto before = tracks.size();
        auto done = false;
        projects.importAudio ({ wav }, [&] (juce::Result) { done = true; });
        runLoopUntil ([&] { return done; }, 10000);
        CHECK (done && tracks.size() == before + 1 && projects.getUndoDescription() == "Importar audio",
               "importar un archivo es un paso del historial");
        projects.undo();
        CHECK (tracks.size() == before, "Ctrl+Z quita la pista importada");
        projects.redo();
        CHECK (tracks.size() == before + 1 && tracks.back()->hasClips(), "Ctrl+Y la vuelve a poner, con su audio");

        // Abrir un proyecto no deja pasos que deshacer (ni los del anterior).
        const auto folder = outputFolder().getChildFile ("ProyectoPistas");
        folder.deleteRecursively();
        projects.saveAs (folder);
        done = false;
        projects.openProject (folder, [&] (juce::Result) { done = true; });
        runLoopUntil ([&] { return done; }, 10000);
        CHECK (done && ! projects.canUndo() && ! projects.canRedo() && tracks.size() == before + 1,
               "abrir un proyecto empieza con el historial vacío");

        projects.newProject();
        folder.deleteRecursively();
    }

    void testPasteTrack()
    {
        section ("Copiar y pegar pistas");

        AudioEngine engine;
        ProjectManager projects (engine);
        const auto& tracks = engine.getMixer().getTracks();

        const auto voice = projects.addEmptyTrack ("Pista");
        voice->setName ("Voz");
        projects.editClips (voice, { makeClip (makeSource (44100, 0.3f, false, 44100.0), 22050) }, "Pegar fragmento");
        voice->getVolume().set (-3.0f);
        voice->getPan().set (-0.5f);
        voice->getEffects().findEffect ("eq")->getEnabledParameter().set (1.0f);
        const auto below = projects.addEmptyTrack ("Pista");

        // Copiar = una copia en ese momento; cambiar después la original no la afecta.
        const auto copied = voice->createCopy (voice->getName());
        voice->getVolume().set (0.0f);

        const auto pasted = projects.pasteTrack (*copied, 1);
        CHECK (tracks.size() == 3 && tracks[1] == pasted && tracks[2] == below, "se pega en la posición pedida (debajo de la original)");
        CHECK (pasted->getName() == "Voz (copia)", "nombre: '" << pasted->getName() << "'");
        CHECK (near (pasted->getVolume().get(), -3.0, 1e-4) && near (pasted->getPan().get(), -0.5, 1e-4)
                   && pasted->getEffects().findEffect ("eq")->isEnabled(),
               "copia volumen, paneo y efectos del momento de copiar");

        const auto original = voice->getClips();
        const auto copy = pasted->getClips();
        CHECK (copy.size() == 1 && copy[0].timelineStart == 22050 && copy[0].length == 44100
                   && copy[0].source == original[0].source && copy[0].id != original[0].id,
               "copia los fragmentos (mismo audio compartido, ids nuevos)");

        auto edited = pasted->getClips();
        ClipEditing::split (edited, edited.front().id, 44100);
        projects.editClips (pasted, edited, "Dividir fragmento");
        CHECK (voice->getClips().size() == 1 && pasted->getClips().size() == 2, "editar la copia no toca la original");

        const auto again = projects.pasteTrack (*copied);
        CHECK (again->getName() == "Voz (copia 2)" && tracks.back() == again, "pegar otra vez: 'Voz (copia 2)', al final");

        projects.undo();
        projects.undo();
        projects.undo();
        CHECK (tracks.size() == 2 && tracks[0] == voice && tracks[1] == below, "Ctrl+Z deshace los pegados");
    }

    void testKeyboardInMainWindow()
    {
        section ("Teclado en la ventana: Ctrl+T, Ctrl+Z, Ctrl+C / Ctrl+X / Ctrl+V");

        StemLabLookAndFeel lookAndFeel;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        {
            AudioEngine engine;
            ProjectManager projects (engine);
            AIProcessManager ai (std::make_unique<DemucsSeparator> (DemucsSeparator::findDefaultSettings()));
            MainComponent window (engine, projects, ai);
            auto* list = findChild<TrackListView> (window);
            const auto& tracks = engine.getMixer().getTracks();
            const juce::ModifierKeys ctrl (juce::ModifierKeys::commandModifier);
            const auto press = [&] (int key) { return window.keyPressed (juce::KeyPress (key, ctrl, 0)); };

            CHECK (list != nullptr, "la ventana contiene la lista de pistas");

            // Espacio sin ninguna pista: no hay nada que reproducir.
            window.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
            CHECK (! engine.getTransport().isPlaying() && engine.getTransport().getPosition() == 0,
                   "Espacio sin pistas no reproduce (el cabezal no se mueve)");

            // El caso del error, con las teclas: Ctrl+T, eliminar, Ctrl+T, Ctrl+Z.
            press ('t');
            const auto first = tracks.front();
            projects.removeTrack (*first);          // lo mismo que confirmar "Eliminar"
            press ('t');
            const auto second = tracks.front();
            press ('z');
            CHECK (tracks.empty(), "Ctrl+Z tras Ctrl+T quita la pista nueva (no vuelve la eliminada)");
            press ('y');
            CHECK (tracks.size() == 1 && tracks.front() == second, "Ctrl+Y la vuelve a poner");

            // Pista con un fragmento.
            auto clip = makeClip (makeSource (44100, 0.2f, false, 44100.0), 0);
            projects.editClips (second, { clip }, "Pegar fragmento");
            list->refresh();

            // Fragmento seleccionado: Ctrl+C / Ctrl+V copian el fragmento (al cabezal).
            list->selectClip (second, clip.id);
            engine.getTransport().setPosition (88200);
            press ('c');
            press ('v');
            CHECK (tracks.size() == 1 && second->getClips().size() == 2 && second->getClips().back().timelineStart == 88200,
                   "con un fragmento seleccionado, Ctrl+C / Ctrl+V pegan el fragmento en el cabezal");

            // Ctrl+V con el cabezal sobre audio: se pega justo después (enfrente), nunca encima.
            engine.getTransport().setPosition (22050);
            press ('v');
            const auto afterPaste = second->getClips();
            CHECK (afterPaste.size() == 3 && afterPaste.back().timelineStart == 44100,
                   "Ctrl+V con el cabezal sobre audio pega el fragmento a continuación (44100), no encima");

            // Clic en la cabecera: se selecciona la pista entera (sin fragmento).
            list->selectClip (second, clip.id);
            if (auto* row = findChild<TrackView> (*list))
            {
                const juce::Point<float> header (60.0f, 40.0f);
                row->mouseDown (mouseEventAt (*row, header, header, false));
                row->mouseUp (mouseEventAt (*row, header, header, false));
            }
            CHECK (list->getSelectedTrack() == second && list->getSelectedClipId() == 0,
                   "clic en la cabecera: pista seleccionada, ningún fragmento");

            press ('c');
            press ('v');
            CHECK (tracks.size() == 2 && tracks[1]->getClips().size() == 3 && list->getSelectedTrack() == tracks[1],
                   "sin fragmento seleccionado, Ctrl+C / Ctrl+V duplican la pista debajo y la seleccionan");
            CHECK (projects.getUndoDescription() == "Pegar pista", "pegar una pista se puede deshacer");

            // Cortar la pista pegada y pegarla al final.
            const auto pasted = tracks[1];
            list->selectClip (pasted, 0);
            press ('x');
            CHECK (tracks.size() == 1 && projects.getUndoDescription() == "Cortar pista", "Ctrl+X corta la pista seleccionada");
            press ('v');
            CHECK (tracks.size() == 2 && tracks[1]->getClips().size() == 3, "y Ctrl+V la vuelve a pegar");

            press ('z');
            press ('z');
            CHECK (tracks.size() == 2 && tracks[1] == pasted, "deshacer pegar y cortar devuelve la pista original a su sitio");

            // Copiar un fragmento después de una pista: Ctrl+V pega lo último copiado.
            list->selectClip (second, second->getClips().front().id);
            press ('c');
            const auto clipsBefore = second->getClips().size();
            press ('v');
            CHECK (tracks.size() == 2 && second->getClips().size() == clipsBefore + 1, "Ctrl+V pega lo último que se copió (el fragmento)");

            // Supr y Retroceso eliminan el fragmento seleccionado.
            const auto count = second->getClips().size();
            list->selectClip (second, second->getClips().back().id);
            window.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
            CHECK (second->getClips().size() == count - 1, "Supr elimina el fragmento seleccionado");
            list->selectClip (second, second->getClips().back().id);
            window.keyPressed (juce::KeyPress (juce::KeyPress::backspaceKey));
            CHECK (second->getClips().size() == count - 2, "Retroceso también");

            // Sin fragmento seleccionado (clic en la cabecera), Supr elimina la pista tras confirmar.
            list->selectClip (second, 0);
            window.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
            runLoopUntil ([] { return juce::Component::getCurrentlyModalComponent() != nullptr; }, 3000);
            auto* dialog = juce::Component::getCurrentlyModalComponent();
            CHECK (dialog != nullptr, "Supr con la pista seleccionada pide confirmar su eliminación");

            if (dialog != nullptr)
            {
                dialog->exitModalState (1);             // "Eliminar"
                runLoopUntil ([&] { return tracks.size() == 1; }, 3000);
            }

            CHECK (tracks.size() == 1 && tracks.front() != second, "al confirmar se elimina la pista");
            press ('z');
            CHECK (tracks.size() == 2 && tracks.front() == second, "y Ctrl+Z la recupera");
        }

        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }
}

void runTrackTests()
{
    testTrackUndo();
    testPasteTrack();
    testKeyboardInMainWindow();
}
}
