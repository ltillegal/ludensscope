#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

#include "dsp/SpectrumAnalyzer.h"
#include "dsp/LoudnessMeter.h"

// LudensMatch: a reference-matching EQ.
//
// The analysis runs on the UI thread (the editor owns the FFT), and the result
// reaches the audio thread as eight band gains plus an amount/mix pair. The
// processor never blocks and never allocates in processBlock.
class LudensMatchProcessor : public juce::AudioProcessor
{
public:
    static constexpr int numBands = 8;

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

    // UI thread: eight correction gains in dB, plus the band centre
    // frequencies they belong to.
    void setBandGains (const float* gainsDb, int count) noexcept;
    float bandGain (int band) const noexcept;
    static float bandCentreHz (int band) noexcept;

    // Loudness of the material under analysis, so the correction can ignore
    // the level difference between the mix and the reference.
    float shortTermLufs() const noexcept { return loudness.shortTermLufs(); }
    float integratedLufs() const noexcept { return loudness.integratedLufs(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    void rebuildCoefficients (float amountScale) noexcept;

    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1L = 0.0f, z2L = 0.0f, z1R = 0.0f, z2R = 0.0f;

        void reset() noexcept { z1L = z2L = z1R = z2R = 0.0f; }

        float process (float x, float& z1, float& z2) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    juce::AudioProcessorValueTreeState apvts;

    juce::AudioParameterFloat* amountParam = nullptr;
    juce::AudioParameterFloat* mixParam = nullptr;
    juce::AudioParameterFloat* outputParam = nullptr;

    std::atomic<float> gainsDb[numBands] {};
    std::atomic<int> gainsVersion { 0 };

    LoudnessMeter loudness;
    std::array<Biquad, numBands> filters {};
    int appliedVersion = 0;
    float builtAmount = -1.0f;
    std::array<bool, numBands> bandActive {};
    double preparedRate = 48000.0;

    float smoothedAmount = 1.0f;
    float smoothedMix = 1.0f;

    juce::AbstractFifo fifo { 1 << 15 };
    std::vector<float> fifoLeft, fifoRight;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LudensMatchProcessor)
};
