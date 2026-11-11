#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <cmath>
#include <cstdio>

namespace
{
    constexpr double pi = 3.14159265358979323846;

    void renderTestSignal (juce::AudioBuffer<float>& buffer, int blockIndex)
    {
        constexpr double sampleRate = 48000.0;
        const int numSamples = buffer.getNumSamples();

        for (int i = 0; i < numSamples; ++i)
        {
            const double t = (double) (blockIndex * numSamples + i) / sampleRate;
            const float low = 0.18f * (float) std::sin (2.0 * pi * 70.0 * t);
            const float mid = 0.22f * (float) std::sin (2.0 * pi * 950.0 * t);
            const float high = 0.05f * (float) std::sin (2.0 * pi * 9000.0 * t);

            buffer.setSample (0, i, low + mid + high);
            buffer.setSample (1, i, low - mid + 0.8f * high);
        }
    }

    // Keeps audio flowing into the processor while the message loop runs, so
    // the meters show real values in the snapshot instead of -100 dB.
    struct Feeder final : juce::Timer
    {
        Feeder (LudensScopeProcessor& p, juce::AudioBuffer<float>& b, juce::MidiBuffer& m)
            : processor (p), buffer (b), midi (m) {}

        void timerCallback() override
        {
            for (int i = 0; i < 8; ++i)
            {
                renderTestSignal (buffer, blockIndex++);
                processor.processBlock (buffer, midi);
            }
        }

        LudensScopeProcessor& processor;
        juce::AudioBuffer<float>& buffer;
        juce::MidiBuffer& midi;
        int blockIndex = 400;
    };
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    LudensScopeProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    for (int block = 0; block < 400; ++block)
    {
        renderTestSignal (buffer, block);
        processor.processBlock (buffer, midi);
    }

    std::printf ("momentary %.2f LUFS, integrated %.2f LUFS\n",
                 processor.momentaryLufs(), processor.integratedLufs());

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setSize (1060, 700);

    Feeder feeder (processor, buffer, midi);
    feeder.startTimerHz (120);

    juce::Timer::callAfterDelay (700, []
    {
        juce::MessageManager::getInstance()->stopDispatchLoop();
    });

    juce::MessageManager::getInstance()->runDispatchLoop();
    feeder.stopTimer();

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);

    juce::File output = juce::File::getCurrentWorkingDirectory()
                            .getChildFile ("docs/ui-preview.png");

    if (argc > 1 && argv[1] != nullptr && *argv[1] != 0)
        output = juce::File::getCurrentWorkingDirectory()
                     .getChildFile (juce::String::fromUTF8 (argv[1]));

    output.getParentDirectory().createDirectory();
    output.deleteFile();

    if (auto stream = std::unique_ptr<juce::FileOutputStream> (output.createOutputStream()))
    {
        juce::PNGImageFormat png;
        png.writeImageToStream (image, *stream);
        std::printf ("wrote %s (%dx%d)\n", output.getFullPathName().toRawUTF8(),
                     image.getWidth(), image.getHeight());
    }

    editor.reset();
    return 0;
}
