#include "AudioEngine.h"

#include "Utils/Strings.h"

namespace stemlab
{
namespace
{
    /** Limitador suave: lineal hasta 0,8 y luego se curva sin pasar nunca de
        1,0, así un pico fuerte no produce el recorte brusco (y feo) digital. */
    inline float softClip (float x) noexcept
    {
        constexpr float knee = 0.8f;
        const auto magnitude = std::abs (x);

        if (magnitude <= knee)
            return x;

        const auto shaped = knee + (1.0f - knee) * std::tanh ((magnitude - knee) / (1.0f - knee));
        return std::copysign (shaped, x);
    }
}

AudioEngine::AudioEngine()
    : inputGain (Parameter::continuous ("inputGain", "Entrada",
                                        juce::NormalisableRange<float> (0.0f, 40.0f, 0.5f), 18.0f, "dB"))
{
    formatManager.registerBasicFormats();
    inputGain->setTextFormatter ([] (float db) { return "+" + juce::String (db, 1) + " dB"; });
    inputGainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (inputGain->get()));
}

float AudioEngine::getAndResetInputPeak (int channel) noexcept
{
    return inputPeaks[juce::jlimit (0, 1, channel)].exchange (0.0f, std::memory_order_relaxed);
}

AudioEngine::~AudioEngine()
{
    stopTimer();
    deviceManager.removeChangeListener (this);
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
    recorder.stop();
    cancelPendingUpdate();
}

juce::String AudioEngine::initialise (const juce::XmlElement* savedDeviceState)
{
    // 2 entradas (micrófono / interfaz) y 2 salidas. Si no hay entrada, el
    // dispositivo se abre igualmente y solo falla la grabación.
    const auto error = deviceManager.initialise (2, 2, savedDeviceState, true);
    deviceManager.addAudioCallback (this);

    // El AudioDeviceManager avisa cuando cambia la lista de dispositivos del
    // sistema (en Windows, también cuando cambia la salida predeterminada).
    deviceManager.addChangeListener (this);
    initialised = true;
    updateDevice();

    // Vigilancia: si el dispositivo se cierra o se detiene (p. ej. al conectar o
    // desconectar audífonos), se intenta recuperar sin reiniciar StemLab.
    startTimer (1500);

    return error;
}

//==============================================================================
void AudioEngine::setFollowSystemOutput (bool shouldFollow)
{
    followSystemOutput = shouldFollow;

    // Olvidar la última predeterminada vista: al activarlo se cambia a ella ya.
    knownSystemOutput = {};
    updateDevice();
}

juce::String AudioEngine::getSystemDefaultDeviceName (bool input) const
{
    // WASAPI coloca el dispositivo predeterminado de Windows en el índice que
    // devuelve getDefaultDeviceIndex (el 0). La lista solo es válida después
    // de initialise(), cuando el AudioDeviceManager ya escaneó los dispositivos.
    if (initialised)
    {
        if (auto* type = deviceManager.getCurrentDeviceTypeObject())
        {
            const auto names = type->getDeviceNames (input);
            const auto index = type->getDefaultDeviceIndex (input);

            if (juce::isPositiveAndBelow (index, names.size()))
                return names[index];
        }
    }

    return {};
}

juce::String AudioEngine::getSystemDefaultOutputName() const
{
    return getSystemDefaultDeviceName (false);
}

bool AudioEngine::isDeviceRunning() const
{
    auto* device = deviceManager.getCurrentAudioDevice();
    return device != nullptr && device->isPlaying();
}

juce::String AudioEngine::getCurrentOutputName() const
{
    return isDeviceRunning() ? deviceManager.getAudioDeviceSetup().outputDeviceName : juce::String();
}

void AudioEngine::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateDevice();
}

void AudioEngine::timerCallback()
{
    updateDevice();
}

