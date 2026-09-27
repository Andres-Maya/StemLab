#include "MainWindow.h"

#include "MainComponent.h"
#include "StemLabLookAndFeel.h"

namespace stemlab
{
MainWindow::MainWindow (const juce::String& name, AudioEngine& engine, ProjectManager& projects, AIProcessManager& ai,
                        juce::PropertiesFile* settings)
    : juce::DocumentWindow (name, Palette::background, juce::DocumentWindow::allButtons)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new MainComponent (engine, projects, ai, settings), true);
    setResizable (true, true);
    setResizeLimits (800, 520, 10000, 10000);

    // Ajustar al monitor: en pantallas pequeñas (p. ej. 1366×768) el tamaño
    // preferido no cabe, así que la ventana arranca maximizada.
    const auto monitor = getParentMonitorArea();
    constexpr int margin = 60;   // barra de título y bordes de la ventana nativa

    if (getWidth() + margin > monitor.getWidth() || getHeight() + margin > monitor.getHeight())
    {
        setBounds (monitor.reduced (margin / 2));
        setVisible (true);
        setFullScreen (true);
    }
    else
    {
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    // Para que los atajos de teclado funcionen desde el principio.
    if (auto* content = getContentComponent())
        content->grabKeyboardFocus();
}

void MainWindow::closeButtonPressed()
{
    requestQuit();
}

void MainWindow::openProjectFile (const juce::File& file)
{
    if (auto* content = dynamic_cast<MainComponent*> (getContentComponent()))
        content->openProjectFile (file);
}

void MainWindow::requestQuit()
{
    if (auto* content = dynamic_cast<MainComponent*> (getContentComponent()))
        content->requestQuit();
    else
        juce::JUCEApplication::quit();
}
}
