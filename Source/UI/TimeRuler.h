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

    /** Tramo de la línea de tiempo que se ve (zoom y desplazamiento). */
    void setVisibleRange (double startSeconds, double lengthSeconds);
    void setPlayheadSeconds (double seconds);

    std::function<void (double seconds)> onSeek;

    /** Rueda del ratón (zoom con Ctrl, desplazamiento con Shift). Devuelve true si la usó. */
    std::function<bool (int x, const juce::MouseEvent&, const juce::MouseWheelDetails&)> onWheel;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    double visibleStart = 0.0;
    double visibleLength = 60.0;
    double playheadSeconds = 0.0;
};
}
