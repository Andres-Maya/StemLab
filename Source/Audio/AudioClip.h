#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <memory>
#include <vector>

namespace stemlab
{
/**
    Audio decodificado de un archivo (estéreo, a la frecuencia del motor).

    Es inmutable para el hilo de audio y se comparte entre todos los clips que
    salen del mismo archivo (al dividir un clip, las dos mitades apuntan aquí).
    Solo el hilo de mensajes cambia `file` (al mover el proyecto de carpeta).
*/
struct ClipSource
{
    juce::File file;
    juce::AudioBuffer<float> audio;
    double sampleRate = 44100.0;

    juce::int64 getLength() const noexcept   { return audio.getNumSamples(); }
};

/**
    Un fragmento de audio en la línea de tiempo de una pista.

    La edición es NO destructiva: dividir, recortar o mover solo cambia estos
    números; el archivo de audio nunca se modifica.

        línea de tiempo:  [timelineStart ........ timelineStart + length)
        audio original:   [sourceOffset  ........ sourceOffset  + length)
*/
struct AudioClip
{
    juce::uint32 id = 0;
    std::shared_ptr<ClipSource> source;
    juce::int64 timelineStart = 0;
    juce::int64 sourceOffset = 0;
    juce::int64 length = 0;

    juce::int64 getEnd() const noexcept                         { return timelineStart + length; }
    bool contains (juce::int64 timelinePosition) const noexcept { return timelinePosition >= timelineStart && timelinePosition < getEnd(); }

    /** Identificador único para seleccionar clips aunque cambie la lista. */
    static juce::uint32 createId() noexcept;
};

/** Operaciones de edición sobre copias de la lista de clips (hilo de mensajes). */
namespace ClipEditing
{
    /** Longitud mínima de un clip tras recortarlo o dividirlo (~1,5 ms). */
    constexpr juce::int64 minimumLength = 64;

    AudioClip* find (std::vector<AudioClip>& clips, juce::uint32 clipId) noexcept;

    /** Divide el clip en la posición dada. Devuelve el id de la mitad derecha, o 0. */
    juce::uint32 split (std::vector<AudioClip>& clips, juce::uint32 clipId, juce::int64 timelinePosition);

    /** Mueve el borde izquierdo (reduce o recupera el principio del clip). */
    void trimStart (AudioClip& clip, juce::int64 newTimelineStart) noexcept;

    /** Mueve el borde derecho (reduce o recupera el final del clip). */
    void trimEnd (AudioClip& clip, juce::int64 newTimelineEnd) noexcept;

    /** Desplaza el clip sin cambiar su contenido (nunca antes del 0). */
    void move (AudioClip& clip, juce::int64 newTimelineStart) noexcept;

    /** Quita el clip de la lista. Devuelve true si existía. */
    bool remove (std::vector<AudioClip>& clips, juce::uint32 clipId);
}
}
