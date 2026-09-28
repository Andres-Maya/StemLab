#include "Tests.h"
#include "TestSupport.h"

#include "AI/AIProcessManager.h"
#include "AI/DemucsSeparator.h"
#include "Application/FileAssociation.h"
#include "Audio/AudioEngine.h"
#include "Project/ProjectManager.h"
#include "UI/MainComponent.h"
#include "UI/StemLabLookAndFeel.h"
#include "Utils/Strings.h"

#include <algorithm>

// Archivos de proyecto .stemlab: dónde se guardan, cómo se abren, proyectos
// antiguos (project.json), "Abrir reciente" y las sesiones temporales.

namespace stemlab::test
{
namespace
{
    bool openAndWait (ProjectManager& projects, const juce::File& file, juce::Result& result)
    {
        auto done = false;
        projects.openProject (file, [&] (juce::Result r) { result = r; done = true; });
        runLoopUntil ([&] { return done; }, 10000);
        return done;
    }

    void testStemlabFiles()
    {
        section ("Proyectos .stemlab: guardar y abrir");

        const auto base = outputFolder().getChildFile ("Proyectos");
        base.deleteRecursively();
        base.createDirectory();

        // Carpeta de "Guardar como" según el archivo elegido en el diálogo.
        CHECK (ProjectManager::folderForSaveAs (base.getChildFile ("MiCancion.stemlab")) == base.getChildFile ("MiCancion"),
               "elegir Proyectos/MiCancion.stemlab guarda en Proyectos/MiCancion/");
        CHECK (ProjectManager::folderForSaveAs (base.getChildFile ("MiCancion/MiCancion.stemlab")) == base.getChildFile ("MiCancion"),
               "elegirlo dentro de su propia carpeta no crea otra carpeta dentro");
        CHECK (ProjectManager::folderForSaveAs (base.getChildFile ("MiCancion")) == base.getChildFile ("MiCancion"),
               "sin escribir la extensión funciona igual");

        AudioEngine engine;
        ProjectManager projects (engine);
        const auto wav = writeToneWav (outputFolder().getChildFile ("proyecto-tono.wav"), 44100.0, 2, 1.0, 330.0);
        auto done = false;
        projects.importAudio ({ wav }, [&] (juce::Result) { done = true; });
        runLoopUntil ([&] { return done; }, 10000);
        engine.getMixer().getTracks().front()->getVolume().set (-4.5f);
        projects.setBpm (101.0);

        const auto folder = ProjectManager::folderForSaveAs (base.getChildFile ("MiCancion.stemlab"));
        CHECK (projects.saveAs (folder).wasOk(), "Guardar como");

        const auto file = folder.getChildFile ("MiCancion.stemlab");
        CHECK (file.existsAsFile() && ! folder.getChildFile ("project.json").exists(), "se crea MiCancion/MiCancion.stemlab (sin project.json)");
        CHECK (folder.getChildFile ("audio/proyecto-tono.wav").existsAsFile() && folder.getChildFile ("recordings").isDirectory(),
               "con el audio copiado en audio/ y las demás carpetas");
        CHECK (projects.getProject().getProjectFile() == file && projects.getProject().getName() == "MiCancion"
                   && ! projects.getProject().isTemporary(),
               "el proyecto abierto pasa a ser ese archivo");
        CHECK (file.loadFileAsString().contains ("\"stemlab-project\"") && file.loadFileAsString().contains ("\"audio/proyecto-tono.wav\""),
               "el .stemlab es el JSON del proyecto, con rutas relativas");

        // Guardar (Ctrl+S) escribe en el mismo archivo.
        projects.setBpm (102.0);
        CHECK (projects.save().wasOk() && file.loadFileAsString().contains ("102"), "Guardar sobrescribe el .stemlab");

        // Abrir el archivo directamente.
        projects.newProject();
        juce::Result result = juce::Result::ok();
        CHECK (openAndWait (projects, file, result) && result.wasOk(), "abrir MiCancion.stemlab " << result.getErrorMessage());
        const auto& tracks = engine.getMixer().getTracks();
        CHECK (tracks.size() == 1 && tracks.front()->hasClips() && near (tracks.front()->getVolume().get(), -4.5, 1e-4)
                   && near (projects.getProject().getBpm(), 102.0, 1e-9) && projects.getProject().getProjectFile() == file,
               "se recuperan pistas, audio, volumen y BPM");
        CHECK (! projects.hasUnsavedChanges(), "recién abierto: sin cambios");

        // Abrir la carpeta también encuentra el .stemlab.
        projects.newProject();
        CHECK (openAndWait (projects, folder, result) && result.wasOk() && projects.getProject().getProjectFile() == file,
               "abrir la carpeta abre el .stemlab que contiene");

        // El proyecto se puede mover de sitio entero (rutas relativas).
        projects.newProject();
        const auto moved = base.getChildFile ("Movido");
        folder.copyDirectoryTo (moved);
        CHECK (openAndWait (projects, moved.getChildFile ("MiCancion.stemlab"), result) && result.wasOk()
                   && engine.getMixer().getTracks().front()->getSourceFile().isAChildOf (moved),
               "una copia de la carpeta en otro sitio usa su propio audio");

        // Errores claros.
        projects.newProject();
        const auto empty = base.getChildFile ("Vacia");
        empty.createDirectory();
        CHECK (openAndWait (projects, empty, result) && result.failed() && result.getErrorMessage().contains (".stemlab"),
               "una carpeta sin proyecto: " << result.getErrorMessage());
        const auto notProject = base.getChildFile ("otra-cosa.stemlab");
        notProject.replaceWithText ("{ \"format\": \"otra-app\" }");
        CHECK (openAndWait (projects, notProject, result) && result.failed() && projects.getProject().isTemporary(),
               "un archivo que no es de StemLab no se abre: " << result.getErrorMessage());

        CHECK (Project::isProjectFile (file) && Project::isProjectFile (base.getChildFile ("x/project.json"))
                   && ! Project::isProjectFile (wav),
               "reconoce .stemlab y el antiguo project.json (para abrir al arrastrar)");

        section ("Proyectos antiguos (project.json)");

        const auto legacy = base.getChildFile ("Antiguo");
        legacy.createDirectory();
        wav.copyFileTo (legacy.getChildFile ("tono.wav"));
        legacy.getChildFile ("project.json").replaceWithText (
            "{ \"format\": \"stemlab-project\", \"version\": 2, \"name\": \"Antiguo\", \"bpm\": 90, \"masterVolume\": 0,"
            "  \"tracks\": [ { \"name\": \"Tono\", \"state\": {}, \"clips\": [ { \"file\": \"tono.wav\", \"start\": 0, \"offset\": 0, \"length\": -1 } ] } ] }");

        CHECK (openAndWait (projects, legacy, result) && result.wasOk() && engine.getMixer().getTracks().size() == 1
                   && projects.getProject().getProjectFile().getFileName() == "project.json",
               "una carpeta con project.json se sigue abriendo");
        CHECK (projects.save().wasOk() && legacy.getChildFile ("project.json").loadFileAsString().contains ("Tono"),
               "Guardar lo mantiene en project.json");

        CHECK (projects.saveAs (legacy).wasOk() && legacy.getChildFile ("Antiguo.stemlab").existsAsFile()
                   && ! legacy.getChildFile ("project.json").exists(),
               "Guardar como en su misma carpeta lo convierte en Antiguo.stemlab (y quita project.json)");

        projects.newProject();
        base.deleteRecursively();
    }

