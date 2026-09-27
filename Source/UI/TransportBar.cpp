#include "TransportBar.h"

#include "Utils/Strings.h"

namespace stemlab
{
TransportBar::TransportBar (AudioEngine& audioEngine, ProjectManager& projectManager)
    : engine (audioEngine),
      projects (projectManager),
      toStartButton ("Ir al inicio", IconButton::Icon::toStart),
      playButton ("Reproducir / Pausa (Espacio)", IconButton::Icon::play),
      stopButton ("Detener", IconButton::Icon::stop),
      recordButton ("Grabar en la pista seleccionada (R)", IconButton::Icon::record),
      inputMeter ([this] (int channel) { return engine.getAndResetInputPeak (channel); }),
      masterMeter ([this] (int channel) { return engine.getMixer().getAndResetMasterPeak (channel); }),
      inputAttachment (engine.getInputGain(), inputSlider),
      masterAttachment (engine.getMixer().getMasterVolume(), masterSlider)
{
    toStartButton.onClick = [this] { if (onToStart != nullptr) onToStart(); };
    playButton.onClick = [this] { if (onPlayPause != nullptr) onPlayPause(); };
    stopButton.onClick = [this] { if (onStop != nullptr) onStop(); };
    recordButton.onClick = [this] { if (onRecord != nullptr) onRecord(); };
    recordButton.setActiveColour (Palette::record);

    for (auto* button : { &toStartButton, &playButton, &stopButton, &recordButton })
        addAndMakeVisible (*button);

    timeLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 20.0f, juce::Font::plain));
    timeLabel.setJustificationType (juce::Justification::centred);
    timeLabel.setColour (juce::Label::backgroundColourId, Palette::background);
    addAndMakeVisible (timeLabel);

    for (auto* caption : { &bpmCaption, &inputCaption, &masterCaption })
    {
        caption->setFont (juce::FontOptions (11.0f));
        caption->setColour (juce::Label::textColourId, Palette::textDim);
        caption->setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (*caption);
    }

    bpmLabel.setEditable (false, true);
    bpmLabel.setJustificationType (juce::Justification::centred);
    bpmLabel.setFont (juce::FontOptions (16.0f));
    bpmLabel.setTooltip ("Doble clic para editar");
    bpmLabel.setColour (juce::Label::backgroundColourId, Palette::background);
    bpmLabel.onTextChange = [this]
    {
        const auto value = bpmLabel.getText().getDoubleValue();

        if (value > 0.0)
            projects.setBpm (value);

        bpmLabel.setText (juce::String (projects.getProject().getBpm(), 1), juce::dontSendNotification);
    };
    addAndMakeVisible (bpmLabel);

    // Ganancia del micrófono antes de grabar, con su medidor (se mueve aunque no
    // se esté grabando, para ajustar el nivel antes).
    inputSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    inputSlider.setTooltip ("Ganancia de la entrada al grabar. Ajústala para que el medidor quede en verde/amarillo "
                            "al hablar o tocar; un limitador suave evita que recorte."_u8);
    inputSlider.setColour (juce::Slider::trackColourId, Palette::record);
    addAndMakeVisible (inputCaption);
    addAndMakeVisible (inputSlider);
    addAndMakeVisible (inputMeter);

    deviceLabel.setFont (juce::FontOptions (11.0f));
    deviceLabel.setMinimumHorizontalScale (0.7f);
    deviceLabel.setJustificationType (juce::Justification::centredRight);
    deviceLabel.setTooltip ("Salida de audio actual (Audio > Configuración de audio)"_u8);
    addAndMakeVisible (deviceLabel);

    masterSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    addAndMakeVisible (masterSlider);
    addAndMakeVisible (masterMeter);

    startTimerHz (20);
    timerCallback();
}

void TransportBar::timerCallback()
{
    const auto sampleRate = engine.getSampleRate();
    const auto position = static_cast<double> (engine.getTransport().getPosition()) / sampleRate;
    const auto length = static_cast<double> (engine.getMixer().getContentLength()) / sampleRate;

    timeLabel.setText (formatTime (position) + "  /  " + formatTime (length), juce::dontSendNotification);

    playButton.setIcon (engine.getTransport().isPlaying() ? IconButton::Icon::pause : IconButton::Icon::play);

    // El botón de grabar parpadea mientras se graba.
    const auto recording = engine.isRecording();
    blinkCounter = recording ? (blinkCounter + 1) % 20 : 0;
    recordButton.setToggleState (recording && blinkCounter < 12, juce::dontSendNotification);

    // Qué salida está sonando; en rojo si el dispositivo de audio no funciona.
    const auto output = engine.getCurrentOutputName();
    deviceLabel.setText (output.isNotEmpty() ? "Salida: " + output
                                             : "Sin audio: revisa Audio > Configuración de audio"_u8,
                         juce::dontSendNotification);
    deviceLabel.setColour (juce::Label::textColourId, output.isNotEmpty() ? Palette::textDim : Palette::record);

    if (! bpmLabel.isBeingEdited())
        bpmLabel.setText (juce::String (projects.getProject().getBpm(), 1), juce::dontSendNotification);
}

void TransportBar::paint (juce::Graphics& g)
{
    g.fillAll (Palette::panel);
    g.setColour (Palette::outline);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, static_cast<float> (getWidth()));
}

void TransportBar::resized()
{
    auto bounds = getLocalBounds().reduced (12, 10);

    for (auto* button : { &toStartButton, &playButton, &stopButton, &recordButton })
    {
        button->setBounds (bounds.removeFromLeft (bounds.getHeight() + 8));
        bounds.removeFromLeft (6);
    }

    bounds.removeFromLeft (10);
    timeLabel.setBounds (bounds.removeFromLeft (250));
    bounds.removeFromLeft (16);

    bpmCaption.setBounds (bounds.removeFromLeft (34));
    bpmLabel.setBounds (bounds.removeFromLeft (64));

    // Entrada (junto a la zona de grabación)
    bounds.removeFromLeft (12);
    inputCaption.setBounds (bounds.removeFromLeft (52));
    inputSlider.setBounds (bounds.removeFromLeft (150));
    bounds.removeFromLeft (6);
    inputMeter.setBounds (bounds.removeFromLeft (60).withSizeKeepingCentre (60, 12));

    // Master (a la derecha)
    masterMeter.setBounds (bounds.removeFromRight (60).withSizeKeepingCentre (60, 12));
    bounds.removeFromRight (8);
    masterSlider.setBounds (bounds.removeFromRight (170));
    masterCaption.setBounds (bounds.removeFromRight (50));
    bounds.removeFromRight (12);
    deviceLabel.setBounds (bounds);
}
}
