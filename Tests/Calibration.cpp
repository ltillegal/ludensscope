#include "dsp/SpectrumAnalyzer.h"

#include <cmath>
#include <cstdio>
#include <vector>

int main()
{
    constexpr double sampleRate = 48000.0;
    constexpr double pi = 3.14159265358979323846;
    constexpr int fftSize = 4096;

    // Bin-centred tone: bin 85 => 996.09 Hz, amplitude 0.5.
    const double frequency = 85.0 * sampleRate / (double) fftSize;

    std::vector<float> sine ((size_t) fftSize);

    for (int i = 0; i < fftSize; ++i)
        sine[(size_t) i] = 0.5f * (float) std::sin (2.0 * pi * frequency * (double) i / sampleRate);

    juce::dsp::FFT fft (12);

    // Rectangular window, raw magnitudes.
    {
        std::vector<float> data ((size_t) (2 * fftSize), 0.0f);
        std::copy (sine.begin(), sine.end(), data.begin());
        fft.performFrequencyOnlyForwardTransform (data.data());
        std::printf ("rect  |X[85]| = %.6f  (expect ~%.1f unnormalised, ~%.4f with 1/N)\n",
                     data[85], 0.5 * fftSize / 2.0, 0.5 * fftSize / 2.0 / fftSize);
    }

    // Hann window, raw magnitudes.
    {
        std::vector<float> data ((size_t) (2 * fftSize), 0.0f);
        double windowSum = 0.0;

        for (int i = 0; i < fftSize; ++i)
        {
            const float w = (float) (0.5 - 0.5 * std::cos (2.0 * pi * (double) i / (double) fftSize));
            windowSum += w;
            data[(size_t) i] = sine[(size_t) i] * w;
        }

        fft.performFrequencyOnlyForwardTransform (data.data());
        std::printf ("hann  windowSum = %.1f  |X[85]| = %.6f\n", windowSum, data[85]);
        std::printf ("      => A = |X| * 2/windowSum = %.4f\n",
                     data[85] * 2.0 / windowSum);
    }

    // Full analyser path.
    SpectrumAnalyzer analyzer;
    analyzer.prepare (sampleRate);

    std::vector<float> left ((size_t) (fftSize * 4)), right ((size_t) (fftSize * 4));

    for (int i = 0; i < (int) left.size(); ++i)
        left[(size_t) i] = right[(size_t) i] =
            0.5f * (float) std::sin (2.0 * pi * frequency * (double) i / sampleRate);

    analyzer.push (left.data(), right.data(), (int) left.size());

    // Let the display smoothing settle before reading the levels.
    for (int i = 0; i < 30; ++i)
        analyzer.update();

    float best = -1000.0f;
    int bestBin = 0;

    for (int i = 0; i < SpectrumAnalyzer::numBins; ++i)
    {
        if (analyzer.levels()[(size_t) i] > best)
        {
            best = analyzer.levels()[(size_t) i];
            bestBin = i;
        }
    }

    std::printf ("analyser peak = %.2f dBFS at bin %d (expected about -6.0)\n", best, bestBin);

    // A flat-top window should keep the same amplitude calibration.
    SpectrumAnalyzer flatAnalyzer;
    flatAnalyzer.setWindow (SpectrumAnalyzer::Window::flatTop);
    flatAnalyzer.prepare (sampleRate);
    flatAnalyzer.push (left.data(), right.data(), (int) left.size());

    for (int i = 0; i < 30; ++i)
        flatAnalyzer.update();

    float flatBest = -1000.0f;

    for (int i = 0; i < SpectrumAnalyzer::numBins; ++i)
        flatBest = std::max (flatBest, flatAnalyzer.levels()[(size_t) i]);

    const bool flatOk = std::abs (flatBest + 6.0f) <= 0.6f;
    std::printf ("flat-top window peak = %.2f dBFS (expected about -6.0) %s\n",
                 flatBest, flatOk ? "ok" : "FAIL");

    // The 4x overlapped FFT path must keep the same calibration.
    SpectrumAnalyzer overlapAnalyzer;
    overlapAnalyzer.setOverlap (4);
    overlapAnalyzer.prepare (sampleRate);
    overlapAnalyzer.push (left.data(), right.data(), (int) left.size());

    for (int i = 0; i < 30; ++i)
        overlapAnalyzer.update();

    float overlapBest = -1000.0f;

    for (int i = 0; i < SpectrumAnalyzer::numBins; ++i)
        overlapBest = std::max (overlapBest, overlapAnalyzer.levels()[(size_t) i]);

    const bool overlapOk = std::abs (overlapBest + 6.0f) <= 0.6f;
    std::printf ("4x overlap peak = %.2f dBFS (expected about -6.0) %s\n",
                 overlapBest, overlapOk ? "ok" : "FAIL");

    return flatOk && overlapOk ? 0 : 1;
}
