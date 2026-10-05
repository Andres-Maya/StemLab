#include "AudioTrack.h"

#include "Utils/Localisation.h"      // msg(): los nombres se traducen al mostrarlos

#include <algorithm>

namespace stemlab
{
namespace
{
    // Fundido en los bordes de cada clip para que cortar en mitad de la onda
    // no produzca chasquidos (~1 ms).
    constexpr juce::int64 edgeFadeLength = 48;
}

AudioTrack::AudioTrack (juce::String trackName)
    : name (std::move (trackName)),
      volume (Parameter::continuous ("volume", msg ("Volumen"), juce::NormalisableRange<float> (-60.0f, 12.0f, 0.1f, 2.0f), 0.0f, "dB")),
      pan (Parameter::continuous ("pan", msg ("Paneo"), juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f)),
      mute (Parameter::toggle ("mute", "Mute", false)),
      solo (Parameter::toggle ("solo", "Solo", false))
{
    volume->setTextFormatter ([] (float db) { return db <= -60.0f ? juce::String ("-inf dB") : juce::String (db, 1) + " dB"; });

    pan->setTextFormatter ([] (float value)
    {
        const auto percent = juce::roundToInt (std::abs (value) * 100.0f);
        return percent == 0 ? juce::String ("C") : juce::String (value < 0.0f ? "L " : "R ") + juce::String (percent);
    });
}

juce::var AudioTrack::getState() const
{
    auto* state = new juce::DynamicObject();

    for (const auto* parameter : { volume.get(), pan.get(), mute.get(), solo.get() })
        state->setProperty (parameter->getId(), parameter->toVar());

    state->setProperty ("effects", effects.toVar());

    // Carpeta y origen (separación): solo si los tiene, así los proyectos sin
    // carpetas guardan exactamente lo mismo que antes.
    if (folderId.isNotEmpty())
        state->setProperty ("folder", folderId);

    if (stemGroup.isNotEmpty())
    {
        state->setProperty ("stemGroup", stemGroup);
        state->setProperty ("stemId", stemId);
    }

    return juce::var (state);
}

void AudioTrack::applyState (const juce::var& state)
{
    for (auto* parameter : { volume.get(), pan.get(), mute.get(), solo.get() })
        parameter->fromVar (state.getProperty (parameter->getId(), {}));

    effects.fromVar (state.getProperty ("effects", {}));

    folderId = state.getProperty ("folder", {}).toString();
    stemGroup = state.getProperty ("stemGroup", {}).toString();
    stemId = state.getProperty ("stemId", {}).toString();
}

std::shared_ptr<AudioTrack> AudioTrack::createCopy (const juce::String& newName) const
{
    auto copy = std::make_shared<AudioTrack> (newName);
    auto copiedClips = getClips();

    for (auto& clip : copiedClips)
        clip.id = AudioClip::createId();

    copy->setClips (std::move (copiedClips));
    copy->applyState (getState());
    return copy;
}

//==============================================================================
std::vector<AudioClip> AudioTrack::getClips() const
{
    const juce::ScopedLock lock (clipLock);
    return clips;
}

void AudioTrack::setClips (std::vector<AudioClip> newClips)
{
    juce::int64 end = 0;

    for (const auto& clip : newClips)
        end = juce::jmax (end, clip.getEnd());

    {
        // Solo se intercambian punteros dentro del lock; la lista anterior (y el
        // audio que quizá ya nadie usa) se libera fuera, en este hilo.
        const juce::ScopedLock lock (clipLock);
        clips.swap (newClips);
    }

    endSample.store (end);
}

bool AudioTrack::hasClips() const
{
    const juce::ScopedLock lock (clipLock);
    return ! clips.empty();
}

std::vector<std::shared_ptr<ClipSource>> AudioTrack::getSources() const
{
    std::vector<std::shared_ptr<ClipSource>> sources;

    for (const auto& clip : getClips())
        if (clip.source != nullptr && std::find (sources.begin(), sources.end(), clip.source) == sources.end())
            sources.push_back (clip.source);

    return sources;
}

juce::File AudioTrack::getSourceFile() const
{
    const juce::ScopedLock lock (clipLock);
    return clips.empty() || clips.front().source == nullptr ? juce::File() : clips.front().source->file;
}

double AudioTrack::getSampleRate() const
{
    const juce::ScopedLock lock (clipLock);
    return clips.empty() || clips.front().source == nullptr ? 0.0 : clips.front().source->sampleRate;
}

float AudioTrack::getAndResetPeak (int channel) noexcept
{
    return peaks[juce::jlimit (0, 1, channel)].exchange (0.0f, std::memory_order_relaxed);
}

//==============================================================================
void AudioTrack::prepare (double deviceSampleRate, int maximumBlockSize)
{
    scratch.setSize (2, maximumBlockSize, false, true, false);
    effects.prepare ({ deviceSampleRate, static_cast<juce::uint32> (maximumBlockSize), 2 });

    for (auto* smoothed : { &leftGain, &rightGain })
    {
        smoothed->reset (deviceSampleRate, 0.02);
        smoothed->setCurrentAndTargetValue (0.0f);
    }
}

void AudioTrack::renderClips (int numSamples, juce::int64 timelinePosition) noexcept
{
    const juce::ScopedTryLock lock (clipLock);

    if (! lock.isLocked())
        return;

    // En orden: donde dos clips se solapan, el posterior sobrescribe al anterior.
    for (const auto& clip : clips)
    {
        if (clip.source == nullptr)
            continue;

        const auto offsetInClip = timelinePosition - clip.timelineStart;
        const auto first = juce::jlimit<juce::int64> (0, numSamples, -offsetInClip);
        const auto last = juce::jlimit<juce::int64> (0, numSamples, clip.length - offsetInClip);

        if (last <= first)
            continue;

        for (int ch = 0; ch < 2; ++ch)
            scratch.copyFrom (ch, static_cast<int> (first), clip.source->audio, ch,
                              static_cast<int> (clip.sourceOffset + offsetInClip + first),
                              static_cast<int> (last - first));

        // Fundidos cortos en los bordes del clip.
        for (auto i = first; i < last; ++i)
        {
            const auto positionInClip = offsetInClip + i;
            const auto distanceToEdge = juce::jmin (positionInClip, clip.length - 1 - positionInClip);

            if (distanceToEdge >= edgeFadeLength)
            {
                // Saltar directamente hasta la zona de fundido final.
                const auto fadeOutStart = clip.length - edgeFadeLength - offsetInClip;
                i = juce::jmax (i, fadeOutStart - 1);
                continue;
            }

            const auto gain = static_cast<float> (distanceToEdge) / static_cast<float> (edgeFadeLength);

            for (int ch = 0; ch < 2; ++ch)
                scratch.getWritePointer (ch)[i] *= gain;
        }
    }
}

void AudioTrack::renderAdd (juce::AudioBuffer<float>& destination, int numSamples,
                            juce::int64 timelinePosition, bool audible) noexcept
{
    jassert (numSamples <= scratch.getNumSamples());

    // Volumen + balance. El balance deja el centro a ganancia unidad: así la
    // suma de los stems sin tocar reproduce la mezcla original.
    const auto gain = audible ? juce::Decibels::decibelsToGain (volume->get(), -60.0f) : 0.0f;
    const auto panValue = pan->get();
    const auto halfPi = juce::MathConstants<float>::halfPi;

    leftGain.setTargetValue (gain * (panValue > 0.0f ? std::cos (panValue * halfPi) : 1.0f));
    rightGain.setTargetValue (gain * (panValue < 0.0f ? std::cos (-panValue * halfPi) : 1.0f));

    // Silenciada y con la rampa de salida terminada: no hace falta procesar.
    if (! audible && ! leftGain.isSmoothing() && ! rightGain.isSmoothing())
        return;

    scratch.clear (0, numSamples);
    renderClips (numSamples, timelinePosition);

    effects.process (juce::dsp::AudioBlock<float> (scratch).getSubBlock (0, static_cast<size_t> (numSamples)));

    const auto* inL = scratch.getReadPointer (0);
    const auto* inR = scratch.getReadPointer (1);
    auto* outL = destination.getWritePointer (0);
    auto* outR = destination.getWritePointer (1);
    float peakL = 0.0f, peakR = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto l = inL[i] * leftGain.getNextValue();
        const auto r = inR[i] * rightGain.getNextValue();
        outL[i] += l;
        outR[i] += r;
        peakL = juce::jmax (peakL, std::abs (l));
        peakR = juce::jmax (peakR, std::abs (r));
    }

    peaks[0].store (juce::jmax (peaks[0].load (std::memory_order_relaxed), peakL), std::memory_order_relaxed);
    peaks[1].store (juce::jmax (peaks[1].load (std::memory_order_relaxed), peakR), std::memory_order_relaxed);
}
}
