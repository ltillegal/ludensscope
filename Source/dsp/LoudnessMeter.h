#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

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

    float momentaryLufs()  const noexcept { return momentaryValue.load (std::memory_order_relaxed); }
    float shortTermLufs()  const noexcept { return shortTermValue.load (std::memory_order_relaxed); }
    float integratedLufs() const noexcept { return integratedValue.load (std::memory_order_relaxed); }

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

    void flushSubBlock() noexcept;
    float meanOfLast (const std::array<float, windowSubBlocks>& values, int count) const noexcept;
    float computeIntegrated() const noexcept;

    Biquad preL, preR, rlbL, rlbR;

    double sampleRate = 48000.0;
    int samplesPerSubBlock = 4800;
    int subBlockCounter = 0;
    double sumL = 0.0, sumR = 0.0;

    std::array<float, windowSubBlocks> blockMsL {}, blockMsR {};
    int blockWriteIndex = 0;
    int blocksSeen = 0;

    std::array<float, maxIntegratedBlocks> integratedEnergy {};
    int integratedCount = 0;

    std::atomic<float> momentaryValue  { -100.0f };
    std::atomic<float> shortTermValue  { -100.0f };
    std::atomic<float> integratedValue { -100.0f };
};
