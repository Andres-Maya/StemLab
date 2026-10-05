#include "Tests.h"
#include "TestSupport.h"

#include "AI/AIProcessManager.h"
#include "AI/DemucsSeparator.h"
#include "Audio/AudioEngine.h"
#include "Project/ProjectManager.h"
#include "UI/MainComponent.h"
#include "UI/MainWindow.h"
#include "UI/StemLabLookAndFeel.h"
#include "Utils/Strings.h"

#include <cctype>
#include <string>

// Idiomas, temas y tutorial. Las capturas (ventana-*.png, tutorial-*.png)
// quedan en la carpeta de salida para revisarlas a ojo.

namespace stemlab::test
{
namespace
{
    /** Los textos de la interfaz que hay que traducir: el primer argumento de
        tr (...) y msg (...) en Source/ (varios literales seguidos son uno solo). */
    juce::StringArray collectInterfaceTexts (const juce::File& sourceFolder)
    {
        juce::StringArray texts;
        const auto isName = [] (char c) { return std::isalnum (static_cast<unsigned char> (c)) != 0 || c == '_'; };
        const auto isSpace = [] (char c) { return std::isspace (static_cast<unsigned char> (c)) != 0; };

        for (const auto& file : sourceFolder.findChildFiles (juce::File::findFiles, true, "*.cpp;*.h"))
        {
            if (file.getFileName().startsWith ("Translations_"))
                continue;

            const auto source = file.loadFileAsString().toStdString();

            for (size_t position = 0; position + 4 < source.size(); ++position)
            {
                size_t i = 0;

                if (source.compare (position, 2, "tr") == 0)            i = position + 2;
                else if (source.compare (position, 3, "msg") == 0)      i = position + 3;
                else                                                    continue;

                if (position > 0 && isName (source[position - 1]))
                    continue;

                while (i < source.size() && source[i] == ' ')
                    ++i;

                if (i >= source.size() || source[i] != '(')
                    continue;

                ++i;

                while (i < source.size() && isSpace (source[i]))
                    ++i;

                if (i >= source.size() || source[i] != '"')
                    continue;

                std::string text;

                for (;;)
                {
                    ++i;        // después de la comilla que abre

                    while (i < source.size() && source[i] != '"')
                    {
                        if (source[i] == '\\' && i + 1 < source.size())
                        {
                            const auto escaped = source[++i];
                            text += escaped == 'n' ? '\n' : escaped == 't' ? '\t' : escaped;
                        }
                        else
                        {
                            text += source[i];
                        }

                        ++i;
                    }

                    auto next = i + 1;

                    while (next < source.size() && isSpace (source[next]))
                        ++next;

                    if (next >= source.size() || source[next] != '"')
                        break;

                    i = next;
                }

                texts.addIfNotAlreadyThere (juce::String::fromUTF8 (text.c_str()));
            }
        }

        return texts;
    }

    /** Los {0}, {1}... de un texto, ordenados. */
    juce::String placeholdersOf (const juce::String& text)
    {
        juce::StringArray found;

        for (int i = 0; i < 10; ++i)
            if (text.contains ("{" + juce::String (i) + "}"))
                found.add (juce::String (i));

        return found.joinIntoString (",");
    }

