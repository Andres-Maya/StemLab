#include "Tests.h"
#include "TestSupport.h"

#include "AI/AIProcessManager.h"
#include "AI/DemucsSeparator.h"
#include "Audio/AudioEngine.h"
#include "Project/ProjectManager.h"
#include "UI/MainComponent.h"
#include "UI/StemLabLookAndFeel.h"
#include "UI/TrackListView.h"
#include "Utils/Strings.h"

// Carpetas de separación: meter y sacar pistas, guardarlas, verlas en la lista
// (desplegar, plegar, arrastrar) y su ventana de ondas.

namespace stemlab::test
{
namespace
{
    const juce::StringArray fourStems { "vocals", "drums", "bass", "other" };

    /** Lo que hace MainComponent al terminar una separación: la carpeta y sus pistas. */
    juce::String simulateSeparation (ProjectManager& projects, const juce::String& id)
    {
        TrackFolder folder;
        folder.id = id;
        folder.name = "Mi canción"_u8;
        folder.colour = juce::Colour (0xff4fc3f7);
        folder.stems = fourStems;
        projects.addFolder (folder);

        std::vector<ProjectManager::NewTrack> tracks;

        for (const auto& stem : fourStems)
        {
            const auto file = writeConstantWav (projects.getProject().getStemsDirectory().getChildFile (id + "-" + stem + ".wav"),
                                                44100.0, 1.0, 0.1f);
            ProjectManager::NewTrack track { stemDisplayName (stem), file, 0.0, false };
            track.folderId = id;
            track.stemGroup = id;
            track.stemId = stem;
            tracks.push_back (std::move (track));
        }

        auto done = false;
        projects.addTracks (std::move (tracks), [&] (juce::Result) { done = true; }, "Separar instrumentos");
        runLoopUntil ([&] { return done; }, 10000);
        return id;
    }

    int indexIn (const std::vector<std::shared_ptr<AudioTrack>>& tracks, const std::shared_ptr<AudioTrack>& track)
    {
        return (int) std::distance (tracks.begin(), std::find (tracks.begin(), tracks.end(), track));
    }

    void testFolderModel()
    {
        section ("Carpetas: meter y sacar pistas, guardar y abrir");

        AudioEngine engine;
        ProjectManager projects (engine);
        const auto& tracks = engine.getMixer().getTracks();
        const auto song = projects.addEmptyTrack ("Canción"_u8);
        const auto id = simulateSeparation (projects, "carpeta-1");

        CHECK (projects.getFolderTracks (id).size() == 4 && projects.getStemTracks (id).size() == 4,
               "la separación deja sus 4 pistas dentro de la carpeta");
        CHECK (tracks[1]->getStemId() == "vocals" && tracks[1]->getName() == "Voz" && tracks[1]->getFolderId() == id,
               "cada pista sabe su carpeta y de qué stem salió");

        // Sacar la Voz: sigue siendo pista de la separación (su onda se mantiene).
        const auto voice = tracks[1];
        projects.moveTrackToFolder (voice, {}, (int) tracks.size() - 1);
        CHECK (projects.getFolderTracks (id).size() == 3 && projects.getStemTracks (id).size() == 4
                   && voice->getFolderId().isEmpty() && tracks.back() == voice,
               "sacar una pista: fuera de la carpeta, pero sigue contando como pista de la separación");
        CHECK (projects.getUndoDescription() == "Sacar de la carpeta", "se puede deshacer");
        projects.undo();
        CHECK (voice->getFolderId() == id && indexIn (tracks, voice) == 1, "deshacer la devuelve a la carpeta y a su sitio");

        // Meter la canción original.
        projects.moveTrackToFolder (song, id, 4);
        CHECK (song->getFolderId() == id && projects.getFolderTracks (id).size() == 5 && projects.getStemTracks (id).size() == 4,
               "meter otra pista en la carpeta (no es de la separación: no tiene onda)");
        CHECK (projects.getUndoDescription() == "Meter en la carpeta", "también se puede deshacer");
        projects.undo();

        // Plegar y guardar.
        const auto folderPath = outputFolder().getChildFile ("ProyectoCarpetas");
        folderPath.deleteRecursively();
        CHECK (projects.saveAs (folderPath).wasOk(), "guardar el proyecto con la carpeta");
        projects.setFolderExpanded (id, false);
        CHECK (projects.hasUnsavedChanges(), "plegar la carpeta es un cambio (se guarda)");
        projects.save();

        projects.newProject();
        CHECK (projects.getFolders().empty(), "proyecto nuevo: sin carpetas");

        auto done = false;
        projects.openProject (folderPath, [&] (juce::Result) { done = true; });
        runLoopUntil ([&] { return done; }, 10000);
        const auto* reopened = projects.findFolder (id);
        CHECK (done && reopened != nullptr && reopened->name == "Mi canción"_u8 && ! reopened->expanded
                   && reopened->stems == fourStems && reopened->colour == juce::Colour (0xff4fc3f7),
               "al abrir se recupera la carpeta (nombre, color, plegada y sus stems)");
        CHECK (projects.getFolderTracks (id).size() == 4 && projects.getStemTracks (id).size() == 4
                   && tracks[1]->getStemId() == "vocals",
               "y sus pistas vuelven dentro, sabiendo de qué stem salieron");
        CHECK (! projects.hasUnsavedChanges(), "recién abierto: sin cambios");

        // Sin pistas, la carpeta no se guarda.
        for (const auto& stem : projects.getStemTracks (id))
            projects.removeTrack (*stem);

        projects.save();
        CHECK (! folderPath.getChildFile ("ProyectoCarpetas.stemlab").loadFileAsString().contains (id),
               "una carpeta que se quedó sin pistas ya no se guarda");

        projects.newProject();
        folderPath.deleteRecursively();
    }

