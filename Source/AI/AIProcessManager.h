#pragma once

#include <juce_events/juce_events.h>

#include "AudioSeparator.h"

#include <atomic>
#include <functional>
#include <memory>

namespace stemlab
{
/**
    Ejecuta la separación en un hilo propio para que la interfaz nunca se
    congele (el modelo puede tardar minutos).

    La UI consulta getProgress() / getStatus() con un timer, y un Timer interno
    detecta el final y llama al callback en el hilo de mensajes. No hay
    MessageManager::callAsync, así que no hay riesgo de callbacks que lleguen
    después de destruir este objeto.
*/
class AIProcessManager final : private juce::Thread,
                               private juce::Timer,
                               private SeparationProgress
{
public:
    using FinishedCallback = std::function<void (const SeparationResult&)>;

    explicit AIProcessManager (std::unique_ptr<AudioSeparator> separator);
    ~AIProcessManager() override;

    AudioSeparator& getSeparator() noexcept     { return *separator; }

    /** Hilo de mensajes. Devuelve false si ya hay una separación en marcha. */
    bool start (SeparationRequest request, FinishedCallback onFinished);
    void cancel();

    bool isBusy() const noexcept                { return busy.load(); }
    double getProgress() const noexcept         { return progress.load(); }
    juce::String getStatus() const;

private:
    void run() override;
    void timerCallback() override;

    void setProgress (double newProgress) override;
    void setStatus (const juce::String& newStatus) override;
    bool shouldCancel() const override;

    std::unique_ptr<AudioSeparator> separator;
    SeparationRequest request;
    SeparationResult result;
    FinishedCallback finishedCallback;

    std::atomic<bool> busy { false };
    std::atomic<bool> finished { false };
    std::atomic<double> progress { -1.0 };

    mutable juce::CriticalSection statusLock;
    juce::String status;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIProcessManager)
};
}