    void testLanguages()
    {
        section ("Idiomas: todos los textos de la interfaz tienen traducción");

        // STEMLAB_PYTHON_DIR es <raíz>/python: el código está en <raíz>/Source.
        const auto sourceFolder = juce::File (STEMLAB_PYTHON_DIR).getParentDirectory().getChildFile ("Source");
        const auto texts = collectInterfaceTexts (sourceFolder);
        CHECK (texts.size() > 250, "se encuentran los textos de la interfaz en Source/ (" + juce::String (texts.size()) + ")");

        // Los nombres de los stems no pasan por tr ("...") con un literal: se traducen por su id.
        const juce::StringArray stemNames { "Voz", "Batería"_u8, "Bajo", "Guitarra", "Piano", "Otros" };

        for (const auto& info : Localisation::getLanguages())
        {
            if (info.language == Language::spanish)
                continue;

            const auto& dictionary = Localisation::getDictionary (info.language);
            const juce::String name (info.code);
            juce::StringArray missing, different, unused;

            for (const auto& text : texts)
            {
                const auto found = dictionary.find (text);

                if (found == dictionary.end())
                    missing.add (text);
                else if (placeholdersOf (found->second) != placeholdersOf (text))
                    different.add (text);
            }

            for (const auto& entry : dictionary)
                if (! texts.contains (entry.first) && ! stemNames.contains (entry.first))
                    unused.add (entry.first);

            CHECK (missing.isEmpty(), name + ": todos los textos tienen traducción " + missing.joinIntoString (" | ").substring (0, 300));
            CHECK (different.isEmpty(), name + ": cada traducción usa los mismos {n} que su texto " + different.joinIntoString (" | ").substring (0, 300));
            CHECK (unused.isEmpty(), name + ": no sobran traducciones de textos que ya no existen " + unused.joinIntoString (" | ").substring (0, 300));
        }

        CHECK (Localisation::getLanguage() == Language::spanish && tr ("Añadir pista") == "Añadir pista"_u8,
               "por defecto, español: el texto del código");
        CHECK (tr ("Pista \"{0}\" eliminada. Ctrl+Z la recupera.", juce::String ("Voz")) == "Pista \"Voz\" eliminada. Ctrl+Z la recupera.",
               "los datos variables sustituyen a {0}");
        CHECK (tr ("{0} (copia {1})", juce::String ("Bajo"), 3) == "Bajo (copia 3)", "también los números");

        Localisation::setLanguage (Language::english);
        CHECK (tr ("Añadir pista") == "Add track" && tr ("{0} pistas", 4) == "4 tracks", "inglés");
        CHECK (tr (juce::String ("Umbral")) == "Threshold" && tr (msg ("Saturación")) == "Saturation",
               "los nombres de efectos y parámetros se traducen al mostrarlos");
        CHECK (tr ("Mi canción") == "Mi canción"_u8, "un texto sin traducción se queda como está");
        CHECK (stemDisplayName ("vocals") == "Vocals" && stemDisplayName ("drums") == "Drums", "los stems, en el idioma elegido");
        CHECK (trackColourFor ("Vocals", 3) == trackColourFor ("Voz", 0) && trackColourFor ("Recording 2", 1) == trackColourFor ("Grabación 1"_u8, 0),
               "las pistas conservan su color en cualquier idioma");

        const juce::StringArray patterns { msg ("Leyendo {0}..."), msg ("Cargando modelo {0} en {1} (la primera vez se descarga)...") };
        CHECK (Localisation::translateMatching ("Cargando modelo htdemucs en cpu (la primera vez se descarga)...", patterns)
                   == "Loading model htdemucs on cpu (it is downloaded the first time)..."
                   && Localisation::translateMatching ("Leyendo Mi canción.mp3..."_u8, patterns) == "Reading Mi canción.mp3..."_u8,
               "los mensajes del script de Python se traducen por su plantilla");
        CHECK (Localisation::translateMatching ("Algo que el script no avisaba", patterns) == "Algo que el script no avisaba",
               "y uno desconocido se muestra tal cual");

        Localisation::setLanguage (Language::spanish);
        CHECK (Localisation::fromCode ("en-US", Language::spanish) == Language::english
                   && Localisation::fromCode ("es_CO", Language::english) == Language::spanish
                   && Localisation::fromCode ("fr", Language::english) == Language::english
                   && Localisation::fromCode ({}, Language::spanish) == Language::spanish,
               "el idioma guardado o el del sistema: es, en; cualquier otro, el de reserva");
    }

