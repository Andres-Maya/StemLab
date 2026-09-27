#include "Tests.h"
#include "TestSupport.h"

#include "Audio/MixExporter.h"

// Exportar la mezcla a WAV (el MP3 necesita Python: PythonTests.cpp).

namespace stemlab::test
{
namespace
{
    constexpr double rate = 48000.0;

    std::shared_ptr<AudioTrack> addConstantTrack (AudioMixer& mixer, const char* name, float value,
                                                  double startSeconds, double seconds)
    {
        auto track = std::make_shared<AudioTrack> (name);
        track->setClips ({ makeClip (makeSource ((int) (seconds * rate), value), (juce::int64) (startSeconds * rate)) });
        mixer.addTrack (track);
        return track;
    }

    float sampleAt (const juce::AudioBuffer<float>& audio, double seconds, int channel = 0)
    {
        return audio.getSample (channel, (int) (seconds * rate));
    }

    void testWavExport()
    {
        section ("Exportar mezcla: WAV");

        AudioMixer mixer;
        mixer.prepare (rate, 512);

        // A: 0,25 en [0, 1) s.   B: 0,25 a -6 dB en [0,5, 1,5) s.
        const auto a = addConstantTrack (mixer, "A", 0.25f, 0.0, 1.0);
        const auto b = addConstantTrack (mixer, "B", 0.25f, 0.5, 1.0);
        b->getVolume().set (-6.0f);
        const auto halfGain = juce::Decibels::decibelsToGain (-6.0f);

        MixExporter exporter (mixer, rate);
        CHECK (exporter.getLengthInSamples() == (juce::int64) (1.5 * rate), "exporta desde 0 hasta el final del último fragmento");

        // Cambiar la mezcla después de crear el exportador no afecta al archivo.
        a->getVolume().set (-60.0f);

        const auto file = outputFolder().getChildFile ("mezcla24.wav");
        file.deleteFile();
        std::vector<double> progress;
        const auto result = exporter.render ({ file, ExportFormat::wav24, 0 },
                                            [&] (double p) { progress.push_back (p); return true; });
        CHECK (result.status.wasOk() && file.existsAsFile(), "WAV 24 bits escrito " << result.status.getErrorMessage());
        CHECK (! progress.empty() && std::is_sorted (progress.begin(), progress.end()) && near (progress.back(), 1.0, 1e-9),
               "el progreso crece hasta 1 (" << (int) progress.size() << " avisos)");
        CHECK (near (result.seconds, 1.5, 1e-9), "duración informada: " << result.seconds << " s");

        juce::AudioBuffer<float> audio;
        double fileRate = 0.0;

        if (readAudioFile (file, audio, fileRate))
        {
            CHECK (near (fileRate, rate, 1e-9) && audio.getNumChannels() == 2 && audio.getNumSamples() == (int) (1.5 * rate),
                   "48 kHz, estéreo, 72000 muestras (" << audio.getNumSamples() << ")");
            CHECK (near (sampleAt (audio, 0.25), 0.25, 1e-3), "solo A: 0,25 (" << sampleAt (audio, 0.25) << ")");
            CHECK (near (sampleAt (audio, 0.75), 0.25 + 0.25 * halfGain, 1e-3),
                   "A + B con B a -6 dB: " << sampleAt (audio, 0.75));
            CHECK (near (sampleAt (audio, 1.25), 0.25 * halfGain, 1e-3), "solo B a -6 dB: " << sampleAt (audio, 1.25));
            CHECK (near (audio.getSample (0, 1), 0.0, 0.02), "empieza sin salto: las rampas se estabilizan antes del 0");
            CHECK (near (result.peak, 0.25 + 0.25 * halfGain, 1e-3), "pico de la mezcla: " << result.peak);
        }
        else
        {
            CHECK (false, "no se pudo leer el WAV exportado");
        }

        a->getVolume().set (0.0f);

        section ("Exportar mezcla: mute, solo, efectos y master");

        b->getMute().set (1.0f);
        a->getEffects().findEffect ("gain")->getEnabledParameter().set (1.0f);
        a->getEffects().findEffect ("gain")->findParameter ("gain")->set (6.0f);
        mixer.getMasterVolume().set (-6.0f);

        const auto fx = outputFolder().getChildFile ("mezcla-efectos.wav");
        MixExporter (mixer, rate).render ({ fx, ExportFormat::wav24, 0 }, nullptr);

        if (readAudioFile (fx, audio, fileRate))
        {
            const auto expected = 0.25 * juce::Decibels::decibelsToGain (6.0) * halfGain;
            CHECK (near (sampleAt (audio, 0.25), expected, 2e-3), "efecto Gain +6 dB y master -6 dB: " << sampleAt (audio, 0.25));
            CHECK (near (sampleAt (audio, 1.25), 0.0, 1e-6), "la pista en mute no suena");
            CHECK (audio.getNumSamples() == (int) (1.5 * rate), "una pista en mute sigue contando para la duración");
        }

        b->getMute().set (0.0f);
        b->getSolo().set (1.0f);
        const auto solo = outputFolder().getChildFile ("mezcla-solo.wav");
        MixExporter (mixer, rate).render ({ solo, ExportFormat::wav16, 0 }, nullptr);

        if (readAudioFile (solo, audio, fileRate))
            CHECK (near (sampleAt (audio, 0.25), 0.0, 1e-4) && near (sampleAt (audio, 1.25), 0.25 * halfGain * halfGain, 2e-3),
                   "Solo en B: solo suena B (" << sampleAt (audio, 1.25) << ")");

        b->getSolo().set (0.0f);
        a->getEffects().findEffect ("gain")->getEnabledParameter().set (0.0f);
        mixer.getMasterVolume().set (0.0f);

        section ("Exportar mezcla: formatos y recorte");

        std::unique_ptr<juce::AudioFormatReader> reader;
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        reader.reset (formats.createReaderFor (solo));
        CHECK (reader != nullptr && reader->bitsPerSample == 16, "WAV 16 bits");

        // Mezcla que pasa de 0 dBFS: +12 dB de master sobre 0,375.
        mixer.getMasterVolume().set (6.0f);
        b->getVolume().set (6.0f);
        const auto loud24 = outputFolder().getChildFile ("mezcla-fuerte24.wav");
        const auto loudFloat = outputFolder().getChildFile ("mezcla-fuerte32f.wav");
        const auto clipped = MixExporter (mixer, rate).render ({ loud24, ExportFormat::wav24, 0 }, nullptr);
        const auto kept = MixExporter (mixer, rate).render ({ loudFloat, ExportFormat::wav32Float, 0 }, nullptr);
        CHECK (clipped.peak > 1.0f && clipped.clipped, "WAV 24 bits: avisa del recorte (pico " << clipped.peak << ")");
        CHECK (kept.peak > 1.0f && ! kept.clipped, "WAV 32 bits coma flotante: no recorta");

        reader.reset (formats.createReaderFor (loudFloat));
        CHECK (reader != nullptr && reader->bitsPerSample == 32 && reader->usesFloatingPointData, "WAV 32 bits float");

        if (readAudioFile (loudFloat, audio, fileRate))
            CHECK (sampleAt (audio, 0.75) > 1.0f, "el float conserva los valores por encima de 1 (" << sampleAt (audio, 0.75) << ")");

        if (readAudioFile (loud24, audio, fileRate))
            CHECK (sampleAt (audio, 0.75) <= 1.0f, "el WAV entero queda recortado a 1");

        mixer.getMasterVolume().set (0.0f);
        b->getVolume().set (-6.0f);

        section ("Exportar mezcla: cancelar y errores");

        const auto cancelled = outputFolder().getChildFile ("mezcla-cancelada.wav");
        cancelled.deleteFile();
        const auto cancelResult = MixExporter (mixer, rate).render ({ cancelled, ExportFormat::wav24, 0 },
                                                                    [] (double p) { return p < 0.3; });
        CHECK (cancelResult.cancelled && cancelResult.status.failed(), "cancelar a mitad devuelve 'cancelada'");
        CHECK (! cancelled.existsAsFile(), "y no deja un archivo a medias");

        // Si ya existía, cancelar lo conserva intacto.
        cancelled.replaceWithText ("anterior");
        MixExporter (mixer, rate).render ({ cancelled, ExportFormat::wav24, 0 }, [] (double) { return false; });
        CHECK (cancelled.loadFileAsString() == "anterior", "cancelar no estropea un archivo que ya existía");
        cancelled.deleteFile();

        AudioMixer empty;
        empty.prepare (rate, 512);
        const auto nothing = MixExporter (empty, rate).render ({ outputFolder().getChildFile ("vacio.wav"), ExportFormat::wav24, 0 }, nullptr);
        CHECK (nothing.status.failed(), "un proyecto sin audio no se exporta: " << nothing.status.getErrorMessage());

        CHECK (MixExporter::getFileExtension (ExportFormat::mp3) == ".mp3" && MixExporter::getFileExtension (ExportFormat::wav16) == ".wav",
               "extensiones por formato");
        CHECK (near (MixExporter::getMp3SampleRate (44100.0), 44100.0, 0) && near (MixExporter::getMp3SampleRate (96000.0), 48000.0, 0)
                   && near (MixExporter::getMp3SampleRate (88200.0), 44100.0, 0),
               "MP3 hasta 48 kHz: 96 kHz -> 48 kHz, 88,2 kHz -> 44,1 kHz");
    }
}

void runExportTests()
{
    testWavExport();
}
}
