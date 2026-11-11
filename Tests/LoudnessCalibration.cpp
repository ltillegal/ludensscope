#include "dsp/LoudnessMeter.h"
#include "dsp/TruePeakDetector.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    constexpr double pi = 3.14159265358979323846;

    struct Result
    {
        float momentary = 0.0f;
        float shortTerm = 0.0f;
        float integrated = 0.0f;
        float lra = -1.0f;
    };

    Result measureSine (double sampleRate, double amplitude, double frequency,
                        double seconds, bool stereo)
    {
        LoudnessMeter meter;
        meter.prepare (sampleRate);

        const int blockSize = 512;
        const int totalSamples = (int) (sampleRate * seconds);

        std::vector<float> left ((size_t) blockSize);
        std::vector<float> right ((size_t) blockSize);

        for (int offset = 0; offset < totalSamples; offset += blockSize)
        {
            const int n = std::min (blockSize, totalSamples - offset);

            for (int i = 0; i < n; ++i)
            {
                const double t = (double) (offset + i) / sampleRate;
                left[(size_t) i] = (float) (amplitude * std::sin (2.0 * pi * frequency * t));
                right[(size_t) i] = stereo ? left[(size_t) i] : 0.0f;
            }

            meter.process (left.data(), right.data(), n);
        }

        return { meter.momentaryLufs(), meter.shortTermLufs(),
                 meter.integratedLufs(), meter.lraLu() };
    }
}

int main()
{
    int failures = 0;

    const auto check = [&] (const char* name, float value, float expected, float tolerance)
    {
        const bool ok = std::abs (value - expected) <= tolerance;
        std::printf ("%-36s %7.2f  (expected %6.2f +/- %.2f)  %s\n",
                     name, value, expected, tolerance, ok ? "ok" : "FAIL");
        if (!ok)
            ++failures;
    };

    // Full-scale 1 kHz sine in both channels: 0 LUFS (the K-weighting shelf adds
    // about +0.69 dB at 1 kHz, which cancels the -0.691 offset).
    {
        const auto r = measureSine (48000.0, 1.0, 1000.0, 6.0, true);
        check ("0 dBFS stereo sine, momentary", r.momentary, 0.0f, 0.35f);
        check ("0 dBFS stereo sine, short-term", r.shortTerm, 0.0f, 0.35f);
        check ("0 dBFS stereo sine, integrated", r.integrated, 0.0f, 0.35f);
    }

    // -20 dBFS stereo sine => -20 LUFS.
    {
        const auto r = measureSine (48000.0, 0.1, 1000.0, 6.0, true);
        check ("-20 dBFS stereo sine, momentary", r.momentary, -20.0f, 0.35f);
        check ("-20 dBFS stereo sine, integrated", r.integrated, -20.0f, 0.35f);
        check ("-20 dBFS stereo sine, LRA (LU)", r.lra, 0.0f, 0.5f);
    }

    // Loudness range: 10 s at -20 LUFS then 10 s at -35 LUFS => about 15 LU.
    {
        LoudnessMeter meter;
        meter.prepare (48000.0);

        constexpr int blockSize = 512;
        const int total = 48000 * 20;

        std::vector<float> left ((size_t) blockSize);
        std::vector<float> right ((size_t) blockSize);

        for (int offset = 0; offset < total; offset += blockSize)
        {
            const int n = std::min (blockSize, total - offset);
            const double amplitude = (offset < total / 2)
                ? 0.1
                : 0.1 * std::pow (10.0, -15.0 / 20.0);

            for (int i = 0; i < n; ++i)
            {
                const double t = (double) (offset + i) / 48000.0;
                left[(size_t) i] = right[(size_t) i] =
                    (float) (amplitude * std::sin (2.0 * pi * 1000.0 * t));
            }

            meter.process (left.data(), right.data(), n);
        }

        check ("-20 then -35 LUFS, LRA (LU)", meter.lraLu(), 15.0f, 1.0f);
    }

    // Single channel at -20 dBFS is 3 dB quieter than the stereo pair.
    {
        const auto r = measureSine (48000.0, 0.1, 1000.0, 6.0, false);
        check ("-20 dBFS mono sine, integrated", r.integrated, -23.01f, 0.35f);
    }

    // 44.1 kHz must give the same result as 48 kHz.
    {
        const auto r = measureSine (44100.0, 0.1, 1000.0, 6.0, true);
        check ("-20 dBFS @44.1k, integrated", r.integrated, -20.0f, 0.35f);
    }

    // Silence stays at the floor.
    {
        const auto r = measureSine (48000.0, 0.0, 1000.0, 2.0, true);
        check ("silence, integrated", r.integrated, -100.0f, 0.01f);
        check ("silence, LRA is unavailable", r.lra, -1.0f, 0.01f);
    }

    // True peak: fs/4 sine at 45 degrees has samples at +/-0.707 but a true peak
    // of 1.0 (-3.01 dBFS sample peak vs 0 dBTP true peak).
    {
        constexpr int numSamples = 48000;
        std::vector<float> signal ((size_t) numSamples);

        for (int i = 0; i < numSamples; ++i)
        {
            const double t = (double) i / 48000.0;
            signal[(size_t) i] = (float) std::sin (2.0 * pi * 12000.0 * t + pi / 4.0);
        }

        float samplePeak = 0.0f;
        for (float s : signal)
            samplePeak = std::max (samplePeak, std::abs (s));

        TruePeakDetector detector;
        const float truePeak = detector.process (signal.data(), numSamples);

        check ("fs/4 sine, sample peak (dBFS)",
               20.0f * std::log10 (samplePeak), -3.01f, 0.1f);
        check ("fs/4 sine, true peak (dBTP)",
               20.0f * std::log10 (truePeak), 0.0f, 0.15f);
    }

    std::printf ("%s\n", failures == 0 ? "all calibration checks passed" : "CALIBRATION CHECKS FAILED");
    return failures == 0 ? 0 : 1;
}
