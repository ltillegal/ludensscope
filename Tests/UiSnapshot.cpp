#include "PluginEditor.h"
#include "dsp/ReferenceAnalysis.h"
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

            // Slow swell on the high band so the waterfall has visible history.
            const float highGain = 0.2f + 0.8f * (0.5f + 0.5f * (float) std::sin ((double) blockIndex * 0.01));
            const float high = 0.05f * highGain * (float) std::sin (2.0 * pi * 9000.0 * t);

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

    // Reference file: write a 2 s 1 kHz wav, analyse it, and load it into the
    // player near the end of the snapshot loop.
    juce::File referenceFile (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getChildFile ("ludensscope-reference-test.wav"));
    bool referenceOk = false;
    ReferenceAnalysis referenceAnalysis;

    {
        referenceFile.deleteFile();

        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wavFormat.createWriterFor (new juce::FileOutputStream (referenceFile),
                                       48000.0, 2, 16, juce::StringPairArray(), 0));

        if (writer != nullptr)
        {
            juce::AudioBuffer<float> sine (2, 96000);

            for (int i = 0; i < sine.getNumSamples(); ++i)
            {
                const float value = 0.5f * (float) std::sin (2.0 * pi * 1000.0 * (double) i / 48000.0);
                sine.setSample (0, i, value);
                sine.setSample (1, i, value);
            }

            writer->writeFromAudioSampleBuffer (sine, 0, sine.getNumSamples());
        }
    }

    if (referenceFile.existsAsFile())
    {
        referenceAnalysis = analyzeReferenceFile (referenceFile);

        if (referenceAnalysis.ok)
        {
            int bestBin = 0;

            for (int i = 1; i < SpectrumAnalyzer::numBins; ++i)
                if (referenceAnalysis.levels[(size_t) i] > referenceAnalysis.levels[(size_t) bestBin])
                    bestBin = i;

            std::printf ("reference peak bin %d, LUFS %.2f, samples %d\n",
                         bestBin, referenceAnalysis.lufs,
                         referenceAnalysis.audio != nullptr
                             ? referenceAnalysis.audio->getNumSamples() : 0);

            referenceOk = std::abs (bestBin - 290) <= 4
                          && referenceAnalysis.audio != nullptr
                          && referenceAnalysis.audio->getNumSamples() == 96000
                          && std::abs (referenceAnalysis.lufs + 6.0f) <= 0.6f;
        }
    }

    juce::Timer::callAfterDelay (7000, [&processor, &referenceAnalysis]
    {
        const float lufs = referenceAnalysis.lufs;
        processor.setReferenceAudio (std::move (referenceAnalysis.audio), lufs);
        processor.setReferenceGainDb (0.0f);
        processor.setReferencePlaying (true);
        processor.setReferenceAudible (true);
    });

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

    juce::Timer::callAfterDelay (7500, []
    {
        juce::MessageManager::getInstance()->stopDispatchLoop();
    });

    juce::MessageManager::getInstance()->runDispatchLoop();
    feeder.stopTimer();

    // Exercise the reference curve so the snapshot also covers that path.
    if (auto* ludens = dynamic_cast<LudensScopeEditor*> (editor.get()))
    {
        ludens->captureReference();
        ludens->setGuidesEnabled (true);
    }

    // Exercise the spectrum inspector so the snapshot also covers that path.
    // The editor is never shown on screen, so mark it visible for hit-testing.
    editor->setVisible (true);

    if (auto* target = editor->getComponentAt (628, 200))
    {
        const auto local = target->getLocalPoint (editor.get(), juce::Point<int> (628, 200)).toFloat();
        const juce::MouseEvent moveEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                          local, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                          target, target, juce::Time::getCurrentTime(),
                                          local, juce::Time::getCurrentTime(), 0, false);
        target->mouseMove (moveEvent);
    }

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

    // Clip counter smoke check: one full-scale sample in each channel.
    juce::AudioBuffer<float> clipBuffer (2, 64);
    clipBuffer.clear();
    clipBuffer.setSample (0, 0, 1.0f);
    clipBuffer.setSample (1, 63, -1.0f);
    processor.processBlock (clipBuffer, midi);

    const auto clipped = processor.clippedSampleCount();
    std::printf ("clip counter %llu (expected 2)\n", (unsigned long long) clipped);

    // Undo of a reset restores the long-term loudness and the clip counter.
    const float loudnessBeforeReset = processor.integratedLufs();

    processor.requestUndoCapture();
    processor.requestReset();

    {
        juce::AudioBuffer<float> resetBlock (2, 512);
        resetBlock.clear();
        processor.processBlock (resetBlock, midi);
    }

    const float loudnessAfterReset = processor.integratedLufs();

    processor.requestUndoRestore();

    {
        juce::AudioBuffer<float> undoBlock (2, 512);
        undoBlock.clear();
        processor.processBlock (undoBlock, midi);
    }

    const float loudnessAfterUndo = processor.integratedLufs();
    const bool undoOk = loudnessAfterReset < -90.0f
        && std::abs (loudnessAfterUndo - loudnessBeforeReset) < 0.1f
        && processor.clippedSampleCount() == 2;
    std::printf ("reset undo: %.2f -> %.2f -> %.2f (%s)\n",
                 loudnessBeforeReset, loudnessAfterReset, loudnessAfterUndo,
                 undoOk ? "ok" : "FAIL");

    // The reference player should output the loaded file instead of silence.
    juce::AudioBuffer<float> refOut (2, 512);
    refOut.clear();
    juce::MidiBuffer refMidi;
    processor.processBlock (refOut, refMidi);

    const float referencePeak = refOut.getMagnitude (0, 0, 512);
    std::printf ("reference output peak %.3f\n", referencePeak);

    if (referencePeak < 0.05f)
        referenceOk = false;

    // Snapshot persistence: the project blob must round-trip the slots.
    std::array<float, LudensScopeProcessor::snapshotBins> testLevels {};

    for (int i = 0; i < (int) testLevels.size(); ++i)
        testLevels[(size_t) i] = -60.0f - 0.05f * (float) i;

    processor.setSnapshot (1, testLevels.data());

    LudensScopeProcessor::FftSettings testSettings;
    testSettings.fftSize = 8192;
    testSettings.timingPreset = 2;
    testSettings.tiltDbPerOctave = 4.5f;
    testSettings.windowType = 2;
    testSettings.smoothingPreset = 2;
    testSettings.overlapPreset = 2;
    testSettings.noteGrid = 1;
    processor.setFftSettings (testSettings);

    LudensScopeProcessor::ViewSettings testView;
    testView.guidesEnabled = 1;
    testView.guidePreset = 2;
    testView.sideCurve = 1;
    testView.peakMarkers = 0;
    testView.difference = 1;
    testView.differenceSource = 1;
    testView.spanIndex = 2;
    testView.rangeLow = 50.0f;
    testView.rangeHigh = 5000.0f;
    testView.targetLufs = -16.0f;
    testView.waterfallHistory = 4;
    testView.oscWindow = 4096;
    testView.oscGain = 2.0f;
    processor.setViewSettings (testView);

    processor.setReferenceCurveInfo (referenceFile.getFullPathName(),
                                     referenceAnalysis.levels.data(), true);

    juce::MemoryBlock projectState;
    processor.getStateInformation (projectState);
    processor.clearSnapshots();
    processor.setStateInformation (projectState.getData(), (int) projectState.getSize());

    const auto restoredSettings = processor.getFftSettings();
    const auto restoredView = processor.getViewSettings();
    const auto restoredReference = processor.getReferenceSettings();
    const bool settingsPersisted = restoredSettings.fftSize == 8192
        && restoredSettings.timingPreset == 2
        && restoredSettings.windowType == 2
        && restoredSettings.smoothingPreset == 2
        && restoredSettings.overlapPreset == 2
        && restoredSettings.noteGrid == 1
        && std::abs (restoredSettings.tiltDbPerOctave - 4.5f) < 1.0e-4f;

    const bool viewPersisted = restoredView.guidesEnabled == 1
        && restoredView.guidePreset == 2
        && restoredView.sideCurve == 1
        && restoredView.peakMarkers == 0
        && restoredView.difference == 1
        && restoredView.differenceSource == 1
        && restoredView.spanIndex == 2
        && std::abs (restoredView.rangeLow - 50.0f) < 1.0f
        && std::abs (restoredView.rangeHigh - 5000.0f) < 1.0f
        && std::abs (restoredView.targetLufs + 16.0f) < 0.1f
        && restoredView.waterfallHistory == 4
        && restoredView.oscWindow == 4096
        && std::abs (restoredView.oscGain - 2.0f) < 0.01f;

    const bool referencePersisted = restoredReference.valid
        && restoredReference.path == referenceFile.getFullPathName()
        && std::abs (restoredReference.curve[289] - referenceAnalysis.levels[289]) < 1.0e-4f
        && std::abs (restoredReference.lufs - referenceAnalysis.lufs) < 0.1f;

    const bool snapshotPersisted = processor.isSnapshotFilled (1)
        && std::abs (processor.snapshotLevels (1)[100] - testLevels[100]) < 1.0e-4f
        && settingsPersisted && viewPersisted && referencePersisted;
    std::printf ("snapshot state round-trip: %s\n",
                 snapshotPersisted && settingsPersisted && viewPersisted ? "ok" : "FAIL");

    // The transport envelope should have been built from the reference.
    const auto& waveform = processor.referenceWaveform();
    float waveformEnergy = 0.0f;

    for (const float value : waveform)
        waveformEnergy += std::abs (value);

    const bool waveformOk = waveform.size() == 512 && waveformEnergy > 0.01f;
    std::printf ("reference waveform columns %d, energy %.2f (%s)\n",
                 (int) waveform.size() / 2, waveformEnergy, waveformOk ? "ok" : "FAIL");

    // Loop range: playback must stay inside [0.5, 1.0] s.
    processor.setReferenceLoopRange (0.5, 1.0);
    processor.setReferencePositionSeconds (0.9);
    processor.setReferencePlaying (true);
    processor.setReferenceAudible (true);

    {
        juce::AudioBuffer<float> loopBuffer (2, 512);
        juce::MidiBuffer loopMidi;
        loopBuffer.clear();

        for (int b = 0; b < 47; ++b)
            processor.processBlock (loopBuffer, loopMidi);
    }

    const double loopPosition = (double) processor.referencePositionSamples() / 48000.0;
    const bool loopOk = loopPosition >= 0.49 && loopPosition < 1.01;
    std::printf ("reference loop position %.3f s (%s)\n", loopPosition, loopOk ? "ok" : "FAIL");

    // Two reference slots: B starts empty, A keeps its saved metadata.
    processor.switchReferenceSlot (1);
    const bool slotBEmpty = processor.getActiveReferenceSlot() == 1
        && ! processor.hasReferenceAudio()
        && ! processor.getReferenceSettings().valid;
    processor.switchReferenceSlot (0);
    const bool slotABack = processor.getActiveReferenceSlot() == 0
        && processor.getReferenceSettings().valid
        && processor.getReferenceSettings().path == referenceFile.getFullPathName();
    const bool slotsOk = slotBEmpty && slotABack;
    std::printf ("reference slots: %s\n", slotsOk ? "ok" : "FAIL");

    // Band solo: the 1 kHz tone should pass a 500-2000 Hz solo, 100 Hz cut.
    processor.setReferencePlaying (false);
    processor.setReferenceAudible (false);
    processor.clearReferenceAudio();
    processor.setBandSolo (true, 500.0f, 2000.0f);

    const auto measureBand = [&processor] (double hz)
    {
        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer blockMidi;
        float peak = 0.0f;

        for (int b = 0; b < 40; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                {
                    const double t = (double) (b * 512 + i) / 48000.0;
                    block.setSample (ch, i, (float) (0.5 * std::sin (2.0 * pi * hz * t)));
                }

            processor.processBlock (block, blockMidi);

            if (b == 39)
                peak = block.getMagnitude (0, 0, 512);
        }

        return peak;
    };

    const float passPeak = measureBand (1000.0);
    const float cutPeak = measureBand (100.0);
    const bool soloOk = passPeak > 0.3f && cutPeak < 0.05f;
    std::printf ("band solo: 1 kHz %.3f, 100 Hz %.3f (%s)\n",
                 passPeak, cutPeak, soloOk ? "ok" : "FAIL");

    processor.setBandSolo (false, 0.0f, 0.0f);

    editor.reset();
    return clipped == 2 && undoOk && referenceOk && snapshotPersisted && settingsPersisted
           && referencePersisted
           && viewPersisted && slotsOk && soloOk && loopOk && waveformOk ? 0 : 1;
}
