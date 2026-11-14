#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

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
    double getTailLengthSeconds() const override
    {
        const auto length = referenceLength.load (std::memory_order_relaxed);
        const auto rate = getSampleRate();

        return (length > 0 && rate > 0.0)
                   ? juce::jlimit (0.0, 5.0, (double) length / rate)
                   : 0.0;
    }

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

    // Last-reset undo: the editor asks the audio thread to snapshot the
    // long-term loudness state before the reset is applied.
    void requestUndoCapture() noexcept { undoCaptureRequested.store (true, std::memory_order_relaxed); }
    void requestUndoRestore() noexcept { undoRestoreRequested.store (true, std::memory_order_relaxed); }
    bool hasUndo() const noexcept { return undoAvailable.load (std::memory_order_relaxed); }
    void clearUndo() noexcept { undoAvailable.store (false, std::memory_order_relaxed); }

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
    void setReferencePositionSeconds (double seconds) noexcept;
    void setReferenceLoopRange (double startSeconds, double endSeconds) noexcept;

    double referenceLoopStartSeconds() const noexcept;
    double referenceLoopEndSeconds() const noexcept;

    bool isReferencePlaying() const noexcept { return referencePlaying.load(); }
    bool isReferenceAudible() const noexcept { return referenceAudible.load(); }
    bool isReferenceLooping() const noexcept { return referenceLoop.load(); }
    std::int64_t referencePositionSamples() const noexcept { return referencePosition.load(); }
    std::int64_t referenceLengthSamples() const noexcept { return referenceLength.load(); }
    float referenceLufs() const noexcept { return referenceLufsValue.load(); }

    // min/max envelope for the reference transport, 256 columns.
    const std::vector<float>& referenceWaveform() const noexcept { return referenceWaveformData; }

    void rebuildReferenceWaveform();

    // Message-thread reference copy for the reset undo (no audio duplication).
    std::shared_ptr<juce::AudioBuffer<float>> copyReferenceAudio() const;
    void restoreReferenceAudio (std::shared_ptr<juce::AudioBuffer<float>> audio, float lufs,
                                double loopStartSeconds, double loopEndSeconds,
                                double positionSeconds);

    // Reference metadata that survives in the project even when the audio
    // file cannot be reloaded.
    struct ReferenceSettings
    {
        juce::String path;
        std::array<float, 512> curve {};
        bool valid = false;
        float lufs = -100.0f;
        double loopStart = 0.0;
        double loopEnd = 0.0;
        double position = 0.0;
    };

    void setReferenceCurveInfo (const juce::String& path, const float* curveDb, bool valid);
    juce::String getReferencePath() const
    {
        return referenceSlots[(size_t) activeReferenceSlot.load (std::memory_order_relaxed)].path;
    }
    ReferenceSettings getReferenceSettings() const;
    int referenceGeneration() const noexcept { return referenceGenerationValue.load(); }

    int getActiveReferenceSlot() const noexcept
    {
        return activeReferenceSlot.load (std::memory_order_relaxed);
    }

    static constexpr int referenceSlotCount = 4;
    static constexpr juce::int64 maxReferenceTotalBytes = 768ll << 20;

    bool isReferenceSlotFilled (int slot) const noexcept
    {
        return referenceSlots[(size_t) juce::jlimit (0, referenceSlotCount - 1, slot)].valid;
    }
    void switchReferenceSlot (int slot);

    void setReferenceMirror (bool shouldMirror) noexcept
    {
        referenceMirror.store (shouldMirror, std::memory_order_relaxed);
    }
    bool isReferenceMirror() const noexcept { return referenceMirror.load(); }

    // Drop the audio but keep path/curve so the saved reference still shows.
    void unloadReferenceAudio() noexcept;

    // Invalidate all cached slot audio (e.g. after a sample-rate change).
    void dropReferenceAudioCaches();

    // Band solo: high-pass + low-pass pair on the output (monitoring aid).
    void setBandSolo (bool active, float lowHz, float highHz);
    bool isBandSolo() const noexcept { return bandSoloActive.load(); }

    bool supportsMonoBuses() const;
    juce::int64 referenceAudioBytes() const;
    juce::int64 referenceSlotBytes (int slot) const;

    // FFT snapshots (UI state, persisted in the project).
    static constexpr int snapshotBins = SpectrumAnalyzer::numBins;
    static constexpr int snapshotSlots = 4;

    void setSnapshot (int slot, const float* levelsDb);
    bool isSnapshotFilled (int slot) const noexcept;
    const float* snapshotLevels (int slot) const noexcept;
    void clearSnapshots() noexcept;
    void clearSnapshot (int slot) noexcept;
    int selectedSnapshot() const noexcept;
    void setSelectedSnapshot (int slot) noexcept;
    int snapshotGeneration() const noexcept;
    int stateRevision() const noexcept { return stateRevisionValue.load (std::memory_order_acquire); }

    // Analyzer settings (UI state, persisted in the project).
    struct FftSettings
    {
        int fftSize = 4096;
        int timingPreset = 1;
        float tiltDbPerOctave = 0.0f;
        int windowType = 0;
        int smoothingPreset = 0;
        int overlapPreset = 0;
        int noteGrid = 0;
    };

    FftSettings getFftSettings() const noexcept { return fftSettingsValue; }
    void setFftSettings (const FftSettings& settings) noexcept { fftSettingsValue = settings; }

    // View state (UI-only, persisted in the project).
    struct ViewSettings
    {
        int guidesEnabled = 0;
        int guidePreset = 0;
        int sideCurve = 0;
        int peakMarkers = 1;
        int difference = 0;
        int spanIndex = 0;
        float rangeLow = 20.0f;
        float rangeHigh = 20000.0f;
        float targetLufs = -14.0f;
        int waterfallHistory = 2;
        int oscWindow = 0;
        float oscGain = 1.0f;
        int differenceSource = 0;
        juce::String targetCurveName;
        int levelMatch = 1;
        int mirror = 0;
        float waterfallRatio = 0.32f;
        int gainStaging = 0;
        int vuZeroDbfs = -18;
        int showMatch = 0;
        int uiScalePercent = 0;
    };

    ViewSettings getViewSettings() const noexcept { return viewSettingsValue; }
    void setViewSettings (const ViewSettings& settings) noexcept { viewSettingsValue = settings; }

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

    struct BiquadCoefficients
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    };

    struct BiquadMemory
    {
        float z1 = 0.0f, z2 = 0.0f;
    };

    static BiquadCoefficients makeHighPass (double sampleRate, double frequency, double q) noexcept;
    static BiquadCoefficients makeLowPass (double sampleRate, double frequency, double q) noexcept;
    static float processBiquad (const BiquadCoefficients& coefficients,
                                BiquadMemory& memory, float sample) noexcept;

    LoudnessMeter loudness;

    static constexpr int fifoSize = 1 << 17;
    juce::AbstractFifo fifo { fifoSize };
    std::array<float, fifoSize> fifoLeft {};
    std::array<float, fifoSize> fifoRight {};

    std::atomic<bool> resetRequested { false };
    std::atomic<bool> undoCaptureRequested { false };
    std::atomic<bool> undoRestoreRequested { false };
    std::atomic<bool> undoAvailable { false };
    std::unique_ptr<float[]> undoIntegrated;
    std::unique_ptr<std::uint32_t[]> undoHistogram;
    int undoIntegratedCount = 0;
    std::uint64_t undoClippedSamples = 0;
    std::atomic<std::uint64_t> clippedSamples { 0 };

    mutable juce::SpinLock referenceLock;
    std::shared_ptr<juce::AudioBuffer<float>> referenceAudio;
    std::atomic<bool> referencePlaying { false };
    std::atomic<bool> referenceAudible { false };
    std::atomic<bool> referenceLoop { true };
    std::atomic<std::int64_t> referencePosition { 0 };
    std::atomic<std::int64_t> referenceLength { 0 };
    std::atomic<std::int64_t> referenceLoopStart { 0 };
    std::atomic<std::int64_t> referenceLoopEnd { 0 };
    std::atomic<float> referenceGainDb { 0.0f };
    std::atomic<float> referenceLufsValue { -100.0f };

    float referenceMix = 0.0f;
    float referenceCurrentGain = 1.0f;

    static constexpr int referenceWaveformColumns = 256;
    std::vector<float> referenceWaveformData;

    struct ReferenceSlotInfo
    {
        juce::String path;
        std::array<float, 512> curve {};
        bool valid = false;
        float lufs = -100.0f;
        double loopStart = 0.0;
        double loopEnd = 0.0;
        double position = 0.0;
        std::shared_ptr<juce::AudioBuffer<float>> audio;
    };

    std::array<ReferenceSlotInfo, referenceSlotCount> referenceSlots;
    std::atomic<int> activeReferenceSlot { 0 };
    std::atomic<bool> referenceMirror { false };
    std::atomic<int> referenceGenerationValue { 0 };

    void syncActiveReferenceSlot();

    std::array<std::array<float, snapshotBins>, snapshotSlots> snapshots {};
    std::array<bool, snapshotSlots> snapshotFilled {};
    std::atomic<int> selectedSnapshotValue { 0 };
    std::atomic<int> snapshotGenerationValue { 0 };
    std::atomic<int> stateRevisionValue { 0 };

    FftSettings fftSettingsValue;
    ViewSettings viewSettingsValue;

    std::atomic<bool> bandSoloActive { false };
    std::atomic<std::uint32_t> soloVersion { 0 };
    std::atomic<bool> soloResetRequested { false };
    std::atomic<float> soloHighPass[5] {};
    std::atomic<float> soloLowPass[5] {};
    BiquadCoefficients soloCachedHighPass, soloCachedLowPass;
    bool soloCachedActive = false;
    BiquadMemory soloHighPassMemory[2];
    BiquadMemory soloLowPassMemory[2];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LudensScopeProcessor)
};
