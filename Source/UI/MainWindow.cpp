#include "MainWindow.h"

#include "MainComponent.h"
#include "StemLabLookAndFeel.h"

namespace stemlab
{
MainWindow::MainWindow (const juce::String& name, AudioEngine& engine, ProjectManager& projects, AIProcessManager& ai)
    : juce::DocumentWindow (name, Palette::background, juce::DocumentWindow::allButtons)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new MainComponent (engine, projects, ai), true);
    setResizable (true, true);
    setResizeLimits (960, 640, 10000, 10000);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);

    // Para que los atajos de teclado funcionen desde el principio.
    if (auto* content = getContentComponent())
        content->grabKeyboardFocus();
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}
}
