// Pruebas automáticas de StemLab.
//
//   StemLabTests                 pruebas rápidas (sin tarjeta de sonido ni Python)
//   StemLabTests --device        + tarjeta de sonido real (graba unos segundos)
//   StemLabTests --python        + Python: MP3 y separación con Demucs (lento)
//   StemLabTests --all           todo lo anterior
//   StemLabTests --acoustic      solo la prueba acústica (suena ruido por los altavoces)
//   --output <carpeta>           dónde dejar WAV, proyectos y capturas (por defecto
//                                test-output/ junto al ejecutable)
//
// Devuelve 0 si todo pasó (así lo entiende ctest).

#include <juce_gui_basics/juce_gui_basics.h>

#include "TestSupport.h"
#include "Tests.h"

using namespace stemlab::test;

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    juce::StringArray args;

    for (int i = 1; i < argc; ++i)
        args.add (juce::String::fromUTF8 (argv[i]));

    if (const auto index = args.indexOf ("--output"); index >= 0 && index + 1 < args.size())
        setOutputFolder (juce::File::getCurrentWorkingDirectory().getChildFile (args[index + 1]));

    const auto all = args.contains ("--all");
    std::cout << "Salida de las pruebas: " << outputFolder().getFullPathName() << "\n";

    if (args.contains ("--acoustic"))
    {
        runAcousticTest();
    }
    else
    {
        runUnitTests();
        runEditingTests();
        runTrackTests();
        runFolderTests();
        runProjectFileTests();
        runExportTests();
        runUiTests();

        if (all || args.contains ("--device"))
            runDeviceTests();

        if (all || args.contains ("--python"))
            runPythonTests();
    }

    std::cout << "\nRESULTADO: " << (checks - failures) << "/" << checks << " comprobaciones correctas\n";
    return failures == 0 ? 0 : 1;
}
