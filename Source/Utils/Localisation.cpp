#include "Localisation.h"

#include <atomic>

namespace stemlab
{
namespace Localisation
{
    // Translations_en.cpp
    extern const Translation englishTranslations[];
    extern const size_t numEnglishTranslations;

    namespace
    {
        using Dictionary = std::map<juce::String, juce::String>;

        Dictionary makeDictionary (const Translation* translations, size_t count)
        {
            Dictionary dictionary;

            for (size_t i = 0; i < count; ++i)
                dictionary[juce::String::fromUTF8 (translations[i].spanish)] = juce::String::fromUTF8 (translations[i].translated);

            return dictionary;
        }

        // Los diccionarios no cambian una vez creados, así que tr() se puede
        // llamar desde cualquier hilo (la separación informa de su estado
        // desde el suyo); el idioma solo cambia desde la interfaz.
        std::atomic<Language> currentLanguage { Language::spanish };
    }

    const std::vector<LanguageInfo>& getLanguages()
    {
        static const std::vector<LanguageInfo> languages {
            { Language::spanish, "es", "Español" },
            { Language::english, "en", "English" },
        };

        return languages;
    }

    const std::map<juce::String, juce::String>& getDictionary (Language language)
    {
        static const Dictionary spanish;
        static const Dictionary english = makeDictionary (englishTranslations, numEnglishTranslations);

        return language == Language::english ? english : spanish;
    }

    Language getLanguage()                  { return currentLanguage.load(); }
    void setLanguage (Language language)    { currentLanguage.store (language); }

    juce::String getCode (Language language)
    {
        for (const auto& info : getLanguages())
            if (info.language == language)
                return info.code;

        return "es";
    }

    juce::String getName (Language language)
    {
        for (const auto& info : getLanguages())
            if (info.language == language)
                return juce::String::fromUTF8 (info.name);

        return {};
    }

    Language fromCode (const juce::String& code, Language fallback)
    {
        const auto wanted = code.substring (0, 2).toLowerCase();

        for (const auto& info : getLanguages())
            if (wanted == info.code)
                return info.language;

        return fallback;
    }

    Language getSystemLanguage()
    {
        return fromCode (juce::SystemStats::getUserLanguage(), Language::english);
    }

    juce::String translate (const juce::String& spanish)
    {
        const auto& dictionary = getDictionary (getLanguage());
        const auto found = dictionary.find (spanish);
        return found != dictionary.end() ? found->second : spanish;
    }

    juce::String format (juce::String text, const juce::StringArray& arguments)
    {
        for (int i = 0; i < arguments.size(); ++i)
            text = text.replace ("{" + juce::String (i) + "}", arguments[i]);

        return text;
    }

    juce::String translateMatching (const juce::String& text, const juce::StringArray& patterns)
    {
        if (getLanguage() == Language::spanish)
            return text;

        for (const auto& pattern : patterns)
        {
            // La plantilla se recorre a trozos: el texto fijo debe coincidir y
            // lo que queda entre dos trozos fijos es el valor de ese {n}.
            juce::StringArray arguments;
            auto rest = text;
            auto remaining = pattern;
            auto matches = true;

            while (matches && remaining.isNotEmpty())
            {
                const auto open = remaining.indexOfChar ('{');
                const auto close = open >= 0 ? remaining.indexOfChar (open, '}') : -1;
                const auto fixed = close > open ? remaining.substring (0, open) : remaining;

                if (! rest.startsWith (fixed))
                {
                    matches = false;
                    break;
                }

                rest = rest.substring (fixed.length());

                if (close <= open)
                {
                    remaining = {};
                    break;
                }

                const auto index = remaining.substring (open + 1, close).getIntValue();
                remaining = remaining.substring (close + 1);

                // Hasta el siguiente trozo fijo (o hasta el final si no hay más).
                const auto nextOpen = remaining.indexOfChar ('{');
                const auto nextFixed = nextOpen >= 0 ? remaining.substring (0, nextOpen) : remaining;
                const auto end = nextFixed.isEmpty() ? rest.length()
                               : nextOpen < 0 ? rest.lastIndexOf (nextFixed)       // el final de la plantilla
                               : rest.indexOf (nextFixed);

                if (end < 0)
                {
                    matches = false;
                    break;
                }

                while (arguments.size() <= index)
                    arguments.add ({});

                arguments.set (index, rest.substring (0, end));
                rest = rest.substring (end);
            }

            if (matches && rest.isEmpty() && remaining.isEmpty())
                return format (Localisation::translate (pattern), arguments);
        }

        return text;
    }
}

juce::String tr (const char* text)
{
    return Localisation::translate (juce::String::fromUTF8 (text));
}

juce::String tr (const juce::String& text)
{
    return Localisation::translate (text);
}
}
