#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cmath>
#include <functional>
#include <vector>

#include "Theme.h"

// Full-width reference strip: waveform, loop, transport and A-D slots.
class ReferenceStrip : public juce::Component
{
public:
    struct State
    {
        std::vector<float> waveform; // min/max pairs
        double duration = 0.0, position = 0.0, loopStart = 0.0, loopEnd = 0.0;
        bool playing = false, looping = false, audible = false;
        bool match = false, mirror = false;
        int activeSlot = 0;
        std::array<bool, 4> filled { false, false, false, false };
        juce::String name;
        float lufs = -100.0f;
    };

    ReferenceStrip() = default;

    void setState (State s) { state = std::move (s); repaint(); }

    std::function<void()> onPlay, onLoop, onAb, onMatch, onMirror, onRefFile;
    std::function<void (double)> onSeek;
    std::function<void (double, double)> onLoopRange;

    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    static juce::String formatTime (double seconds)
    {
        const int total = juce::jmax (0, (int) std::floor (seconds));
        return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
    }

    float xAtTime (double seconds) const
    {
        if (state.duration <= 0.0)
            return waveArea.getX();

        return waveArea.getX() + (float) juce::jlimit (0.0, 1.0, seconds / state.duration)
                                    * waveArea.getWidth();
    }

    double timeAtX (float x) const
    {
        if (waveArea.getWidth() <= 0.0f || state.duration <= 0.0)
            return 0.0;

        return juce::jlimit (0.0, 1.0, (double) (x - waveArea.getX()) / waveArea.getWidth())
                   * state.duration;
    }

    State state;
    juce::Rectangle<float> waveArea, playArea, loopArea, abArea, matchArea, mirrorArea;
    juce::Rectangle<float> refFileArea, nameArea;

    bool draggingLoop = false, draggingStart = false, draggingEnd = false;
    double dragAnchor = 0.0;
};

inline void ReferenceStrip::resized()
{
    const float s = juce::jlimit (0.8f, 1.4f, (float) getHeight() / 100.0f);
    auto b = getLocalBounds().toFloat().reduced (10.0f * s, 6.0f * s);
    auto right = b.removeFromRight (230.0f * s);

    waveArea = b.withTrimmedTop (2.0f * s).withTrimmedBottom (12.0f * s);

    auto row = right.removeFromTop (30.0f * s);
    const float btnW = 42.0f * s;
    const float gap = 4.0f * s;

    playArea = row.removeFromLeft (btnW);
    row.removeFromLeft (gap);
    loopArea = row.removeFromLeft (btnW);
    row.removeFromLeft (gap);
    abArea = row.removeFromLeft (btnW);
    row.removeFromLeft (gap);
    matchArea = row.removeFromLeft (btnW);
    row.removeFromLeft (gap);
    mirrorArea = row.removeFromLeft (btnW);

    right.removeFromTop (12.0f * s); // time
    right.removeFromTop (12.0f * s); // name
    refFileArea = right.removeFromTop (14.0f * s);
}

