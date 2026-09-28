#pragma once

#include <juce_graphics/juce_graphics.h>

namespace stemlab
{
/**
    Carpeta de pistas. La crea la separación por IA con las pistas que genera
    (Voz, Batería...), que se muestran dentro, se despliegan y se pliegan.
    Las pistas se pueden sacar y volver a meter (AudioTrack::getFolderId).

    Guarda también lo necesario para su ventana de ondas: el color y el audio
    de la canción separada (para el anillo de frecuencias) y los stems, en
    orden (una onda por cada uno mientras exista su pista).
*/
struct TrackFolder
{
    juce::String id;
    juce::String name;
    juce::Colour colour;
    bool expanded = true;

    juce::StringArray stems;                // "vocals", "drums"...

    juce::File sourceFile;                  // audio de la canción separada
    double sourceStartSeconds = 0.0;        // tramo que usaba su fragmento
    double sourceLengthSeconds = -1.0;
};
}
