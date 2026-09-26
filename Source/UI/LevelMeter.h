#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

namespace stemlab
{
/**
    Medidor de pico estéreo. Lee los picos acumulados por el hilo de audio (a
    través de una función, normalmente getAndResetPeak) 30 veces por segundo
    y aplica una caída suave. Vertical u horizontal según su proporción.
*/
class LevelMeter final : public juce::Component, private juce::Timer
{
public:
    using PeakReader = std::function<float (int channel)>;

    explicit LevelMeter (PeakReader reader);

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    PeakReader readPeak;
    std::array<float, 2> levels {};
};
}
