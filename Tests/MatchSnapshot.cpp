#include <juce_audio_formats/juce_audio_formats.h>

#include "Licence.h"

#include "match/MatchProcessor.h"
#include "match/MatchEditor.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <vector>

namespace
{
    // The mix is bright, the reference is dark: the correction should slope
    // down, which is what the snapshot has to show.
    void renderMix (juce::AudioBuffer<float>& buffer, juce::Random& rng, float lowpass)
    {
        static float left = 0.0f;
        static float right = 0.0f;

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            left  = lowpass * left  + (1.0f - lowpass) * (rng.nextFloat() * 2.0f - 1.0f);
            right = lowpass * right + (1.0f - lowpass) * (rng.nextFloat() * 2.0f - 1.0f);

            buffer.setSample (0, i, 0.30f * left);
            buffer.setSample (1, i, 0.26f * right);
        }
    }

    juce::File writeReferenceFile()
    {
        const int numSamples = 48000 * 6;
        const double sampleRate = 48000.0;

        juce::AudioBuffer<float> audio (2, numSamples);
        juce::Random rng (20261127);
        renderMix (audio, rng, 0.90f);   // dark

        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("ludensmatch-reference.wav");
        file.deleteFile();

        juce::WavAudioFormat format;
        std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());

        if (stream == nullptr)
            return {};

        std::unique_ptr<juce::AudioFormatWriter> writer (
            format.createWriterFor (stream.get(), sampleRate, 2, 24, {}, 0));

        if (writer == nullptr)
            return {};

        stream.release();
        writer->writeFromAudioSampleBuffer (audio, 0, numSamples);
        return file;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    // Optional: render the FULL state with a real key.
    LudensScopeLicence::setProduct ("LudensMatch");

    const auto licenceDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("ludensmatch-licence-test");
    licenceDir.createDirectory();
    LudensScopeLicence::setLicenceDirectoryForTesting (licenceDir);

    if (auto* key = std::getenv ("LUDENSCOPE_MATCH_KEY"))
        LudensScopeLicence::save (key);
    else
        licenceDir.getChildFile ("licence.txt").deleteFile();   // clean machine


    LudensMatchProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    auto* editor = static_cast<LudensMatchEditor*> (processor.createEditor());

    if (editor == nullptr)
    {
        std::printf ("no editor\n");
        return 1;
    }

    std::unique_ptr<juce::AudioProcessorEditor> owner (editor);
    editor->setSize (980, 660);

    const auto reference = writeReferenceFile();

    if (reference.existsAsFile())
        editor->loadReferenceFile (reference);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    juce::Random rng (42);

    for (int block = 0; block < 600; ++block)
    {
        renderMix (buffer, rng, 0.25f);   // bright
        processor.processBlock (buffer, midi);
    }

    juce::MessageManager::getInstance()->runDispatchLoopUntil (1200);

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile ("docs/match-preview.png");
    file.deleteFile();

    if (auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
    {
        juce::PNGImageFormat png;
        png.writeImageToStream (image, *stream);
        std::printf ("wrote %s (%dx%d)\n", file.getFullPathName().toRawUTF8(),
                     image.getWidth(), image.getHeight());
    }

    return 0;
}
