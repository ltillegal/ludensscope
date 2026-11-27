#pragma once

#include "PdfWriter.h"
#include "TrackReport.h"

#include <array>
#include <cmath>

// Draws the A4 report card for a TrackReport. Vector only: the numbers and the
// curves stay sharp at any zoom in a PDF reader.
namespace report
{
inline juce::String clock (double seconds)
{
    const int total = (int) std::round (seconds);
    return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
}

inline juce::String percent (float value)
{
    if (value < 0.5f)
        return "0";

    return juce::String (juce::roundToInt (value));
}
}  // namespace report

inline bool writeTrackReportPdf (const TrackReport& report,
                                 const std::array<float, SpectrumAnalyzer::numBins>* targetCurve,
                                 const juce::File& file,
                                 const juce::String& pluginVersion,
                                 const juce::String& comment = {})
{
    PdfWriter pdf;

    const juce::Colour ink   { 0xff17262e };
    const juce::Colour dim   { 0xff6b7c85 };
    const juce::Colour line  { 0xffd7e0e5 };
    const juce::Colour panel { 0xfff2f6f8 };
    const juce::Colour accent{ 0xff076b7f };
    const juce::Colour hot   { 0xffb53327 };

    const float margin = 40.0f;
    const float right = PdfWriter::pageWidth - margin;
    const float contentWidth = right - margin;

    // --- header ---------------------------------------------------------
    pdf.setFill (accent);
    pdf.text (margin, 46.0f, "LUDENSSCOPE  TRACK REPORT", 9.0f, true);
    pdf.setFill (dim);
    pdf.text (right, 46.0f, juce::Time::getCurrentTime().formatted ("%Y-%m-%d"),
              9.0f, false, PdfWriter::Align::right);

    pdf.setFill (line);
    pdf.fillRect (margin, 54.0f, contentWidth, 0.8f);

    // Long file names must not run off the page.
    auto fit = [] (const juce::String& value, float size, bool bold, float maxWidth)
    {
        juce::String text = value;

        while (text.length() > 4
               && PdfWriter::textWidth (text + "...", size, bold) > maxWidth)
            text = text.dropLastCharacters (1);

        if (text != value)
            text += "...";

        return text;
    };

    pdf.setFill (ink);
    pdf.text (margin, 82.0f, fit (report.fileName, 19.0f, true, contentWidth), 19.0f, true);

    juce::String meta;
    meta << report::clock (report.durationSeconds)
         << "   -   " << juce::String (report.sampleRate / 1000.0, 1) << " kHz"
         << "   -   " << (report.channels > 1 ? "stereo" : "mono")
         << "   -   target " << juce::String (report.targetLufs, 1) << " LUFS";

    if (report.truncated)
        meta << "   -   first 12 minutes analysed";

    const float metaSize = PdfWriter::textWidth (meta, 9.0f, false) > contentWidth ? 7.5f : 9.0f;

    pdf.setFill (dim);
    pdf.text (margin, 100.0f, fit (meta, metaSize, false, contentWidth), metaSize);

    // --- KPI tiles ------------------------------------------------------
    struct Tile { const char* label; juce::String value; juce::String unit; juce::Colour colour; };

    juce::String plrText = report.plrLu > 0.0f ? juce::String (report.plrLu, 1) : juce::String ("-");
    juce::String lraText = report.lraLu > 0.05f ? juce::String (report.lraLu, 1) : juce::String ("-");

    const std::array<Tile, 4> tiles { {
        { "INTEGRATED", juce::String (report.integratedLufs, 1), "LUFS", ink },
        { "LOUDNESS RANGE", lraText, "LU", ink },
        { "PEAK TO LOUDNESS", plrText, "LU", ink },
        { "TRUE PEAK", juce::String (report.truePeakDbtp, 1), "dBTP",
          report.truePeakDbtp > -1.0f ? hot : ink },
    } };

    const float tileTop = 122.0f;
    const float tileHeight = 64.0f;
    const float tileGap = 8.0f;
    const float tileWidth = (contentWidth - tileGap * 3.0f) / 4.0f;

    for (int i = 0; i < 4; ++i)
    {
        const float x = margin + (tileWidth + tileGap) * (float) i;

        pdf.setFill (panel);
        pdf.fillRect (x, tileTop, tileWidth, tileHeight);
        pdf.setStroke (line, 0.6f);
        pdf.strokeRect (x, tileTop, tileWidth, tileHeight);

        pdf.setFill (dim);
        pdf.text (x + 10.0f, tileTop + 15.0f, tiles[(size_t) i].label, 7.0f, true);

        pdf.setFill (tiles[(size_t) i].colour);
        pdf.text (x + 10.0f, tileTop + 44.0f, tiles[(size_t) i].value, 21.0f, true);
        pdf.setFill (dim);
        pdf.text (x + 10.0f + PdfWriter::textWidth (tiles[(size_t) i].value, 21.0f, true) + 5.0f,
                  tileTop + 44.0f, tiles[(size_t) i].unit, 8.0f);
    }

    // --- loudness timeline ---------------------------------------------
    const float timelineTop = 216.0f;
    const float timelineBottom = 392.0f;
    const float timelineHeight = timelineBottom - timelineTop;

    pdf.setFill (ink);
    pdf.text (margin, timelineTop - 12.0f, "SHORT-TERM LOUDNESS", 8.0f, true);
    pdf.setFill (dim);
    pdf.text (right, timelineTop - 12.0f,
              "above target: " + juce::String (report.timeAboveTarget, 1) + " s in "
                  + juce::String (report.excursionCount)
                  + (report.excursionCount == 1 ? " passage" : " passages"),
              8.0f, false, PdfWriter::Align::right);

    // Y range: keep the target visible with headroom above it.
    float highest = report.maxShortTermLufs;
    if (highest < report.targetLufs + 3.0f)
        highest = report.targetLufs + 3.0f;

    const float topLufs = std::ceil ((highest + 1.5f) / 3.0f) * 3.0f;
    const float bottomLufs = -36.0f;

    auto lufsToY = [&] (float lufs)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (lufs - bottomLufs) / (topLufs - bottomLufs));
        return timelineBottom - timelineHeight * t;
    };

    pdf.setFill (panel);
    pdf.fillRect (margin, timelineTop, contentWidth, timelineHeight);

    for (float lufs = 0.0f; lufs >= bottomLufs; lufs -= 6.0f)
    {
        const float y = lufsToY (lufs);
        pdf.setStroke (line, 0.5f);
        pdf.line (margin, y, right, y);

        if (lufs > topLufs - 1.0f)
            continue;

        pdf.setFill (dim);
        pdf.text (margin - 4.0f, y + 3.0f, juce::String ((int) lufs), 6.5f, false,
                  PdfWriter::Align::right);
    }

    // Shade the passages above target before the curve goes on top. The list
    // comes from the analyser, so a passage that is still open at the end of
    // the file is shaded as well.
    {
        auto timeToX = [&] (double seconds)
        {
            const double d = juce::jmax (0.001, report.durationSeconds);
            return margin + contentWidth * (float) juce::jlimit (0.0, 1.0, seconds / d);
        };

        for (const auto& excursion : report.excursions)
        {
            const float x0 = timeToX (excursion.startSeconds);
            const float x1 = timeToX (excursion.startSeconds + excursion.durationSeconds);

            pdf.setFill (panel.interpolatedWith (hot, 0.11f));
            pdf.fillRect (x0, timelineTop, juce::jmax (1.0f, x1 - x0), timelineHeight);
        }
    }

    pdf.setStroke (hot, 0.8f);
    pdf.dashedLine (margin, lufsToY (report.targetLufs), right, lufsToY (report.targetLufs));

    if (! report.shortTermTimeline.empty())
    {
        std::vector<juce::Point<float>> points;
        points.reserve (report.shortTermTimeline.size());

        const double d = juce::jmax (0.001, report.durationSeconds);

        for (size_t i = 0; i < report.shortTermTimeline.size(); ++i)
        {
            const double seconds = (double) i * 0.1;
            const float x = margin + contentWidth
                                  * (float) juce::jlimit (0.0, 1.0, seconds / d);
            const float value = report.shortTermTimeline[i];

            points.push_back ({ x, lufsToY (value > bottomLufs ? value : bottomLufs) });
        }

        pdf.setStroke (accent, 1.1f);
        pdf.polyline (points);
    }

    // --- spectrum -------------------------------------------------------
    const float spectrumTop = 432.0f;
    const float spectrumBottom = 604.0f;
    const float spectrumHeight = spectrumBottom - spectrumTop;

    pdf.setFill (ink);
    pdf.text (margin, spectrumTop - 12.0f, "AVERAGE SPECTRUM", 8.0f, true);
    pdf.setFill (dim);
    pdf.text (right, spectrumTop - 12.0f,
              "low " + report::percent (report.bandLowPercent) + " %   mid "
                  + report::percent (report.bandMidPercent) + " %   high "
                  + report::percent (report.bandHighPercent) + " %",
              8.0f, false, PdfWriter::Align::right);

    pdf.setFill (panel);
    pdf.fillRect (margin, spectrumTop, contentWidth, spectrumHeight);

    constexpr float minHz = 20.0f;
    constexpr float maxHz = 20000.0f;

    auto hzToX = [&] (float hz)
    {
        return margin + contentWidth
                       * (float) (std::log (hz / minHz) / std::log (maxHz / minHz));
    };

    auto dbToY = [&] (float db)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (db + 72.0f) / 72.0f);
        return spectrumBottom - spectrumHeight * t;
    };

    for (float db = 0.0f; db >= -72.0f; db -= 12.0f)
    {
        const float y = dbToY (db);
        pdf.setStroke (line, 0.5f);
        pdf.line (margin, y, right, y);

        if (db > -0.5f)
            continue;

        pdf.setFill (dim);
        pdf.text (margin - 4.0f, y + 3.0f, juce::String ((int) db), 6.5f, false,
                  PdfWriter::Align::right);
    }

    for (const float hz : { 100.0f, 1000.0f, 10000.0f })
    {
        const float x = hzToX (hz);
        pdf.setStroke (line, 0.5f);
        pdf.line (x, spectrumTop, x, spectrumBottom);
        pdf.setFill (dim);
        const juce::String label = hz < 1000.0f ? juce::String ((int) hz)
                                                : juce::String (hz / 1000.0f, 0) + "k";
        pdf.text (x, spectrumBottom + 9.0f, label, 6.5f, false, PdfWriter::Align::centre);
    }

    if (targetCurve != nullptr)
    {
        std::vector<juce::Point<float>> targetPoints;

        for (int i = 0; i < SpectrumAnalyzer::numBins; ++i)
        {
            const float hz = minHz * std::pow (maxHz / minHz,
                                               (float) i / (float) (SpectrumAnalyzer::numBins - 1));
            targetPoints.push_back ({ hzToX (hz), dbToY ((*targetCurve)[(size_t) i]) });
        }

        pdf.setStroke (juce::Colour (0xffc98a2b), 0.8f);
        pdf.polyline (targetPoints);
    }

    {
        std::vector<juce::Point<float>> points;
        points.reserve (SpectrumAnalyzer::numBins);

        for (int i = 0; i < SpectrumAnalyzer::numBins; ++i)
        {
            const float hz = minHz * std::pow (maxHz / minHz,
                                               (float) i / (float) (SpectrumAnalyzer::numBins - 1));
            points.push_back ({ hzToX (hz), dbToY (report.spectrum[(size_t) i]) });
        }

        pdf.setStroke (accent, 1.0f);
        pdf.polyline (points);
    }

    // --- excursions table ----------------------------------------------
    float y = spectrumBottom + 34.0f;

    pdf.setFill (ink);
    pdf.text (margin, y, "WHERE IT WENT OVER TARGET", 8.0f, true);

    y += 12.0f;

    pdf.setStroke (line, 0.6f);
    pdf.line (margin, y, right, y);

    y += 11.0f;
    pdf.setFill (dim);
    pdf.text (margin, y, "START", 6.5f, true);
    pdf.text (margin + 90.0f, y, "LENGTH", 6.5f, true);
    pdf.text (margin + 170.0f, y, "PEAK", 6.5f, true);
    pdf.text (margin + 260.0f, y, "OVER TARGET", 6.5f, true);

    y += 10.0f;
    pdf.setStroke (line, 0.5f);
    pdf.line (margin, y, right, y);

    y += 12.0f;

    if (report.worstExcursions.empty())
    {
        pdf.setFill (dim);
        pdf.text (margin, y, "No passage exceeded the target by more than 0.5 LU.", 8.0f);
    }
    else
    {
        for (const auto& excursion : report.worstExcursions)
        {
            pdf.setFill (ink);
            pdf.text (margin, y, report::clock (excursion.startSeconds), 8.0f);
            pdf.text (margin + 90.0f, y, juce::String (excursion.durationSeconds, 1) + " s", 8.0f);
            pdf.text (margin + 170.0f, y, juce::String (excursion.peakLufs, 1) + " LUFS", 8.0f);
            pdf.setFill (hot);
            pdf.text (margin + 260.0f, y,
                      "+" + juce::String (excursion.peakLufs - report.targetLufs, 1) + " LU",
                      8.0f);
            y += 13.0f;
        }
    }

    // --- footer ---------------------------------------------------------
    pdf.setStroke (line, 0.6f);
    pdf.line (margin, PdfWriter::pageHeight - 58.0f, right, PdfWriter::pageHeight - 58.0f);

    pdf.setFill (dim);
    pdf.text (margin, PdfWriter::pageHeight - 44.0f,
              "LudensScope " + pluginVersion + "  -  alek.ludens@gmail.com  -  ludensscope.pages.dev",
              7.5f);

    if (comment.isNotEmpty())
    {
        pdf.setFill (ink);
        pdf.text (margin, PdfWriter::pageHeight - 30.0f, comment, 8.0f);
    }

    return pdf.save (file);
}
