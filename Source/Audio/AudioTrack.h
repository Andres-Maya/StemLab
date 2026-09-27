#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "AudioClip.h"
#include "DSP/EffectChain.h"
#include "Utils/Parameter.h"

#include <atomic>

namespace stemlab
{
/**
    Una pista: lista de clips en la línea de tiempo + controles de canal
    (volumen, paneo, mute, solo) + cadena de efectos.

    El audio de los clips ya está decodificado en memoria (ClipSource), así que
    el hilo de audio solo copia muestras: nada de disco ni conversiones.

    Hilos:
      - Hilo de mensajes: nombre, clips (getClips/setClips), estado, prepare().
      - Hilo de audio: renderAdd(). Lee la lista de clips con ScopedTryLock: si
        en ese instante se está editando, esa pista sale en silencio un bloque
        en lugar de bloquear el audio.
*/
class AudioTrack
{
public:
    explicit AudioTrack (juce::String name);

    //==========================================================================
    // Hilo de mensajes
    const juce::String& getName() const noexcept            { return name; }
    void setName (juce::String newName)                     { name = std::move (newName); }

    /** Pista en la que se está grabando (solo una a la vez; lo gestiona ProjectManager). */
    bool isArmed() const noexcept                           { return armed; }
    void setArmed (bool shouldBeArmed) noexcept             { armed = shouldBeArmed; }

    Parameter& getVolume() noexcept                         { return *volume; }
    Parameter& getPan() noexcept                            { return *pan; }
    Parameter& getMute() noexcept                           { return *mute; }
    Parameter& getSolo() noexcept                           { return *solo; }
    EffectChain& getEffects() noexcept                      { return effects; }

    juce::var getState() const;
    void applyState (const juce::var& state);

    /** Pista nueva con los mismos clips (con ids nuevos; el audio se comparte),
        volumen, paneo, mute, solo y efectos. No copia el estado de grabación. */
    std::shared_ptr<AudioTrack> createCopy (const juce::String& newName) const;

    //==========================================================================
    // Clips (hilo de mensajes). Los clips posteriores suenan por encima de los
    // anteriores donde se solapan.
    std::vector<AudioClip> getClips() const;
    void setClips (std::vector<AudioClip> newClips);
    bool hasClips() const;

    /** Archivos de audio distintos que usan los clips. */
    std::vector<std::shared_ptr<ClipSource>> getSources() const;

    /** Archivo del primer clip (el que se envía a la separación por IA). */
    juce::File getSourceFile() const;

    /** Frecuencia del audio de los clips, o 0 si la pista está vacía. */
    double getSampleRate() const;

    /** Fin del último clip en la línea de tiempo (seguro desde cualquier hilo). */
    juce::int64 getEndSample() const noexcept               { return endSample.load(); }

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
    void renderClips (int numSamples, juce::int64 timelinePosition) noexcept;

    juce::String name;
    bool armed = false;

    mutable juce::CriticalSection clipLock;
    std::vector<AudioClip> clips;
    std::atomic<juce::int64> endSample { 0 };

    std::unique_ptr<Parameter> volume, pan, mute, solo;
    EffectChain effects;

    juce::AudioBuffer<float> scratch;
    juce::SmoothedValue<float> leftGain, rightGain;
    std::atomic<float> peaks[2] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioTrack)
};
}
