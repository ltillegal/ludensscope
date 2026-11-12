#include "PluginEditor.h"

#include <cmath>

#include "dsp/ReferenceAnalysis.h"

#include <algorithm>

class ReferenceLoader final : public juce::Thread
{
public:
    ReferenceLoader (juce::File f, double targetRate,
                     std::function<void (ReferenceAnalysis&)> finished)
        : juce::Thread ("LudensScope reference analysis"),
          file (std::move (f)), targetSampleRate (targetRate),
          onFinished (std::move (finished))
    {
    }

    void run() override
    {
        auto analysis = analyzeReferenceFile (file, [this] { return threadShouldExit(); });

        if (analysis.ok && analysis.audio != nullptr && targetSampleRate > 0.0)
        {
            analysis.audio = resampleReference (*analysis.audio,
                                                analysis.sampleRate, targetSampleRate);
            analysis.sampleRate = targetSampleRate;
        }

        auto shared = std::make_shared<ReferenceAnalysis> (std::move (analysis));

        juce::MessageManager::callAsync ([done = onFinished, shared]
        {
            done (*shared);
        });
    }

private:
    juce::File file;
    double targetSampleRate;
    std::function<void (ReferenceAnalysis&)> onFinished;
};

class ScopeLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& backgroundColour,
                               bool isHighlighted, bool isDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        auto fill = backgroundColour;

        if (isDown)             fill = fill.brighter (0.08f);
        else if (isHighlighted) fill = fill.brighter (0.04f);

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, 13.0f);

        g.setColour (button.getToggleState() ? theme::accent.withAlpha (0.85f)
                                             : theme::panelLine.brighter (0.10f));
        g.drawRoundedRectangle (bounds, 13.0f, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool, bool) override
    {
        g.setFont (theme::labelFont (10.5f));

        g.setColour (button.findColour (button.getToggleState()
                                            ? juce::TextButton::textColourOnId
                                            : juce::TextButton::textColourOffId));
        g.drawFittedText (button.getButtonText(), button.getLocalBounds(),
                          juce::Justification::centred, 1, 0.9f);
    }
};

