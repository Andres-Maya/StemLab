#pragma once

#include <juce_core/juce_core.h>

namespace stemlab
{
/**
    Metadatos y carpeta de un proyecto:

        MiProyecto/
            MiProyecto.stemlab   el proyecto (JSON); se abre con Archivo > Abrir
            audio/          archivos importados (copias)
            stems/          resultados de la separación por IA
            recordings/     grabaciones
            exports/        mezclas exportadas

    El audio nunca va dentro del JSON: el JSON guarda rutas relativas a esta
    carpeta, así el proyecto se puede mover o copiar entero.

    Los proyectos antiguos se guardaban en "project.json": se siguen abriendo,
    y al "Guardar como" en su misma carpeta pasan a ser un .stemlab.
*/
class Project
{
public:
    /** Extensión de los archivos de proyecto. */
    static constexpr auto fileExtension = ".stemlab";

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

    /** El archivo que se lee y se escribe (por defecto <carpeta>/<carpeta>.stemlab). */
    const juce::File& getProjectFile() const noexcept   { return projectFile; }
    void setProjectFile (juce::File newFile)            { projectFile = std::move (newFile); }

    /** <carpeta>/<nombre de la carpeta>.stemlab */
    static juce::File projectFileFor (const juce::File& folder);

    /** El proyecto que hay dentro de una carpeta: <carpeta>.stemlab, otro
        .stemlab o el antiguo project.json. Vacío si no hay ninguno. */
    static juce::File findProjectFileIn (const juce::File& folder);

    /** ¿Es un archivo de proyecto de StemLab (.stemlab o el antiguo project.json)? */
    static bool isProjectFile (const juce::File& file);
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
    juce::File projectFile;
    bool temporary = false;
    double bpm = 120.0;
};
}
