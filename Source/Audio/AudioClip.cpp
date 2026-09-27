#include "AudioClip.h"

#include <algorithm>
#include <atomic>

namespace stemlab
{
juce::uint32 AudioClip::createId() noexcept
{
    static std::atomic<juce::uint32> next { 1 };
    return next.fetch_add (1);
}

namespace ClipEditing
{
AudioClip* find (std::vector<AudioClip>& clips, juce::uint32 clipId) noexcept
{
    for (auto& clip : clips)
        if (clip.id == clipId)
            return &clip;

    return nullptr;
}

juce::uint32 split (std::vector<AudioClip>& clips, juce::uint32 clipId, juce::int64 timelinePosition)
{
    const auto it = std::find_if (clips.begin(), clips.end(), [clipId] (const AudioClip& c) { return c.id == clipId; });

    if (it == clips.end()
        || timelinePosition < it->timelineStart + minimumLength
        || timelinePosition > it->getEnd() - minimumLength)
        return 0;

    const auto leftLength = timelinePosition - it->timelineStart;

    AudioClip right = *it;
    right.id = AudioClip::createId();
    right.timelineStart = timelinePosition;
    right.sourceOffset += leftLength;
    right.length -= leftLength;

    it->length = leftLength;

    // La mitad derecha va justo detrás: conserva el mismo orden de apilado.
    clips.insert (it + 1, right);
    return right.id;
}

void trimStart (AudioClip& clip, juce::int64 newTimelineStart) noexcept
{
    // Límites: no ir antes del principio del audio original, ni antes del 0,
    // ni dejar el clip más corto que el mínimo.
    const auto earliest = juce::jmax<juce::int64> (0, clip.timelineStart - clip.sourceOffset);
    const auto latest = clip.getEnd() - minimumLength;
    newTimelineStart = juce::jlimit (earliest, juce::jmax (earliest, latest), newTimelineStart);

    const auto delta = newTimelineStart - clip.timelineStart;
    clip.timelineStart = newTimelineStart;
    clip.sourceOffset += delta;
    clip.length -= delta;
}

void trimEnd (AudioClip& clip, juce::int64 newTimelineEnd) noexcept
{
    const auto available = clip.source != nullptr ? clip.source->getLength() - clip.sourceOffset : clip.length;
    newTimelineEnd = juce::jlimit (clip.timelineStart + minimumLength,
                                   clip.timelineStart + juce::jmax (minimumLength, available),
                                   newTimelineEnd);
    clip.length = newTimelineEnd - clip.timelineStart;
}

void move (AudioClip& clip, juce::int64 newTimelineStart) noexcept
{
    clip.timelineStart = juce::jmax<juce::int64> (0, newTimelineStart);
}

bool remove (std::vector<AudioClip>& clips, juce::uint32 clipId)
{
    const auto it = std::find_if (clips.begin(), clips.end(), [clipId] (const AudioClip& c) { return c.id == clipId; });

    if (it == clips.end())
        return false;

    clips.erase (it);
    return true;
}

juce::int64 findFreeSpace (const std::vector<AudioClip>& clips, juce::int64 position, juce::int64 length)
{
    position = juce::jmax<juce::int64> (0, position);
    length = juce::jmax<juce::int64> (1, length);

    // Mientras algún clip ocupe parte de [position, position + length), se
    // salta a su final. Termina porque position solo avanza.
    for (auto moved = true; moved;)
    {
        moved = false;

        for (const auto& clip : clips)
        {
            if (clip.length > 0 && clip.timelineStart < position + length && clip.getEnd() > position)
            {
                position = clip.getEnd();
                moved = true;
            }
        }
    }

    return position;
}

std::pair<juce::int64, juce::int64> freeGapAt (const std::vector<AudioClip>& clips, juce::int64 position)
{
    const auto start = findFreeSpace (clips, position, 1);
    auto end = std::numeric_limits<juce::int64>::max();

    for (const auto& clip : clips)
        if (clip.length > 0 && clip.timelineStart >= start)
            end = juce::jmin (end, clip.timelineStart);

    return { start, end };
}

std::pair<juce::int64, juce::int64> freeRangeAround (const std::vector<AudioClip>& clips, const AudioClip& clip)
{
    juce::int64 lowest = 0;
    auto highest = std::numeric_limits<juce::int64>::max();

    for (const auto& other : clips)
    {
        if (other.id == clip.id || other.length <= 0)
            continue;

        if (other.getEnd() <= clip.timelineStart)
            lowest = juce::jmax (lowest, other.getEnd());
        else if (other.timelineStart >= clip.getEnd())
            highest = juce::jmin (highest, other.timelineStart);
    }

    return { lowest, highest };
}

bool fitIntoFreeSpace (const std::vector<AudioClip>& existing, AudioClip& clip)
{
    const auto [gapStart, gapEnd] = freeGapAt (existing, clip.timelineStart);
    const auto start = juce::jmax (gapStart, clip.timelineStart);
    const auto end = juce::jmin (gapEnd, clip.getEnd());

    if (end - start < minimumLength)
        return false;

    clip.sourceOffset += start - clip.timelineStart;
    clip.timelineStart = start;
    clip.length = end - start;
    return true;
}
}
}