LudensScopeEditor::LudensScopeEditor (LudensScopeProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p)
{
    lookAndFeel = std::make_unique<ScopeLookAndFeel>();
    setLookAndFeel (lookAndFeel.get());

    setOpaque (true);
    setResizable (true, true);
    setResizeLimits (860, 540, 2400, 1500);

    addAndMakeVisible (spectrumView);
    addAndMakeVisible (spectrogramView);
    addAndMakeVisible (meterPanel);
    addAndMakeVisible (vectorScopeView);
    addAndMakeVisible (oscilloscopeView);

    freezeButton.setClickingTogglesState (true);
    freezeButton.setColour (juce::TextButton::buttonColourId, theme::panel);
    freezeButton.setColour (juce::TextButton::buttonOnColourId, theme::accent);
    freezeButton.setColour (juce::TextButton::textColourOffId, theme::text);
    freezeButton.setColour (juce::TextButton::textColourOnId, theme::bg);
    freezeButton.onClick = [this]
    {
        frozen = freezeButton.getToggleState();
        oscilloscopeView.setData (scopeLeft.data(), scopeRight.data(), scopeSize, frozen);
        oscilloscopeView.repaint();
    };
    addAndMakeVisible (freezeButton);

    resetButton.setColour (juce::TextButton::buttonColourId, theme::panel);
    resetButton.setColour (juce::TextButton::textColourOffId, theme::text);
    resetButton.onClick = [this] { resetDisplay(); };
    addAndMakeVisible (resetButton);

    captureButton.setColour (juce::TextButton::buttonColourId, theme::panel);
    captureButton.setColour (juce::TextButton::textColourOffId, theme::text);
    captureButton.onClick = [this] { captureReference(); };
    addAndMakeVisible (captureButton);

    monoButton.setClickingTogglesState (true);
    monoButton.setColour (juce::TextButton::buttonColourId, theme::panel);
    monoButton.setColour (juce::TextButton::buttonOnColourId, theme::accent);
    monoButton.setColour (juce::TextButton::textColourOffId, theme::text);
    monoButton.setColour (juce::TextButton::textColourOnId, theme::bg);
    monoButton.onClick = [this] { monoActive = monoButton.getToggleState(); };
    addAndMakeVisible (monoButton);

    spanButton.setColour (juce::TextButton::buttonColourId, theme::panel);
    spanButton.setColour (juce::TextButton::textColourOffId, theme::text);
    spanButton.onClick = [this] { cycleSpan(); };
    addAndMakeVisible (spanButton);

    guidesButton.setClickingTogglesState (true);
    guidesButton.setColour (juce::TextButton::buttonColourId, theme::panel);
    guidesButton.setColour (juce::TextButton::buttonOnColourId, theme::accent);
    guidesButton.setColour (juce::TextButton::textColourOffId, theme::text);
    guidesButton.setColour (juce::TextButton::textColourOnId, theme::bg);
    guidesButton.onClick = [this]
    {
        spectrumView.setGuides (guidesButton.getToggleState());
    };
    addAndMakeVisible (guidesButton);

    referenceButton.setColour (juce::TextButton::buttonColourId, theme::panel);
    referenceButton.setColour (juce::TextButton::textColourOffId, theme::text);
    referenceButton.onClick = [this] { chooseReferenceFile(); };
    addAndMakeVisible (referenceButton);

    spectrumView.onRangeChange = [this] (float lowHz, float highHz)
    {
        applyRange (lowHz, highHz);
    };

    spectrumView.onRangeReset = [this]
    {
        spanIndex = 0;
        applyRange (20.0f, 20000.0f);
    };

    spectrumView.onPlayToggle = [this] { toggleReferencePlay(); };
    spectrumView.onLoopToggle = [this] { toggleReferenceLoop(); };
    spectrumView.onAbToggle = [this] { toggleReferenceAb(); };

    spectrumView.setSnapshots (snapshots, snapshotFilled, snapshotSlot);

    spectrumView.onSnapshotSelect = [this] (int slot)
    {
        snapshotSlot = juce::jlimit (0, LudensScopeProcessor::snapshotSlots - 1, slot);
        processor.setSelectedSnapshot (snapshotSlot);
        spectrumView.setSnapshots (snapshots, snapshotFilled, snapshotSlot);
    };

    syncSnapshots();

    readLeft.resize (readChunk);
    readRight.resize (readChunk);

    preparedSampleRate = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
    analyzer.prepare (preparedSampleRate);
    oscilloscopeView.setSampleRate (preparedSampleRate);

    setSize (1060, 700);
    startTimerHz (30);
}

LudensScopeEditor::~LudensScopeEditor()
{
    if (referenceLoader != nullptr)
    {
        referenceLoader->signalThreadShouldExit();
        referenceLoader->stopThread (2000);
    }

    setLookAndFeel (nullptr);
}

void LudensScopeEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient::vertical (theme::bg, 0.0f,
                                                       theme::bgBottom, (float) getHeight()));
    g.fillAll();

    // Soft accent glow behind the spectrum area.
    juce::ColourGradient glow (theme::accent.withAlpha (0.05f),
                               getWidth() * 0.30f, -80.0f,
                               juce::Colours::transparentBlack,
                               getWidth() * 0.62f, getHeight() * 0.55f,
                               true);
    g.setGradientFill (glow);
    g.fillAll();

    auto header = getLocalBounds().reduced (14).removeFromTop (36);

    auto brand = header.removeFromLeft (168);

    auto mark = brand.removeFromLeft (16).withSizeKeepingCentre (13, 13).toFloat();

    g.setColour (theme::accent.withAlpha (0.85f));
    g.drawRoundedRectangle (mark, 3.5f, 1.2f);

    juce::Path markWave;

    for (int i = 0; i <= 12; ++i)
    {
        const float t = (float) i / 12.0f;
        const float x = mark.getX() + 2.0f + t * (mark.getWidth() - 4.0f);
        const float y = mark.getCentreY() - std::sin (t * 2.0f * juce::MathConstants<float>::pi) * 2.6f;

        if (i == 0) markWave.startNewSubPath (x, y);
        else        markWave.lineTo (x, y);
    }

    g.strokePath (markWave, juce::PathStrokeType (1.1f));

    brand.removeFromLeft (10);

    g.setFont (theme::boldFont (19.0f));
    g.setColour (theme::text);
    g.drawText ("LUDENSCOPE", brand, juce::Justification::centredLeft);

    g.setFont (theme::labelFont (9.5f));
    g.setColour (theme::textDim);
    g.drawText ("STEREO ANALYZER", header, juce::Justification::centredLeft);

    g.setColour (juce::Colour (0x12ffffff));
    g.fillRect (14, 53, getWidth() - 28, 1);
}