void AudioEngine::updateDevice()
{
    if (! initialised || applyingDeviceChange)
        return;

    auto* type = deviceManager.getCurrentDeviceTypeObject();

    if (type == nullptr)
        return;

    auto setup = deviceManager.getAudioDeviceSetup();
    const auto systemOutput = getSystemDefaultDeviceName (false);
    const auto systemInput = getSystemDefaultDeviceName (true);

    // Un dispositivo cerrado o detenido nunca vuelve solo: JUCE solo reabre
    // automáticamente si todavía hay uno abierto. Hay que reabrirlo.
    auto needsReopen = false;

    if (isDeviceRunning())
    {
        lastRunningSetup = setup;
        hadRunningDevice = true;
    }
    else if (hadRunningDevice || followSystemOutput)
    {
        needsReopen = true;

        // Tras un fallo al abrir, JUCE borra los nombres de la configuración:
        // se parte de la última que funcionó.
        if (setup.outputDeviceName.isEmpty())
            setup.outputDeviceName = lastRunningSetup.outputDeviceName;

        if (setup.inputDeviceName.isEmpty())
            setup.inputDeviceName = lastRunningSetup.inputDeviceName;
    }

    if (followSystemOutput && systemOutput.isNotEmpty())
    {
        const auto systemChanged = systemOutput != knownSystemOutput;
        knownSystemOutput = systemOutput;

        if (setup.outputDeviceName != systemOutput)
        {
            if (! systemChanged && ! needsReopen)
            {
                // Windows no cambió de salida pero el dispositivo sí: lo eligió
                // el usuario en la configuración (incluso "ninguna"). Se respeta.
                followSystemOutput = false;
                return;
            }

            setup.outputDeviceName = systemOutput;
            needsReopen = true;
        }
    }

    if (! needsReopen)
        return;

    // No reintentar en bucle si el dispositivo sigue fallando.
    const auto now = juce::Time::getMillisecondCounter();

    if (! isDeviceRunning() && now - lastRecoveryAttempt < 3000)
        return;

    lastRecoveryAttempt = now;

    // Al conectar un headset, el controlador puede desactivar el micrófono
    // interno: una entrada que ya no existe impide abrir el dispositivo.
    const auto outputs = type->getDeviceNames (false);
    const auto inputs = type->getDeviceNames (true);

    if (! outputs.contains (setup.outputDeviceName))
        setup.outputDeviceName = systemOutput;

    if (setup.inputDeviceName.isNotEmpty() && ! inputs.contains (setup.inputDeviceName))
        setup.inputDeviceName = systemInput;

    const juce::ScopedValueSetter<bool> applying (applyingDeviceChange, true);

    // Un dispositivo abierto pero detenido no se reinicia con la misma
    // configuración: hay que cerrarlo primero.
    if (deviceManager.getCurrentAudioDevice() != nullptr && ! isDeviceRunning())
        deviceManager.closeAudioDevice();

    auto error = deviceManager.setAudioDeviceSetup (setup, true);

    if (error.isNotEmpty() && setup.inputDeviceName != systemInput)
    {
        setup.inputDeviceName = systemInput;
        error = deviceManager.setAudioDeviceSetup (setup, true);
    }

    if (error.isNotEmpty() && setup.inputDeviceName.isNotEmpty())
    {
        // Último recurso: solo salida. Se puede reproducir aunque no grabar.
        setup.inputDeviceName = {};
        error = deviceManager.setAudioDeviceSetup (setup, true);
    }

    if (error.isNotEmpty())
        DBG ("No se pudo abrir el dispositivo de audio: " << error);
}

//==============================================================================
juce::Result AudioEngine::startRecording (const juce::File& file)
{
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
        return juce::Result::fail ("No hay ningún dispositivo de audio activo."_u8);

    const auto numInputs = device->getActiveInputChannels().countNumberOfSetBits();

    if (numInputs == 0)
        return juce::Result::fail ("El dispositivo actual no tiene entradas activas.\n"
                                   "Actívalas en Audio > Configuración de audio."_u8);

    recordingLatency = device->getInputLatencyInSamples() + device->getOutputLatencyInSamples();

    const auto result = recorder.start (file, device->getCurrentSampleRate(), juce::jmin (numInputs, 2));

    if (result.wasOk())
        transport.play();

    return result;
}

std::optional<juce::int64> AudioEngine::getRecordingClipStart() const noexcept
{
    const auto start = recorder.getStartPosition();

    if (start < 0)
        return std::nullopt;

    return start - recordingLatency;
}

RecordingInfo AudioEngine::stopRecording()
{
    RecordingInfo info;
    info.file = recorder.getFile();
    info.timelineStart = recorder.getStartPosition();
    info.latencySamples = recordingLatency;
    info.sampleRate = getSampleRate();

    recorder.stop();
    return info;
}

