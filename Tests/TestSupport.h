#pragma once

// Utilidades comunes de las pruebas: comprobaciones, señales de prueba,
// archivos WAV sintéticos y render del mezclador.

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include "Audio/AudioClip.h"
#include "Audio/AudioMixer.h"
#include "DSP/AudioEffect.h"

#include <functional>
#include <iostream>
#include <memory>

namespace stemlab::test
{
/** Comprobaciones hechas y fallidas en toda la ejecución. */
extern int checks;
extern int failures;

#define CHECK(cond, msg)                                                          \
    do {                                                                          \
        ++::stemlab::test::checks;                                                \
        if (! (cond)) { ++::stemlab::test::failures; std::cout << "  FALLO: " << msg << "\n"; } \
        else          { std::cout << "  ok: " << msg << "\n"; }                   \
    } while (false)

void section (const char* name);

/** Carpeta para los archivos que generan las pruebas (WAV, proyectos,
    capturas PNG). Por defecto test-output/ junto al ejecutable. */
juce::File outputFolder();
void setOutputFolder (const juce::File& folder);

bool near (double a, double b, double tolerance);

//==============================================================================
// Señales y análisis
juce::AudioBuffer<float> makeSine (int channels, int numSamples, double frequency, double sampleRate, float amplitude);
float peakFrom (const juce::AudioBuffer<float>& buffer, int start);
float rmsFrom (const juce::AudioBuffer<float>& buffer, int start);
void processEffect (AudioEffect& effect, juce::AudioBuffer<float>& buffer, double sampleRate);

/** Procesa mensajes (cargas asíncronas, timers) hasta que done() o el tiempo máximo. */
void runLoopUntil (std::function<bool()> done, int timeoutMs);

/** WAV de 24 bits con un tono (amplitud 0,5). */
juce::File writeToneWav (const juce::File& file, double sampleRate, int channels, double seconds, double frequency);

/** WAV de 24 bits con un valor constante: fácil de reconocer al renderizar. */
juce::File writeConstantWav (const juce::File& file, double sampleRate, double seconds, float value);

/** Lee un archivo de audio entero (sin remuestrear). */
bool readAudioFile (const juce::File& file, juce::AudioBuffer<float>& destination, double& sampleRate);

void saveSnapshot (const juce::Image& image, const juce::String& name);

//==============================================================================
// Clips y mezclador
std::shared_ptr<ClipSource> makeSource (int numSamples, float value, bool ramp = false, double sampleRate = 48000.0);
AudioClip makeClip (std::shared_ptr<ClipSource> source, juce::int64 start, juce::int64 offset = 0, juce::int64 length = -1);

/** Renderiza [start, start + count) tras un precalentamiento que estabiliza las rampas de volumen. */
juce::AudioBuffer<float> renderSpan (AudioMixer& mixer, juce::int64 start, int count);

/** Evento de ratón sintético sobre un componente (para probar la interacción). */
juce::MouseEvent mouseEventAt (juce::Component& component, juce::Point<float> position,
                               juce::Point<float> mouseDownPosition, bool wasDragged);
}
