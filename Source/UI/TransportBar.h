#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Audio/AudioEngine.h"
#include "IconButton.h"
#include "LevelMeter.h"
#include "ParameterAttachments.h"
#include "Project/ProjectManager.h"

#include <functional>

namespace stemlab
{
/** ⏮ ▶ ⏹ ⏺ · tiempo · BPM · volumen master. */
class TransportBar final : public juce::Component,
                           private juce::Timer
{
public:
    TransportBar (AudioEngine& engine, ProjectManager& projects);

    std::function<void()> onToStart, onPlayPause, onStop, onRecord;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Las zonas de la barra (para el tutorial), en coordenadas de la barra.
    juce::Rectangle<int> getButtonsArea() const;
    juce::Rectangle<int> getTimeArea() const;
    juce::Rectangle<int> getInputArea() const;
    juce::Rectangle<int> getMasterArea() const;

private:
    void timerCallback() override;

    AudioEngine& engine;
    ProjectManager& projects;

    IconButton toStartButton;
    IconButton playButton;
    IconButton stopButton;
    IconButton recordButton;

    juce::Label timeLabel;
    juce::Label bpmCaption { {}, "BPM" };
    juce::Label bpmLabel;
    juce::Label inputCaption;                   // "Entrada"
    juce::Slider inputSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    LevelMeter inputMeter;
    juce::Label deviceLabel;
    juce::Label masterCaption { {}, "Master" };
    juce::Slider masterSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    LevelMeter masterMeter;

    SliderAttachment inputAttachment;
    SliderAttachment masterAttachment;
    int blinkCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};
}
