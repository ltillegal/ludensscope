#pragma once

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>

// Per-band stereo image: correlation and side/mid energy ratio, derived from the
// complex cross-spectrum of the left and right channels.
//
// SpectrumAnalyzer averages power over several overlapping windows, but
// correlation cannot be averaged that way - phase between windows cancels. One
// window per update is enough here, and it keeps the extra cost to two FFTs.
class StereoFieldAnalyzer
{
public:
    static constexpr int numBins = 512;
    static constexpr int maxFftOrder = 14;
    static constexpr int maxFftSize = 1 << maxFftOrder;

    static constexpr float minFrequency = 20.0f;
    static constexpr float maxFrequency = 20000.0f;

    StereoFieldAnalyzer()
    {
        buildFft (fftSize);
        prepare (sampleRate);
    }

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;

        buildWindow();
        buildBins();

        reset();
    }

    void setFftSize (int newSize)
    {
        const int order = (int) std::lround (std::log2 ((double) newSize));

        if (newSize < 1024 || newSize > maxFftSize || (1 << order) != newSize)
            return;

        if (newSize == fftSize)
            return;

        buildFft (newSize);
        prepare (sampleRate);
    }

    // 0 = Hann, 1 = Blackman-Harris, 2 = flat top - mirrors SpectrumAnalyzer.
    void setWindow (int newWindow)
    {
        newWindow = juce::jlimit (0, 2, newWindow);

        if (newWindow == windowType)
            return;

        windowType = newWindow;
        buildWindow();
    }

    void setTiming (float newAttack, float newRelease) noexcept
    {
        attackCoefficient = juce::jlimit (0.01f, 0.99f, newAttack);
        releaseCoefficient = juce::jlimit (0.01f, 0.99f, newRelease);
    }

    void push (const float* left, const float* right, int numSamples) noexcept
    {
        const int historySize = (int) historyL.size();
        const int n = std::min (numSamples, historySize);

        if (n <= 0)
            return;

        const int keep = historySize - n;

        if (keep > 0)
        {
            std::memmove (historyL.data(), historyL.data() + n, sizeof (float) * (size_t) keep);
            std::memmove (historyR.data(), historyR.data() + n, sizeof (float) * (size_t) keep);
        }

        for (int i = 0; i < n; ++i)
        {
            const int index = numSamples - n + i;
            historyL[(size_t) (keep + i)] = left[index];
            historyR[(size_t) (keep + i)] = right != nullptr ? right[index] : left[index];
        }

        hasAudio = true;
    }

    void reset() noexcept
    {
        historyL.fill (0.0f);
        historyR.fill (0.0f);
        bufL.fill (0.0f);
        bufR.fill (0.0f);

        averageL2.fill (0.0);
        averageR2.fill (0.0);
        averageCross.fill (0.0);

        corrValues.fill (1.0f);
        sideMidValues.fill (kSideMidFloorDb);

        smoothedCorrelation.fill (1.0f);
        smoothedSideMid.fill (kSideMidFloorDb);

        smoothedInitialised = false;
        hasAudio = false;
    }

    void update()
    {
        if (! hasAudio)
            return;

        const int n = fftSize;
        const int half = n / 2;

        std::fill (bufL.begin(), bufL.begin() + 2 * n, 0.0f);
        std::fill (bufR.begin(), bufR.begin() + 2 * n, 0.0f);

        for (int i = 0; i < n; ++i)
        {
            const float w = window[(size_t) i];
            bufL[(size_t) i] = historyL[(size_t) ((int) historyL.size() - n + i)] * w;
            bufR[(size_t) i] = historyR[(size_t) ((int) historyR.size() - n + i)] * w;
        }

        fft->performRealOnlyForwardTransform (bufL.data(), true);
        fft->performRealOnlyForwardTransform (bufR.data(), true);

        const bool initialise = ! smoothedInitialised;

        // A single frame gives a very noisy cross-spectrum estimate: in narrow
        // bands the phase of one noisy bin swings correlation between +1 and
        // -1. Average the accumulators over time and derive correlation from
        // the averages - the estimate then converges instead of flickering.
        constexpr double averageCoefficient = 0.25;

        for (int b = 0; b < numBins; ++b)
        {
            const int first = juce::jlimit (1, half - 1, binStart[(size_t) b]);
            const int last  = juce::jlimit (first + 1, half, binEnd[(size_t) b]);

            double l2 = 0.0;
            double r2 = 0.0;
            double crossReal = 0.0;

            for (int bin = first; bin < last; ++bin)
            {
                const float lr = bufL[(size_t) (2 * bin)];
                const float li = bufL[(size_t) (2 * bin + 1)];
                const float rr = bufR[(size_t) (2 * bin)];
                const float ri = bufR[(size_t) (2 * bin + 1)];

                l2 += (double) lr * lr + (double) li * li;
                r2 += (double) rr * rr + (double) ri * ri;
                crossReal += (double) lr * rr + (double) li * ri;
            }

            const double energy = l2 + r2;
            const double level = energy * (double) magnitudeScale * (double) magnitudeScale;

            if (level <= kNoiseFloorLinear)
                continue;

            double& avgL = averageL2[(size_t) b];
            double& avgR = averageR2[(size_t) b];
            double& avgX = averageCross[(size_t) b];

            if (initialise)
            {
                avgL = l2;
                avgR = r2;
                avgX = crossReal;
            }
            else
            {
                avgL += averageCoefficient * (l2 - avgL);
                avgR += averageCoefficient * (r2 - avgR);
                avgX += averageCoefficient * (crossReal - avgX);
            }

            const double denom = std::sqrt (avgL * avgR);

            if (denom <= 0.0)
                continue;

            corrValues[(size_t) b] = (float) juce::jlimit (-1.0, 1.0, avgX / denom);

            // Fully inverted material makes mid cancel exactly, so add a tiny
            // relative floor instead of branching on the sign - the branch
            // flipped to silence on rounding noise.
            const double total = avgL + avgR;
            const double side = std::max (0.0, total - 2.0 * avgX);
            const double mid  = std::max (0.0, total + 2.0 * avgX);
            const double tiny = total * 1.0e-12;

            sideMidValues[(size_t) b] = (float) juce::jlimit ((double) kSideMidFloorDb, 24.0,
                                                              10.0 * std::log10 ((side + tiny)
                                                                                 / (mid + tiny)));
        }

        for (int b = 0; b < numBins; ++b)
        {
            const float corr = corrValues[(size_t) b];
            const float sideMid = sideMidValues[(size_t) b];

            float& smoothCorr = smoothedCorrelation[(size_t) b];
            float& smoothSideMid = smoothedSideMid[(size_t) b];

            if (initialise)
            {
                smoothCorr = corr;
                smoothSideMid = sideMid;
                continue;
            }

            const float corrCoefficient = (corr < smoothCorr) ? releaseCoefficient : attackCoefficient;
            smoothCorr += corrCoefficient * (corr - smoothCorr);

            const float sideCoefficient = (sideMid < smoothSideMid) ? releaseCoefficient
                                                                    : attackCoefficient;
            smoothSideMid += sideCoefficient * (sideMid - smoothSideMid);
        }

        smoothedInitialised = true;
    }

    const std::array<float, numBins>& correlation() const noexcept { return smoothedCorrelation; }
    const std::array<float, numBins>& sideMid() const noexcept { return smoothedSideMid; }
    bool hasData() const noexcept { return smoothedInitialised; }

    float frequencyForBin (int bin) const noexcept
    {
        return binHz[(size_t) juce::jlimit (0, numBins - 1, bin)];
    }

