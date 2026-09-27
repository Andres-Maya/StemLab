#include "AudioEngine.h"

#include "Utils/Strings.h"

namespace stemlab
{
AudioEngine::AudioEngine()
{
    formatManager.registerBasicFormats();
}

AudioEngine::~AudioEngine()
{
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
    updateFollowedOutput();

    return error;
}

//==============================================================================
void AudioEngine::setFollowSystemOutput (bool shouldFollow)
{
    followSystemOutput = shouldFollow;

    // Olvidar la última predeterminada vista: al activarlo se cambia a ella ya.
    knownSystemOutput = {};
    updateFollowedOutput();
}

juce::String AudioEngine::getSystemDefaultOutputName() const
{
    // WASAPI coloca la salida predeterminada de Windows en el índice que
    // devuelve getDefaultDeviceIndex (el 0).
    if (deviceManager.getCurrentAudioDevice() != nullptr)
    {
        if (auto* type = deviceManager.getCurrentDeviceTypeObject())
        {
            const auto names = type->getDeviceNames (false);
            const auto index = type->getDefaultDeviceIndex (false);

            if (juce::isPositiveAndBelow (index, names.size()))
                return names[index];
        }
    }

    return {};
}

void AudioEngine::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateFollowedOutput();
}

void AudioEngine::updateFollowedOutput()
{
    if (! followSystemOutput || applyingSystemOutput)
        return;

    const auto systemOutput = getSystemDefaultOutputName();

    if (systemOutput.isEmpty())
        return;

    auto setup = deviceManager.getAudioDeviceSetup();
    const auto systemChanged = systemOutput != knownSystemOutput;
    knownSystemOutput = systemOutput;

    if (setup.outputDeviceName == systemOutput)
        return;

    if (! systemChanged)
    {
        // Windows no cambió de salida pero el dispositivo sí: lo eligió el
        // usuario en la configuración (incluso "ninguna"). Se respeta.
        followSystemOutput = false;
        return;
    }

    setup.outputDeviceName = systemOutput;

    const juce::ScopedValueSetter<bool> applying (applyingSystemOutput, true);
    const auto error = deviceManager.setAudioDeviceSetup (setup, true);

    if (error.isNotEmpty())
        DBG ("No se pudo cambiar a la salida del sistema: " << error);
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

    if (playing)
        recorder.write (inputChannelData, numInputChannels, numSamples, blockStart);

    // Algunos drivers entregan bloques mayores que el tamaño anunciado: se
    // procesan en trozos que caben en los buffers preparados.
    for (int offset = 0; offset < numSamples;)
    {
        const auto count = juce::jmin (numSamples - offset, blockCapacity);
        mixer.render (mixBus, count, blockStart + offset, playing);
        writeToOutputs (outputChannelData, numOutputChannels, offset, count);
        offset += count;
    }

    if (playing)
    {
        transport.advance (numSamples);

        // Fin de la canción: parar y volver al inicio (salvo si se está grabando).
        const auto end = mixer.getContentLength();

        if (! recorder.isRecording() && end > 0 && blockStart + numSamples >= end)
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
    mixer.prepare (newRate, blockSize);

    if (! juce::exactlyEqual (sampleRate.exchange (newRate), newRate))
        triggerAsyncUpdate();
}

void AudioEngine::audioDeviceStopped()
{
    mixBus.setSize (2, 0);
}

void AudioEngine::handleAsyncUpdate()
{
    // Hilo de mensajes: si alguna pista se decodificó a otra frecuencia, sonaría
    // desafinada y a otra velocidad. El ProjectManager la vuelve a cargar.
    const auto rate = getSampleRate();

    for (const auto& track : mixer.getTracks())
    {
        if (std::abs (track->getSampleRate() - rate) > 0.5)
        {
            transport.stop();

            if (onSampleRateChanged != nullptr)
                onSampleRateChanged();

            return;
        }
    }
}
}
