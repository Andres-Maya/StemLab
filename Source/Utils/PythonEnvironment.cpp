#include "PythonEnvironment.h"

namespace stemlab
{
PythonEnvironment PythonEnvironment::find (const juce::String& scriptName)
{
    juce::Array<juce::File> candidates;
    candidates.add (juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                        .getParentDirectory().getChildFile ("python"));
   #ifdef STEMLAB_PYTHON_DIR
    candidates.add (juce::File (juce::String::fromUTF8 (STEMLAB_PYTHON_DIR)));
   #endif

    PythonEnvironment result;
    result.scriptsFolder = candidates.getLast();

    for (const auto& folder : candidates)
    {
        if (folder.getChildFile (scriptName).existsAsFile())
        {
            result.scriptsFolder = folder;
            break;
        }
    }

    const auto fromEnvironment = juce::SystemStats::getEnvironmentVariable ("STEMLAB_PYTHON", {});
    result.pythonCommand = fromEnvironment.isNotEmpty() ? fromEnvironment : interpreterIn (result.scriptsFolder);
    return result;
}

juce::String PythonEnvironment::interpreterIn (const juce::File& scriptsFolder)
{
    // Primero el entorno virtual (desarrollo) y después el Python autónomo con
    // los paquetes dentro (instalación): en el repositorio, runtime/ es solo la
    // base del entorno virtual y no tiene Demucs.
   #if JUCE_WINDOWS
    const juce::StringArray candidates { ".venv/Scripts/python.exe", "runtime/python.exe" };
    const juce::String systemPython ("python");
   #else
    const juce::StringArray candidates { ".venv/bin/python3", "runtime/bin/python3" };
    const juce::String systemPython ("python3");
   #endif

    for (const auto& candidate : candidates)
        if (const auto python = scriptsFolder.getChildFile (candidate); python.existsAsFile())
            return python.getFullPathName();

    return systemPython;
}

juce::StringArray PythonEnvironment::commandFor (const juce::String& scriptName) const
{
    juce::StringArray args;
    args.add (pythonCommand);
    args.add ("-u");
    args.add ("-X");
    args.add ("utf8");
    args.add (getScript (scriptName).getFullPathName());
    return args;
}
}