void LudensScopeEditor::resized()
{
    auto r = getLocalBounds().reduced (14);
    auto header = r.removeFromTop (36);

    resetButton.setBounds (header.removeFromRight (84).withSizeKeepingCentre (84, 26));
    header.removeFromRight (8);
    freezeButton.setBounds (header.removeFromRight (94).withSizeKeepingCentre (94, 26));
    header.removeFromRight (8);
    captureButton.setBounds (header.removeFromRight (98).withSizeKeepingCentre (98, 26));
    header.removeFromRight (8);
    monoButton.setBounds (header.removeFromRight (74).withSizeKeepingCentre (74, 26));
    header.removeFromRight (8);
    spanButton.setBounds (header.removeFromRight (92).withSizeKeepingCentre (92, 26));
    header.removeFromRight (8);
    guidesButton.setBounds (header.removeFromRight (84).withSizeKeepingCentre (84, 26));
    header.removeFromRight (8);
    referenceButton.setBounds (header.removeFromRight (88).withSizeKeepingCentre (88, 26));

    r.removeFromTop (8);

    constexpr int gap = 10;
    const int bottomHeight = juce::jlimit (150, 230, (int) ((float) r.getHeight() * 0.34f));
    auto bottom = r.removeFromBottom (bottomHeight);
    r.removeFromBottom (gap);

    const int rightWidth = juce::jlimit (250, 340, (int) ((float) r.getWidth() * 0.28f));
    auto rightColumn = r.removeFromRight (rightWidth);
    r.removeFromRight (gap);

    leftColumnBounds = r;

    const int minSpectrumHeight = 140;
    const int maxWaterfallHeight = juce::jmax (0, r.getHeight() - minSpectrumHeight);
    const int waterfallHeight = juce::jlimit (
        0, maxWaterfallHeight,
        (int) std::lround (waterfallRatio * (float) r.getHeight()));

    auto waterfall = r.removeFromBottom (waterfallHeight);
    waterfallDivider = r.removeFromBottom (gap);

    spectrumView.setBounds (r);
    spectrogramView.setBounds (waterfall);
    meterPanel.setBounds (rightColumn);

    auto vectorBounds = bottom.removeFromLeft (bottomHeight);
    bottom.removeFromLeft (gap);

    vectorScopeView.setBounds (vectorBounds);
    oscilloscopeView.setBounds (bottom);
}

