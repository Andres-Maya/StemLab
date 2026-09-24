#pragma once

#include "AudioTrack.h"

#include <memory>
#include <vector>

namespace stemlab
{
/**
    Lista de pistas + bus master.

    La lista se protege con un CriticalSection, pero el hilo de audio solo usa
    ScopedTryLock: si en ese instante la UI está añadiendo o quitando una pista,
    ese bloque sale en silencio en vez de bloquear el audio. La UI solo retiene
    el lock para mover punteros, nunca para trabajo pesado.

    Las pistas se guardan como shared_ptr y el hilo de audio nunca copia uno:
    así una pista jamás se destruye en el hilo de audio.
*/
class AudioMixer
{
public:
    AudioMixer();

    //==========================================================================
    // Hilo de mensajes
    void addTrack (std::shared_ptr<AudioTrack> track);
    std::shared_ptr<AudioTrack> removeTrack (const AudioTrack* track);
    std::vector<std::shared_ptr<AudioTrack>> removeAllTracks();

    /** Solo desde el hilo de mensajes (el único que modifica la lista). */
    const std::vector<std::shared_ptr<AudioTrack>>& getTracks() const noexcept   { return tracks; }

    /** Recalcular tras mover una pista en la línea de tiempo. */
    void updateContentLength();
    juce::int64 getContentLength() const noexcept   { return contentLength.load(); }

    Parameter& getMasterVolume() noexcept           { return *masterVolume; }
    const Parameter& getMasterVolume() const noexcept { return *masterVolume; }
    float getAndResetMasterPeak (int channel) noexcept;

    //==========================================================================
    // Motor de audio
    /** Con el audio parado (audioDeviceAboutToStart). */
    void prepare (double sampleRate, int maximumBlockSize);

    /** Hilo de audio: escribe la mezcla estéreo en bus[0, numSamples). */
    void render (juce::AudioBuffer<float>& bus, int numSamples,
                 juce::int64 timelinePosition, bool playing) noexcept;

private:
    juce::CriticalSection tracksLock;
    std::vector<std::shared_ptr<AudioTrack>> tracks;

    std::atomic<double> preparedSampleRate { 44100.0 };
    std::atomic<int> preparedBlockSize { 512 };
    std::atomic<juce::int64> contentLength { 0 };

    std::unique_ptr<Parameter> masterVolume;
    juce::SmoothedValue<float> masterGain;
    std::atomic<float> masterPeaks[2] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioMixer)
};
}
