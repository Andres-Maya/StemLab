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
    // Vista previa en directo: un pico por cada previewBinSize muestras.
    static constexpr int previewBinSize = 512;

    /** Hilo de mensajes: copia en dest los picos nuevos desde la última lectura
        y devuelve cuántos. */
    int readPreviewPeaks (float* dest, int maxPeaks) noexcept;

    //==========================================================================
    // Hilo de audio
    void write (const float* const* input, int numInputChannels, int numSamples,
                juce::int64 timelinePosition) noexcept;

private:
    void pushPreviewPeak (float peak) noexcept;

    // Cola sin bloqueos audio -> UI con la memoria ya reservada (~40 s de margen).
    static constexpr int previewCapacity = 4096;
    juce::AbstractFifo previewFifo { previewCapacity };
    std::vector<float> previewPeaks = std::vector<float> (previewCapacity, 0.0f);
    float binPeak = 0.0f;
    int binCount = 0;

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
