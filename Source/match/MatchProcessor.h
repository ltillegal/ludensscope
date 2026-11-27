#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/SpectrumAnalyzer.h"

// LudensMatch: a reference-matching EQ. This first milestone is the shell -
// the window, the parameters and the analysis feed. The correction DSP comes
// next; the processor currently passes audio through untouched.
class LudensMatchProcessor : public juce::AudioProcessor
{
public:
    LudensMatchProcessor();
    ~LudensMatchProcessor() override = default;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "LudensMatch"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& parameters() noexcept { return apvts; }

    // The editor pulls audio here and runs its own analyser on the UI thread.
    int readSamples (float* left, float* right, int maxFrames) noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();

    juce::AudioProcessorValueTreeState apvts;

    juce::AbstractFifo fifo { 1 << 15 };
    std::vector<float> fifoLeft, fifoRight;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LudensMatchProcessor)
};
