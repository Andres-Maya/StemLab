#pragma once

#include <juce_core/juce_core.h>

namespace stemlab
{
/**
    Metadatos y carpeta de un proyecto:

        MiProyecto/
            project.json
            audio/          archivos importados (copias)
            stems/          resultados de la separación por IA
            recordings/     grabaciones
            exports/        mezclas exportadas

    El audio nunca va dentro del JSON: el JSON guarda rutas relativas a esta
    carpeta, así el proyecto se puede mover o copiar entero.
*/
class Project
{
public:
    Project() = default;
    Project (juce::String name, juce::File directory, bool isTemporary);

    const juce::String& getName() const noexcept        { return name; }
    void setName (juce::String newName)                 { name = std::move (newName); }

    const juce::File& getDirectory() const noexcept     { return directory; }
    void setDirectory (juce::File newDirectory)         { directory = std::move (newDirectory); }

    /** Sesión sin guardar (vive en la carpeta temporal hasta "Guardar como"). */
    bool isTemporary() const noexcept                   { return temporary; }
    void setTemporary (bool shouldBeTemporary)          { temporary = shouldBeTemporary; }

    double getBpm() const noexcept                      { return bpm; }
    void setBpm (double newBpm)                         { bpm = juce::jlimit (20.0, 300.0, newBpm); }

    juce::File getProjectFile() const                   { return directory.getChildFile ("project.json"); }
    juce::File getAudioDirectory() const                { return directory.getChildFile ("audio"); }
    juce::File getStemsDirectory() const                { return directory.getChildFile ("stems"); }
    juce::File getRecordingsDirectory() const           { return directory.getChildFile ("recordings"); }
    juce::File getExportsDirectory() const              { return directory.getChildFile ("exports"); }

    static juce::StringArray getSubfolderNames()        { return juce::StringArray ("audio", "stems", "recordings", "exports"); }

    juce::Result createFolderStructure() const;

    /** Ruta relativa con '/' si el archivo está dentro del proyecto; absoluta si no. */
    juce::String toStoredPath (const juce::File& file) const;
    juce::File fromStoredPath (const juce::String& storedPath) const;

private:
    juce::String name;
    juce::File directory;
    bool temporary = false;
    double bpm = 120.0;
};
}
