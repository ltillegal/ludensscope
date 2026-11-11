#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <vector>

#include "PluginProcessor.h"
#include "dsp/SpectrumAnalyzer.h"
#include "dsp/TruePeakDetector.h"
#include "ui/MeterPanel.h"
#include "ui/OscilloscopeView.h"
#include "ui/SpectrumView.h"
#include "ui/Theme.h"
#include "ui/VectorScopeView.h"

class LudensScopeEditor : public juce::AudioProcessorEditor,
                       private juce::Timer
{
public:
    explicit LudensScopeEditor (LudensScopeProcessor&);
    ~LudensScopeEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Freezes the current averaged spectrum as a reference curve.
    void captureReference();

private:
    void timerCallback() override;
    void appendScope (const float* left, const float* right, int numSamples) noexcept;
    void resetDisplay();

    static constexpr int scopeSize = 4096;
    static constexpr int readChunk = 8192;

    LudensScopeProcessor& processor;

    SpectrumView spectrumView;
    MeterPanel meterPanel;
    VectorScopeView vectorScopeView;
    OscilloscopeView oscilloscopeView;

    juce::TextButton freezeButton { "FREEZE" };
    juce::TextButton resetButton  { "RESET" };
    juce::TextButton captureButton { "CAPTURE" };

    std::array<float, SpectrumAnalyzer::numBins> referenceDb {};
    bool hasReference = false;

    SpectrumAnalyzer analyzer;
    TruePeakDetector truePeakLeft, truePeakRight;

    std::vector<float> readLeft, readRight;
    std::array<float, scopeSize> scopeLeft {}, scopeRight {};

    double preparedSampleRate = 0.0;
    bool frozen = false;

    float truePeakDb = -100.0f;
    float truePeakHoldDb = -100.0f;
    float samplePeakDb = -100.0f;
    float correlation = 0.0f;
    int truePeakOvers = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LudensScopeEditor)
};
