#pragma once

#include "Project.h"

#include <vector>

namespace stemlab
{
/** Descripción de una pista tal como se guarda en project.json. */
struct TrackDescription
{
    juce::String name;
    juce::File file;                // ruta absoluta en memoria, relativa en disco
    double startSeconds = 0.0;      // en segundos: independiente del sample rate
    juce::var state;                // volumen, paneo, mute, solo, efectos
};

/** Contenido de un proyecto que no está en Project (pistas y master). */
struct ProjectDocument
{
    std::vector<TrackDescription> tracks;
    float masterVolumeDb = 0.0f;
};

/**
    Lectura y escritura de project.json. No depende del motor de audio: recibe
    y devuelve descripciones, así es fácil de probar y de versionar.
*/
struct ProjectSerializer
{
    static constexpr int currentVersion = 1;

    static juce::Result write (const Project& project, const ProjectDocument& document);
    static juce::Result read (const juce::File& projectFile, Project& project, ProjectDocument& document);
};
}
