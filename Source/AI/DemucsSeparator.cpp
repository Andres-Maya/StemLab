#include "DemucsSeparator.h"

#include "Utils/PythonEnvironment.h"
#include "Utils/Strings.h"

#include <algorithm>
#include <string>

namespace stemlab
{
namespace
{
    constexpr auto scriptName = "stemlab_separate.py";

    SeparationResult failure (const juce::String& message)
    {
        SeparationResult result;
        result.status = juce::Result::fail (message);
        return result;
    }

    SeparationResult cancellation()
    {
        SeparationResult result;
        result.cancelled = true;
        result.status = juce::Result::fail (tr ("Separación cancelada."));
        return result;
    }
}

DemucsSeparator::Settings DemucsSeparator::findDefaultSettings()
{
    const auto environment = PythonEnvironment::find (scriptName);

    Settings result;
    result.scriptFile = environment.getScript (scriptName);
    result.pythonCommand = environment.pythonCommand;
    return result;
}

DemucsSeparator::DemucsSeparator (Settings initialSettings)
    : settings (std::move (initialSettings))
{
}

std::vector<AudioSeparator::ModelInfo> DemucsSeparator::getAvailableModels() const
{
    const juce::StringArray fourStems { "vocals", "drums", "bass", "other" };

    return {
        { "htdemucs",    tr ("4 pistas: voz, batería, bajo y otros (recomendado)"), fourStems },
        { "htdemucs_ft", tr ("4 pistas, más calidad (unas 4 veces más lento)"), fourStems },
        { "htdemucs_6s", tr ("6 pistas: añade guitarra y piano (experimental)"),
          { "vocals", "drums", "bass", "guitar", "piano", "other" } },
    };
}

juce::String DemucsSeparator::getCurrentModel() const
{
    const juce::ScopedLock lock (settingsLock);
    return settings.model;
}

void DemucsSeparator::setCurrentModel (const juce::String& modelId)
{
    const juce::ScopedLock lock (settingsLock);
    settings.model = modelId;
}

DemucsSeparator::Settings DemucsSeparator::getSettings() const
{
    const juce::ScopedLock lock (settingsLock);
    return settings;
}

//==============================================================================
SeparationResult DemucsSeparator::separate (const SeparationRequest& request, SeparationProgress& progress)
{
    cancelRequested.store (false);
    const auto config = getSettings();

    if (! config.scriptFile.existsAsFile())
        return failure (tr ("No se encontró el script de separación:\n{0}", config.scriptFile.getFullPathName()));

    if (! request.inputFile.existsAsFile())
        return failure (tr ("No existe el archivo de entrada:\n{0}", request.inputFile.getFullPathName()));

    if (const auto created = request.outputDirectory.createDirectory(); created.failed())
        return failure (created.getErrorMessage());

    // -u: sin buffer en stdout (el progreso llega al momento).
    // -X utf8: rutas y mensajes con acentos llegan bien por la tubería.
    juce::StringArray args;
    args.add (config.pythonCommand);
    args.add ("-u");
    args.add ("-X");
    args.add ("utf8");
    args.add (config.scriptFile.getFullPathName());
    args.add ("--input");
    args.add (request.inputFile.getFullPathName());
    args.add ("--output");
    args.add (request.outputDirectory.getFullPathName());
    args.add ("--model");
    args.add (config.model);
    args.add ("--device");
    args.add (config.device);
    args.add ("--shifts");
    args.add (juce::String (config.shifts));

    progress.setProgress (-1.0);
    progress.setStatus (tr ("Iniciando Python..."));

    juce::ChildProcess process;

    {
        const juce::ScopedLock lock (processLock);

        if (cancelRequested.load() || progress.shouldCancel())
            return cancellation();

        if (! process.start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
            return failure (tr ("No se pudo ejecutar Python ({0}).\n\n"
                                "Crea el entorno virtual descrito en README.md o define la variable "
                                "de entorno STEMLAB_PYTHON con la ruta a python.exe.", config.pythonCommand));

        activeProcess = &process;
    }

    juce::String scriptError;
    juce::StringArray logTail;

    // El script escribe sus mensajes en español: con estas plantillas se
    // muestran en el idioma de la interfaz.
    const juce::StringArray scriptMessages {
        msg ("Cargando PyTorch..."),
        msg ("Cargando modelo {0} en {1} (la primera vez se descarga)..."),
        msg ("Leyendo {0}..."),
        msg ("Separando instrumentos ({0})..."),
        msg ("Guardando pistas..."),
    };

    const auto handleLine = [&] (const juce::String& text)
    {
        if (text.startsWith ("@@PROGRESS "))
            progress.setProgress (text.substring (11).getDoubleValue());
        else if (text.startsWith ("@@STATUS "))
            progress.setStatus (Localisation::translateMatching (text.substring (9), scriptMessages));
        else if (text.startsWith ("@@ERROR "))
            scriptError = text.substring (8).replace ("\\n", "\n");
        else if (text.startsWith ("@@"))
            return;     // @@STEM / @@DONE: informativos
        else if (text.isNotEmpty())
        {
            DBG ("[demucs] " << text);
            logTail.add (text);

            if (logTail.size() > 20)
                logTail.remove (0);
        }
    };

    // readProcessOutput() espera hasta llenar el buffer pedido, así que se lee
    // byte a byte para procesar cada línea en cuanto llega. La salida es corta
    // (unas pocas líneas por segundo), el coste es despreciable.
    std::string line;
    char c = 0;

    while (process.readProcessOutput (&c, 1) == 1)
    {
        if (c == '\n' || c == '\r')
        {
            if (! line.empty())
                handleLine (juce::String::fromUTF8 (line.data(), static_cast<int> (line.size())).trimEnd());

            line.clear();
        }
        else
        {
            line.push_back (c);
        }

        if (progress.shouldCancel() && ! cancelRequested.load())
            cancel();
    }

    if (! line.empty())
        handleLine (juce::String::fromUTF8 (line.data(), static_cast<int> (line.size())).trimEnd());

    {
        const juce::ScopedLock lock (processLock);
        activeProcess = nullptr;
    }

    process.waitForProcessToFinish (10000);

    // Al matar el proceso en Windows el código de salida puede ser 0: se mira
    // primero si fue una cancelación.
    if (cancelRequested.load() || progress.shouldCancel())
        return cancellation();

    const auto exitCode = process.getExitCode();

    if (scriptError.isNotEmpty())
        return failure (scriptError);

    if (exitCode != 0)
        return failure (tr ("Python terminó con código {0}:\n\n{1}", exitCode, logTail.joinIntoString ("\n")));

    SeparationResult result;
    result.stems = collectStems (request.outputDirectory);

    if (result.stems.empty())
        return failure (tr ("El modelo terminó pero no generó archivos WAV en:\n{0}", request.outputDirectory.getFullPathName()));

    return result;
}

void DemucsSeparator::cancel()
{
    cancelRequested.store (true);

    const juce::ScopedLock lock (processLock);

    if (activeProcess != nullptr)
        activeProcess->kill();
}

std::vector<Stem> DemucsSeparator::collectStems (const juce::File& folder)
{
    std::vector<Stem> stems;

    for (const auto& file : folder.findChildFiles (juce::File::findFiles, false, "*.wav"))
        stems.push_back ({ file.getFileNameWithoutExtension(), file });

    std::sort (stems.begin(), stems.end(), [] (const Stem& a, const Stem& b)
    {
        return stemSortOrder (a.name) < stemSortOrder (b.name);
    });

    return stems;
}
}
