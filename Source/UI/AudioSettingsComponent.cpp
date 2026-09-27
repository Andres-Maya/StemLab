#include "AudioSettingsComponent.h"

#include "StemLabLookAndFeel.h"
#include "Utils/Strings.h"

namespace stemlab
{
AudioSettingsComponent::AudioSettingsComponent (AudioEngine& audioEngine)
    : engine (audioEngine),
      selector (engine.getDeviceManager(),
                0, 2,       // entradas
                2, 2,       // salidas
                false, false, true, false)
{
    followSystemButton.setToggleState (engine.isFollowingSystemOutput(), juce::dontSendNotification);
    followSystemButton.onClick = [this] { engine.setFollowSystemOutput (followSystemButton.getToggleState()); };
    addAndMakeVisible (followSystemButton);

    hintLabel.setText ("StemLab cambia solo a los audífonos al conectarlos, como el resto de programas. "
                       "Si eliges otra salida abajo, esta opción se desactiva."_u8,
                       juce::dontSendNotification);
    hintLabel.setFont (juce::FontOptions (12.0f));
    hintLabel.setColour (juce::Label::textColourId, Palette::textDim);
    hintLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (hintLabel);

    addAndMakeVisible (selector);

    setSize (560, 500);
    startTimerHz (4);
}

void AudioSettingsComponent::timerCallback()
{
    followSystemButton.setToggleState (engine.isFollowingSystemOutput(), juce::dontSendNotification);
}

void AudioSettingsComponent::paint (juce::Graphics& g)
{
    g.fillAll (Palette::panel);
}

void AudioSettingsComponent::resized()
{
    auto bounds = getLocalBounds().reduced (12);
    followSystemButton.setBounds (bounds.removeFromTop (26));
    hintLabel.setBounds (bounds.removeFromTop (34).withTrimmedLeft (28));
    bounds.removeFromTop (6);
    selector.setBounds (bounds);
}
}
