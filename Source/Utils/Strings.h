#pragma once

#include <juce_core/juce_core.h>

#include "Localisation.h"       // tr(): los textos de la interfaz, en el idioma elegido

namespace stemlab
{
    /** Literal para textos con acentos que no se traducen: "Sesión"_u8.

        El proyecto se compila como UTF-8 (/utf-8 en MSVC), pero juce::String
        interpreta un const char* como ASCII (y lanza un jassert si no lo es),
        así que los textos con caracteres no ASCII se convierten explícitamente.
        Los textos que ve el usuario van con tr() (Localisation.h), que ya
        hace esa conversión.
    */
    inline juce::String operator""_u8 (const char* text, std::size_t length)
    {
        return juce::String::fromUTF8 (text, static_cast<int> (length));
    }

    /** Formatea segundos como mm:ss.mmm */
    inline juce::String formatTime (double seconds)
    {
        seconds = juce::jmax (0.0, seconds);
        const auto totalMillis = static_cast<juce::int64> (seconds * 1000.0);
        const auto minutes = totalMillis / 60000;
        const auto secs = (totalMillis / 1000) % 60;
        const auto millis = totalMillis % 1000;

        return juce::String (minutes).paddedLeft ('0', 2) + ":"
             + juce::String (secs).paddedLeft ('0', 2) + "."
             + juce::String (millis).paddedLeft ('0', 3);
    }
}
