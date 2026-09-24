#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "DSP/EffectChain.h"
#include "Utils/Parameter.h"

#include <atomic>

namespace stemlab
{
/**
    Una pista: audio en memoria + posición en la línea de tiempo + controles de
    canal (volumen, paneo, mute, solo) + cadena de efectos.

    El audio se decodifica completo en memoria (estéreo, a la frecuencia del
    dispositivo) en un hilo de trabajo antes de crear la pista. Así el hilo de
    audio solo copia muestras: nada de lecturas de disco ni conversiones.

    Hilos:
      - Hilo de mensajes: nombre, archivo, estado, prepare().
      - Hilo de audio: renderAdd(). Solo lee atómicos y el buffer (inmutable).
*/
class AudioTrack
{
public:
    AudioTrack (juce::String name, juce::File sourceFile, juce::AudioBuffer<float> audio, double sampleRate);

    //==========================================================================
    // Hilo de mensajes
    const juce::String& getName() const noexcept            { return name; }
    void setName (juce::String newName)                     { name = std::move (newName); }

    const juce::File& getSourceFile() const noexcept        { return sourceFile; }
    void setSourceFile (juce::File newFile)                 { sourceFile = std::move (newFile); }

    Parameter& getVolume() noexcept                         { return *volume; }
    Parameter& getPan() noexcept                            { return *pan; }
    Parameter& getMute() noexcept                           { return *mute; }
    Parameter& getSolo() noexcept                           { return *solo; }
    EffectChain& getEffects() noexcept                      { return effects; }

    juce::var getState() const;
    void applyState (const juce::var& state);

    //==========================================================================
    // Datos inmutables del audio (seguros desde cualquier hilo)
    const juce::AudioBuffer<float>& getAudio() const noexcept   { return audio; }
    double getSampleRate() const noexcept                       { return sampleRate; }
    juce::int64 getLengthInSamples() const noexcept             { return audio.getNumSamples(); }
    double getLengthInSeconds() const noexcept                  { return static_cast<double> (getLengthInSamples()) / sampleRate; }

    juce::int64 getStartSample() const noexcept                 { return startSample.load (std::memory_order_relaxed); }
    void setStartSample (juce::int64 newStart) noexcept         { startSample.store (juce::jmax<juce::int64> (0, newStart)); }
    juce::int64 getEndSample() const noexcept                   { return getStartSample() + getLengthInSamples(); }

    /** Pico desde la última lectura (para los medidores de la UI). */
    float getAndResetPeak (int channel) noexcept;

    //==========================================================================
    // Motor de audio
    /** Fuera del hilo de audio: reserva los buffers internos. */
    void prepare (double deviceSampleRate, int maximumBlockSize);

    /** Hilo de audio: procesa la pista y SUMA el resultado en destination. */
    void renderAdd (juce::AudioBuffer<float>& destination, int numSamples,
                    juce::int64 timelinePosition, bool audible) noexcept;

private:
    juce::String name;
    juce::File sourceFile;

    const juce::AudioBuffer<float> audio;
    const double sampleRate;
    std::atomic<juce::int64> startSample { 0 };

    std::unique_ptr<Parameter> volume, pan, mute, solo;
    EffectChain effects;

    juce::AudioBuffer<float> scratch;
    juce::SmoothedValue<float> leftGain, rightGain;
    std::atomic<float> peaks[2] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioTrack)
};
}
