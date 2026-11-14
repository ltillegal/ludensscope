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
    std::function<void (int)> onSelectSlot;
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
    std::array<juce::Rectangle<float>, 4> slotAreas;

    bool draggingLoop = false, draggingStart = false, draggingEnd = false;
    double dragAnchor = 0.0;
};

inline void ReferenceStrip::resized()
{
    auto b = getLocalBounds().toFloat().reduced (10.0f, 6.0f);
    auto left = b.removeFromLeft (210.0f);
    auto right = b.removeFromRight (190.0f);
    auto mid = b.reduced (10.0f, 0.0f);

    left.removeFromTop (12.0f);
    auto tabs = left.removeFromTop (20.0f);

    for (auto& slot : slotAreas)
    {
        slot = tabs.removeFromLeft (24.0f);
        tabs.removeFromLeft (3.0f);
    }

    tabs.removeFromLeft (4.0f);
    refFileArea = tabs.removeFromLeft (72.0f);
    nameArea = left.removeFromTop (12.0f);

    waveArea = mid.withTrimmedTop (2.0f).withTrimmedBottom (12.0f);

    auto row = right.removeFromTop (20.0f);
    playArea = row.removeFromLeft (24.0f);
    loopArea = row.removeFromLeft (40.0f);
    abArea = row.removeFromLeft (36.0f);
    matchArea = row.removeFromLeft (46.0f);
    mirrorArea = row.removeFromLeft (44.0f);
}

inline void ReferenceStrip::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    theme::drawPanel (g, b);

    auto area = b.reduced (10.0f, 6.0f);
    auto left = area.removeFromLeft (210.0f);
    auto right = area.removeFromRight (190.0f);
    auto mid = area.reduced (10.0f, 0.0f);

    auto caption = left.removeFromTop (12.0f);
    g.setFont (theme::detailFont (10.0f));
    g.setColour (theme::textDim);
    g.drawText ("REFERENCE", caption, juce::Justification::centredLeft);

    for (int i = 0; i < 4; ++i)
    {
        const bool active = state.activeSlot == i;
        const bool filled = state.filled[(size_t) i];

        g.setColour (active ? theme::accent.withAlpha (0.18f) : theme::bg.withAlpha (0.5f));
        g.fillRoundedRectangle (slotAreas[(size_t) i], 3.0f);
        g.setColour (active ? theme::accent : theme::panelLine);
        g.drawRoundedRectangle (slotAreas[(size_t) i].reduced (0.5f), 3.0f, 1.0f);
        g.setFont (theme::labelFont (8.5f));
        g.setColour (active ? theme::accent : (filled ? theme::text : theme::textDim));
        g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + i)),
                    slotAreas[(size_t) i], juce::Justification::centred);
    }

    g.setFont (theme::labelFont (8.0f));
    g.setColour (theme::textDim);
    g.drawText ("REF FILE", refFileArea, juce::Justification::centred);

    left.removeFromTop (20.0f); // slot tabs, drawn from slotAreas above
    auto nameRow = left.removeFromTop (12.0f);
    g.setFont (theme::labelFont (8.5f));
    g.setColour (state.name.isNotEmpty() ? theme::text : theme::textDim);
    g.drawText (state.name.isEmpty() ? "no reference" : state.name,
                nameRow, juce::Justification::centredLeft, true);

    g.setFont (theme::labelFont (7.5f));
    g.setColour (theme::textDim.withAlpha (0.85f));
    g.drawText (state.lufs > -99.0f
                    ? juce::String (state.lufs, 1) + " LUFS"
                    : "-inf LUFS",
                left.removeFromTop (10.0f), juce::Justification::centredLeft);

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

    // Transport.
    g.setColour (state.playing ? theme::accent2 : theme::accent);

    if (state.playing)
    {
        g.fillRect (playArea.getCentreX() - 3.0f, playArea.getCentreY() - 3.0f, 6.0f, 6.0f);
    }
    else
    {
        juce::Path triangle;
        triangle.addTriangle (playArea.getCentreX() - 3.0f, playArea.getCentreY() - 4.0f,
                              playArea.getCentreX() - 3.0f, playArea.getCentreY() + 4.0f,
                              playArea.getCentreX() + 4.0f, playArea.getCentreY());
        g.fillPath (triangle);
    }

    g.setFont (theme::labelFont (8.0f));
    g.setColour (state.looping ? theme::accent2 : theme::textDim);
    g.drawText ("LOOP", loopArea, juce::Justification::centred);
    g.setColour (state.audible ? theme::accent : theme::textDim);
    g.drawText (state.audible ? "REF" : "MIX", abArea, juce::Justification::centred);
    g.setColour (state.match ? theme::accent2 : theme::textDim);
    g.drawText ("MATCH", matchArea, juce::Justification::centred);
    g.setColour (state.mirror ? theme::accent : theme::textDim);
    g.drawText ("MIRROR", mirrorArea, juce::Justification::centred);

    right.removeFromTop (20.0f); // transport row, drawn from the areas above
    g.setFont (theme::monoFont (8.0f));
    g.setColour (theme::textDim);
    g.drawText (state.duration > 0.0
                    ? formatTime (state.position) + " / " + formatTime (state.duration)
                    : "--:--",
                right.removeFromTop (12.0f), juce::Justification::centredRight);
}

inline void ReferenceStrip::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position;

    for (int i = 0; i < 4; ++i)
        if (slotAreas[(size_t) i].contains (p))
        {
            if (onSelectSlot)
                onSelectSlot (i);

            return;
        }

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