void LudensScopeEditor::timerCallback()
{
    const double sampleRate = processor.getSampleRate();

    if (sampleRate > 0.0 && std::abs (sampleRate - preparedSampleRate) > 1.0e-6)
    {
        preparedSampleRate = sampleRate;
        analyzer.prepare (sampleRate);
        oscilloscopeView.setSampleRate (sampleRate);
    }

    const int numRead = processor.readSamples (readLeft.data(), readRight.data(), readChunk);

    // Mono check: sum the input for the analyzers without touching the DAW signal.
    if (numRead > 0 && monoActive)
    {
        for (int i = 0; i < numRead; ++i)
        {
            const float mono = 0.5f * (readLeft[(size_t) i] + readRight[(size_t) i]);
            readLeft[(size_t) i] = readRight[(size_t) i] = mono;
        }
    }

    float blockTruePeak = 0.0f;
    float blockSamplePeak = 0.0f;

    if (numRead > 0)
    {
        const float* left = readLeft.data();
        const float* right = readRight.data();

        blockTruePeak = std::max (truePeakLeft.process (left, numRead),
                                  truePeakRight.process (right, numRead));

        for (int i = 0; i < numRead; ++i)
        {
            blockSamplePeak = std::max (blockSamplePeak,
                                        std::max (std::abs (left[i]), std::abs (right[i])));
        }

        if (! frozen)
        {
            analyzer.push (left, right, numRead);
            appendScope (left, right, numRead);

            double sumLL = 0.0, sumRR = 0.0, sumLR = 0.0;

            for (int i = 0; i < numRead; ++i)
            {
                sumLL += (double) left[i] * (double) left[i];
                sumRR += (double) right[i] * (double) right[i];
                sumLR += (double) left[i] * (double) right[i];
            }

            const double denominator = std::sqrt (sumLL * sumRR);
            const float instant = denominator > 1.0e-12 ? (float) (sumLR / denominator) : 0.0f;
            correlation += 0.25f * (instant - correlation);
        }
    }

    if (frozen)
        return;

    if (blockTruePeak > 1.0f)
        ++truePeakOvers;

    constexpr float peakReleasePerTick = 0.35f;

    const float truePeakTarget = juce::Decibels::gainToDecibels (blockTruePeak, -100.0f);
    const float samplePeakTarget = juce::Decibels::gainToDecibels (blockSamplePeak, -100.0f);

    // Peak ballistics: instant attack, slow release; the "max" value is the
    // session maximum and never decays, so the readout stops jittering.
    truePeakDb = std::max (truePeakTarget, truePeakDb - peakReleasePerTick);
    samplePeakDb = std::max (samplePeakTarget, samplePeakDb - peakReleasePerTick);

    if (truePeakDb > truePeakHoldDb)
        truePeakHoldDb = truePeakDb;

    analyzer.update();

    spectrogramView.push (analyzer.levels().data(), SpectrumAnalyzer::numBins);

    const float integrated = processor.integratedLufs();
    const float plr = (integrated > -70.0f && truePeakHoldDb > -99.0f)
                          ? truePeakHoldDb - integrated
                          : -100.0f;

    spectrumView.setData (analyzer.levels().data(), analyzer.peaks().data());
    meterPanel.setValues (processor.momentaryLufs(), processor.shortTermLufs(),
                          integrated, truePeakDb, samplePeakDb,
                          correlation, truePeakHoldDb, processor.lraLu(), plr,
                          processor.clippedSampleCount(), truePeakOvers);

    const double referenceRate = preparedSampleRate > 0.0 ? preparedSampleRate : 48000.0;

    spectrumView.setTransport (processor.hasReferenceAudio(),
                               processor.isReferencePlaying(),
                               processor.isReferenceLooping(),
                               processor.isReferenceAudible(),
                               (double) processor.referencePositionSamples() / referenceRate,
                               (double) processor.referenceLengthSamples() / referenceRate);

    if (processor.snapshotGeneration() != snapshotGeneration)
        syncSnapshots();

    vectorScopeView.setData (scopeLeft.data() + (scopeSize - 1024),
                             scopeRight.data() + (scopeSize - 1024), 1024);
    oscilloscopeView.setData (scopeLeft.data(), scopeRight.data(), scopeSize, false);

    spectrumView.repaint();
    meterPanel.repaint();
    vectorScopeView.repaint();
    oscilloscopeView.repaint();
}

void LudensScopeEditor::appendScope (const float* left, const float* right, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    const int take = std::min (numSamples, scopeSize);
    const int keep = scopeSize - take;

    if (keep > 0)
    {
        std::memmove (scopeLeft.data(),  scopeLeft.data()  + take, sizeof (float) * (size_t) keep);
        std::memmove (scopeRight.data(), scopeRight.data() + take, sizeof (float) * (size_t) keep);
    }

    std::memcpy (scopeLeft.data()  + keep, left  + (numSamples - take), sizeof (float) * (size_t) take);
    std::memcpy (scopeRight.data() + keep, right + (numSamples - take), sizeof (float) * (size_t) take);
}

