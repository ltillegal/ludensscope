#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

#include "ui/SpectrumView.h"
#include "dsp/SpectrumAnalyzer.h"

class LudensMatchProcessor;

class LudensMatchEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit LudensMatchEditor (LudensMatchProcessor&);
    ~LudensMatchEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void styleSlider (juce::Slider&) const;

    LudensMatchProcessor& processor;

    SpectrumView spectrum;
    SpectrumAnalyzer analyzer;

    juce::Slider amount, mix, output;
    juce::Label amountCaption, mixCaption, outputCaption;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;

    std::vector<float> readLeft, readRight;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LudensMatchEditor)
};
