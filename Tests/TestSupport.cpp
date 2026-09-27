#include "TestSupport.h"

namespace stemlab::test
{
int checks = 0;
int failures = 0;

namespace
{
    juce::File& outputFolderStorage()
    {
        static juce::File folder = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                                       .getParentDirectory().getChildFile ("test-output");
        return folder;
    }
}

void section (const char* name)
{
    std::cout << "\n== " << name << "\n";
}

juce::File outputFolder()
{
    auto& folder = outputFolderStorage();
    folder.createDirectory();
    return folder;
}

void setOutputFolder (const juce::File& folder)
{
    outputFolderStorage() = folder;
}

bool near (double a, double b, double tolerance)
{
    return std::abs (a - b) <= tolerance;
}

//==============================================================================
juce::AudioBuffer<float> makeSine (int channels, int numSamples, double frequency, double sampleRate, float amplitude)
{
    juce::AudioBuffer<float> buffer (channels, numSamples);

    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < numSamples; ++i)
            buffer.setSample (ch, i, amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * frequency * i / sampleRate));

    return buffer;
}

float peakFrom (const juce::AudioBuffer<float>& buffer, int start)
{
    float peak = 0.0f;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        peak = juce::jmax (peak, buffer.getMagnitude (ch, start, buffer.getNumSamples() - start));

    return peak;
}

float rmsFrom (const juce::AudioBuffer<float>& buffer, int start)
{
    return buffer.getRMSLevel (0, start, buffer.getNumSamples() - start);
}

void processEffect (AudioEffect& effect, juce::AudioBuffer<float>& buffer, double sampleRate)
{
    effect.prepare ({ sampleRate, 512u, 2u });
    juce::dsp::AudioBlock<float> block (buffer);

    for (int start = 0; start < buffer.getNumSamples(); start += 512)
        effect.process (block.getSubBlock ((size_t) start, (size_t) juce::jmin (512, buffer.getNumSamples() - start)));
}

void runLoopUntil (std::function<bool()> done, int timeoutMs)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

    while (! done() && juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
}

namespace
{
    juce::File writeWav (const juce::File& file, double sampleRate, const juce::AudioBuffer<float>& audio)
    {
        file.deleteFile();
        file.getParentDirectory().createDirectory();
        std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate)
                                                       .withNumChannels (audio.getNumChannels()).withBitsPerSample (24));
        writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
        return file;
    }
}

juce::File writeToneWav (const juce::File& file, double sampleRate, int channels, double seconds, double frequency)
{
    return writeWav (file, sampleRate, makeSine (channels, (int) (sampleRate * seconds), frequency, sampleRate, 0.5f));
}

juce::File writeConstantWav (const juce::File& file, double sampleRate, double seconds, float value)
{
    juce::AudioBuffer<float> audio (2, (int) (sampleRate * seconds));

    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill (audio.getWritePointer (ch), value, audio.getNumSamples());

    return writeWav (file, sampleRate, audio);
}

bool readAudioFile (const juce::File& file, juce::AudioBuffer<float>& destination, double& sampleRate)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
        return false;

    destination.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&destination, 0, destination.getNumSamples(), 0, true, true);
    sampleRate = reader->sampleRate;
    return true;
}

void saveSnapshot (const juce::Image& image, const juce::String& name)
{
    const auto png = outputFolder().getChildFile (name);
    png.deleteFile();
    juce::FileOutputStream out (png);
    juce::PNGImageFormat().writeImageToStream (image, out);
}

//==============================================================================
std::shared_ptr<ClipSource> makeSource (int numSamples, float value, bool ramp, double sampleRate)
{
    auto source = std::make_shared<ClipSource>();
    source->sampleRate = sampleRate;
    source->audio.setSize (2, numSamples);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < numSamples; ++i)
            source->audio.setSample (ch, i, ramp ? (float) i / (float) numSamples : value);

    return source;
}

AudioClip makeClip (std::shared_ptr<ClipSource> source, juce::int64 start, juce::int64 offset, juce::int64 length)
{
    const auto clipLength = length < 0 ? source->getLength() - offset : length;
    return { AudioClip::createId(), std::move (source), start, offset, clipLength };
}

juce::AudioBuffer<float> renderSpan (AudioMixer& mixer, juce::int64 start, int count)
{
    juce::AudioBuffer<float> block (2, 512), out (2, count);

    for (juce::int64 pos = start - 4096; pos < start; pos += 512)
        mixer.render (block, 512, pos, true);

    for (int done = 0; done < count;)
    {
        const auto n = juce::jmin (512, count - done);
        mixer.render (block, n, start + done, true);

        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, done, block, ch, 0, n);

        done += n;
    }

    return out;
}

juce::MouseEvent mouseEventAt (juce::Component& component, juce::Point<float> position,
                               juce::Point<float> mouseDownPosition, bool wasDragged)
{
    const auto now = juce::Time::getCurrentTime();

    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), position,
                             juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                             juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                             juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
                             juce::MouseInputSource::defaultTiltY, &component, &component, now,
                             mouseDownPosition, now, 1, wasDragged);
}
}
