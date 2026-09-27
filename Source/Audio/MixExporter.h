#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include "AudioMixer.h"

#include <functional>
#include <memory>

namespace stemlab
{
/** Formato del archivo exportado. */
enum class ExportFormat
{
    wav16,
    wav24,
    wav32Float,
    mp3
};

struct ExportSettings
{
    juce::File file;
    ExportFormat format = ExportFormat::wav24;
    int mp3Bitrate = 320;   // kbps (solo MP3)
};

struct ExportResult
{
    juce::Result status { juce::Result::ok() };
    bool cancelled = false;
    float peak = 0.0f;      // pico de la mezcla antes de convertir al formato (1.0 = 0 dBFS)
    bool clipped = false;   // el pico pasó de 0 dBFS y el formato (entero o MP3) lo recortó
    double seconds = 0.0;
};

/**
    Exporta la mezcla completa (desde 0 hasta el final del último clip) a WAV
    o MP3, fuera de tiempo real.

    Al crearse (hilo de mensajes) copia las pistas: clips, volumen, paneo,
    mute, solo, efectos y volumen master. El render usa esa copia en un hilo de
    trabajo, así que no toca el motor de audio: se puede seguir escuchando o
    editando mientras exporta, y el archivo refleja la mezcla del momento de
    pulsar Exportar. El audio de los clips (ClipSource) se comparte sin copiar;
    es de solo lectura.

    MP3: libsndfile/JUCE solo decodifican MP3, así que se escribe un WAV
    temporal de 16 bits y lo codifica python/stemlab_encode_mp3.py con LAME
    (paquete lameenc, que ya instala Demucs).
*/
class MixExporter
{
public:
    /** Hilo de mensajes. */
    MixExporter (const AudioMixer& source, double sampleRate);

    double getSampleRate() const noexcept       { return sampleRate; }
    juce::int64 getLengthInSamples() const noexcept { return length; }

    /** Hilo de trabajo. progress recibe 0..1 y devuelve false para cancelar. */
    ExportResult render (const ExportSettings& settings, std::function<bool (double)> progress);

    /** Extensión (con punto) de cada formato. */
    static juce::String getFileExtension (ExportFormat format);

    /** Frecuencias que admite MP3 (LAME): hasta 48 kHz. */
    static double getMp3SampleRate (double sampleRate);

private:
    juce::Result renderToWav (const juce::File& file, int bitsPerSample, double outputRate,
                              const std::function<bool (double)>& progress, float& peak);

    juce::Result encodeMp3 (const juce::File& wav, const juce::File& mp3, int bitrate,
                            const std::function<bool (double)>& progress);

    AudioMixer mixer;
    double sampleRate;
    juce::int64 length = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixExporter)
};
}
