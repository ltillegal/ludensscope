#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

#include "dsp/LoudnessMeter.h"
#include "dsp/SpectrumAnalyzer.h"
#include "dsp/TruePeakDetector.h"

// Offline analysis of a whole file for the report card. Streaming: the file is
// decoded block by block and never held in memory, so a ten minute track costs
// the same as a thirty second one.
struct TrackReport
{
    struct Excursion
    {
        double startSeconds = 0.0;
        double durationSeconds = 0.0;
        float peakLufs = -100.0f;
    };

    bool ok = false;
    bool cancelled = false;
    bool truncated = false;

    juce::String fileName;
    double durationSeconds = 0.0;
    double sampleRate = 0.0;
    int channels = 0;

    float integratedLufs = -100.0f;
    float lraLu = 0.0f;
    float maxShortTermLufs = -100.0f;
    float truePeakDbtp = -100.0f;
    float samplePeakDbfs = -100.0f;
    float plrLu = 0.0f;

    float targetLufs = -14.0f;
    double timeAboveTarget = 0.0;
    int excursionCount = 0;

    // 10 Hz short-term loudness, starting at 0 s.
    std::vector<float> shortTermTimeline;

    // Every passage above the target, in time order; worstExcursions is the
    // longest few, for the table.
    std::vector<Excursion> excursions;
    std::vector<Excursion> worstExcursions;

    std::array<float, SpectrumAnalyzer::numBins> spectrum {};
    float bandLowPercent = 0.0f;
    float bandMidPercent = 0.0f;
    float bandHighPercent = 0.0f;
};

inline TrackReport analyzeTrackFile (const juce::File& file, float targetLufs,
                                     double maxSeconds = 720.0,
                                     std::function<bool()> shouldExit = {})
{
    TrackReport report;
    report.fileName = file.getFileName();
    report.targetLufs = targetLufs;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || reader->lengthInSamples <= 0)
        return report;

    const double rate = reader->sampleRate > 0.0 ? reader->sampleRate : 48000.0;
    const int channels = (int) juce::jmax (1u, reader->numChannels);
    const auto maxSamples = (juce::int64) (rate * maxSeconds);
    const auto totalSamples = juce::jmin (reader->lengthInSamples, maxSamples);

    if (totalSamples <= 0)
        return report;

    report.truncated = reader->lengthInSamples > totalSamples;
    report.sampleRate = rate;
    report.channels = channels;

    LoudnessMeter meter;
    meter.prepare (rate);

    TruePeakDetector truePeakL, truePeakR;

    SpectrumAnalyzer spectrum;
    spectrum.prepare (rate);
    spectrum.setFftSize (4096);

    constexpr int blockSize = 4096;
    juce::AudioBuffer<float> buffer (2, blockSize);

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);

    juce::int64 position = 0;
    double nextTimelinePoint = 0.0;
    double nextSpectrumUpdate = 0.0;
    float samplePeakLinear = 0.0f;

    while (position < totalSamples)
    {
        if (shouldExit && shouldExit())
        {
            report.cancelled = true;
            return report;
        }

        const int n = (int) juce::jmin ((juce::int64) blockSize, totalSamples - position);

        if (! reader->read (&buffer, 0, n, position, true, channels > 1))
            break;

        if (channels == 1)
            buffer.copyFrom (1, 0, buffer, 0, 0, n);

        meter.process (left, right, n);

        report.truePeakDbtp = std::max (report.truePeakDbtp,
                                        juce::Decibels::gainToDecibels (
                                            std::max (truePeakL.process (left, n),
                                                      truePeakR.process (right, n)), -100.0f));

        for (int i = 0; i < n; ++i)
            samplePeakLinear = std::max (samplePeakLinear,
                                         std::max (std::abs (left[i]), std::abs (right[i])));

        spectrum.push (left, right, n);

        position += n;

        const double seconds = (double) position / rate;

        while (nextTimelinePoint <= seconds)
        {
            const float shortTerm = meter.shortTermLufs();
            report.shortTermTimeline.push_back (shortTerm);
            report.maxShortTermLufs = std::max (report.maxShortTermLufs, shortTerm);
            nextTimelinePoint += 0.1;
        }

        // The spectrum average only needs a few updates per second.
        if (seconds >= nextSpectrumUpdate)
        {
            spectrum.update();
            nextSpectrumUpdate = seconds + 0.1;
        }

    }

    report.samplePeakDbfs = juce::Decibels::gainToDecibels (samplePeakLinear, -100.0f);
    report.integratedLufs = meter.integratedLufs();
    report.lraLu = juce::jmax (0.0f, meter.lraLu());

    if (report.truePeakDbtp > -99.0f && report.integratedLufs > -99.0f)
        report.plrLu = report.truePeakDbtp - report.integratedLufs;

    report.durationSeconds = (double) position / rate;

    // Loudness excursions: start above target + 0.5 LU, end below target - 0.1
    // LU, so a curve hovering on the line does not produce a hundred entries.
    const float high = report.targetLufs + 0.5f;
    const float low = report.targetLufs - 0.1f;
    bool inside = false;
    double start = 0.0;
    float peak = -100.0f;

    for (size_t i = 0; i < report.shortTermTimeline.size(); ++i)
    {
        const float value = report.shortTermTimeline[i];
        const double seconds = (double) i * 0.1;

        if (! inside && value > high)
        {
            inside = true;
            start = seconds;
            peak = value;
        }
        else if (inside)
        {
            peak = std::max (peak, value);

            if (value < low)
            {
                inside = false;
                const double duration = seconds - start;

                if (duration >= 0.2)
                {
                    report.excursionCount += 1;
                    report.timeAboveTarget += duration;
                    report.excursions.push_back ({ start, duration, peak });
                }

                peak = -100.0f;
            }
        }
    }

    if (inside)
    {
        const double duration = report.durationSeconds - start;

        if (duration >= 0.2)
        {
            report.excursionCount += 1;
            report.timeAboveTarget += duration;
            report.excursions.push_back ({ start, duration, peak });
        }
    }

    // A track that ends while still above the target leaves its last excursion
    // open; it is already in the list above, so the drawing sees it too.
    report.worstExcursions = report.excursions;

    std::sort (report.worstExcursions.begin(), report.worstExcursions.end(),
               [] (const TrackReport::Excursion& a, const TrackReport::Excursion& b)
               {
                   return a.durationSeconds > b.durationSeconds;
               });

    if (report.worstExcursions.size() > 5)
        report.worstExcursions.resize (5);

    report.spectrum = spectrum.averageLevels();

    // Same band balance as the HUD pills: power weighted by band width.
    float bandPower[3] = {};

    for (int i = 0; i < SpectrumAnalyzer::numBins; ++i)
    {
        constexpr float minHz = 20.0f;
        constexpr float maxHz = 20000.0f;
        const float hz = minHz * std::pow (maxHz / minHz,
                                           (float) i / (float) (SpectrumAnalyzer::numBins - 1));
        const float power = std::pow (10.0f, report.spectrum[(size_t) i] / 10.0f) * hz;
        const int band = hz < 250.0f ? 0 : (hz < 4000.0f ? 1 : 2);

        bandPower[band] += power;
    }

    const float total = bandPower[0] + bandPower[1] + bandPower[2];

    if (total > 1.0e-9f)
    {
        report.bandLowPercent = 100.0f * bandPower[0] / total;
        report.bandMidPercent = 100.0f * bandPower[1] / total;
        report.bandHighPercent = 100.0f * bandPower[2] / total;
    }

    report.ok = true;
    return report;
}
