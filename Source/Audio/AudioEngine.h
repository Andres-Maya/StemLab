#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include "AudioMixer.h"
#include "AudioRecorder.h"
#include "Transport.h"

#include <functional>
#include <optional>

namespace stemlab
{
/** Datos de una grabación terminada, para convertirla en pista. */
struct RecordingInfo
{
    juce::File file;
    juce::int64 timelineStart = -1;     // posición del primer sample grabado (-1: no se grabó nada)
    int latencySamples = 0;             // latencia de ida y vuelta entrada + salida
    double sampleRate = 0.0;
};

/**
    Núcleo de audio: conecta el dispositivo (AudioDeviceManager) con el
    transporte, el mezclador y el grabador.

    audioDeviceIOCallbackWithContext() es el hilo de audio (tiempo real): no
    reserva memoria, no bloquea, no toca disco. Todo lo pesado (decodificar,
    IA, guardar) ocurre en otros hilos y llega aquí ya preparado.
*/
class AudioEngine final : private juce::AudioIODeviceCallback,
                          private juce::AsyncUpdater,
                          private juce::ChangeListener,
                          private juce::Timer
{
public:
    AudioEngine();
    ~AudioEngine() override;

    /** Abre el dispositivo de audio. Devuelve un mensaje de error o cadena vacía. */
    juce::String initialise (const juce::XmlElement* savedDeviceState);

    juce::AudioDeviceManager& getDeviceManager() noexcept   { return deviceManager; }
    juce::AudioFormatManager& getFormatManager() noexcept   { return formatManager; }
    Transport& getTransport() noexcept                      { return transport; }
    AudioMixer& getMixer() noexcept                         { return mixer; }
    const AudioMixer& getMixer() const noexcept             { return mixer; }

    double getSampleRate() const noexcept                   { return sampleRate.load(); }

    //==========================================================================
    // Salida predeterminada del sistema (hilo de mensajes)

    /** Si está activo, StemLab usa siempre la salida predeterminada de Windows
        y la sigue cuando cambia (p. ej. al conectar o desconectar audífonos).
        Elegir otra salida a mano en la configuración lo desactiva. */
    void setFollowSystemOutput (bool shouldFollow);
    bool isFollowingSystemOutput() const noexcept           { return followSystemOutput; }

    /** Nombre de la salida predeterminada del sistema para el tipo de dispositivo actual. */
    juce::String getSystemDefaultOutputName() const;

    /** true si el dispositivo de audio está abierto y funcionando. */
    bool isDeviceRunning() const;

    /** Salida que está sonando, o cadena vacía si no hay audio. */
    juce::String getCurrentOutputName() const;

    //==========================================================================
    // Grabación (hilo de mensajes)
    juce::Result startRecording (const juce::File& file);
    RecordingInfo stopRecording();
    bool isRecording() const noexcept                       { return recorder.isRecording(); }

    /** Ganancia digital aplicada a la entrada antes de grabar (dB), con un
        limitador suave para que no recorte de forma brusca. */
    Parameter& getInputGain() noexcept                      { return *inputGain; }

    /** Pico de la entrada (ya con la ganancia) desde la última lectura: para el
        medidor de entrada, funciona aunque no se esté grabando. */
    float getAndResetInputPeak (int channel) noexcept;

    /** Posición en la línea de tiempo donde quedará la grabación en curso (ya
        compensada por latencia), o nada si todavía no ha llegado audio.
        Puede ser negativa al grabar desde el principio: ese trozo se recorta. */
    std::optional<juce::int64> getRecordingClipStart() const noexcept;

    /** Picos nuevos de la grabación en curso, uno por cada
        AudioRecorder::previewBinSize muestras (hilo de mensajes). */
    int readRecordingPeaks (float* dest, int maxPeaks) noexcept { return recorder.readPreviewPeaks (dest, maxPeaks); }

    /** Se llama en el hilo de mensajes si el dispositivo cambió de sample rate
        y hay pistas cargadas a otra frecuencia (hay que recargarlas). */
    std::function<void()> onSampleRateChanged;

private:
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void handleAsyncUpdate() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    /** Sigue la salida de Windows y recupera el dispositivo si se cerró. */
    void updateDevice();
    juce::String getSystemDefaultDeviceName (bool input) const;
    void writeToOutputs (float* const* outputs, int numOutputs, int offset, int numSamples) noexcept;

    juce::AudioDeviceManager deviceManager;
    juce::AudioFormatManager formatManager;

    Transport transport;
    AudioMixer mixer;
    AudioRecorder recorder;

    juce::AudioBuffer<float> mixBus;
    juce::AudioBuffer<float> inputBus;                      // entrada con ganancia (reservado en aboutToStart)
    std::unique_ptr<Parameter> inputGain;
    juce::SmoothedValue<float> inputGainSmoothed;
    std::atomic<float> inputPeaks[2] {};
    std::atomic<double> sampleRate { 44100.0 };
    int recordingLatency = 0;

    bool initialised = false;
    bool followSystemOutput = true;
    bool applyingDeviceChange = false;
    juce::String knownSystemOutput;     // última salida predeterminada vista
    juce::uint32 lastRecoveryAttempt = 0;
    juce::AudioDeviceManager::AudioDeviceSetup lastRunningSetup;   // última configuración que funcionó
    bool hadRunningDevice = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
}
