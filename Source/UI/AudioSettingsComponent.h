#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "Audio/AudioEngine.h"

namespace stemlab
{
/**
    Contenido del diálogo Audio > Configuración de audio:

        [x] Usar la salida predeterminada de Windows
        [selector de dispositivos de JUCE]
*/
class AudioSettingsComponent final : public juce::Component,
                                     private juce::Timer
{
public:
    explicit AudioSettingsComponent (AudioEngine& engine);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // El motor puede desactivar la opción por su cuenta (si eliges otra salida
    // en el selector), así que la casilla se sincroniza periódicamente.
    void timerCallback() override;

    AudioEngine& engine;
    juce::ToggleButton followSystemButton;      // "Usar la salida predeterminada de Windows"
    juce::Label hintLabel;
    juce::AudioDeviceSelectorComponent selector;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioSettingsComponent)
};
}
