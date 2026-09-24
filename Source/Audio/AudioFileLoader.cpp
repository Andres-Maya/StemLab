#include "AudioFileLoader.h"

#include "Utils/Strings.h"

#include <limits>

namespace stemlab
{
juce::Result AudioFileLoader::load (juce::AudioFormatManager& formatManager, const juce::File& file,
                                    double targetSampleRate, juce::AudioBuffer<float>& destination)
{
    if (! file.existsAsFile())
        return juce::Result::fail ("El archivo no existe.");

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

    if (reader == nullptr)
        return juce::Result::fail ("Formato de audio no soportado.");

    const auto sourceRate = reader->sampleRate;
    const auto sourceLength = reader->lengthInSamples;

    if (sourceRate <= 0.0 || sourceLength <= 0)
        return juce::Result::fail ("El archivo está vacío."_u8);

    const auto ratio = sourceRate / targetSampleRate;
    const auto destinationLength = static_cast<juce::int64> (std::ceil (static_cast<double> (sourceLength) / ratio));

    if (destinationLength > std::numeric_limits<int>::max())
        return juce::Result::fail ("El archivo es demasiado largo.");

    const auto numSamples = static_cast<int> (destinationLength);
    destination.setSize (2, numSamples, false, true, false);

    // Mismo sample rate: lectura directa. AudioFormatReader duplica
    // automáticamente un archivo mono en los dos canales.
    if (std::abs (ratio - 1.0) < 1.0e-9)
    {
        reader->read (&destination, 0, numSamples, 0, true, true);
        return juce::Result::ok();
    }

    // Distinto sample rate: se remuestrea por bloques. ResamplingAudioSource
    // aplica un filtro antialiasing cuando se reduce la frecuencia.
    juce::AudioFormatReaderSource readerSource (reader.get(), false);
    juce::ResamplingAudioSource resampler (&readerSource, false, 2);
    resampler.setResamplingRatio (ratio);

    constexpr int blockSize = 8192;
    resampler.prepareToPlay (blockSize, targetSampleRate);

    for (int written = 0; written < numSamples;)
    {
        const auto count = juce::jmin (blockSize, numSamples - written);
        const juce::AudioSourceChannelInfo info (&destination, written, count);
        resampler.getNextAudioBlock (info);
        written += count;
    }

    resampler.releaseResources();
    return juce::Result::ok();
}
}
