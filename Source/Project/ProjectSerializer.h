#pragma once

#include "Project.h"

#include <vector>

namespace stemlab
{
/** Un clip tal como se guarda: todo en segundos, independiente del sample rate. */
struct ClipDescription
{
    juce::File file;                // ruta absoluta en memoria, relativa en disco
    double startSeconds = 0.0;      // posición en la línea de tiempo (puede ser < 0: se recorta)
    double offsetSeconds = 0.0;     // desde dónde del archivo empieza
    double lengthSeconds = -1.0;    // < 0: hasta el final del archivo
};

/** Descripción de una pista tal como se guarda en el archivo .stemlab. */
struct TrackDescription
{
    juce::String name;
    juce::var state;                        // volumen, paneo, mute, solo, efectos
    std::vector<ClipDescription> clips;     // puede estar vacía (pista sin audio)
};

/** Contenido de un proyecto que no está en Project (pistas y master). */
struct ProjectDocument
{
    std::vector<TrackDescription> tracks;
    float masterVolumeDb = 0.0f;
};

/**
    Lectura y escritura del archivo de proyecto (.stemlab, o el antiguo
    project.json: es el mismo JSON). No depende del motor de audio: recibe
    y devuelve descripciones, así es fácil de probar y de versionar.

    Versión 1: una pista = un archivo + inicio.
    Versión 2: una pista = lista de clips (se siguen leyendo los de la v1).
*/
struct ProjectSerializer
{
    static constexpr int currentVersion = 2;

    /** El JSON que se guardaría (también sirve para detectar cambios sin guardar). */
    static juce::String toJson (const Project& project, const ProjectDocument& document);

    static juce::Result write (const Project& project, const ProjectDocument& document);
    static juce::Result read (const juce::File& projectFile, Project& project, ProjectDocument& document);
};
}
