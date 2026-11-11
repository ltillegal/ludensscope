#include "PluginEditor.h"

LudensScopeEditor::LudensScopeEditor (LudensScopeProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p)
{
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

    readLeft.resize (readChunk);
    readRight.resize (readChunk);

    preparedSampleRate = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
    analyzer.prepare (preparedSampleRate);

    setSize (1060, 700);
    startTimerHz (30);
}

void LudensScopeEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient::vertical (theme::bg, 0.0f,
                                                       theme::bgBottom, (float) getHeight()));
    g.fillAll();

    auto header = getLocalBounds().reduced (14).removeFromTop (36);

    g.setFont (theme::boldFont (19.0f));
    g.setColour (theme::text);
    g.drawText ("LUDENSCOPE", header.removeFromLeft (124), juce::Justification::centredLeft);

    g.setFont (theme::labelFont (10.0f));
    g.setColour (theme::textDim);
    g.drawText ("STEREO ANALYZER  /  SPECTRUM  LOUDNESS  IMAGE", header,
                juce::Justification::centredLeft);
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

    r.removeFromTop (8);

    constexpr int gap = 10;
    const int bottomHeight = juce::jlimit (150, 230, (int) ((float) r.getHeight() * 0.34f));
    auto bottom = r.removeFromBottom (bottomHeight);
    r.removeFromBottom (gap);

    const int rightWidth = juce::jlimit (250, 340, (int) ((float) r.getWidth() * 0.28f));
    auto rightColumn = r.removeFromRight (rightWidth);
    r.removeFromRight (gap);

    const int waterfallHeight = juce::jlimit (90, 150, (int) ((float) r.getHeight() * 0.32f));
    auto waterfall = r.removeFromBottom (waterfallHeight);
    r.removeFromBottom (gap);

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

    truePeakDb = juce::Decibels::gainToDecibels (blockTruePeak, -100.0f);
    samplePeakDb = juce::Decibels::gainToDecibels (blockSamplePeak, -100.0f);
    truePeakHoldDb = std::max (truePeakDb, truePeakHoldDb - 0.6f);

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

void LudensScopeEditor::captureReference()
{
    referenceDb = analyzer.averageLevels();
    hasReference = true;
    spectrumView.setReference (referenceDb.data(), true);
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
