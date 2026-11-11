#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

#include "dsp/LoudnessMeter.h"

class LudensScopeProcessor : public juce::AudioProcessor
{
public:
    LudensScopeProcessor();
    ~LudensScopeProcessor() override = default;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "LudensScope"; }
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

    // UI side: drain samples captured by the audio thread.
    int readSamples (float* left, float* right, int maxFrames) noexcept;

    void requestReset() noexcept { resetRequested.store (true, std::memory_order_relaxed); }

    float momentaryLufs()  const noexcept { return loudness.momentaryLufs(); }
    float shortTermLufs()  const noexcept { return loudness.shortTermLufs(); }
    float integratedLufs() const noexcept { return loudness.integratedLufs(); }

private:
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    LoudnessMeter loudness;

    static constexpr int fifoSize = 1 << 15;
    juce::AbstractFifo fifo { fifoSize };
    std::array<float, fifoSize> fifoLeft {};
    std::array<float, fifoSize> fifoRight {};

    std::atomic<bool> resetRequested { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LudensScopeProcessor)
};