void LudensScopeEditor::chooseReferenceFile()
{
    referenceChooser = std::make_unique<juce::FileChooser> (
        "Load reference audio", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg");

    referenceChooser->launchAsync (juce::FileBrowserComponent::openMode
                                       | juce::FileBrowserComponent::canSelectFiles,
                                   [this] (const juce::FileChooser& chooser)
                                   {
                                       const auto file = chooser.getResult();

                                       if (file.existsAsFile())
                                           loadReferenceFile (file);
                                   });
}

void LudensScopeEditor::loadReferenceFile (const juce::File& file)
{
    if (referenceLoader != nullptr)
        referenceLoader->stopThread (2000);

    referenceButton.setButtonText ("LOADING...");
    referenceButton.setEnabled (false);

    const double targetRate = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;

    referenceLoader = std::make_unique<ReferenceLoader> (
        file, targetRate,
        [safe = juce::Component::SafePointer<LudensScopeEditor> (this)]
        (ReferenceAnalysis& analysis)
        {
            if (safe == nullptr)
                return;

            safe->referenceButton.setButtonText ("REF FILE");
            safe->referenceButton.setEnabled (true);

            if (! analysis.ok)
                return;

            safe->referenceDb = analysis.levels;
            safe->hasReference = true;
            safe->spectrumView.setReference (safe->referenceDb.data(), true);

            safe->processor.setReferenceAudio (std::move (analysis.audio), analysis.lufs);
        });

    referenceLoader->startThread();
}

void LudensScopeEditor::updateReferenceGain()
{
    const float mixLufs = processor.integratedLufs();
    const float referenceLufs = processor.referenceLufs();

    float gainDb = 0.0f;

    if (mixLufs > -70.0f && referenceLufs > -70.0f)
        gainDb = juce::jlimit (-24.0f, 24.0f, mixLufs - referenceLufs);

    processor.setReferenceGainDb (gainDb);
}

void LudensScopeEditor::toggleReferencePlay()
{
    if (! processor.hasReferenceAudio())
        return;

    const bool shouldPlay = ! processor.isReferencePlaying();

    if (shouldPlay)
    {
        updateReferenceGain();
        processor.setReferenceAudible (true);
    }

    processor.setReferencePlaying (shouldPlay);
}

void LudensScopeEditor::toggleReferenceLoop()
{
    processor.setReferenceLoop (! processor.isReferenceLooping());
}

void LudensScopeEditor::toggleReferenceAb()
{
    if (! processor.hasReferenceAudio())
        return;

    const bool toReference = ! processor.isReferenceAudible();

    if (toReference)
    {
        updateReferenceGain();

        if (! processor.isReferencePlaying())
            processor.setReferencePlaying (true);
    }

    processor.setReferenceAudible (toReference);
}

void LudensScopeEditor::setGuidesEnabled (bool enabled)
{
    guidesButton.setToggleState (enabled, juce::dontSendNotification);
    spectrumView.setGuides (enabled);
}

void LudensScopeEditor::captureReference()
{
    processor.setSnapshot (snapshotSlot, analyzer.averageLevels().data());
    syncSnapshots();
}

void LudensScopeEditor::syncSnapshots()
{
    for (int slot = 0; slot < LudensScopeProcessor::snapshotSlots; ++slot)
    {
        snapshotFilled[(size_t) slot] = processor.isSnapshotFilled (slot);

        if (snapshotFilled[(size_t) slot])
            std::copy (processor.snapshotLevels (slot),
                       processor.snapshotLevels (slot) + LudensScopeProcessor::snapshotBins,
                       snapshots[(size_t) slot].begin());
    }

    snapshotSlot = juce::jlimit (0, LudensScopeProcessor::snapshotSlots - 1,
                                 processor.selectedSnapshot());
    snapshotGeneration = processor.snapshotGeneration();

    spectrumView.setSnapshots (snapshots, snapshotFilled, snapshotSlot);
}

void LudensScopeEditor::resetDisplay()
{
    processor.requestReset();
    processor.resetClipCounters();

    analyzer.resetPeaks();
    analyzer.resetAverage();
    truePeakLeft.reset();
    truePeakRight.reset();

    hasReference = false;
    spectrumView.setReference (nullptr, false);
    spectrogramView.clear();

    for (auto& filled : snapshotFilled)
        filled = false;

    processor.clearSnapshots();
    syncSnapshots();

    processor.clearReferenceAudio();
    spectrumView.setTransport (false, false, true, false, 0.0, 0.0);

    truePeakDb = -100.0f;
    truePeakHoldDb = -100.0f;
    samplePeakDb = -100.0f;
    correlation = 0.0f;
    truePeakOvers = 0;

    scopeLeft.fill (0.0f);
    scopeRight.fill (0.0f);

    analyzer.update();

    spectrumView.setData (analyzer.levels().data(), analyzer.peaks().data());
    meterPanel.setValues (processor.momentaryLufs(), processor.shortTermLufs(),
                          processor.integratedLufs(), truePeakDb, samplePeakDb,
                          correlation, truePeakHoldDb, -1.0f, -100.0f, 0, 0);
    vectorScopeView.setData (scopeLeft.data() + (scopeSize - 1024),
                             scopeRight.data() + (scopeSize - 1024), 1024);
    oscilloscopeView.setData (scopeLeft.data(), scopeRight.data(), scopeSize, frozen);

    spectrumView.repaint();
    meterPanel.repaint();
    vectorScopeView.repaint();
    oscilloscopeView.repaint();
}

void LudensScopeEditor::cycleSpan()
{
    static constexpr int spans[] = { 10, 8, 6, 4, 2, 1 };

    spanIndex = (spanIndex + 1) % 6;

    if (spanIndex == 0)
    {
        applyRange (20.0f, 20000.0f);
        return;
    }

    const float octaves = (float) spans[spanIndex];
    const float centre = std::sqrt (rangeLow * rangeHigh);
    const float halfSpan = std::pow (2.0f, octaves * 0.5f);

    applyRange (centre / halfSpan, centre * halfSpan);
}

void LudensScopeEditor::applyRange (float lowHz, float highHz)
{
    lowHz = juce::jlimit (20.0f, 20000.0f, lowHz);
    highHz = juce::jlimit (lowHz * 1.01f, 20000.0f, highHz);

    if (std::abs (lowHz - rangeLow) < 0.5f && std::abs (highHz - rangeHigh) < 0.5f)
        return;

    rangeLow = lowHz;
    rangeHigh = highHz;

    spectrumView.setRange (lowHz, highHz);
    spectrogramView.setRange (lowHz, highHz);

    const int octaves = juce::jlimit (1, 10,
                                      (int) std::lround (std::log2 (highHz / lowHz)));
    spanButton.setButtonText ("SPAN " + juce::String (octaves));
}

void LudensScopeEditor::mouseMove (const juce::MouseEvent& e)
{    const bool overDivider = waterfallDivider.expanded (0, 4).contains (e.getPosition());

    setMouseCursor (overDivider ? juce::MouseCursor::UpDownResizeCursor
                                : juce::MouseCursor::NormalCursor);
}

void LudensScopeEditor::mouseDown (const juce::MouseEvent& e)
{
    draggingDivider = waterfallDivider.expanded (0, 4).contains (e.getPosition());
}

void LudensScopeEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (! draggingDivider)
        return;

    const int maximum = juce::jmax (0, leftColumnBounds.getHeight() - 140);
    const int height = juce::jlimit (0, maximum, leftColumnBounds.getBottom() - e.y);

    waterfallRatio = (float) height / (float) juce::jmax (1, leftColumnBounds.getHeight());
    resized();
}

void LudensScopeEditor::mouseUp (const juce::MouseEvent&)
{
    draggingDivider = false;
}

void LudensScopeEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! waterfallDivider.expanded (0, 4).contains (e.getPosition()))
        return;

    waterfallRatio = 0.32f;
    resized();
}
