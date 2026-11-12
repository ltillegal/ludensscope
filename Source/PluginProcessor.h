#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

#include "dsp/LoudnessMeter.h"
#include "dsp/SpectrumAnalyzer.h"

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

    // Reference player. The editor decodes/resamples on a background thread
    // and hands the finished buffer over here; the audio thread only reads it.
    void setReferenceAudio (std::unique_ptr<juce::AudioBuffer<float>> audio, float lufs);
    void clearReferenceAudio();
    bool hasReferenceAudio() const noexcept
    {
        return referenceLength.load (std::memory_order_relaxed) > 0;
    }

    void setReferencePlaying (bool shouldPlay) noexcept;
    void setReferenceAudible (bool shouldBeAudible) noexcept;
    void setReferenceLoop (bool shouldLoop) noexcept { referenceLoop.store (shouldLoop); }
    void setReferenceGainDb (float gainDb) noexcept { referenceGainDb.store (gainDb); }

    bool isReferencePlaying() const noexcept { return referencePlaying.load(); }
    bool isReferenceAudible() const noexcept { return referenceAudible.load(); }
    bool isReferenceLooping() const noexcept { return referenceLoop.load(); }
    std::int64_t referencePositionSamples() const noexcept { return referencePosition.load(); }
    std::int64_t referenceLengthSamples() const noexcept { return referenceLength.load(); }
    float referenceLufs() const noexcept { return referenceLufsValue.load(); }

    // FFT snapshots (UI state, persisted in the project).
    static constexpr int snapshotBins = SpectrumAnalyzer::numBins;
    static constexpr int snapshotSlots = 4;

    void setSnapshot (int slot, const float* levelsDb);
    bool isSnapshotFilled (int slot) const noexcept;
    const float* snapshotLevels (int slot) const noexcept;
    void clearSnapshots() noexcept;
    int selectedSnapshot() const noexcept;
    void setSelectedSnapshot (int slot) noexcept;
    int snapshotGeneration() const noexcept;

    std::uint64_t clippedSampleCount() const noexcept
    {
        return clippedSamples.load (std::memory_order_relaxed);
    }

    void resetClipCounters() noexcept
    {
        clippedSamples.store (0, std::memory_order_relaxed);
    }

    float momentaryLufs()  const noexcept { return loudness.momentaryLufs(); }
    float shortTermLufs()  const noexcept { return loudness.shortTermLufs(); }
    float integratedLufs() const noexcept { return loudness.integratedLufs(); }
    float lraLu()          const noexcept { return loudness.lraLu(); }

private:
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    LoudnessMeter loudness;

    static constexpr int fifoSize = 1 << 15;
    juce::AbstractFifo fifo { fifoSize };
    std::array<float, fifoSize> fifoLeft {};
    std::array<float, fifoSize> fifoRight {};

    std::atomic<bool> resetRequested { false };
    std::atomic<std::uint64_t> clippedSamples { 0 };

    juce::SpinLock referenceLock;
    std::shared_ptr<juce::AudioBuffer<float>> referenceAudio;
    std::atomic<bool> referencePlaying { false };
    std::atomic<bool> referenceAudible { false };
    std::atomic<bool> referenceLoop { true };
    std::atomic<std::int64_t> referencePosition { 0 };
    std::atomic<std::int64_t> referenceLength { 0 };
    std::atomic<float> referenceGainDb { 0.0f };
    std::atomic<float> referenceLufsValue { -100.0f };

    float referenceMix = 0.0f;
    float referenceCurrentGain = 1.0f;

    std::array<std::array<float, snapshotBins>, snapshotSlots> snapshots {};
    std::array<bool, snapshotSlots> snapshotFilled {};
    std::atomic<int> selectedSnapshotValue { 0 };
    std::atomic<int> snapshotGenerationValue { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LudensScopeProcessor)
};
