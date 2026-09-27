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
        result.status = juce::Result::fail ("Separación cancelada."_u8);
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
        { "htdemucs",    "4 pistas: voz, batería, bajo y otros (recomendado)"_u8, fourStems },
        { "htdemucs_ft", "4 pistas, más calidad (unas 4 veces más lento)"_u8, fourStems },
        { "htdemucs_6s", "6 pistas: añade guitarra y piano (experimental)"_u8,
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
        return failure ("No se encontró el script de separación:\n"_u8 + config.scriptFile.getFullPathName());

    if (! request.inputFile.existsAsFile())
        return failure ("No existe el archivo de entrada:\n" + request.inputFile.getFullPathName());

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
    progress.setStatus ("Iniciando Python..."_u8);

    juce::ChildProcess process;

    {
        const juce::ScopedLock lock (processLock);

        if (cancelRequested.load() || progress.shouldCancel())
            return cancellation();

        if (! process.start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
            return failure ("No se pudo ejecutar Python (" + config.pythonCommand + ").\n\n"
                            "Crea el entorno virtual descrito en README.md o define la variable "
                            "de entorno STEMLAB_PYTHON con la ruta a python.exe.");

        activeProcess = &process;
    }

    juce::String scriptError;
    juce::StringArray logTail;

    const auto handleLine = [&] (const juce::String& text)
    {
        if (text.startsWith ("@@PROGRESS "))
            progress.setProgress (text.substring (11).getDoubleValue());
        else if (text.startsWith ("@@STATUS "))
            progress.setStatus (text.substring (9));
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
        return failure ("Python terminó con código "_u8 + juce::String (exitCode) + ":\n\n"
                        + logTail.joinIntoString ("\n"));

    SeparationResult result;
    result.stems = collectStems (request.outputDirectory);

    if (result.stems.empty())
        return failure ("El modelo terminó pero no generó archivos WAV en:\n"_u8 + request.outputDirectory.getFullPathName());

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
