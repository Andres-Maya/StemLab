#include "MixExporter.h"

#include "Utils/PythonEnvironment.h"
#include "Utils/Strings.h"

#include <string>
#include <algorithm>

namespace stemlab
{
namespace
{
    constexpr auto mp3ScriptName = "stemlab_encode_mp3.py";
    constexpr int renderBlockSize = 1024;

    /** Fuente de audio que lee la mezcla desde una posición de la línea de
        tiempo. Permite pasarla por un ResamplingAudioSource (MP3 a 48 kHz). */
    class MixSource final : public juce::AudioSource
    {
    public:
        MixSource (AudioMixer& m, juce::int64 startPosition) : mixer (m), position (startPosition) {}

        void prepareToPlay (int, double) override       { bus.setSize (2, renderBlockSize); }
        void releaseResources() override                {}

        void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override
        {
            for (int done = 0; done < info.numSamples;)
            {
                const auto count = juce::jmin (renderBlockSize, info.numSamples - done);
                mixer.render (bus, count, position, true);

                for (int ch = 0; ch < info.buffer->getNumChannels(); ++ch)
                    info.buffer->copyFrom (ch, info.startSample + done, bus, juce::jmin (ch, 1), 0, count);

                position += count;
                done += count;
            }
        }

    private:
        AudioMixer& mixer;
        juce::int64 position;
        juce::AudioBuffer<float> bus;
    };

    /** Ruido triangular de ±1 bit menos significativo: al reducir a 16 bits,
        el error de cuantización se convierte en un siseo muy bajo en vez de
        distorsión en los pasajes suaves y las colas. */
    void addDither (juce::AudioBuffer<float>& buffer, int numSamples, juce::Random& random)
    {
        constexpr float lsb = 1.0f / 32768.0f;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* data = buffer.getWritePointer (ch);

            for (int i = 0; i < numSamples; ++i)
                data[i] += (random.nextFloat() - random.nextFloat()) * lsb;
        }
    }
}

//==============================================================================
MixExporter::MixExporter (const AudioMixer& source, double rate)
    : sampleRate (rate)
{
    // Copia independiente de cada pista: sus efectos y rampas de volumen
    // tienen su propio estado, así que el render no interfiere con lo que
    // suena en el dispositivo.
    for (const auto& original : source.getTracks())
        mixer.addTrack (original->createCopy (original->getName()));

    mixer.getMasterVolume().set (source.getMasterVolume().get());
    mixer.prepare (sampleRate, renderBlockSize);
    length = mixer.getContentLength();
}

juce::String MixExporter::getFileExtension (ExportFormat format)
{
    return format == ExportFormat::mp3 ? ".mp3" : ".wav";
}

double MixExporter::getMp3SampleRate (double rate)
{
    // LAME no admite más de 48 kHz: 88,2 kHz se baja a 44,1 y el resto a 48.
    if (rate <= 48000.0)
        return rate;

    return std::abs (std::fmod (rate, 44100.0)) < 1.0 ? 44100.0 : 48000.0;
}

//==============================================================================
ExportResult MixExporter::render (const ExportSettings& settings, std::function<bool (double)> progress)
{
    ExportResult result;
    result.seconds = static_cast<double> (length) / sampleRate;

    // Si progress pide cancelar, las fases devuelven un error y aquí se
    // distingue la cancelación de un fallo real.
    auto cancelled = false;
    const auto report = [&cancelled, progress] (double value)
    {
        if (progress == nullptr || progress (value))
            return true;

        cancelled = true;
        return false;
    };

    if (length <= 0)
    {
        result.status = juce::Result::fail (tr ("No hay nada que exportar: el proyecto no tiene audio."));
        return result;
    }

    if (const auto created = settings.file.getParentDirectory().createDirectory(); created.failed())
    {
        result.status = created;
        return result;
    }

    if (settings.format != ExportFormat::mp3)
    {
        const auto bits = settings.format == ExportFormat::wav16 ? 16 : settings.format == ExportFormat::wav24 ? 24 : 32;

        // Se escribe en un archivo temporal junto al destino y solo al terminar
        // se sustituye: cancelar o fallar nunca deja un WAV a medias.
        juce::TemporaryFile temp (settings.file);
        result.status = renderToWav (temp.getFile(), bits, sampleRate, report, result.peak);

        if (result.status.wasOk() && ! temp.overwriteTargetFileWithTemporary())
            result.status = juce::Result::fail (tr ("No se pudo escribir el archivo:\n{0}", settings.file.getFullPathName()));
    }
    else
    {
        // Render (70 % de la barra) a un WAV temporal de 16 bits + codificación (30 %).
        juce::TemporaryFile wav (".wav");
        juce::TemporaryFile mp3 (settings.file);

        result.status = renderToWav (wav.getFile(), 16, getMp3SampleRate (sampleRate),
                                     [&report] (double p) { return report (p * 0.7); }, result.peak);

        if (result.status.wasOk())
            result.status = encodeMp3 (wav.getFile(), mp3.getFile(), settings.mp3Bitrate,
                                       [&report] (double p) { return report (0.7 + p * 0.3); });

        if (result.status.wasOk() && ! mp3.overwriteTargetFileWithTemporary())
            result.status = juce::Result::fail (tr ("No se pudo escribir el archivo:\n{0}", settings.file.getFullPathName()));
    }

    result.cancelled = cancelled;
    result.clipped = result.peak > 1.0f && settings.format != ExportFormat::wav32Float;

    return result;
}