    void testRecentProjects()
    {
        section ("Abrir reciente");

        StemLabLookAndFeel lookAndFeel;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        {
            const auto base = outputFolder().getChildFile ("Recientes");
            base.deleteRecursively();

            juce::PropertiesFile::Options options;
            options.millisecondsBeforeSaving = -1;
            juce::PropertiesFile settings (base.getChildFile ("ajustes.settings"), options);

            AudioEngine engine;
            ProjectManager projects (engine);
            AIProcessManager ai (std::make_unique<DemucsSeparator> (DemucsSeparator::findDefaultSettings()));

            // Dos proyectos guardados.
            projects.addEmptyTrack ("Pista");
            projects.saveAs (base.getChildFile ("Uno"));
            const auto one = projects.getProject().getProjectFile();
            projects.newProject();
            projects.addEmptyTrack ("Pista");
            projects.saveAs (base.getChildFile ("Dos"));
            const auto two = projects.getProject().getProjectFile();
            projects.newProject();

            {
                MainComponent window (engine, projects, ai, &settings);
                window.openProjectFile (one);
                runLoopUntil ([&] { return projects.getProject().getProjectFile() == one && ! projects.isLoading(); }, 10000);
                window.openProjectFile (two);
                runLoopUntil ([&] { return projects.getProject().getProjectFile() == two && ! projects.isLoading(); }, 10000);
                runLoopUntil ([] { return false; }, 100);

                CHECK (projects.getProject().getProjectFile() == two, "openProjectFile abre el proyecto (como al soltarlo en la ventana)");

                juce::RecentlyOpenedFilesList recent;
                recent.restoreFromString (settings.getValue ("recentProjects"));
                CHECK (recent.getNumFiles() == 2 && recent.getFile (0) == two && recent.getFile (1) == one,
                       "los proyectos abiertos quedan en 'Abrir reciente', el último primero");

                const auto fileMenu = window.getMenuForIndex (0, "Archivo");
                auto foundRecent = false;

                for (juce::PopupMenu::MenuItemIterator it (fileMenu, true); it.next();)
                    foundRecent = foundRecent || it.getItem().text == two.getFullPathName();

                CHECK (foundRecent, "el menú Archivo > Abrir reciente los muestra");
            }

            // Una ventana nueva (otra sesión de StemLab) recuerda la lista.
            MainComponent later (engine, projects, ai, &settings);
            const auto fileMenu = later.getMenuForIndex (0, "Archivo");
            auto count = 0;

            for (juce::PopupMenu::MenuItemIterator it (fileMenu, true); it.next();)
                if (it.getItem().text == one.getFullPathName() || it.getItem().text == two.getFullPathName())
                    ++count;

            CHECK (count == 2, "la lista se conserva entre sesiones (ajustes del usuario)");

            projects.newProject();
            base.deleteRecursively();
        }

        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void testFileAssociationValues()
    {
        section ("Asociación de .stemlab en Windows (sin escribir en el registro)");

        const juce::File exe ("C:/Program Files/StemLab/StemLab.exe");     // con espacios
        const juce::File icon ("C:/Users/Ana María/AppData/Local/StemLab/Icons/StemLab-0123456789ab.ico");
        const auto values = FileAssociation::registryValuesFor (exe, icon);
        const auto valueOf = [&values] (const juce::String& path)
        {
            for (const auto& [key, value] : values)
                if (key == path)
                    return value;

            return "(no está)"_u8;
        };

        const juce::String classes ("HKEY_CURRENT_USER\\Software\\Classes\\");
        const auto quotedExe = "\"" + exe.getFullPathName() + "\"";
        CHECK (values.size() == 4 && std::all_of (values.begin(), values.end(), [&] (const auto& v) { return v.first.startsWith (classes); }),
               "solo en el registro del usuario actual (sin administrador)");
        CHECK (valueOf (classes + ".stemlab\\") == "StemLab.Project", ".stemlab -> StemLab.Project");
        CHECK (valueOf (classes + "StemLab.Project\\DefaultIcon\\") == "\"" + icon.getFullPathName() + "\"",
               "icono: el .ico propio de los proyectos, entre comillas");
        CHECK (valueOf (classes + "StemLab.Project\\shell\\open\\command\\") == quotedExe + " \"%1\"",
               "doble clic: la ruta va entre comillas aunque tenga espacios");

        section ("Icono de los proyectos .stemlab");

        const auto folder = outputFolder().getChildFile ("Iconos");
        folder.deleteRecursively();
        folder.createDirectory();
        const auto oldIcon = folder.getChildFile ("StemLab-viejo.ico");
        oldIcon.replaceWithText ("icono de otra versión");

        const auto written = FileAssociation::writeProjectIcon (folder);
        CHECK (written.existsAsFile() && written.getFileName().startsWith ("StemLab-") && written.hasFileExtension (".ico"),
               "se escribe como " << written.getFileName());
        CHECK (! oldIcon.exists(), "y borra el icono de la versión anterior");

        juce::MemoryBlock data;
        written.loadFileAsData (data);
        const auto* bytes = static_cast<const juce::uint8*> (data.getData());
        const auto count = data.getSize() >= 6 ? bytes[4] | (bytes[5] << 8) : 0;
        CHECK (data.getSize() > 6 && bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 1 && count == 10,
               "es un .ico con 10 tamaños, de 16 a 256 px (" << count << ")");

        const auto modified = written.getLastModificationTime();
        juce::Thread::sleep (20);
        CHECK (FileAssociation::writeProjectIcon (folder) == written && written.getLastModificationTime() == modified,
               "la segunda vez no lo reescribe (mismo nombre, mismo archivo)");

        folder.deleteRecursively();
    }

    void testTemporarySessions()
    {
        section ("Sesiones temporales: se borran al dejarlas");

        const auto base = outputFolder().getChildFile ("Sesiones");
        base.deleteRecursively();
        base.createDirectory();

        juce::File firstSession;

        {
            AudioEngine engine;
            ProjectManager projects (engine);
            firstSession = projects.getProject().getDirectory();
            CHECK (projects.getProject().isTemporary() && firstSession.isDirectory()
                       && firstSession.getFileName().startsWith ("Sesion-"),
                   "un proyecto nuevo vive en %TEMP%/StemLab/Sesion-...");

            // Algo de audio en la sesión, como una grabación sin guardar.
            const auto take = writeConstantWav (projects.getProject().getRecordingsDirectory().getChildFile ("Grabacion.wav"),
                                                44100.0, 0.2, 0.1f);

            // (La sesión nueva puede reutilizar el nombre si es en el mismo segundo.)
            projects.newProject();
            const auto secondSession = projects.getProject().getDirectory();
            CHECK (! take.exists() && secondSession.isDirectory()
                       && secondSession.getNumberOfChildFiles (juce::File::findFiles, "*") == 0,
                   "Nuevo proyecto borra la sesión anterior sin guardar (y su grabación)");

            // Guardar una sesión la convierte en proyecto: ese ya no se borra.
            projects.addEmptyTrack ("Pista");
            const auto saved = base.getChildFile ("Guardado");
            CHECK (projects.saveAs (saved).wasOk() && ! secondSession.exists(), "Guardar como se lleva la sesión");

            projects.newProject();
            const auto thirdSession = projects.getProject().getDirectory();
            CHECK (saved.getChildFile ("Guardado.stemlab").existsAsFile(), "Nuevo proyecto no toca un proyecto guardado");

            auto done = false;
            projects.openProject (saved, [&] (juce::Result) { done = true; });
            runLoopUntil ([&] { return done; }, 10000);
            CHECK (done && ! thirdSession.exists() && saved.isDirectory(), "abrir otro proyecto borra la sesión sin guardar");

            projects.newProject();
            firstSession = projects.getProject().getDirectory();

            // Un proyecto que no se puede abrir deja la sesión como estaba.
            done = false;
            projects.openProject (base.getChildFile ("NoExiste"), [&] (juce::Result) { done = true; });
            CHECK (done && firstSession.isDirectory() && projects.getProject().isTemporary(),
                   "si no se pudo abrir, la sesión sigue ahí");
        }

        CHECK (! firstSession.exists(), "cerrar StemLab borra la sesión sin guardar");
        base.deleteRecursively();
    }
}

void runProjectFileTests()
{
    testStemlabFiles();
    testFileAssociationValues();
    testRecentProjects();
    testTemporarySessions();
}
}
