#pragma once

#include <algorithm>
#include <array>
#include <cmath>

// 4x oversampled true-peak detector using a windowed-sinc polyphase FIR.
// Keeps its own history, so feed it every sample exactly once.
class TruePeakDetector
{
public:
    TruePeakDetector() { buildCoefficients(); reset(); }

    void reset() noexcept { history.fill (0.0f); }

    float process (const float* samples, int numSamples) noexcept
    {
        float peak = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            for (int t = taps - 1; t > 0; --t)
                history[(size_t) t] = history[(size_t) (t - 1)];

            history[0] = samples[i];

            for (int phase = 0; phase < phases; ++phase)
            {
                float y = 0.0f;

                for (int t = 0; t < taps; ++t)
                    y += coefficients[(size_t) phase][(size_t) t] * history[(size_t) t];

                peak = std::max (peak, std::abs (y));
            }
        }

        return peak;
    }

private:
    static constexpr int taps = 12;
    static constexpr int phases = 4;
    static constexpr double kPi = 3.14159265358979323846;

    void buildCoefficients() noexcept
    {
        constexpr int protoLength = phases * taps;
        std::array<double, protoLength> prototype {};

        const double centre = (double) (protoLength - 1) * 0.5;

        for (int i = 0; i < protoLength; ++i)
        {
            const double x = ((double) i - centre) / (double) phases;
            const double sinc = std::abs (x) < 1.0e-9
                                    ? 1.0
                                    : std::sin (kPi * x) / (kPi * x);

            const double phase = 2.0 * kPi * (double) i / (double) (protoLength - 1);
            const double window = 0.42 - 0.5 * std::cos (phase) + 0.08 * std::cos (2.0 * phase);

            prototype[(size_t) i] = sinc * window;
        }

        for (int phase = 0; phase < phases; ++phase)
        {
            double sum = 0.0;

            for (int t = 0; t < taps; ++t)
                sum += prototype[(size_t) (phase + phases * t)];

            const double norm = std::abs (sum) < 1.0e-12 ? 1.0 : 1.0 / sum;

            for (int t = 0; t < taps; ++t)
                coefficients[(size_t) phase][(size_t) t] =
                    (float) (prototype[(size_t) (phase + phases * t)] * norm);
        }
    }

    std::array<std::array<float, taps>, phases> coefficients {};
    std::array<float, taps> history {};
};
