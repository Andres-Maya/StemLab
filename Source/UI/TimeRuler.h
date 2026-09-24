#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace stemlab
{
/** Regla de tiempo sobre las formas de onda. Clic o arrastre para mover el cabezal. */
class TimeRuler final : public juce::Component
{
public:
    TimeRuler();

    void setTimelineLength (double seconds);
    void setPlayheadSeconds (double seconds);

    std::function<void (double seconds)> onSeek;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    double timelineLength = 60.0;
    double playheadSeconds = 0.0;
};
}
