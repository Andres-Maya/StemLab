#include "AIProcessManager.h"

#include "Utils/Strings.h"

namespace stemlab
{
AIProcessManager::AIProcessManager (std::unique_ptr<AudioSeparator> audioSeparator)
    : juce::Thread ("StemLab AI"),
      separator (std::move (audioSeparator))
{
    jassert (separator != nullptr);
}

AIProcessManager::~AIProcessManager()
{
    stopTimer();

    if (busy.load())
    {
        signalThreadShouldExit();
        separator->cancel();
    }

    stopThread (10000);
}

bool AIProcessManager::start (SeparationRequest newRequest, FinishedCallback onFinished)
{
    jassert (juce::MessageManager::existsAndIsCurrentThread());

    if (busy.load())
        return false;

    // El hilo anterior ya terminó run(); esto solo lo recoge.
    stopThread (1000);

    request = std::move (newRequest);
    finishedCallback = std::move (onFinished);
    result = {};
    progress.store (-1.0);
    setStatus ("Preparando separación..."_u8);
    finished.store (false);
    busy.store (true);

    startThread();
    startTimerHz (10);
    return true;
}

void AIProcessManager::cancel()
{
    if (! busy.load())
        return;

    setStatus ("Cancelando..."_u8);
    signalThreadShouldExit();
    separator->cancel();
}

juce::String AIProcessManager::getStatus() const
{
    const juce::ScopedLock lock (statusLock);
    return status;
}

//==============================================================================
void AIProcessManager::run()
{
    result = separator->separate (request, *this);
    finished.store (true);
}

void AIProcessManager::timerCallback()
{
    if (! finished.load())
        return;

    stopTimer();
    stopThread (2000);
    busy.store (false);

    progress.store (result.status.wasOk() ? 1.0 : 0.0);
    setStatus (result.cancelled ? "Separación cancelada."_u8
                                : (result.status.wasOk() ? "Separación completada."_u8
                                                         : "Error en la separación."_u8));

    if (auto callback = std::exchange (finishedCallback, nullptr))
        callback (result);
}

//==============================================================================
void AIProcessManager::setProgress (double newProgress)
{
    progress.store (newProgress < 0.0 ? -1.0 : juce::jlimit (0.0, 1.0, newProgress));
}

void AIProcessManager::setStatus (const juce::String& newStatus)
{
    const juce::ScopedLock lock (statusLock);
    status = newStatus;
}

bool AIProcessManager::shouldCancel() const
{
    return threadShouldExit();
}
}
