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

   #if JUCE_WINDOWS
    const auto venvPython = result.scriptsFolder.getChildFile (".venv/Scripts/python.exe");
    const juce::String systemPython ("python");
   #else
    const auto venvPython = result.scriptsFolder.getChildFile (".venv/bin/python3");
    const juce::String systemPython ("python3");
   #endif

    const auto fromEnvironment = juce::SystemStats::getEnvironmentVariable ("STEMLAB_PYTHON", {});

    if (fromEnvironment.isNotEmpty())
        result.pythonCommand = fromEnvironment;
    else if (venvPython.existsAsFile())
        result.pythonCommand = venvPython.getFullPathName();
    else
        result.pythonCommand = systemPython;

    return result;
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
