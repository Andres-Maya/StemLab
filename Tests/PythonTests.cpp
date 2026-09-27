#include "Tests.h"
#include "TestSupport.h"

#include "AI/DemucsSeparator.h"
#include "Audio/AudioFileLoader.h"
#include "Audio/MixExporter.h"

#include <atomic>
#include <thread>

// Pruebas que ejecutan Python (--python): exportar a MP3 con lameenc y separar
// instrumentos con Demucs. Necesitan el entorno python/.venv (ver README.md).

namespace stemlab::test
{
namespace
{
    /** Mezcla sintética para Demucs: bombo, bajo, un acorde y ruido de platillos. */
    juce::File writeSyntheticSong (const juce::File& file, double sampleRate, double seconds)
    {
        const auto numSamples = (int) (sampleRate * seconds);
        juce::AudioBuffer<float> song (2, numSamples);
        juce::Random random (42);
        const auto twoPi = juce::MathConstants<double>::twoPi;
        const auto beat = sampleRate * 0.5;     // 120 BPM

        for (int i = 0; i < numSamples; ++i)
        {
            const auto t = i / sampleRate;
            const auto inBeat = std::fmod ((double) i, beat) / sampleRate;
            const auto kick = std::sin (twoPi * (50.0 + 80.0 * std::exp (-inBeat * 30.0)) * inBeat) * std::exp (-inBeat * 8.0);
            const auto bass = 0.3 * std::sin (twoPi * (i < numSamples / 2 ? 55.0 : 73.4) * t);
            const auto chord = 0.08 * (std::sin (twoPi * 261.6 * t) + std::sin (twoPi * 329.6 * t) + std::sin (twoPi * 392.0 * t));
            const auto offBeat = std::fmod ((double) i + beat * 0.5, beat) / sampleRate;
            const auto hat = 0.15 * (random.nextFloat() * 2.0f - 1.0f) * std::exp (-offBeat * 40.0);
            const auto value = (float) (0.5 * kick + bass + chord + hat);
            song.setSample (0, i, value);
            song.setSample (1, i, value);
        }

        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate)
                                                       .withNumChannels (2).withBitsPerSample (24));
        writer->writeFromAudioSampleBuffer (song, 0, numSamples);
        return file;
    }

    void testMp3Export()
    {
        section ("Exportar mezcla: MP3 (Python + LAME)");

        for (const auto rate : { 48000.0, 96000.0 })
        {
            AudioMixer mixer;
            mixer.prepare (rate, 512);
            auto track = std::make_shared<AudioTrack> ("Tono");
            auto source = std::make_shared<ClipSource>();
            source->sampleRate = rate;
            source->audio = makeSine (2, (int) (3.0 * rate), 440.0, rate, 0.5f);
            track->setClips ({ makeClip (source, 0) });
            mixer.addTrack (track);

            const auto file = outputFolder().getChildFile ("mezcla-" + juce::String ((int) rate) + ".mp3");
            file.deleteFile();
            std::vector<double> progress;
            const auto result = MixExporter (mixer, rate).render ({ file, ExportFormat::mp3, 192 },
                                                                  [&] (double p) { progress.push_back (p); return true; });
            CHECK (result.status.wasOk() && file.existsAsFile(),
                   "MP3 desde " << (int) rate << " Hz escrito " << result.status.getErrorMessage());
            CHECK (! progress.empty() && near (progress.back(), 1.0, 1e-9) && std::is_sorted (progress.begin(), progress.end()),
                   "progreso del render y de la codificación hasta 1");

            // 192 kbps durante 3 s son unos 72 kB.
            CHECK (file.getSize() > 60000 && file.getSize() < 90000, "tamaño acorde a 192 kbps: " << (int) file.getSize() << " bytes");

            juce::AudioBuffer<float> decoded;
            double decodedRate = 0.0;

            if (readAudioFile (file, decoded, decodedRate))
            {
                CHECK (near (decodedRate, MixExporter::getMp3SampleRate (rate), 0.0), "frecuencia del MP3: " << decodedRate << " Hz");
                CHECK (std::abs (decoded.getNumSamples() / decodedRate - 3.0) < 0.1, "dura unos 3 s (" << decoded.getNumSamples() / decodedRate << ")");

                const auto rms = decoded.getRMSLevel (0, (int) decodedRate, (int) decodedRate);
                CHECK (near (rms, 0.5 / std::sqrt (2.0), 0.02), "el nivel se conserva (RMS " << rms << ")");
            }
            else
            {
                CHECK (false, "JUCE no pudo leer el MP3 exportado");
            }
        }

        AudioMixer mixer;
        mixer.prepare (48000.0, 512);
        auto track = std::make_shared<AudioTrack> ("Tono");
        track->setClips ({ makeClip (makeSource (48000 * 20, 0.2f), 0) });
        mixer.addTrack (track);
        const auto file = outputFolder().getChildFile ("mezcla-cancelada.mp3");
        file.deleteFile();
        const auto result = MixExporter (mixer, 48000.0).render ({ file, ExportFormat::mp3, 320 },
                                                                [] (double p) { return p < 0.85; });
        CHECK (result.cancelled && ! file.existsAsFile(), "cancelar durante la codificación no deja un MP3 a medias");
    }

    struct RecordingProgress : SeparationProgress
    {
        void setProgress (double p) override    { if (p >= 0) values.push_back (p); }
        void setStatus (const juce::String& s) override { statuses.add (s); }
        bool shouldCancel() const override      { return cancel.load(); }

        std::vector<double> values;
        juce::StringArray statuses;
        std::atomic<bool> cancel { false };
    };

    void testSeparation()
    {
        section ("IA: DemucsSeparator (C++ -> Python -> Demucs)");

        const auto settings = DemucsSeparator::findDefaultSettings();
        CHECK (settings.pythonCommand.contains (".venv"), "encuentra el entorno virtual: " << settings.pythonCommand);
        CHECK (settings.scriptFile.existsAsFile(), "encuentra el script");

        // Una "canción" sintética de 8 s (bombo, bajo, acordes, platillos).
        const auto clip = writeSyntheticSong (outputFolder().getChildFile ("cancion-sintetica.wav"), 44100.0, 8.0);

        DemucsSeparator separator (settings);
        CHECK (separator.getAvailableModels().size() == 3 && separator.getCurrentModel() == "htdemucs", "modelos disponibles");

        const auto out = outputFolder().getChildFile ("cpp-stems");
        out.deleteRecursively();

        RecordingProgress progress;
        const auto start = juce::Time::getMillisecondCounterHiRes();
        const auto result = separator.separate ({ clip, out }, progress);
        const auto seconds = (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;

        CHECK (result.status.wasOk(), "separación correcta en " << juce::String (seconds, 1) << " s " << result.status.getErrorMessage());
        CHECK (result.stems.size() == 4 && result.stems.front().name == "vocals" && result.stems.back().name == "other",
               "4 stems ordenados (voz primero, otros al final)");
        CHECK (! progress.values.empty() && std::is_sorted (progress.values.begin(), progress.values.end())
                   && near (progress.values.back(), 1.0, 1e-9),
               "progreso creciente hasta 1 (" << (int) progress.values.size() << " actualizaciones)");
        CHECK (progress.statuses.size() >= 4, "mensajes de estado: " << progress.statuses.joinIntoString (" | "));

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        juce::AudioBuffer<float> vocals, drums, bass, other, original;
        const std::pair<juce::AudioBuffer<float>*, const char*> stemFiles[] { { &vocals, "vocals" }, { &drums, "drums" }, { &bass, "bass" }, { &other, "other" } };
        for (const auto& [buf, name] : stemFiles)
            AudioFileLoader::load (formats, out.getChildFile (juce::String (name) + ".wav"), 44100.0, *buf);
        AudioFileLoader::load (formats, clip, 44100.0, original);

        double err = 0, ref = 0;
        const auto n = juce::jmin (original.getNumSamples(), vocals.getNumSamples());
        for (int i = 0; i < n; ++i)
        {
            const auto sum = vocals.getSample (0, i) + drums.getSample (0, i) + bass.getSample (0, i) + other.getSample (0, i);
            err += std::pow (sum - original.getSample (0, i), 2.0);
            ref += std::pow (original.getSample (0, i), 2.0);
        }
        const auto snr = 10.0 * std::log10 (ref / juce::jmax (err, 1e-12));
        CHECK (snr > 15.0, "la suma de los stems reconstruye la mezcla (SNR " << juce::String (snr, 1) << " dB)");

        section ("IA: cancelar una separación en curso");
        RecordingProgress cancelProgress;
        SeparationResult cancelResult;
        std::thread worker ([&] { cancelResult = separator.separate ({ clip, outputFolder().getChildFile ("cpp-stems-cancel") }, cancelProgress); });
        juce::Thread::sleep (6000);
        const auto cancelStart = juce::Time::getMillisecondCounter();
        cancelProgress.cancel = true;
        separator.cancel();
        worker.join();
        CHECK (cancelResult.cancelled, "resultado marcado como cancelado");
        CHECK (juce::Time::getMillisecondCounter() - cancelStart < 3000, "el proceso de Python se detiene al momento");

        section ("IA: error de Python bien reportado");
        auto badSettings = settings;
        badSettings.model = "modelo_que_no_existe";
        DemucsSeparator bad (badSettings);
        RecordingProgress badProgress;
        const auto badResult = bad.separate ({ clip, outputFolder().getChildFile ("cpp-stems-bad") }, badProgress);
        CHECK (badResult.status.failed() && badResult.status.getErrorMessage().isNotEmpty(),
               "modelo inexistente -> error: " << badResult.status.getErrorMessage().substring (0, 120));
    }
}

void runPythonTests()
{
    testMp3Export();
    testSeparation();
}
}
