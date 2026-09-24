#pragma once

#include "AudioSeparator.h"

#include <atomic>

namespace stemlab
{
/**
    Separación con Demucs ejecutando python/stemlab_separate.py como proceso
    externo:

        python -u -X utf8 stemlab_separate.py --input X --output DIR --model M

    El script informa por stdout con líneas "@@PROGRESS 0.42", "@@STATUS ...",
    "@@ERROR ..." (ver el propio script). C++ no depende de PyTorch: solo lanza
    el proceso, lee su salida y recoge los WAV generados.
*/
class DemucsSeparator final : public AudioSeparator
{
public:
    struct Settings
    {
        juce::String pythonCommand;         // ruta a python(.exe), o "python" para usar el del PATH
        juce::File scriptFile;
        juce::String model { "htdemucs" };
        juce::String device { "auto" };     // auto | cpu | cuda | mps
        int shifts = 1;
    };

    /** Busca el script y el entorno virtual (python/.venv). La variable de
        entorno STEMLAB_PYTHON permite forzar otro intérprete. */
    static Settings findDefaultSettings();

    explicit DemucsSeparator (Settings settings);

    juce::String getName() const override   { return "Demucs"; }

    std::vector<ModelInfo> getAvailableModels() const override;
    juce::String getCurrentModel() const override;
    void setCurrentModel (const juce::String& modelId) override;

    SeparationResult separate (const SeparationRequest& request, SeparationProgress& progress) override;
    void cancel() override;

private:
    Settings getSettings() const;
    static std::vector<Stem> collectStems (const juce::File& folder);

    mutable juce::CriticalSection settingsLock;
    Settings settings;

    juce::CriticalSection processLock;
    juce::ChildProcess* activeProcess = nullptr;
    std::atomic<bool> cancelRequested { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DemucsSeparator)
};
}
