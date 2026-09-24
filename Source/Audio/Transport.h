#pragma once

#include <juce_core/juce_core.h>

#include <atomic>

namespace stemlab
{
/**
    Cabezal de reproducción único para todas las pistas.

    Todas las pistas leen de la misma posición, así que siempre están
    sincronizadas a nivel de muestra (a diferencia de usar un
    AudioTransportSource independiente por pista).

    Solo el hilo de audio avanza la posición. Los saltos pedidos desde la UI se
    dejan en pendingSeek y el hilo de audio los aplica al empezar el siguiente
    bloque, para que un salto nunca se pierda ni se mezcle con un avance.
*/
class Transport
{
public:
    void play() noexcept                { playing.store (true); }
    void pause() noexcept               { playing.store (false); }
    void togglePlayPause() noexcept     { playing.store (! playing.load()); }
    bool isPlaying() const noexcept     { return playing.load(); }

    void stop() noexcept
    {
        playing.store (false);
        setPosition (0);
    }

    void setPosition (juce::int64 newPosition) noexcept
    {
        newPosition = juce::jmax<juce::int64> (0, newPosition);
        pendingSeek.store (newPosition);

        // Parado: la posición se actualiza ya para que la UI la muestre.
        if (! playing.load())
            position.store (newPosition);
    }

    juce::int64 getPosition() const noexcept    { return position.load(); }

    //==========================================================================
    // Hilo de audio
    juce::int64 beginBlock() noexcept
    {
        if (const auto seek = pendingSeek.exchange (-1); seek >= 0)
            position.store (seek);

        return position.load();
    }

    void advance (int numSamples) noexcept      { position.fetch_add (numSamples); }

private:
    std::atomic<bool> playing { false };
    std::atomic<juce::int64> position { 0 };
    std::atomic<juce::int64> pendingSeek { -1 };
};
}
