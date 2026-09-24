#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

namespace stemlab
{
/**
    Decodifica un archivo completo a memoria, en estéreo y a la frecuencia de
    muestreo del motor.

    Es bloqueante y puede tardar (MP3 largos, remuestreo): llamar siempre desde
    un hilo de trabajo, nunca desde el de audio ni desde el de mensajes.
*/
struct AudioFileLoader
{
    static juce::Result load (juce::AudioFormatManager& formatManager, const juce::File& file,
                              double targetSampleRate, juce::AudioBuffer<float>& destination);
};
}
