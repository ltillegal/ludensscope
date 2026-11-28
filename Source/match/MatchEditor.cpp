#include "MatchEditor.h"
#include "MatchProcessor.h"

#include "ui/Theme.h"
#include "dsp/ReferenceAnalysis.h"
#include "Licence.h"

#include <cmath>
#include <thread>
#include <utility>

namespace
{
    constexpr int editorWidth = 980;
    constexpr int editorHeight = 660;

    constexpr float bandEdges[LudensMatchProcessor::numBands + 1] = {
        20.0f, 60.0f, 120.0f, 250.0f, 500.0f, 1000.0f, 2500.0f, 6000.0f, 20000.0f
    };

    constexpr float minHz = 20.0f;
    constexpr float maxHz = 20000.0f;

    float binFrequency (int bin)
    {
        return minHz * std::pow (maxHz / minHz,
                                 (float) bin / (float) (SpectrumAnalyzer::numBins - 1));
    }
}

LudensMatchEditor::LudensMatchEditor (LudensMatchProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setSize (editorWidth, editorHeight);

    addAndMakeVisible (spectrum);
    spectrum.setRange (20.0f, 20000.0f);

    addAndMakeVisible (loadButton);
    loadButton.onClick = [this] { chooseReference(); };
    loadButton.setColour (juce::TextButton::buttonColourId, theme::panel);
    loadButton.setColour (juce::TextButton::textColourOnId, theme::text);
    loadButton.setColour (juce::TextButton::textColourOffId, theme::text);
    loadButton.setTooltip ("Load a reference track - the correction follows its balance");

    addAndMakeVisible (levelLabel);

    addAndMakeVisible (editionLabel);
    editionLabel.setFont (theme::labelFont (10.0f));
    editionLabel.setJustificationType (juce::Justification::centredRight);

    addAndMakeVisible (unlockButton);
    unlockButton.onClick = []
    {
        juce::URL ("mailto:alek.ludens@gmail.com?subject=LudensMatch%20FULL%20licence")
            .launchInDefaultBrowser();
    };
    unlockButton.setColour (juce::TextButton::buttonColourId, theme::accent);
    unlockButton.setColour (juce::TextButton::textColourOffId, theme::bg);
    unlockButton.setColour (juce::TextButton::textColourOnId, theme::bg);
    levelLabel.setFont (theme::detailMonoFont (10.0f, 1.0f));
    levelLabel.setColour (juce::Label::textColourId, theme::textDim);
    levelLabel.setJustificationType (juce::Justification::centredRight);

    addAndMakeVisible (referenceLabel);
    referenceLabel.setText ("no reference", juce::dontSendNotification);
    referenceLabel.setFont (theme::labelFont (10.5f));
    referenceLabel.setColour (juce::Label::textColourId, theme::textDim);
    referenceLabel.setJustificationType (juce::Justification::centredLeft);

    auto setup = [this] (juce::Slider& slider, juce::Label& caption, const juce::String& text)
    {
        addAndMakeVisible (slider);
        addAndMakeVisible (caption);

        styleSlider (slider);
        caption.setText (text, juce::dontSendNotification);
        caption.setFont (theme::labelFont (10.5f));
        caption.setColour (juce::Label::textColourId, theme::textDim);
        caption.setJustificationType (juce::Justification::centredLeft);
    };

    setup (amount, amountCaption, "AMOUNT");
    setup (mix, mixCaption, "MIX");
    setup (output, outputCaption, "OUTPUT");

    auto& state = processor.parameters();
    amountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, "amount", amount);
    mixAttachment    = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, "mix", mix);
    outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, "output", output);

    readLeft.resize (4096);
    readRight.resize (4096);

    analyzer.prepare (processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0);

    LudensScopeLicence::setProduct ("LudensMatch");
    refreshEdition();

    startTimerHz (30);
}

void LudensMatchEditor::styleSlider (juce::Slider& slider) const
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 54, 18);
    slider.setColour (juce::Slider::textBoxTextColourId, theme::text);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::trackColourId, theme::accent.withAlpha (0.75f));
    slider.setColour (juce::Slider::backgroundColourId, theme::grid);
    slider.setColour (juce::Slider::thumbColourId, theme::accent);
}

void LudensMatchEditor::chooseReference()
{
    auto chooser = std::make_shared<juce::FileChooser> (
        "Reference track", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
        "*.wav;*.aif;*.aiff;*.flac;*.mp3");

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();

                              if (! file.existsAsFile())
                                  return;

                              referenceLabel.setText ("analysing " + file.getFileName(),
                                                      juce::dontSendNotification);

                              juce::Component::SafePointer<LudensMatchEditor> safe (this);
                              const int generation = ++referenceGeneration;

                              std::thread ([safe, file, generation]
                              {
                                  const auto analysis = analyzeReferenceFile (file);

                                  // ReferenceAnalysis owns its audio buffer, so
                                  // only the curve crosses threads.
                                  const auto levels = analysis.levels;
                                  const bool ok = analysis.ok;
                                  const float lufs = analysis.lufs;

                                  juce::MessageManager::callAsync ([safe, levels, ok, lufs, file, generation]
                                  {
                                      if (safe == nullptr
                                          || generation != safe->referenceGeneration)
                                          return;

                                      if (! ok)
                                      {
                                          safe->referenceLabel.setText ("could not read that file",
                                                                        juce::dontSendNotification);
                                          return;
                                      }

                                      safe->applyReference (levels, lufs, file.getFileName());
                                  });
                              }).detach();
                          });
}

