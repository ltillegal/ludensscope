#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <vector>

#include "PluginProcessor.h"
#include "dsp/SpectrumAnalyzer.h"
#include "dsp/TruePeakDetector.h"
#include "ui/MeterPanel.h"
#include "ui/OscilloscopeView.h"
#include "ui/SpectrogramView.h"
#include "ui/SpectrumView.h"
#include "ui/Theme.h"
#include "ui/VectorScopeView.h"

class ScopeLookAndFeel;

class LudensScopeEditor : public juce::AudioProcessorEditor,
                       private juce::Timer
{
public:
    explicit LudensScopeEditor (LudensScopeProcessor&);
    ~LudensScopeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    // Freezes the current averaged spectrum as a reference curve.
    void captureReference();
private:
    void timerCallback() override;
    void appendScope (const float* left, const float* right, int numSamples) noexcept;
    void resetDisplay();
    void cycleSpan();
    void applyRange (float lowHz, float highHz);

    static constexpr int scopeSize = 4096;
    static constexpr int readChunk = 8192;

    LudensScopeProcessor& processor;

    std::unique_ptr<ScopeLookAndFeel> lookAndFeel;

    SpectrumView spectrumView;
    SpectrogramView spectrogramView;
    MeterPanel meterPanel;
    VectorScopeView vectorScopeView;
    OscilloscopeView oscilloscopeView;

    juce::TextButton freezeButton { "FREEZE" };
    juce::TextButton resetButton  { "RESET" };
    juce::TextButton captureButton { "CAPTURE" };
    juce::TextButton monoButton { "MONO" };
    juce::TextButton spanButton { "SPAN 10" };

    bool monoActive = false;

    float rangeLow = 20.0f;
    float rangeHigh = 20000.0f;
    int spanIndex = 0;

    std::array<float, SpectrumAnalyzer::numBins> referenceDb {};
    bool hasReference = false;

    juce::Rectangle<int> leftColumnBounds;
    juce::Rectangle<int> waterfallDivider;
    float waterfallRatio = 0.32f;
    bool draggingDivider = false;

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