    void testThemes()
    {
        section ("Temas: oscuro y claro");

        const auto pastel = trackColourFor ("Piano", 0);
        CHECK (Palette::getTheme() == Theme::dark && Palette::background == DarkPalette::background && Palette::onBackground (pastel) == pastel,
               "por defecto, el tema oscuro de siempre");

        Palette::setTheme (Theme::light);
        CHECK (Palette::background.getBrightness() > 0.9f && Palette::text.getBrightness() < 0.25f, "claro: fondo claro y texto oscuro");
        CHECK (Palette::text.contrasting().getBrightness() > 0.5f
                   && std::abs (Palette::accent.getPerceivedBrightness() - Palette::panel.getPerceivedBrightness()) > 0.3f,
               "el color de acento se distingue sobre los paneles claros");
        CHECK (Palette::onBackground (pastel).getBrightness() < pastel.getBrightness(), "los colores de las pistas se oscurecen sobre el fondo claro");
        CHECK (Palette::record == juce::Colour (0xffe5484d), "el rojo de grabar es el mismo en los dos temas");

        Palette::setTheme (Theme::dark);
        CHECK (Palette::background == DarkPalette::background && Palette::text == DarkPalette::text, "y se vuelve al oscuro");
    }

    void testWindowAndTour()
    {
        section ("Ventana: idioma, tema y tutorial");

        StemLabLookAndFeel lookAndFeel;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        AudioEngine engine;
        ProjectManager projects (engine);
        AIProcessManager ai (std::make_unique<DemucsSeparator> (DemucsSeparator::findDefaultSettings()));

        {
            MainComponent window (engine, projects, ai);
            window.setVisible (true);
            window.setSize (1280, 820);
            CHECK (window.getMenuBarNames().size() == 7 && window.getMenuBarNames()[5] == "Ver" && window.getMenuBarNames()[6] == "Ayuda",
                   "menús: Archivo, Editar, Proyecto, Audio, IA, Ver y Ayuda");
            CHECK (window.getTour() == nullptr, "el tutorial no se abre al crear la ventana (lo decide el arranque)");
            saveSnapshot (window.createComponentSnapshot (window.getLocalBounds()), "ventana-oscura-es.png");

            // Cambiar de idioma o de tema lo hace quien contiene la ventana (MainWindow).
            int requests = 0;
            auto requestedLanguage = Language::spanish;
            auto requestedTheme = Theme::dark;
            auto requestedTour = true;
            window.onInterfaceChange = [&] (Language language, Theme theme, bool reopenTour)
            {
                ++requests;
                requestedLanguage = language;
                requestedTheme = theme;
                requestedTour = reopenTour;
            };

            window.changeInterface (Language::spanish, Theme::dark);
            CHECK (requests == 0, "elegir el idioma y el tema que ya están no hace nada");
            window.changeInterface (Language::english, Theme::light);
            CHECK (requests == 1 && requestedLanguage == Language::english && requestedTheme == Theme::light && ! requestedTour,
                   "Ver > Tema / Idioma pide volver a crear la interfaz");

            // El tutorial.
            window.showTour();
            auto* tour = window.getTour();
            CHECK (tour != nullptr && tour->getNumSteps() == 12 && tour->getStepIndex() == 0, "Ayuda > Tutorial lo abre en el primer paso");

            if (tour != nullptr)
            {
                CHECK (tour->getBounds() == window.getLocalBounds() && tour->getSpotlightBounds().isEmpty()
                           && tour->getCardBounds().getCentre() == window.getLocalBounds().getCentre(),
                       "el primer paso (bienvenida) no señala nada: tarjeta en el centro");
                saveSnapshot (window.createComponentSnapshot (window.getLocalBounds()), "tutorial-1.png");

                auto inside = true, apart = true, lit = true;

                for (int step = 1; step < tour->getNumSteps(); ++step)
                {
                    tour->next();
                    const auto card = tour->getCardBounds();
                    const auto spot = tour->getSpotlightBounds();
                    inside = inside && window.getLocalBounds().contains (card);
                    lit = lit && ! spot.isEmpty() && window.getLocalBounds().contains (spot);
                    // Fuera de la zona iluminada; si esta ocupa casi toda la ventana
                    // (las pistas) y no cabe fuera, entera dentro.
                    apart = apart && (! card.intersects (spot) || spot.contains (card));

                    if (step == 2 || step == 6 || step == 9)
                        saveSnapshot (window.createComponentSnapshot (window.getLocalBounds()), "tutorial-" + juce::String (step + 1) + ".png");
                }

                CHECK (tour->getStepIndex() == tour->getNumSteps() - 1, "Siguiente recorre todos los pasos");
                CHECK (lit, "cada paso ilumina una zona de la ventana");
                CHECK (inside && apart, "y su tarjeta queda dentro de la ventana, sin tapar el borde de la zona iluminada");

                tour->back();
                CHECK (tour->getStepIndex() == tour->getNumSteps() - 2, "Atrás vuelve al paso anterior");

                // El teclado es del tutorial: los atajos de la aplicación no actúan.
                const auto tracksBefore = engine.getMixer().getTracks().size();
                CHECK (tour->keyPressed (juce::KeyPress ('t', juce::ModifierKeys (juce::ModifierKeys::commandModifier), 0))
                           && engine.getMixer().getTracks().size() == tracksBefore,
                       "con el tutorial abierto Ctrl+T no añade pistas");
                tour->keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
                CHECK (tour->getStepIndex() == tour->getNumSteps() - 1, "la flecha derecha avanza");

                // Elegir idioma en el primer paso: se pide el cambio con el tutorial abierto.
                if (tour->onLanguageChosen != nullptr)
                    tour->onLanguageChosen (Language::english);

                CHECK (requests == 2 && requestedTour, "elegir idioma en el tutorial lo vuelve a abrir en la interfaz nueva");

                tour->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
                runLoopUntil ([&] { return window.getTour() == nullptr; }, 1000);
                CHECK (window.getTour() == nullptr, "Esc cierra el tutorial");
            }
        }

        // La misma ventana en inglés y con el tema claro (como la crea MainWindow tras el cambio).
        Localisation::setLanguage (Language::english);
        Palette::setTheme (Theme::light);
        lookAndFeel.applyTheme();

        // La ventana de ondas de la separación también sigue al tema.
        {
            std::vector<SeparationView::Stem> stems;

            for (const auto* id : { "vocals", "drums", "bass", "other" })
                stems.push_back ({ stemDisplayName (id), trackColourFor (stemDisplayName (id), 0) });

            SeparationView view ("My song", trackColourFor ("My song", 0), stems);
            view.getProgress = [] { return 0.9; };
            view.getStatus = [] { return juce::String ("Separating instruments (cpu)..."); };

            for (int frame = 0; frame < 120; ++frame)
                view.advance (1.0 / 60.0);

            const auto image = view.createComponentSnapshot (view.getLocalBounds());
            saveSnapshot (image, "separacion-clara.png");
            CHECK (view.getNumVisibleStems() == 4 && image.getPixelAt (4, image.getHeight() / 2).getBrightness() > 0.85f,
                   "con el tema claro, la ventana de ondas tiene el fondo claro");
        }

        {
            MainComponent window (engine, projects, ai);
            window.setVisible (true);
            window.setSize (1280, 820);
            CHECK (window.getMenuBarNames() == juce::StringArray ("File", "Edit", "Project", "Audio", "AI", "View", "Help"),
                   "en inglés: File, Edit, Project, Audio, AI, View y Help");

            const auto track = projects.addEmptyTrack (tr ("Pista"));
            runLoopUntil ([] { return false; }, 100);
            CHECK (track->getName() == "Track 1", "las pistas nuevas se llaman Track 1...");
            CHECK (projects.getUndoDescription() == "Añadir pista"_u8 && tr (projects.getUndoDescription()) == "Add track",
                   "las acciones de deshacer se guardan en español y se muestran traducidas");
            saveSnapshot (window.createComponentSnapshot (window.getLocalBounds()), "ventana-clara-en.png");

            window.showTour();
            saveSnapshot (window.createComponentSnapshot (window.getLocalBounds()), "tutorial-claro-en.png");

            if (auto* tour = window.getTour())
            {
                for (int step = 0; step < 6; ++step)
                    tour->next();

                saveSnapshot (window.createComponentSnapshot (window.getLocalBounds()), "tutorial-claro-en-7.png");
                tour->close();
            }

            runLoopUntil ([&] { return window.getTour() == nullptr; }, 1000);
            projects.undo();
        }

        Localisation::setLanguage (Language::spanish);
        Palette::setTheme (Theme::dark);
        lookAndFeel.applyTheme();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    /** El arranque, con la ventana de verdad y un archivo de ajustes (como
        StemLabApplication): sin proyecto se abre el tutorial, siempre. */
    void testStartup()
    {
        section ("Arranque: sin proyecto se abre el tutorial, siempre");

        StemLabLookAndFeel lookAndFeel;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        const auto settingsFile = outputFolder().getChildFile ("arranque.settings");
        settingsFile.deleteFile();

        juce::PropertiesFile::Options options;
        options.applicationName = "StemLabTests";
        options.filenameSuffix = ".settings";

        AudioEngine engine;
        ProjectManager projects (engine);
        AIProcessManager ai (std::make_unique<DemucsSeparator> (DemucsSeparator::findDefaultSettings()));
        const auto tourOf = [] (MainWindow& window) -> TourOverlay*
        {
            auto* content = dynamic_cast<MainComponent*> (window.getContentComponent());
            return content != nullptr ? content->getTour() : nullptr;
        };

        for (int launch = 1; launch <= 2; ++launch)
        {
            juce::PropertiesFile settings (settingsFile, options);
            MainWindow window ("StemLab", engine, projects, ai, &settings);
            CHECK (tourOf (window) == nullptr, "la ventana se crea sin tutorial (aún no se sabe si se abre un proyecto)");

            window.start ({});
            runLoopUntil ([] { return false; }, 800);

            auto* tour = tourOf (window);
            auto* content = window.getContentComponent();
            CHECK (tour != nullptr && tour->isVisible() && tour->getBounds() == content->getLocalBounds(),
                   "arranque " + juce::String (launch) + " sin proyecto: el tutorial, a la vista, ocupa la ventana");

           #if JUCE_WINDOWS    // en la pantalla virtual de Linux (GitHub Actions) la ventana de pruebas no recibe el foco
            CHECK (tour != nullptr && tour->isShowing() && tour->hasKeyboardFocus (true), "y tiene el teclado (Esc, flechas e Intro son suyos)");
           #endif

            if (tour != nullptr)
            {
                if (launch == 1)
                    saveSnapshot (window.createComponentSnapshot (window.getLocalBounds()), "arranque.png");

                tour->close();
                runLoopUntil ([&] { return tourOf (window) == nullptr; }, 1000);
            }

            CHECK (tourOf (window) == nullptr, "Saltar tutorial lo cierra");
            settings.saveIfNeeded();
        }

        // Abriendo un proyecto ("Abrir con", doble clic en un .stemlab), no.
        {
            const auto folder = outputFolder().getChildFile ("Arranque");
            folder.deleteRecursively();
            projects.addEmptyTrack ("Pista");
            CHECK (projects.saveAs (folder).wasOk(), "se guarda un proyecto de prueba");
            const auto projectFile = projects.getProject().getProjectFile();
            projects.newProject();

            juce::PropertiesFile settings (settingsFile, options);
            MainWindow window ("StemLab", engine, projects, ai, &settings);
            window.start (projectFile);
            runLoopUntil ([&] { return ! projects.isLoading() && ! engine.getMixer().getTracks().empty(); }, 5000);
            CHECK (tourOf (window) == nullptr && engine.getMixer().getTracks().size() == 1,
                   "al arrancar con un proyecto se abre el proyecto, sin tutorial");
            projects.newProject();
            folder.deleteRecursively();
        }

        settingsFile.deleteFile();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }
}

void runInterfaceTests()
{
    testLanguages();
    testThemes();
    testWindowAndTour();
    testStartup();
}
}
