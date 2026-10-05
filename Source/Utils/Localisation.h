#pragma once

#include <juce_core/juce_core.h>

#include <map>
#include <vector>

namespace stemlab
{
/**
    Idioma de la interfaz.

    El código está escrito en español y ese texto es la clave de la traducción:
    tr ("Añadir pista") devuelve el texto en el idioma elegido (o el mismo
    texto si es español o falta la traducción). Los datos variables van con
    {0}, {1}...:

        tr ("Pista \"{0}\" eliminada. Ctrl+Z la recupera.", track.getName())

    Un idioma nuevo es una tabla más (ver Translations_en.cpp) y una entrada en
    Localisation::getLanguages(). Las pruebas avisan de los textos sin traducir.
*/
enum class Language { spanish, english };

namespace Localisation
{
    struct LanguageInfo
    {
        Language language;
        const char* code;       // "es", "en": así se guarda en los ajustes
        const char* name;       // en su propio idioma (UTF-8)
    };

    const std::vector<LanguageInfo>& getLanguages();

    Language getLanguage();
    void setLanguage (Language language);

    juce::String getCode (Language language);
    juce::String getName (Language language);

    /** El idioma de un código ("es", "en-US"...), o fallback si no se conoce. */
    Language fromCode (const juce::String& code, Language fallback);

    /** El del sistema si StemLab lo tiene; si no, inglés. */
    Language getSystemLanguage();

    /** El texto en el idioma actual (el mismo si no hay traducción). */
    juce::String translate (const juce::String& spanish);

    /** Sustituye {0}, {1}... por los argumentos. */
    juce::String format (juce::String text, const juce::StringArray& arguments);

    /** Traduce un texto que llega ya compuesto (los mensajes de los scripts
        de Python, escritos en español): busca la plantilla que lo produce. */
    juce::String translateMatching (const juce::String& text, const juce::StringArray& patterns);

    /** Las traducciones de un idioma (vacío para el español). */
    const std::map<juce::String, juce::String>& getDictionary (Language language);

    /** Par texto en español → traducción, en UTF-8 (las tablas Translations_*.cpp). */
    struct Translation
    {
        const char* spanish;
        const char* translated;
    };
}

/** Texto de la interfaz en el idioma elegido. El literal es UTF-8 (puede
    llevar acentos sin el sufijo _u8). */
juce::String tr (const char* text);

/** Lo mismo para un texto que ya es un String (nombres de parámetros, de
    acciones de deshacer...: se guardan en español y se traducen al mostrarlos). */
juce::String tr (const juce::String& text);

template <typename First, typename... Rest>
juce::String tr (const char* text, const First& first, const Rest&... rest)
{
    return Localisation::format (tr (text), juce::StringArray { juce::String (first), juce::String (rest)... });
}

/** Marca un texto en español que se traducirá más tarde con tr() (y lo
    convierte desde UTF-8, como _u8): así las pruebas comprueban que tiene
    traducción. */
inline juce::String msg (const char* text)
{
    return juce::String::fromUTF8 (text);
}
}
