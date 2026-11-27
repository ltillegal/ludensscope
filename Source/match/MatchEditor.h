#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <memory>
#include <vector>

#include "ui/SpectrumView.h"
#include "dsp/SpectrumAnalyzer.h"
#include "match/MatchProcessor.h"

class LudensMatchEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit LudensMatchEditor (LudensMatchProcessor&);
    ~LudensMatchEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Analyses a file synchronously and adopts its curve. Public so the
    // snapshot tool can render the window with a reference loaded.
    void loadReferenceFile (const juce::File& file);

private:
    void timerCallback() override;
    void styleSlider (juce::Slider&) const;
    void chooseReference();
    void updateBandGains();
    void refreshEdition();
    void applyReference (const std::array<float, SpectrumAnalyzer::numBins>& levels,
                         float lufs, const juce::String& name);

    LudensMatchProcessor& processor;

    SpectrumView spectrum;
    SpectrumAnalyzer analyzer;

    juce::TextButton loadButton { "LOAD REFERENCE" };
    juce::Label referenceLabel;
    juce::Label levelLabel;
    juce::Label editionLabel;
    juce::TextButton unlockButton { "UNLOCK 29 EUR" };
    juce::Slider amount, mix, output;
    juce::Label amountCaption, mixCaption, outputCaption;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;

    std::vector<float> readLeft, readRight;
    double analyzerRate = 0.0;

    std::array<float, SpectrumAnalyzer::numBins> referenceCurve {};
    std::array<float, SpectrumAnalyzer::numBins> correctionCurve {};
    std::array<float, LudensMatchProcessor::numBands> bandGains {};
    bool hasReference = false;
    bool isFull = false;
    float referenceLufs = -100.0f;
    float levelOffsetDb = 0.0f;
    int referenceGeneration = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LudensMatchEditor)
};
