#pragma once

#include <juce_core/juce_core.h>

namespace stemlab
{
/**
    Dónde están los scripts de Python de StemLab y con qué intérprete se
    ejecutan. Lo usan la separación por IA y la exportación a MP3.

    Carpeta de scripts:
      1. "python" junto al ejecutable (instalación).
      2. python/ del repositorio (desarrollo; la define CMake).

    Intérprete:
      1. Variable de entorno STEMLAB_PYTHON.
      2. El entorno virtual .venv de esa carpeta (desarrollo, ver README.md).
      3. El Python autónomo runtime/ de esa carpeta, con los paquetes instalados
         dentro (el que trae el instalador de Windows).
      4. "python" del PATH.
*/
struct PythonEnvironment
{
    juce::String pythonCommand;
    juce::File scriptsFolder;

    /** Busca la carpeta que contiene el script indicado. */
    static PythonEnvironment find (const juce::String& scriptName);

    /** El intérprete de una carpeta de scripts: los pasos 2 a 4 de arriba
        (STEMLAB_PYTHON lo mira find()). */
    static juce::String interpreterIn (const juce::File& scriptsFolder);

    juce::File getScript (const juce::String& scriptName) const   { return scriptsFolder.getChildFile (scriptName); }

    /** Argumentos para ejecutar un script: python -u -X utf8 script.
        -u: sin buffer en stdout (el progreso llega al momento).
        -X utf8: rutas y mensajes con acentos llegan bien por la tubería. */
    juce::StringArray commandFor (const juce::String& scriptName) const;
};
}
