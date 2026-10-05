#include "MainWindow.h"

#include "MainComponent.h"
#include "StemLabLookAndFeel.h"

namespace stemlab
{
MainWindow::MainWindow (const juce::String& name, AudioEngine& audioEngine, ProjectManager& projectManager,
                        AIProcessManager& aiManager, juce::PropertiesFile* userSettings)
    : juce::DocumentWindow (name, Palette::background, juce::DocumentWindow::allButtons),
      engine (audioEngine),
      projects (projectManager),
      ai (aiManager),
      settings (userSettings)
{
    setUsingNativeTitleBar (true);
    setContentOwned (createContent(), true);
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

    // La primera vez que se abre StemLab, el tutorial.
    if (settings != nullptr && ! settings->getBoolValue ("tutorialSeen", false))
        if (auto* content = dynamic_cast<MainComponent*> (getContentComponent()))
            content->showTour();
}

MainComponent* MainWindow::createContent()
{
    auto* content = new MainComponent (engine, projects, ai, settings);

    content->onInterfaceChange = [this] (Language language, Theme theme, bool showTour)
    {
        // Lo pide un menú o un botón del contenido actual, que se va a
        // destruir: se cambia cuando ese clic ya ha terminado.
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainWindow> (this), language, theme, showTour]
        {
            if (safe != nullptr)
                safe->setInterface (language, theme, showTour);
        });
    };

    return content;
}

void MainWindow::setInterface (Language language, Theme theme, bool showTour)
{
    Localisation::setLanguage (language);
    Palette::setTheme (theme);

    if (settings != nullptr)
    {
        settings->setValue ("language", Localisation::getCode (language));
        settings->setValue ("theme", theme == Theme::light ? "light" : "dark");
    }

    if (auto* stemLabLook = dynamic_cast<StemLabLookAndFeel*> (&juce::LookAndFeel::getDefaultLookAndFeel()))
        stemLabLook->applyTheme();

    setBackgroundColour (Palette::background);

    std::shared_ptr<AudioTrack> selected;

    if (auto* old = dynamic_cast<MainComponent*> (getContentComponent()))
        selected = old->getSelectedTrack();

    // Sin cambiar el tamaño de la ventana (false): el contenido nuevo ocupa el sitio del anterior.
    auto* content = createContent();
    setContentOwned (content, false);

    if (selected != nullptr)
        content->selectTrack (selected);

    content->grabKeyboardFocus();

    if (showTour)
        content->showTour();
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