void LudensMatchEditor::applyReference (const std::array<float, SpectrumAnalyzer::numBins>& levels,
                                        float lufs, const juce::String& name)
{
    referenceCurve = levels;
    referenceLufs = lufs;
    hasReference = true;
    spectrum.setReference (referenceCurve.data(), true);
    referenceLabel.setText (name, juce::dontSendNotification);
}

void LudensMatchEditor::loadReferenceFile (const juce::File& file)
{
    const auto analysis = analyzeReferenceFile (file);

    if (! analysis.ok)
    {
        referenceLabel.setText ("could not read that file", juce::dontSendNotification);
        return;
    }

    ++referenceGeneration;
    applyReference (analysis.levels, analysis.lufs, file.getFileName());
}

void LudensMatchEditor::updateBandGains()
{
    if (! hasReference)
        return;

    const auto& mixCurve = analyzer.averageLevels();

    // Nothing to compare against until the input has produced a spectrum.
    if (mixCurve[(size_t) (SpectrumAnalyzer::numBins / 2)] < -110.0f)
        return;

    std::array<float, LudensMatchProcessor::numBands> target {};

    for (int band = 0; band < LudensMatchProcessor::numBands; ++band)
    {
        const float low = bandEdges[band];
        const float high = bandEdges[band + 1];

        float sum = 0.0f;
        int count = 0;

        for (int bin = 0; bin < SpectrumAnalyzer::numBins; ++bin)
        {
            const float hz = binFrequency (bin);

            if (hz < low || hz >= high)
                continue;

            const float diff = referenceCurve[(size_t) bin] - mixCurve[(size_t) bin];

            if (diff < -95.0f)
                continue;

            sum += diff;
            ++count;
        }

        target[(size_t) band] = count > 0 ? sum / (float) count : 0.0f;
    }

    // Removing the level difference must not use LUFS: K-weighting lifts the
    // top end, so a dark reference reads as "quieter" even when it is not, and
    // subtracting that would cut the treble by several dB. The mean of the band
    // differences is the honest normaliser - it conserves broadband energy, so
    // the correction changes the balance and nothing else.
    float mean = 0.0f;

    for (const float value : target)
        mean += value;

    levelOffsetDb = mean / (float) LudensMatchProcessor::numBands;

    for (auto& value : target)
        value = juce::jlimit (-9.0f, 9.0f, value - levelOffsetDb);

    bool changed = false;

    for (int band = 0; band < LudensMatchProcessor::numBands; ++band)
    {
        float& current = bandGains[(size_t) band];
        const float next = current + 0.25f * (target[(size_t) band] - current);

        if (std::abs (next - current) > 0.01f)
            changed = true;

        current = next;
    }

    if (changed)
        processor.setBandGains (bandGains.data(), LudensMatchProcessor::numBands);

    // The correction is drawn as a curve over the spectrum: every bin takes the
    // gain of the band it belongs to.
    for (int bin = 0; bin < SpectrumAnalyzer::numBins; ++bin)
    {
        const float hz = binFrequency (bin);
        int band = LudensMatchProcessor::numBands - 1;

        for (int b = 0; b < LudensMatchProcessor::numBands; ++b)
        {
            if (hz >= bandEdges[b] && hz < bandEdges[b + 1])
            {
                band = b;
                break;
            }
        }

        correctionCurve[(size_t) bin] = bandGains[(size_t) band];
    }

    // The view's Y axis is dBFS, so the correction is drawn around -24 dBFS:
    // 0 dB of correction sits there, +/-9 dB moves the line up and down.
    for (int bin = 0; bin < SpectrumAnalyzer::numBins; ++bin)
    {
        const float hz = binFrequency (bin);

        // Interpolate between band centres in log frequency, so the line reads
        // as a curve rather than eight steps.
        if (hz <= juce::MathConstants<float>::twoPi * 5.0f)
        {
            correctionCurve[(size_t) bin] = -24.0f + bandGains[0];
            continue;
        }

        if (hz >= LudensMatchProcessor::bandCentreHz (LudensMatchProcessor::numBands - 1))
        {
            correctionCurve[(size_t) bin] = -24.0f + bandGains[LudensMatchProcessor::numBands - 1];
            continue;
        }

        int upper = 1;

        while (upper < LudensMatchProcessor::numBands
               && LudensMatchProcessor::bandCentreHz (upper) < hz)
            ++upper;

        const float f0 = LudensMatchProcessor::bandCentreHz (upper - 1);
        const float f1 = LudensMatchProcessor::bandCentreHz (upper);
        const float t = juce::jlimit (0.0f, 1.0f,
                                      std::log (hz / f0) / std::log (f1 / f0));
        const float gain = bandGains[(size_t) (upper - 1)]
                           + t * (bandGains[(size_t) upper] - bandGains[(size_t) (upper - 1)]);

        correctionCurve[(size_t) bin] = -24.0f + gain;
    }

    spectrum.setTargetCurve (correctionCurve.data(), true, "MATCH");
}

