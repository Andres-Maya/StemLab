#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>

namespace stemlab
{
/**
    Graba la entrada del dispositivo en un WAV de 24 bits.

    El hilo de audio no escribe en disco: copia las muestras a un FIFO sin
    bloqueo (AudioFormatWriter::ThreadedWriter) y un hilo de fondo las vuelca
    al archivo.
*/
class AudioRecorder
{
public:
    AudioRecorder();
    ~AudioRecorder();

    //==========================================================================
    // Hilo de mensajes
    juce::Result start (const juce::File& file, double sampleRate, int numChannels);
    void stop();

    bool isRecording() const noexcept                   { return activeWriter.load() != nullptr; }
    const juce::File& getFile() const noexcept          { return currentFile; }

    /** Posición de la línea de tiempo del primer sample grabado (-1 si aún nada). */
    juce::int64 getStartPosition() const noexcept       { return startPosition.load(); }

    //==========================================================================
    // Hilo de audio
    void write (const float* const* input, int numInputChannels, int numSamples,
                juce::int64 timelinePosition) noexcept;

private:
    juce::TimeSliceThread writerThread { "StemLab Recorder" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter;

    juce::CriticalSection writerLock;
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> activeWriter { nullptr };
    std::atomic<juce::int64> startPosition { -1 };
    int numChannelsToRecord = 0;
    juce::File currentFile;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioRecorder)
};
}