    std::vector<TrackView*> trackViewsIn (juce::Component& parent)
    {
        std::vector<TrackView*> views;

        for (auto* child : parent.getChildren())
        {
            if (auto* view = dynamic_cast<TrackView*> (child))
                views.push_back (view);
            else
                for (auto* nested : trackViewsIn (*child))
                    views.push_back (nested);
        }

        return views;
    }

    TrackView* viewOf (TrackListView& list, const std::shared_ptr<AudioTrack>& track)
    {
        for (auto* view : trackViewsIn (list))
            if (view->getTrackPointer() == track)
                return view;

        return nullptr;
    }

    /** Arrastra la cabecera de la pista para que su borde superior quede en newTop. */
    void dragRowTo (TrackView& row, int newTop)
    {
        const juce::Point<float> grab (60.0f, 40.0f);
        const juce::Point<float> to (60.0f, 40.0f + (float) (newTop - row.getY()));
        row.mouseDown (mouseEventAt (row, grab, grab, false));
        row.mouseDrag (mouseEventAt (row, to, grab, true));
        row.mouseUp (mouseEventAt (row, to, grab, true));
    }

    void testFolderList()
    {
        section ("Lista de pistas: carpeta desplegable y arrastrar dentro o fuera");

        StemLabLookAndFeel lookAndFeel;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        {
            AudioEngine engine;
            ProjectManager projects (engine);
            projects.addEmptyTrack ("Canción"_u8);
            const auto id = simulateSeparation (projects, "carpeta-lista");
            const auto extra = projects.addEmptyTrack ("Pista");
            const auto& tracks = engine.getMixer().getTracks();

            TrackListView list (engine);
            list.setSize (1000, 700);
            list.setVisible (true);

            const auto updateFolders = [&]
            {
                std::vector<TrackListView::FolderInfo> infos;

                for (const auto& folder : projects.getFolders())
                    infos.push_back ({ folder.id, folder.name, folder.colour, folder.expanded, ! projects.getStemTracks (folder.id).empty(), false });

                list.setFolders (infos);
                list.refresh();
            };

            juce::String toggled, toggledWaves;
            list.onToggleFolder = [&] (const juce::String& folderId) { toggled = folderId; };
            list.onToggleFolderWindow = [&] (const juce::String& folderId) { toggledWaves = folderId; };
            list.onTrackDropped = [&] (std::shared_ptr<AudioTrack> track, const juce::String& folderId, int index)
            {
                projects.moveTrackToFolder (track, folderId, index);
                updateFolders();
            };
            updateFolders();

            // Canción, [carpeta: Voz, Batería, Bajo, Otros], Pista 1
            const auto header = list.getFolderHeaderBounds (id);
            const auto song = viewOf (list, tracks[0]), voice = viewOf (list, tracks[1]), other = viewOf (list, tracks[4]);
            const auto extraRow = viewOf (list, extra);
            CHECK (header.getY() == song->getBottom() && voice->getY() == header.getBottom() && extraRow->getY() == other->getBottom(),
                   "la cabecera de la carpeta va antes de sus pistas, y la lista sigue después");
            saveSnapshot (list.createComponentSnapshot (list.getLocalBounds()), "carpeta.png");

            projects.setFolderExpanded (id, false);
            updateFolders();
            CHECK (! voice->isVisible() && ! other->isVisible() && extraRow->getY() == list.getFolderHeaderBounds (id).getBottom(),
                   "plegada: sus pistas se ocultan y la lista se cierra");
            saveSnapshot (list.createComponentSnapshot (list.getLocalBounds()), "carpeta-plegada.png");

            // Arrastrar "Pista 1" sobre la cabecera plegada: entra en la carpeta.
            const auto collapsedHeader = list.getFolderHeaderBounds (id);
            dragRowTo (*extraRow, collapsedHeader.getCentreY() - TrackView::preferredHeight / 2);
            CHECK (extra->getFolderId() == id && tracks.back() == extra, "soltar una pista sobre la carpeta plegada la mete (al final)");
            CHECK (projects.getUndoDescription() == "Meter en la carpeta", "y se puede deshacer");

            projects.setFolderExpanded (id, true);
            updateFolders();

            // Sacar la Voz arrastrándola encima de todo.
            dragRowTo (*voice, 0);
            CHECK (tracks[0] == voice->getTrackPointer() && voice->getTrack().getFolderId().isEmpty(),
                   "arrastrar una pista por encima de la carpeta la saca");

            // Sacar "Otros" soltándola justo debajo del bloque (fuera); "Bajo", al final del bloque (dentro).
            auto listBottom = 0;
            for (auto* row : trackViewsIn (list))
                if (row->isVisible())
                    listBottom = juce::jmax (listBottom, row->getBottom());

            const auto otherTrack = other->getTrackPointer();
            dragRowTo (*other, listBottom - TrackView::preferredHeight / 2 + 4);
            CHECK (otherTrack->getFolderId().isEmpty() && tracks.back() == otherTrack, "soltarla debajo del bloque, fuera de la carpeta");

            const auto bass = viewOf (list, tracks[indexIn (tracks, projects.getFolderTracks (id)[1])]);
            const auto lastMember = viewOf (list, projects.getFolderTracks (id).back());
            // (Los huecos se calculan sin la pista arrastrada: lo que está debajo sube una fila.)
            dragRowTo (*bass, lastMember->getBottom() - TrackView::preferredHeight - 20 - TrackView::preferredHeight / 2);
            CHECK (bass->getTrack().getFolderId() == id && projects.getFolderTracks (id).back() == bass->getTrackPointer(),
                   "soltarla al final del bloque la deja dentro, la última");

            // La cabecera: un clic despliega o pliega; su botón "Ondas" abre la ventana.
            juce::Component* headerComponent = nullptr;
            const auto headerBounds = list.getFolderHeaderBounds (id);

            std::function<void (juce::Component&)> findHeader = [&] (juce::Component& parent)
            {
                for (auto* child : parent.getChildren())
                {
                    if (child->getBounds() == headerBounds && dynamic_cast<TrackView*> (child) == nullptr && child->isVisible())
                        headerComponent = child;
                    else
                        findHeader (*child);
                }
            };
            findHeader (list);

            if (headerComponent != nullptr)
            {
                const juce::Point<float> click (120.0f, 12.0f);
                headerComponent->mouseUp (mouseEventAt (*headerComponent, click, click, false));

                for (auto* child : headerComponent->getChildren())
                    if (auto* button = dynamic_cast<juce::Button*> (child))
                        button->triggerClick();

                runLoopUntil ([&] { return toggledWaves.isNotEmpty(); }, 1000);
            }

            CHECK (headerComponent != nullptr && toggled == id, "un clic en la cabecera despliega o pliega la carpeta");
            CHECK (toggledWaves == id, "su botón Ondas abre o cierra la ventana de ondas");
        }

        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void testFolderWindow()
    {
        section ("Ventana de ondas de la carpeta: botón Ondas y pistas que desaparecen");

        StemLabLookAndFeel lookAndFeel;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        {
            AudioEngine engine;
            ProjectManager projects (engine);
            AIProcessManager ai (std::make_unique<DemucsSeparator> (DemucsSeparator::findDefaultSettings()));
            MainComponent window (engine, projects, ai);
            const auto id = simulateSeparation (projects, "carpeta-ventana");
            runLoopUntil ([] { return false; }, 100);

            CHECK (! window.isFolderWindowOpen (id), "al principio la ventana de ondas está cerrada");
            window.toggleFolderWindow (id);
            CHECK (window.isFolderWindowOpen (id), "el botón Ondas de la carpeta la abre");
            window.toggleFolderWindow (id);
            CHECK (! window.isFolderWindowOpen (id), "y la vuelve a cerrar");
            window.toggleFolderWindow (id);

            // Sacar una pista de la carpeta no quita su onda; eliminarla, sí.
            auto stems = projects.getStemTracks (id);
            projects.moveTrackToFolder (stems[0], {}, 0);
            runLoopUntil ([] { return false; }, 200);
            CHECK (window.isFolderWindowOpen (id), "sacar una pista de la carpeta no cierra la ventana");

            projects.removeTrack (*stems[1]);
            runLoopUntil ([] { return false; }, 200);
            CHECK (window.isFolderWindowOpen (id), "eliminar una pista deja la ventana abierta con las demás");

            for (const auto& stem : projects.getStemTracks (id))
                projects.removeTrack (*stem);

            runLoopUntil ([&] { return ! window.isFolderWindowOpen (id); }, 1000);
            CHECK (! window.isFolderWindowOpen (id), "sin ninguna pista de la separación, la ventana se cierra");
            window.toggleFolderWindow (id);
            CHECK (! window.isFolderWindowOpen (id), "y ya no se puede abrir");

            projects.undo();
            runLoopUntil ([] { return false; }, 100);
            window.toggleFolderWindow (id);
            CHECK (window.isFolderWindowOpen (id), "al deshacer (vuelve una pista) se puede abrir otra vez");
        }

        section ("Cada onda sigue a su pista");

        {
            std::vector<SeparationView::Stem> stems;

            for (const auto& id : fourStems)
                stems.push_back ({ stemDisplayName (id), trackColourFor (stemDisplayName (id), 0) });

            SeparationView view ("Mi canción"_u8, juce::Colours::skyblue, stems);
            std::vector<bool> exists (4, true);
            view.isStemPresent = [&] (int index) { return (bool) exists[(size_t) index]; };
            view.setFinished (true);
            view.advance (1.0);
            CHECK (view.getNumVisibleStems() == 4, "al terminar se ven las 4 ondas");

            exists[1] = false;
            view.advance (0.5);
            CHECK (view.getNumVisibleStems() == 3, "si se elimina la pista de la Batería, su onda desaparece");
            saveSnapshot (view.createComponentSnapshot (view.getLocalBounds()), "ondas-sin-bateria.png");

            exists[1] = true;
            view.advance (0.5);
            CHECK (view.getNumVisibleStems() == 4, "si vuelve (deshacer), su onda también");
        }

        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }
}

void runFolderTests()
{
    testFolderModel();
    testFolderList();
    testFolderWindow();
}
}