//==============================================================================
void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                                    float* const* outputChannelData, int numOutputChannels,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto blockCapacity = mixBus.getNumSamples();

    if (blockCapacity <= 0)
    {
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (outputChannelData[ch] != nullptr)
                juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);

        return;
    }

    const auto blockStart = transport.beginBlock();
    const auto playing = transport.isPlaying();
    const auto numInputs = juce::jmin (numInputChannels, inputBus.getNumChannels());

    inputGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (inputGain->get()));

    // Algunos drivers entregan bloques mayores que el tamaño anunciado: se
    // procesan en trozos que caben en los buffers preparados.
    for (int offset = 0; offset < numSamples;)
    {
        const auto count = juce::jmin (numSamples - offset, blockCapacity);

        // Entrada: ganancia + limitador suave. El medidor funciona siempre; la
        // grabación solo mientras suena el transporte.
        if (numInputs > 0 && inputChannelData != nullptr && inputChannelData[0] != nullptr)
        {
            const float* processed[2] {};
            float peaks[2] {};

            for (int i = 0; i < count; ++i)
            {
                const auto gain = inputGainSmoothed.getNextValue();

                for (int ch = 0; ch < numInputs; ++ch)
                {
                    const auto* source = inputChannelData[ch] != nullptr ? inputChannelData[ch] : inputChannelData[0];
                    const auto sample = softClip (source[offset + i] * gain);
                    inputBus.getWritePointer (ch)[i] = sample;
                    peaks[ch] = juce::jmax (peaks[ch], std::abs (sample));
                }
            }

            for (int ch = 0; ch < numInputs; ++ch)
            {
                processed[ch] = inputBus.getReadPointer (ch);
                inputPeaks[ch].store (juce::jmax (inputPeaks[ch].load (std::memory_order_relaxed), peaks[ch]),
                                      std::memory_order_relaxed);
            }

            if (playing)
                recorder.write (processed, numInputs, count, blockStart + offset);
        }

        mixer.render (mixBus, count, blockStart + offset, playing);
        writeToOutputs (outputChannelData, numOutputChannels, offset, count);
        offset += count;
    }

    if (playing)
    {
        const auto end = mixer.getContentLength();

        // Sin audio (ninguna pista, o todas vacías) no hay nada que reproducir:
        // el cabezal se queda donde está. Al grabar sí avanza.
        if (! recorder.isRecording() && end <= 0)
        {
            transport.pause();
            return;
        }

        transport.advance (numSamples);

        // Fin de la canción: parar y volver al inicio (salvo si se está grabando).
        if (! recorder.isRecording() && blockStart + numSamples >= end)
            transport.stop();
    }
}

void AudioEngine::writeToOutputs (float* const* outputs, int numOutputs, int offset, int numSamples) noexcept
{
    const auto* left = mixBus.getReadPointer (0);
    const auto* right = mixBus.getReadPointer (1);

    for (int ch = 0; ch < numOutputs; ++ch)
    {
        auto* out = outputs[ch];

        if (out == nullptr)
            continue;

        out += offset;

        if (numOutputs == 1)
        {
            juce::FloatVectorOperations::copy (out, left, numSamples);
            juce::FloatVectorOperations::add (out, right, numSamples);
            juce::FloatVectorOperations::multiply (out, 0.5f, numSamples);
        }
        else if (ch < 2)
        {
            juce::FloatVectorOperations::copy (out, ch == 0 ? left : right, numSamples);
        }
        else
        {
            juce::FloatVectorOperations::clear (out, numSamples);
        }
    }
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const auto newRate = device->getCurrentSampleRate();
    const auto blockSize = juce::jmax (1, device->getCurrentBufferSizeSamples());

    mixBus.setSize (2, blockSize, false, true, false);
    inputBus.setSize (2, blockSize, false, true, false);
    inputGainSmoothed.reset (newRate, 0.05);
    inputGainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (inputGain->get()));
    mixer.prepare (newRate, blockSize);

    if (! juce::exactlyEqual (sampleRate.exchange (newRate), newRate))
        triggerAsyncUpdate();
}

void AudioEngine::audioDeviceStopped()
{
    mixBus.setSize (2, 0);
    inputBus.setSize (2, 0);
}

void AudioEngine::handleAsyncUpdate()
{
    // Hilo de mensajes: si alguna pista se decodificó a otra frecuencia, sonaría
    // desafinada y a otra velocidad. El ProjectManager la vuelve a cargar.
    const auto rate = getSampleRate();

    for (const auto& track : mixer.getTracks())
    {
        const auto trackRate = track->getSampleRate();   // 0 = pista vacía

        if (trackRate > 0.0 && std::abs (trackRate - rate) > 0.5)
        {
            transport.stop();

            if (onSampleRateChanged != nullptr)
                onSampleRateChanged();

            return;
        }
    }
}
}
