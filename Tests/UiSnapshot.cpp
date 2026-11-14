#include "PluginEditor.h"
#include "ui/CurvePresets.h"
#include "dsp/ReferenceAnalysis.h"
#include "PluginProcessor.h"

#include <cmath>
#include <cstdio>
#include <vector>

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

    // Mono reference: the same sine in one channel must measure ~3.01 dB
    // below the dual-mono stereo file (no double counting in LUFS).
    juce::File monoReferenceFile (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                      .getChildFile ("ludensscope-reference-mono-test.wav"));
    monoReferenceFile.deleteFile();

    {
        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wavFormat.createWriterFor (new juce::FileOutputStream (monoReferenceFile),
                                       48000.0, 1, 16, juce::StringPairArray(), 0));

        if (writer != nullptr)
        {
            juce::AudioBuffer<float> sine (1, 96000);

            for (int i = 0; i < sine.getNumSamples(); ++i)
                sine.setSample (0, i, 0.5f * (float) std::sin (2.0 * pi * 1000.0 * (double) i / 48000.0));

            writer->writeFromAudioSampleBuffer (sine, 0, sine.getNumSamples());
        }
    }

    const auto monoReferenceAnalysis = analyzeReferenceFile (monoReferenceFile);
    const bool monoReferenceOk = monoReferenceAnalysis.ok && referenceAnalysis.ok
                                 && std::abs ((referenceAnalysis.lufs - monoReferenceAnalysis.lufs)
                                              - 3.01f) < 0.2f
                                 && std::abs (monoReferenceAnalysis.lufs + 9.0f) < 0.8f;

    std::printf ("mono reference LUFS %.2f vs stereo %.2f (delta %.2f) %s\n",
                 (double) monoReferenceAnalysis.lufs, (double) referenceAnalysis.lufs,
                 (double) (referenceAnalysis.lufs - monoReferenceAnalysis.lufs),
                 monoReferenceOk ? "ok" : "FAIL");
    monoReferenceFile.deleteFile();

    juce::Timer::callAfterDelay (7000, [&processor, &referenceAnalysis, &referenceFile]
    {
        const float lufs = referenceAnalysis.lufs;
        processor.setReferenceAudio (std::move (referenceAnalysis.audio), lufs);
        processor.setReferenceCurveInfo (referenceFile.getFullPathName(),
                                         referenceAnalysis.levels.data(), true);
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

    // The editor must load the globally saved theme in its constructor.
    const auto themeFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                               .getChildFile ("ludensscope-theme-test.txt");
    themeFile.deleteFile();
    theme::setSettingsFileForTesting (themeFile);
    theme::saveTheme (4);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    const bool themeLoaded = theme::getTheme() == 4;

    theme::setSettingsFileForTesting (juce::File());
    themeFile.deleteFile();

    std::printf ("theme load from settings: %s\n", themeLoaded ? "ok" : "FAIL");
    editor->setSize (1060, 700);

    juce::Timer::callAfterDelay (7200, [&processor, &referenceFile, &editor]
    {
        processor.switchReferenceSlot (1);

        if (auto* ludens = dynamic_cast<LudensScopeEditor*> (editor.get()))
        {
            auto* drop = static_cast<juce::FileDragAndDropTarget*> (ludens);
            drop->filesDropped ({ referenceFile.getFullPathName() }, 0, 0);
        }
    });

    Feeder feeder (processor, buffer, midi);
    feeder.startTimerHz (120);

    juce::Timer::callAfterDelay (7500, []
    {
        juce::MessageManager::getInstance()->stopDispatchLoop();
    });

    juce::MessageManager::getInstance()->runDispatchLoop();
    feeder.stopTimer();

    auto* scopeEditor = dynamic_cast<LudensScopeEditor*> (editor.get());

    if (scopeEditor != nullptr)
        scopeEditor->setInterfaceTheme (0);
    const bool slotBLoaded = scopeEditor != nullptr
        && processor.getActiveReferenceSlot() == 1
        && processor.getReferenceSettings().valid
        && processor.hasReferenceAudio()
        && scopeEditor->isReferenceCurveVisible();
    std::printf ("reference slot B load: %s\n", slotBLoaded ? "ok" : "FAIL");

    processor.switchReferenceSlot (0);

    // Both reference slots hold the same test file now and switch back.
    processor.switchReferenceSlot (1);
    const bool slotB = processor.getActiveReferenceSlot() == 1
        && processor.hasReferenceAudio()
        && processor.getReferenceSettings().valid
        && processor.getReferenceSettings().path == referenceFile.getFullPathName();
    processor.switchReferenceSlot (0);
    const bool slotABack = processor.getActiveReferenceSlot() == 0
        && processor.hasReferenceAudio()
        && processor.getReferenceSettings().valid
        && processor.getReferenceSettings().path == referenceFile.getFullPathName();
    processor.switchReferenceSlot (1);

    const bool slotsOk = slotB && slotABack;
    std::printf ("reference slots: %s\n", slotsOk ? "ok" : "FAIL");

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

    // Mono buses and processing.
    const bool monoLayoutOk = processor.supportsMonoBuses();

    // Drain the FIFO left over from the tests above.
    float drainL[4096] = {}, drainR[4096] = {};

    while (processor.readSamples (drainL, drainR, 4096) > 0)
    {
    }

    processor.setReferencePlaying (false);
    processor.setReferenceAudible (false);
    processor.setBandSolo (false, 0.0f, 0.0f);

    juce::AudioBuffer<float> monoBuffer (1, 512);

    for (int i = 0; i < 512; ++i)
        monoBuffer.setSample (0, i, 0.25f * std::sin (juce::MathConstants<float>::twoPi
                                                      * 1000.0f * (float) i / 48000.0f));

    juce::MidiBuffer monoMidi;
    processor.processBlock (monoBuffer, monoMidi);

    float readL[512] = {}, readR[512] = {};
    const int monoRead = processor.readSamples (readL, readR, 512);

    float monoPeak = 0.0f;
    bool monoDual = monoRead > 0;

    for (int i = 0; i < monoRead; ++i)
    {
        monoPeak = std::max (monoPeak, std::abs (readL[i]));
        monoDual = monoDual && std::abs (readL[i] - readR[i]) < 1.0e-6f;
    }

    const bool monoOk = monoLayoutOk && monoRead > 0 && monoPeak > 0.01f && monoDual;
    std::printf ("mono support: %s (layout %d, read %d, peak %.3f, dual %d)\n",
                 monoOk ? "ok" : "FAIL", (int) monoLayoutOk, monoRead,
                 (double) monoPeak, (int) monoDual);

    // BS.1770: a single channel must not be counted twice. Compare the
    // mono meter with a dual-mono stereo feed of the same sine.
    LoudnessMeter monoMeter, stereoMeter;
    monoMeter.prepare (48000.0);
    stereoMeter.prepare (48000.0);

    std::vector<float> probe ((size_t) 96000);

    for (int i = 0; i < (int) probe.size(); ++i)
        probe[(size_t) i] = 0.25f * std::sin (juce::MathConstants<float>::twoPi
                                             * 1000.0f * (float) i / 48000.0f);

    monoMeter.processMono (probe.data(), (int) probe.size());
    stereoMeter.process (probe.data(), probe.data(), (int) probe.size());

    const float monoLufs = monoMeter.integratedLufs();
    const float stereoLufs = stereoMeter.integratedLufs();
    const bool monoLufsOk = std::abs ((stereoLufs - monoLufs) - 3.01f) < 0.2f;

    std::printf ("mono LUFS: %.2f / dual %.2f (delta %.2f) %s\n",
                 (double) monoLufs, (double) stereoLufs,
                 (double) (stereoLufs - monoLufs), monoLufsOk ? "ok" : "FAIL");

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
    // The editor's continuous loudness match ran during the message loop; reset
    // the gain here so the playback checks stay deterministic.
    processor.setReferenceGainDb (0.0f);
    processor.setReferencePlaying (true);
    processor.setReferenceAudible (true);

    juce::AudioBuffer<float> refOut (2, 512);
    juce::MidiBuffer refMidi;

    for (int block = 0; block < 4; ++block)
    {
        refOut.clear();
        processor.processBlock (refOut, refMidi);
    }

    const float referencePeak = refOut.getMagnitude (0, 0, 512);
    std::printf ("reference output peak %.3f\n", referencePeak);

    if (referencePeak < 0.05f)
        referenceOk = false;

    // Gain match: -6 dB halves the reference output.
    processor.setReferenceGainDb (-6.0f);

    for (int block = 0; block < 4; ++block)
    {
        refOut.clear();
        processor.processBlock (refOut, refMidi);
    }

    const float referenceGainPeak = refOut.getMagnitude (0, 0, 512);
    processor.setReferenceGainDb (0.0f);
    const bool gainOk = std::abs (referenceGainPeak - 0.25f) < 0.02f;
    std::printf ("reference -6 dB peak %.3f (%s)\n",
                 referenceGainPeak, gainOk ? "ok" : "FAIL");

    // Seek to 0.5 s and play one block.
    processor.setReferencePlaying (true);
    processor.setReferenceAudible (true);
    processor.setReferencePositionSeconds (0.5);
    refOut.clear();
    processor.processBlock (refOut, refMidi);
    const double seekPosition = (double) processor.referencePositionSamples() / 48000.0;
    const bool seekOk = std::abs (seekPosition - 0.5 - 512.0 / 48000.0) < 0.01;
    std::printf ("reference seek %.3f s (%s)\n", seekPosition, seekOk ? "ok" : "FAIL");

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
    testView.targetCurveName = "Test target";
    testView.levelMatch = 0;
    testView.mirror = 1;
    testView.waterfallRatio = 0.45f;
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
        && restoredView.targetCurveName == "Test target"
        && restoredView.levelMatch == 0
        && restoredView.mirror == 1
        && std::abs (restoredView.waterfallRatio - 0.45f) < 0.01f
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
    // Curve preset store: save, reload from disk, delete.
    const auto presetFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("ludensscope-curve-presets-test.json");
    presetFile.deleteFile();

    CurvePresetStore presetStore (presetFile);
    std::array<float, 512> presetCurve {};
    presetCurve.fill (-30.0f);
    presetCurve[100] = -12.0f;

    const bool presetSaved = presetStore.save ("Test curve", presetCurve.data());
    const auto* presetFound = presetStore.find ("Test curve");

    CurvePresetStore presetStoreReloaded (presetFile);
    const auto* presetReloaded = presetStoreReloaded.find ("Test curve");

    const bool presetOk = presetSaved
        && presetFound != nullptr
        && std::abs (presetFound->curve[100] + 12.0f) < 1.0e-4f
        && presetStore.find ("Missing") == nullptr
        && presetReloaded != nullptr
        && std::abs (presetReloaded->curve[100] + 12.0f) < 1.0e-4f
        && presetStoreReloaded.remove ("Test curve")
        && presetStoreReloaded.find ("Test curve") == nullptr;

    presetFile.deleteFile();
    std::printf ("curve presets: %s\n", presetOk ? "ok" : "FAIL");

    return clipped == 2 && undoOk && referenceOk && gainOk && seekOk
           && snapshotPersisted && settingsPersisted
           && referencePersisted
           && viewPersisted && presetOk && slotsOk && slotBLoaded && soloOk && loopOk && waveformOk ? 0 : 1;
}
