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
class ReferenceLoader;

class LudensScopeEditor : public juce::AudioProcessorEditor,
                           private juce::Timer,
                           public juce::FileDragAndDropTarget
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

    void setGuidesEnabled (bool enabled);
private:
    void timerCallback() override;
    void appendScope (const float* left, const float* right, int numSamples) noexcept;
    void resetDisplay();
    void cycleSpan();
    void applyRange (float lowHz, float highHz);
    void syncSnapshots();
    void chooseReferenceFile();
    void loadReferenceFile (const juce::File& file);
    void toggleReferencePlay();
    void toggleReferenceLoop();
    void toggleReferenceAb();
    void updateReferenceGain();
    void showFftMenu();
    void applyFftMenu (int result);
    void applyFftSettings (const LudensScopeProcessor::FftSettings& settings);
    void saveFftSettings();
    void exportSnapshots();
    void showGuidesMenu();
    void toggleGuideSolo (int index);
    void clearGuideSolo();
    void applyViewSettings (const LudensScopeProcessor::ViewSettings& settings);
    void saveViewSettings();
    void applyReferenceSettings();
    void undoReset();

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void exportViewPng();
    void applySmoothing();

    bool keyPressed (const juce::KeyPress& key) override;

    static constexpr int scopeSize = 131072;
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
    juce::TextButton undoButton   { "UNDO" };
    juce::TextButton captureButton { "CAPTURE" };
    juce::TextButton monoButton { "MONO" };
    juce::TextButton spanButton { "SPAN 10" };
    juce::TextButton guidesButton { "GUIDES" };
    juce::TextButton referenceButton { "REF FILE" };
    juce::TextButton fftButton { "FFT 4096" };

    int timingPreset = 1;
    int guidePreset = 0;
    bool guidesEnabled = false;
    bool showPeakMarkers = true;
    bool showDifference = false;
    bool showSide = false;
    bool showNoteGrid = false;
    int smoothingPreset = 0;
    int overlapPreset = 0;
    int differenceSource = 0;
    bool fileDragActive = false;
    int waterfallHistory = 2;
    int soloZone = -1;
    bool applyingSettings = false;

    // State captured before the last RESET.
    bool undoSnapshotValid = false;
    int undoSnapshotSlot = 0;
    std::array<float, SpectrumAnalyzer::numBins> undoSnapshotLevels {};
    std::array<float, SpectrumAnalyzer::numBins> undoAverage {};
    std::array<float, SpectrumAnalyzer::numBins> undoPeaks {};
    std::array<float, SpectrumAnalyzer::numBins> undoReferenceDb {};
    std::shared_ptr<juce::AudioBuffer<float>> undoReferenceAudio;
    float undoReferenceLufs = -100.0f;
    double undoLoopStart = 0.0;
    double undoLoopEnd = 0.0;
    double undoPosition = 0.0;
    bool undoReferenceValid = false;
    juce::String undoReferencePath;
    int undoTruePeakOvers = 0;
    float undoTruePeakHold = -100.0f;

    juce::String referencePath;
    double pendingReferenceLoopStart = -1.0;
    double pendingReferenceLoopEnd = -1.0;
    double pendingReferencePosition = 0.0;
    int lastReferenceGeneration = 0;

    std::unique_ptr<juce::FileChooser> referenceChooser;
    std::unique_ptr<ReferenceLoader> referenceLoader;
    std::unique_ptr<juce::FileChooser> snapshotChooser;
    std::unique_ptr<juce::FileChooser> pngChooser;
    std::unique_ptr<juce::TooltipWindow> tooltipWindow;

    bool monoActive = false;

    float rangeLow = 20.0f;
    float rangeHigh = 20000.0f;
    int spanIndex = 0;

    std::array<float, SpectrumAnalyzer::numBins> referenceDb {};
    bool hasReference = false;

    std::array<std::array<float, LudensScopeProcessor::snapshotBins>,
               LudensScopeProcessor::snapshotSlots> snapshots {};
    std::array<bool, LudensScopeProcessor::snapshotSlots> snapshotFilled {};
    int snapshotSlot = 0;
    int snapshotGeneration = -1;

    juce::Rectangle<int> leftColumnBounds;
    juce::Rectangle<int> waterfallDivider;
    float waterfallRatio = 0.32f;
    bool draggingDivider = false;

    SpectrumAnalyzer analyzer;
    SpectrumAnalyzer sideAnalyzer;
    std::vector<float> sideBuffer;
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
