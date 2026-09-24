#include "AudioSeparator.h"

#include <iterator>

namespace stemlab
{
namespace
{
    struct StemName
    {
        const char* id;
        const char* displayName;
    };

    constexpr StemName knownStems[] {
        { "vocals", "Voz" },
        { "drums",  "Batería" },
        { "bass",   "Bajo" },
        { "guitar", "Guitarra" },
        { "piano",  "Piano" },
        { "other",  "Otros" },
    };
}

juce::String stemDisplayName (const juce::String& stemId)
{
    for (const auto& stem : knownStems)
        if (stemId == stem.id)
            return juce::String::fromUTF8 (stem.displayName);

    return stemId;
}

int stemSortOrder (const juce::String& stemId)
{
    for (int i = 0; i < static_cast<int> (std::size (knownStems)); ++i)
        if (stemId == knownStems[i].id)
            return i;

    return static_cast<int> (std::size (knownStems));
}
}