inline void ReferenceStrip::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    theme::drawPanel (g, b);

    const float s = juce::jlimit (0.8f, 1.4f, (float) getHeight() / 100.0f);
    auto area = b.reduced (10.0f * s, 6.0f * s);
    auto right = area.removeFromRight (230.0f * s);

    // Waveform.
    g.setColour (theme::bg.withAlpha (0.55f));
    g.fillRoundedRectangle (waveArea, 3.0f);
    g.setColour (theme::panelLine);
    g.drawRoundedRectangle (waveArea.reduced (0.5f), 3.0f, 1.0f);

    if (state.duration > 0.0 && ! state.waveform.empty())
    {
        const int columns = (int) state.waveform.size() / 2;
        const float midY = waveArea.getCentreY();
        const float amp = waveArea.getHeight() * 0.44f;

        g.setColour (theme::textDim.withAlpha (0.2f));
        g.drawHorizontalLine ((int) midY, waveArea.getX(), waveArea.getRight());

        g.setColour (theme::cold.withAlpha (0.8f));

        for (int x = 0; x < (int) waveArea.getWidth(); ++x)
        {
            const int c = juce::jlimit (0, columns - 1, x * columns / juce::jmax (1, (int) waveArea.getWidth()));
            const float low = state.waveform[(size_t) (c * 2)];
            const float high = state.waveform[(size_t) (c * 2 + 1)];
            g.drawVerticalLine ((int) waveArea.getX() + x,
                                midY - juce::jmax (0.0f, high) * amp,
                                midY - juce::jmin (0.0f, low) * amp);
        }

        const float loopX = xAtTime (state.loopStart);
        const float loopRight = xAtTime (state.loopEnd);

        if (loopRight > loopX + 1.0f)
        {
            g.setColour (theme::accent.withAlpha (0.10f));
            g.fillRect (juce::Rectangle<float> (loopX, waveArea.getY(),
                                                loopRight - loopX, waveArea.getHeight()));
            g.setColour (theme::accent.withAlpha (0.7f));
            g.drawVerticalLine ((int) loopX, waveArea.getY(), waveArea.getBottom());
            g.drawVerticalLine ((int) loopRight, waveArea.getY(), waveArea.getBottom());
        }

        const float playX = xAtTime (state.position);
        g.setColour (theme::accent2);
        g.drawVerticalLine ((int) playX, waveArea.getY(), waveArea.getBottom());

        g.setFont (theme::labelFont (7.0f));
        g.setColour (theme::textDim);
        g.drawText (formatTime (state.loopStart) + "-" + formatTime (state.loopEnd),
                    waveArea.withTrimmedBottom (-13.0f).withTrimmedTop (waveArea.getHeight() + 1.0f),
                    juce::Justification::centredLeft);
        g.drawText (formatTime (state.position) + " / " + formatTime (state.duration),
                    waveArea.withTrimmedBottom (-13.0f).withTrimmedTop (waveArea.getHeight() + 1.0f),
                    juce::Justification::centredRight);
    }
    else
    {
        g.setFont (theme::labelFont (8.5f));
        g.setColour (theme::textDim);
        g.drawText ("load a reference file to see its waveform and loop",
                    waveArea, juce::Justification::centred);
    }

    // Transport: a round play button and rounded pills, evenly spaced.
    const auto playPill = [&]
    {
        g.setColour (state.playing ? theme::accent.withAlpha (0.16f) : theme::bg.withAlpha (0.5f));
        g.fillRoundedRectangle (playArea, playArea.getHeight() * 0.5f);
        g.setColour (state.playing ? theme::accent : theme::panelLine);
        g.drawRoundedRectangle (playArea.reduced (0.5f), playArea.getHeight() * 0.5f, 1.0f);
    };

    playPill();
    g.setColour (state.playing ? theme::accent : theme::accent2);

    if (state.playing)
    {
        g.fillRect (playArea.getCentreX() - 3.5f, playArea.getCentreY() - 3.5f, 7.0f, 7.0f);
    }
    else
    {
        juce::Path triangle;
        triangle.addTriangle (playArea.getCentreX() - 3.0f, playArea.getCentreY() - 4.5f,
                              playArea.getCentreX() - 3.0f, playArea.getCentreY() + 4.5f,
                              playArea.getCentreX() + 5.0f, playArea.getCentreY());
        g.fillPath (triangle);
    }

    const auto pill = [&] (juce::Rectangle<float> r, bool on, const juce::String& text)
    {
        g.setColour (on ? theme::accent.withAlpha (0.16f) : theme::bg.withAlpha (0.5f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (on ? theme::accent : theme::panelLine);
        g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.0f);
        g.setFont (theme::labelFont (8.5f));
        g.setColour (on ? theme::accent : theme::textDim);
        g.drawText (text, r, juce::Justification::centred);
    };

    pill (loopArea, state.looping, "LOOP");
    pill (abArea, state.audible, state.audible ? "REF" : "MIX");
    pill (matchArea, state.match, "MATCH");
    pill (mirrorArea, state.mirror, "MIRROR");

    right.removeFromTop (30.0f * s); // transport row, drawn from the areas above
    g.setFont (theme::monoFont (8.0f));
    g.setColour (theme::textDim);
    g.drawText (state.duration > 0.0
                    ? formatTime (state.position) + " / " + formatTime (state.duration)
                    : "--:--",
                right.removeFromTop (12.0f * s), juce::Justification::centredRight);

    g.setFont (theme::labelFont (8.0f));
    g.setColour (state.name.isNotEmpty() ? theme::text : theme::textDim);
    g.drawText (state.name.isEmpty() ? "no reference" : state.name,
                right.removeFromTop (12.0f * s), juce::Justification::centredRight, true);

    g.setColour (theme::bg.withAlpha (0.5f));
    g.fillRoundedRectangle (refFileArea, refFileArea.getHeight() * 0.5f);
    g.setColour (theme::panelLine);
    g.drawRoundedRectangle (refFileArea.reduced (0.5f), refFileArea.getHeight() * 0.5f, 1.0f);
    g.setFont (theme::labelFont (8.0f));
    g.setColour (theme::textDim);
    g.drawText ("REF FILE", refFileArea, juce::Justification::centred);

    right.removeFromTop (14.0f * s);
    g.setFont (theme::labelFont (7.5f));
    g.setColour (theme::textDim.withAlpha (0.85f));
    g.drawText (state.lufs > -99.0f
                    ? juce::String (state.lufs, 1) + " LUFS"
                    : "-inf LUFS",
                right.removeFromTop (10.0f * s), juce::Justification::centredRight);

    right.removeFromTop (10.0f * s);
    g.setFont (theme::detailFont (9.0f));
    g.setColour (theme::textDim.withAlpha (0.7f));
    g.drawText ("REFERENCE", right.removeFromTop (10.0f * s), juce::Justification::centredRight);
}

