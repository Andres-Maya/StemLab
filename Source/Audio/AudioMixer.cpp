#include "AudioMixer.h"

namespace stemlab
{
AudioMixer::AudioMixer()
    : masterVolume (Parameter::continuous ("masterVolume", "Master",
                                           juce::NormalisableRange<float> (-60.0f, 6.0f, 0.1f, 2.0f), 0.0f, "dB"))
{
    masterGain.reset (preparedSampleRate.load(), 0.02);
    masterGain.setCurrentAndTargetValue (1.0f);
}

void AudioMixer::addTrack (std::shared_ptr<AudioTrack> track)
{
    jassert (track != nullptr);

    // La preparación reserva memoria: se hace fuera del lock. Si el dispositivo
    // cambió de configuración mientras tanto, se repite dentro.
    const auto rate = preparedSampleRate.load();
    const auto blockSize = preparedBlockSize.load();
    track->prepare (rate, blockSize);

    {
        const juce::ScopedLock lock (tracksLock);

        if (! juce::exactlyEqual (rate, preparedSampleRate.load()) || blockSize != preparedBlockSize.load())
            track->prepare (preparedSampleRate.load(), preparedBlockSize.load());

        tracks.push_back (std::move (track));
    }

    updateContentLength();
}

std::shared_ptr<AudioTrack> AudioMixer::removeTrack (const AudioTrack* track)
{
    std::shared_ptr<AudioTrack> removed;

    {
        const juce::ScopedLock lock (tracksLock);
        const auto it = std::find_if (tracks.begin(), tracks.end(),
                                      [track] (const auto& t) { return t.get() == track; });

        if (it != tracks.end())
        {
            removed = std::move (*it);
            tracks.erase (it);
        }
    }

    updateContentLength();

    // Quien llama decide cuándo se destruye (siempre fuera del lock y del
    // hilo de audio).
    return removed;
}

std::vector<std::shared_ptr<AudioTrack>> AudioMixer::removeAllTracks()
{
    std::vector<std::shared_ptr<AudioTrack>> removed;

    {
        const juce::ScopedLock lock (tracksLock);
        removed.swap (tracks);
    }

    updateContentLength();
    return removed;
}

void AudioMixer::updateContentLength()
{
    juce::int64 end = 0;

    for (const auto& track : tracks)
        end = juce::jmax (end, track->getEndSample());

    contentLength.store (end);
}

float AudioMixer::getAndResetMasterPeak (int channel) noexcept
{
    return masterPeaks[juce::jlimit (0, 1, channel)].exchange (0.0f, std::memory_order_relaxed);
}

void AudioMixer::prepare (double sampleRate, int maximumBlockSize)
{
    const juce::ScopedLock lock (tracksLock);

    preparedSampleRate.store (sampleRate);
    preparedBlockSize.store (maximumBlockSize);

    masterGain.reset (sampleRate, 0.02);
    masterGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (masterVolume->get(), -60.0f));

    for (const auto& track : tracks)
        track->prepare (sampleRate, maximumBlockSize);
}

void AudioMixer::render (juce::AudioBuffer<float>& bus, int numSamples,
                         juce::int64 timelinePosition, bool playing) noexcept
{
    bus.clear (0, numSamples);

    if (playing)
    {
        const juce::ScopedTryLock lock (tracksLock);

        if (lock.isLocked())
        {
            bool anySolo = false;

            for (const auto& track : tracks)
                anySolo = anySolo || track->getSolo().getBool();

            for (const auto& track : tracks)
            {
                const auto audible = ! track->getMute().getBool()
                                  && (! anySolo || track->getSolo().getBool());

                track->renderAdd (bus, numSamples, timelinePosition, audible);
            }
        }
    }

    masterGain.setTargetValue (juce::Decibels::decibelsToGain (masterVolume->get(), -60.0f));

    auto* left = bus.getWritePointer (0);
    auto* right = bus.getWritePointer (1);
    float peakL = 0.0f, peakR = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto gain = masterGain.getNextValue();
        left[i] *= gain;
        right[i] *= gain;
        peakL = juce::jmax (peakL, std::abs (left[i]));
        peakR = juce::jmax (peakR, std::abs (right[i]));
    }

    masterPeaks[0].store (juce::jmax (masterPeaks[0].load (std::memory_order_relaxed), peakL), std::memory_order_relaxed);
    masterPeaks[1].store (juce::jmax (masterPeaks[1].load (std::memory_order_relaxed), peakR), std::memory_order_relaxed);
}
}
