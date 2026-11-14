#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

// Stereo loudness meter implementing ITU-R BS.1770-4 (K-weighting, gated
// integrated loudness, momentary and short-term windows).
//
// All signal processing happens on the audio thread. Results are published to
// the UI through lock-free atomics, so the editor can read them at any time.
class LoudnessMeter
{
public:
    void prepare (double newSampleRate) noexcept;
    void reset() noexcept;
    void process (const float* left, const float* right, int numSamples) noexcept;
    void processMono (const float* left, int numSamples) noexcept;

    float momentaryLufs()  const noexcept { return momentaryValue.load (std::memory_order_relaxed); }
    float shortTermLufs()  const noexcept { return shortTermValue.load (std::memory_order_relaxed); }
    float integratedLufs() const noexcept { return integratedValue.load (std::memory_order_relaxed); }

    // EBU R128 loudness range (LRA) in LU, computed from the short-term
    // histogram with the -70 LUFS absolute and -20 LU relative gates.
    // Returns -1 when there is not enough short-term data yet.
    float lraLu() const noexcept;

    static constexpr int integratedBlockCapacity = 36000; // 1 h of 400 ms blocks
    static constexpr int lraHistogramBins = 700;

    // Audio-thread only: copy/restore the long-term measurement for undo.
    void captureState (float* integratedOut, int& countOut, std::uint32_t* histogramOut) const noexcept;
    void restoreState (const float* integrated, int count, const std::uint32_t* histogram) noexcept;

private:
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;

        void reset() noexcept { z1 = z2 = 0.0f; }

        float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    static constexpr int windowSubBlocks = 40;        // 4 s at 100 ms per sub-block
    static constexpr int maxIntegratedBlocks = 36000; // 1 h of 400 ms blocks

    static constexpr int lraBins = 700;               // -70..0 LUFS in 0.1 LU steps
    static constexpr int lraMinSamples = 10;          // 1 s of short-term values

    void flushSubBlock() noexcept;
    void clearLraHistogram() noexcept;
    float meanOfLast (const std::array<float, windowSubBlocks>& values, int count) const noexcept;
    float computeIntegrated() const noexcept;

    Biquad preL, preR, rlbL, rlbR;

    double sampleRate = 48000.0;
    int samplesPerSubBlock = 4800;
    int subBlockCounter = 0;
    double sumL = 0.0, sumR = 0.0;
    bool monoMode = false;

    std::array<float, windowSubBlocks> blockMsL {}, blockMsR {};
    int blockWriteIndex = 0;
    int blocksSeen = 0;

    std::array<float, maxIntegratedBlocks> integratedEnergy {};
    int integratedCount = 0;
    int integratedWrite = 0;

    std::array<std::atomic<std::uint32_t>, lraBins> lraHistogram {};

    std::atomic<float> momentaryValue  { -100.0f };
    std::atomic<float> shortTermValue  { -100.0f };
    std::atomic<float> integratedValue { -100.0f };
};