inline void ReferenceStrip::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position;

    if (refFileArea.contains (p)) { if (onRefFile) onRefFile(); return; }
    if (playArea.contains (p))    { if (onPlay) onPlay(); return; }
    if (loopArea.contains (p))    { if (onLoop) onLoop(); return; }
    if (abArea.contains (p))      { if (onAb) onAb(); return; }
    if (matchArea.contains (p))   { if (onMatch) onMatch(); return; }
    if (mirrorArea.contains (p))  { if (onMirror) onMirror(); return; }

    if (waveArea.contains (p) && state.duration > 0.0)
    {
        const double t = timeAtX (p.x);

        if (std::abs (p.x - xAtTime (state.loopStart)) <= 5.0f)
        {
            draggingStart = true;
            return;
        }

        if (std::abs (p.x - xAtTime (state.loopEnd)) <= 5.0f)
        {
            draggingEnd = true;
            return;
        }

        draggingLoop = true;
        dragAnchor = t;

        if (onSeek)
            onSeek (t);
    }
}

inline void ReferenceStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (! draggingLoop && ! draggingStart && ! draggingEnd)
        return;

    const double t = timeAtX (e.position.x);

    if (draggingStart)
    {
        if (onLoopRange)
            onLoopRange (juce::jmin (t, state.loopEnd), state.loopEnd);
    }
    else if (draggingEnd)
    {
        if (onLoopRange)
            onLoopRange (state.loopStart, juce::jmax (t, state.loopStart));
    }
    else if (draggingLoop)
    {
        if (onLoopRange)
            onLoopRange (juce::jmin (dragAnchor, t), juce::jmax (dragAnchor, t));
    }
}

inline void ReferenceStrip::mouseUp (const juce::MouseEvent&)
{
    draggingLoop = draggingStart = draggingEnd = false;
}

inline void ReferenceStrip::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (waveArea.contains (e.position) && state.duration > 0.0 && onLoopRange)
        onLoopRange (0.0, state.duration);
}