void LudensMatchEditor::refreshEdition()
{
    isFull = LudensScopeLicence::isUnlocked();
    processor.setFullVersion (isFull);

    // The reason LITE is on screen matters more than the label: without it a
    // customer only sees that the unlock did not take, never why.
    auto text = juce::String ("FULL");
    auto detail = juce::String();

    if (isFull)
    {
        detail = "LudensMatch FULL, licensed to " + LudensScopeLicence::licensedEmail();
    }
    else
    {
        const auto key = LudensScopeLicence::load();
        const auto parsed = key.isEmpty() ? LudensScopeLicence::Info()
                                          : LudensScopeLicence::parse (key);
        const auto message = LudensScopeLicence::activationMessage();

        if (LudensScopeLicence::isMachineLimitReached())
            text = "LITE - MACHINE LIMIT";
        else if (! key.isEmpty() && ! parsed.valid)
            text = "LITE - BAD KEY";
        else
            text = "LITE - NO KEY";

        detail = "Licence file: " + LudensScopeLicence::file().getFullPathName();

        if (key.isEmpty())
            detail << "\nThe file is missing or empty.";
        else if (! parsed.valid)
            detail << "\n" << parsed.error;
        else
            detail << "\nKey is valid but the edition did not switch on.";

        if (message.isNotEmpty())
            detail << "\n" << message;
    }

    editionLabel.setText (text, juce::dontSendNotification);
    editionLabel.setTooltip (detail);
    editionLabel.setColour (juce::Label::textColourId, isFull ? theme::accent : theme::textDim);

    for (auto* slider : { &amount, &mix, &output })
    {
        slider->setEnabled (isFull);
        slider->setAlpha (isFull ? 1.0f : 0.45f);
    }

    unlockButton.setVisible (! isFull);
}

void LudensMatchEditor::timerCallback()
{
    const double rate = processor.getSampleRate();

    if (rate > 0.0 && std::abs (rate - analyzerRate) > 1.0e-6)
    {
        analyzerRate = rate;
        analyzer.prepare (rate);
    }

    const int numRead = processor.readSamples (readLeft.data(), readRight.data(),
                                               (int) readLeft.size());

    if (numRead > 0)
    {
        analyzer.push (readLeft.data(), readRight.data(), numRead);
        analyzer.update();
        spectrum.setData (analyzer.levels().data(), analyzer.peaks().data());
    }

    updateBandGains();

    const float mixLufs = processor.shortTermLufs() > -60.0f ? processor.shortTermLufs()
                                                             : processor.integratedLufs();

    if (hasReference && mixLufs > -60.0f)
    {
        levelLabel.setText ("ref " + juce::String (referenceLufs, 1) + " LUFS   mix "
                                + juce::String (mixLufs, 1) + " LUFS   balance offset "
                                + (levelOffsetDb >= 0.0f ? "+" : "") + juce::String (levelOffsetDb, 1) + " dB",
                            juce::dontSendNotification);
    }

    spectrum.repaint();
}

void LudensMatchEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);

    const auto bounds = getLocalBounds().toFloat();

    g.setFont (theme::boldFont (19.0f));
    g.setColour (theme::text);
    g.drawText ("LudensMatch", bounds.reduced (18.0f, 0.0f).removeFromTop (44.0f),
                juce::Justification::centredLeft);

    g.setFont (theme::labelFont (10.0f));
    g.setColour (theme::textDim);
    g.drawText ("REFERENCE MATCHING EQ", bounds.reduced (18.0f, 0.0f).removeFromTop (44.0f),
                juce::Justification::centredRight);
}

void LudensMatchEditor::resized()
{
    auto area = getLocalBounds().reduced (18);

    area.removeFromTop (44);                       // header
    auto referenceRow = area.removeFromTop (34);   // reference controls

    loadButton.setBounds (referenceRow.removeFromLeft (150).reduced (0, 4));
    referenceLabel.setBounds (referenceRow.removeFromLeft (240).reduced (8, 4));
    unlockButton.setBounds (referenceRow.removeFromRight (118).reduced (0, 4));
    referenceRow.removeFromRight (8);
    editionLabel.setBounds (referenceRow.removeFromRight (150).reduced (4, 4));
    levelLabel.setBounds (referenceRow.reduced (8, 4));

    auto controls = area.removeFromBottom (92);
    area.removeFromBottom (10);

    spectrum.setBounds (area);

    const int sliderWidth = controls.getWidth() / 3;

    const std::pair<juce::Slider*, juce::Label*> cells[] = {
        { &amount, &amountCaption },
        { &mix, &mixCaption },
        { &output, &outputCaption },
    };

    for (const auto& cellPair : cells)
    {
        auto cell = controls.removeFromLeft (sliderWidth).reduced (6, 4);
        cellPair.second->setBounds (cell.removeFromTop (18));
        cellPair.first->setBounds (cell.removeFromTop (34));
    }
}