private:
    // -100 dBFS: below this a band is leakage or converter noise and its
    // correlation would be random.
    static constexpr double kNoiseFloorLinear = 1.0e-10;
    static constexpr float kSideMidFloorDb = -40.0f;

    void buildFft (int size)
    {
        fftSize = size;
        fftOrder = (int) std::lround (std::log2 ((double) size));
        fft = std::make_unique<juce::dsp::FFT> (fftOrder);
    }

    void buildWindow()
    {
        constexpr double pi = 3.14159265358979323846;
        double windowSum = 0.0;

        for (int i = 0; i < fftSize; ++i)
        {
            const double phase = 2.0 * pi * (double) i / (double) fftSize;
            double value = 0.0;

            switch (windowType)
            {
                case 0:
                    value = 0.5 - 0.5 * std::cos (phase);
                    break;

                case 1:
                    value = 0.35875
                            - 0.48829 * std::cos (phase)
                            + 0.14128 * std::cos (2.0 * phase)
                            - 0.01168 * std::cos (3.0 * phase);
                    break;

                default:
                    value = 0.21557895
                            - 0.41663158 * std::cos (phase)
                            + 0.277263158 * std::cos (2.0 * phase)
                            - 0.083578947 * std::cos (3.0 * phase)
                            + 0.006947368 * std::cos (4.0 * phase);
                    break;
            }

            window[(size_t) i] = (float) value;
            windowSum += value;
        }

        // JUCE's forward FFT is unnormalised: a sine of amplitude A produces a
        // peak magnitude of A * windowSum / 2, so 2 / windowSum reads dBFS.
        magnitudeScale = (float) (2.0 / std::max (1.0, windowSum));
        windowSum = 0.0;
    }

    void buildBins()
    {
        const float minRatio = minFrequency / (float) sampleRate;
        const float maxRatio = maxFrequency / (float) sampleRate;
        const float logRange = std::log (maxRatio / minRatio);
        const int half = fftSize / 2;

        for (int i = 0; i < numBins; ++i)
        {
            const float f0 = minRatio * std::exp (logRange * (float) i / (float) numBins);
            const float f1 = minRatio * std::exp (logRange * (float) (i + 1) / (float) numBins);

            int b0 = (int) std::floor (f0 * (float) fftSize);
            int b1 = (int) std::ceil  (f1 * (float) fftSize);

            b0 = juce::jlimit (1, half - 1, b0);
            b1 = juce::jlimit (b0 + 1, half, b1);

            binStart[(size_t) i] = b0;
            binEnd[(size_t) i] = b1;
            binHz[(size_t) i] = std::sqrt (f0 * f1) * (float) sampleRate;
        }
    }

    std::unique_ptr<juce::dsp::FFT> fft;

    int fftOrder = 12;
    int fftSize = 4096;
    int windowType = 0;

    double sampleRate = 48000.0;

    std::array<float, maxFftSize> historyL {};
    std::array<float, maxFftSize> historyR {};
    std::array<float, 2 * maxFftSize> bufL {};
    std::array<float, 2 * maxFftSize> bufR {};
    std::array<float, maxFftSize> window {};

    std::array<int, numBins> binStart {};
    std::array<int, numBins> binEnd {};
    std::array<float, numBins> binHz {};

    std::array<double, numBins> averageL2 {};
    std::array<double, numBins> averageR2 {};
    std::array<double, numBins> averageCross {};
    std::array<float, numBins> corrValues {};
    std::array<float, numBins> sideMidValues {};
    std::array<float, numBins> smoothedCorrelation {};
    std::array<float, numBins> smoothedSideMid {};

    float magnitudeScale = 1.0f;
    float attackCoefficient = 0.55f;
    float releaseCoefficient = 0.12f;

    bool smoothedInitialised = false;
    bool hasAudio = false;
};
