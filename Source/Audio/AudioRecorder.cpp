#include "AudioRecorder.h"

#include "Utils/Strings.h"

namespace stemlab
{
AudioRecorder::AudioRecorder()
{
    writerThread.startThread();
}

AudioRecorder::~AudioRecorder()
{
    stop();
}

juce::Result AudioRecorder::start (const juce::File& file, double sampleRate, int numChannels)
{
    stop();

    if (numChannels < 1 || numChannels > 2)
        return juce::Result::fail ("Solo se admiten grabaciones mono o estéreo."_u8);

    if (const auto created = file.getParentDirectory().createDirectory(); created.failed())
        return created;

    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();

    if (stream == nullptr)
        return juce::Result::fail ("No se pudo crear el archivo:\n"_u8 + file.getFullPathName());

    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate (sampleRate)
                             .withNumChannels (numChannels)
                             .withBitsPerSample (24);

    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, options);

    if (writer == nullptr)
        return juce::Result::fail ("No se pudo crear el escritor WAV.");

    // 32768 muestras de FIFO ≈ 0,7 s a 48 kHz de margen para el disco.
    threadedWriter = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer.release(), writerThread, 32768);
    numChannelsToRecord = numChannels;
    currentFile = file;
    startPosition.store (-1);

    const juce::ScopedLock lock (writerLock);
    activeWriter.store (threadedWriter.get());
    return juce::Result::ok();
}

void AudioRecorder::stop()
{
    {
        const juce::ScopedLock lock (writerLock);
        activeWriter.store (nullptr);
    }

    // Al destruirse, ThreadedWriter vuelca lo que quede en el FIFO y cierra el WAV.
    threadedWriter.reset();
}

void AudioRecorder::write (const float* const* input, int numInputChannels, int numSamples,
                           juce::int64 timelinePosition) noexcept
{
    if (numInputChannels <= 0 || input == nullptr)
        return;

    // Nunca bloquear el hilo de audio: si justo ahora se está arrancando o
    // parando la grabación, este bloque se descarta.
    const juce::ScopedTryLock lock (writerLock);

    if (! lock.isLocked())
        return;

    auto* writer = activeWriter.load();

    if (writer == nullptr)
        return;

    // Si el dispositivo tiene menos entradas de las previstas, se repite la última.
    const float* channels[2] {};

    for (int ch = 0; ch < numChannelsToRecord; ++ch)
        channels[ch] = input[juce::jmin (ch, numInputChannels - 1)];

    if (startPosition.load() < 0)
        startPosition.store (timelinePosition);

    writer->write (channels, numSamples);
}
}
