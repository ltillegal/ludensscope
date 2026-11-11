#include "LoudnessMeter.h"

namespace
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kLufsOffset = -0.691;
    constexpr double kAbsoluteGateLufs = -70.0;
    constexpr double kRelativeGateDb = -10.0;

    inline float toLufs (double meanSquare) noexcept
    {
        return (float) (kLufsOffset + 10.0 * std::log10 (std::max (meanSquare, 1.0e-12)));
    }
}

void LoudnessMeter::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    samplesPerSubBlock = std::max (1, (int) std::llround (sampleRate * 0.1));

    // K-weighting stage 1: high shelf. Coefficients follow libebur128's
    // derivation of the BS.1770 analogue prototype for any sample rate.
    {
        const double f0 = 1681.974450955533;
        const double G  = 3.999843853973347;
        const double Q  = 0.7071752369554196;
        const double K  = std::tan (kPi * f0 / sampleRate);
        const double Vh = std::pow (10.0, G / 20.0);
        const double Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;

        preL.b0 = preR.b0 = (float) ((Vh + Vb * K / Q + K * K) / a0);
        preL.b1 = preR.b1 = (float) (2.0 * (K * K - Vh) / a0);
        preL.b2 = preR.b2 = (float) ((Vh - Vb * K / Q + K * K) / a0);
        preL.a1 = preR.a1 = (float) (2.0 * (K * K - 1.0) / a0);
        preL.a2 = preR.a2 = (float) ((1.0 - K / Q + K * K) / a0);
    }

    // K-weighting stage 2: RLB high-pass.
    {
        const double f0 = 38.13547087602444;
        const double Q  = 0.5003270373238773;
        const double K  = std::tan (kPi * f0 / sampleRate);
        const double denom = 1.0 + K / Q + K * K;

        rlbL.b0 = rlbR.b0 = 1.0f;
        rlbL.b1 = rlbR.b1 = -2.0f;
        rlbL.b2 = rlbR.b2 = 1.0f;
        rlbL.a1 = rlbR.a1 = (float) (2.0 * (K * K - 1.0) / denom);
        rlbL.a2 = rlbR.a2 = (float) ((1.0 - K / Q + K * K) / denom);
    }

    reset();
}

void LoudnessMeter::reset() noexcept
{
    preL.reset();
    preR.reset();
    rlbL.reset();
    rlbR.reset();

    subBlockCounter = 0;
    sumL = sumR = 0.0;

    blockMsL.fill (0.0f);
    blockMsR.fill (0.0f);
    blockWriteIndex = 0;
    blocksSeen = 0;

    integratedEnergy.fill (0.0f);
    integratedCount = 0;

    momentaryValue.store (-100.0f, std::memory_order_relaxed);
    shortTermValue.store (-100.0f, std::memory_order_relaxed);
    integratedValue.store (-100.0f, std::memory_order_relaxed);
}

void LoudnessMeter::process (const float* left, const float* right, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        const float yl = rlbL.process (preL.process (left[i]));
        const float yr = rlbR.process (preR.process (right[i]));

        sumL += (double) yl * (double) yl;
        sumR += (double) yr * (double) yr;

        if (++subBlockCounter >= samplesPerSubBlock)
            flushSubBlock();
    }
}

void LoudnessMeter::flushSubBlock() noexcept
{
    const float msL = (float) (sumL / (double) std::max (1, subBlockCounter));
    const float msR = (float) (sumR / (double) std::max (1, subBlockCounter));

    blockMsL[(size_t) blockWriteIndex] = msL;
    blockMsR[(size_t) blockWriteIndex] = msR;
    blockWriteIndex = (blockWriteIndex + 1) % windowSubBlocks;
    blocksSeen = std::min (blocksSeen + 1, windowSubBlocks);

    subBlockCounter = 0;
    sumL = sumR = 0.0;

    momentaryValue.store (toLufs (meanOfLast (blockMsL, 4) + meanOfLast (blockMsR, 4)),
                          std::memory_order_relaxed);
    shortTermValue.store (toLufs (meanOfLast (blockMsL, 30) + meanOfLast (blockMsR, 30)),
                          std::memory_order_relaxed);

    if (integratedCount < maxIntegratedBlocks)
    {
        integratedEnergy[(size_t) integratedCount++] =
            meanOfLast (blockMsL, 4) + meanOfLast (blockMsR, 4);

        integratedValue.store (computeIntegrated(), std::memory_order_relaxed);
    }
}

float LoudnessMeter::meanOfLast (const std::array<float, windowSubBlocks>& values, int count) const noexcept
{
    const int n = std::min (count, blocksSeen);

    if (n <= 0)
        return 0.0f;

    double sum = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const int index = (blockWriteIndex - 1 - i + windowSubBlocks) % windowSubBlocks;
        sum += (double) values[(size_t) index];
    }

    return (float) (sum / (double) n);
}

float LoudnessMeter::computeIntegrated() const noexcept
{
    const double absoluteGateEnergy = std::pow (10.0, (kAbsoluteGateLufs - kLufsOffset) / 10.0);

    double sum = 0.0;
    int count = 0;

    for (int i = 0; i < integratedCount; ++i)
    {
        const double energy = (double) integratedEnergy[(size_t) i];

        if (energy > absoluteGateEnergy)
        {
            sum += energy;
            ++count;
        }
    }

    if (count == 0)
        return -100.0f;

    const double relativeGateEnergy = (sum / (double) count) * std::pow (10.0, kRelativeGateDb / 10.0);

    sum = 0.0;
    count = 0;

    for (int i = 0; i < integratedCount; ++i)
    {
        const double energy = (double) integratedEnergy[(size_t) i];

        if (energy > absoluteGateEnergy && energy > relativeGateEnergy)
        {
            sum += energy;
            ++count;
        }
    }

    if (count == 0)
        return -100.0f;

    return toLufs (sum / (double) count);
}
