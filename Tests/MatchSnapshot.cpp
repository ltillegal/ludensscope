#include "match/MatchProcessor.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    constexpr double pi = 3.14159265358979323846;

    void renderTestSignal (juce::AudioBuffer<float>& buffer, int blockIndex)
    {
        const double sampleRate = 48000.0;

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const double t = (double) (blockIndex * buffer.getNumSamples() + i) / sampleRate;
            const float low = 0.20f * (float) std::sin (2.0 * pi * 70.0 * t);
            const float mid = 0.16f * (float) std::sin (2.0 * pi * 900.0 * t);
            const float high = 0.05f * (float) std::sin (2.0 * pi * 7000.0 * t);

            buffer.setSample (0, i, low + mid + high);
            buffer.setSample (1, i, low - mid + 0.8f * high);
        }
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    LudensMatchProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    if (editor == nullptr)
    {
        std::printf ("no editor\n");
        return 1;
    }

    editor->setSize (980, 620);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    for (int block = 0; block < 400; ++block)
    {
        renderTestSignal (buffer, block);
        processor.processBlock (buffer, midi);
    }

    juce::MessageManager::getInstance()->runDispatchLoopUntil (600);

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