juce::Result MixExporter::renderToWav (const juce::File& file, int bitsPerSample, double outputRate,
                                       const std::function<bool (double)>& progress, float& peak)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();

    if (stream == nullptr)
        return juce::Result::fail (tr ("No se pudo crear el archivo:\n{0}", file.getFullPathName()));

    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate (outputRate)
                             .withNumChannels (2)
                             .withBitsPerSample (bitsPerSample)
                             .withSampleFormat (bitsPerSample == 32 ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                                    : juce::AudioFormatWriterOptions::SampleFormat::integral);
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, options);

    if (writer == nullptr)
        return juce::Result::fail (tr ("No se pudo crear el escritor WAV."));

    // Precalentamiento en silencio (antes del 0): las rampas de volumen de las
    // pistas parten de 0 y así llegan a su valor antes de la primera muestra.
    const auto preRoll = static_cast<juce::int64> (0.05 * sampleRate);
    MixSource mix (mixer, -preRoll);
    juce::AudioBuffer<float> block (2, renderBlockSize);

    mix.prepareToPlay (renderBlockSize, sampleRate);

    for (juce::int64 skipped = 0; skipped < preRoll;)
    {
        const auto count = static_cast<int> (std::min<juce::int64> (renderBlockSize, preRoll - skipped));
        mix.getNextAudioBlock (juce::AudioSourceChannelInfo (&block, 0, count));
        skipped += count;
    }

    // A otra frecuencia (MP3 por encima de 48 kHz) se remuestrea con filtro.
    const auto ratio = sampleRate / outputRate;
    std::unique_ptr<juce::ResamplingAudioSource> resampler;
    juce::AudioSource* source = &mix;

    if (std::abs (ratio - 1.0) > 1.0e-9)
    {
        resampler = std::make_unique<juce::ResamplingAudioSource> (&mix, false, 2);
        resampler->setResamplingRatio (ratio);
        resampler->prepareToPlay (renderBlockSize, outputRate);
        source = resampler.get();
    }

    const auto total = static_cast<juce::int64> (std::ceil (static_cast<double> (length) / ratio));
    juce::Random random (0x5713);
    peak = 0.0f;

    for (juce::int64 written = 0; written < total;)
    {
        const auto count = static_cast<int> (std::min<juce::int64> (renderBlockSize, total - written));
        source->getNextAudioBlock (juce::AudioSourceChannelInfo (&block, 0, count));

        for (int ch = 0; ch < 2; ++ch)
            peak = juce::jmax (peak, block.getMagnitude (ch, 0, count));

        if (bitsPerSample == 16)
            addDither (block, count, random);

        if (! writer->writeFromAudioSampleBuffer (block, 0, count))
            return juce::Result::fail (tr ("No se pudo escribir en el disco (¿está lleno?)."));

        written += count;

        if (! progress (static_cast<double> (written) / static_cast<double> (total)))
            return juce::Result::fail (tr ("Exportación cancelada."));
    }

    return juce::Result::ok();
}

juce::Result MixExporter::encodeMp3 (const juce::File& wav, const juce::File& mp3, int bitrate,
                                     const std::function<bool (double)>& progress)
{
    const auto python = PythonEnvironment::find (mp3ScriptName);

    if (! python.getScript (mp3ScriptName).existsAsFile())
        return juce::Result::fail (tr ("No se encontró el script de MP3:\n{0}", python.getScript (mp3ScriptName).getFullPathName()));

    auto args = python.commandFor (mp3ScriptName);
    args.addArray ({ "--input", wav.getFullPathName(), "--output", mp3.getFullPathName(),
                     "--bitrate", juce::String (bitrate) });

    juce::ChildProcess process;

    if (! process.start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
        return juce::Result::fail (tr ("No se pudo ejecutar Python ({0}) para codificar el MP3.\n\n"
                                       "Crea el entorno virtual descrito en README.md, o exporta en WAV.", python.pythonCommand));

    juce::String scriptError;
    juce::StringArray logTail;
    auto cancelled = false;

    const auto handleLine = [&] (const juce::String& text)
    {
        if (text.startsWith ("@@PROGRESS "))
        {
            if (! progress (juce::jlimit (0.0, 1.0, text.substring (11).getDoubleValue())))
                cancelled = true;
        }
        else if (text.startsWith ("@@ERROR "))
        {
            scriptError = text.substring (8).replace ("\\n", "\n");
        }
        else if (text.isNotEmpty() && ! text.startsWith ("@@"))
        {
            logTail.add (text);

            if (logTail.size() > 20)
                logTail.remove (0);
        }
    };

    // Se lee byte a byte para procesar cada línea en cuanto llega (como en
    // DemucsSeparator): el script emite una línea por segundo de audio.
    std::string line;
    char c = 0;

    while (! cancelled && process.readProcessOutput (&c, 1) == 1)
    {
        if (c == '\n' || c == '\r')
        {
            if (! line.empty())
                handleLine (juce::String::fromUTF8 (line.data(), static_cast<int> (line.size())).trimEnd());

            line.clear();
        }
        else
        {
            line.push_back (c);
        }
    }

    if (cancelled)
    {
        process.kill();
        return juce::Result::fail (tr ("Exportación cancelada."));
    }

    if (! line.empty())
        handleLine (juce::String::fromUTF8 (line.data(), static_cast<int> (line.size())).trimEnd());

    process.waitForProcessToFinish (10000);

    if (scriptError.isNotEmpty())
        return juce::Result::fail (scriptError);

    if (const auto exitCode = process.getExitCode(); exitCode != 0)
        return juce::Result::fail (tr ("Python terminó con código {0} al codificar el MP3:\n\n{1}",
                                       exitCode, logTail.joinIntoString ("\n")));

    if (! mp3.existsAsFile() || mp3.getSize() == 0)
        return juce::Result::fail (tr ("El codificador no generó el MP3."));

    return juce::Result::ok();
}
}
