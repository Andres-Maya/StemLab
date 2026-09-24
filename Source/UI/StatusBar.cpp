#include "StatusBar.h"

#include "StemLabLookAndFeel.h"
#include "Utils/Strings.h"

namespace stemlab
{
StatusBar::StatusBar (AIProcessManager& aiManager, ProjectManager& projectManager)
    : ai (aiManager), projects (projectManager)
{
    messageLabel.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (messageLabel);

    projectLabel.setFont (juce::FontOptions (12.0f));
    projectLabel.setColour (juce::Label::textColourId, Palette::textDim);
    projectLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (projectLabel);

    progressBar.setPercentageDisplay (true);
    addChildComponent (progressBar);

    cancelButton.onClick = [this] { if (onCancel != nullptr) onCancel(); };
    addChildComponent (cancelButton);

    setMessage ("Listo.");
    startTimerHz (10);
}

void StatusBar::setMessage (const juce::String& newMessage)
{
    message = newMessage;
    timerCallback();
}

void StatusBar::timerCallback()
{
    const auto aiBusy = ai.isBusy();
    const auto loading = projects.isLoading();

    if (aiBusy)
    {
        messageLabel.setText (ai.getStatus(), juce::dontSendNotification);
        progressValue = ai.getProgress();
    }
    else if (loading)
    {
        messageLabel.setText ("Cargando audio..."_u8, juce::dontSendNotification);
        progressValue = -1.0;
    }
    else
    {
        messageLabel.setText (message, juce::dontSendNotification);
    }

    const auto& project = projects.getProject();
    projectLabel.setText (project.isTemporary() ? project.getName() + " (sin guardar)"_u8
                                                : project.getDirectory().getFullPathName(),
                          juce::dontSendNotification);

    const auto showProgress = aiBusy || loading;

    if (progressBar.isVisible() != showProgress || cancelButton.isVisible() != aiBusy)
    {
        progressBar.setVisible (showProgress);
        cancelButton.setVisible (aiBusy);
        resized();
    }
}

void StatusBar::paint (juce::Graphics& g)
{
    g.fillAll (Palette::panel);
    g.setColour (Palette::outline);
    g.drawHorizontalLine (0, 0.0f, static_cast<float> (getWidth()));
}

void StatusBar::resized()
{
    auto bounds = getLocalBounds().reduced (10, 4);

    if (cancelButton.isVisible())
    {
        cancelButton.setBounds (bounds.removeFromRight (90));
        bounds.removeFromRight (8);
    }

    if (progressBar.isVisible())
    {
        progressBar.setBounds (bounds.removeFromRight (260));
        bounds.removeFromRight (12);
    }

    projectLabel.setBounds (bounds.removeFromRight (bounds.getWidth() / 3));
    messageLabel.setBounds (bounds);
}
}
