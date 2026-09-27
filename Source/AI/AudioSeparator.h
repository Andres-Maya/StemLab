#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace stemlab
{
struct SeparationRequest
{
    juce::File inputFile;
    juce::File outputDirectory;
};

struct Stem
{
    juce::String name;      // identificador del modelo: "vocals", "drums", "bass"...
    juce::File file;
};

struct SeparationResult
{
    juce::Result status { juce::Result::ok() };
    bool cancelled = false;
    std::vector<Stem> stems;
};

/** Canal de progreso entre el separador (hilo de trabajo) y quien lo ejecuta. */
class SeparationProgress
{
public:
    virtual ~SeparationProgress() = default;

    /** 0..1, o negativo si el progreso es indeterminado. */
    virtual void setProgress (double progress) = 0;
    virtual void setStatus (const juce::String& status) = 0;
    virtual bool shouldCancel() const = 0;
};

/**
    Interfaz de cualquier motor de separación de fuentes.

    La aplicación solo depende de esta clase: cambiar Demucs por otro modelo
    (MDX-Net, un modelo específico de guitarra, ONNX en C++...) es escribir
    otra implementación, sin tocar la UI ni el motor de audio.
*/
class AudioSeparator
{
public:
    struct ModelInfo
    {
        juce::String id;
        juce::String description;
        juce::StringArray stems;    // pistas que genera, en orden ("vocals", "drums"...)
    };

    virtual ~AudioSeparator() = default;

    virtual juce::String getName() const = 0;

    virtual std::vector<ModelInfo> getAvailableModels() const = 0;
    virtual juce::String getCurrentModel() const = 0;
    virtual void setCurrentModel (const juce::String& modelId) = 0;

    /** Pistas que generará el modelo actual (para mostrarlas antes de que existan). */
    juce::StringArray getExpectedStems() const;

    /** Bloqueante. Se ejecuta en un hilo de trabajo, nunca en el de mensajes
        ni en el de audio. */
    virtual SeparationResult separate (const SeparationRequest& request, SeparationProgress& progress) = 0;

    /** Thread-safe: se llama desde el hilo de mensajes mientras separate() corre. */
    virtual void cancel() = 0;
};

/** Nombre en español de un stem ("vocals" → "Voz"). */
juce::String stemDisplayName (const juce::String& stemId);

/** Orden de presentación de los stems (voz, batería, bajo, guitarra, piano, otros). */
int stemSortOrder (const juce::String& stemId);
}
