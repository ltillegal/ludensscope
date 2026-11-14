#include "PluginEditor.h"
#include "BinaryData.h"

#include <cmath>

#include "dsp/ReferenceAnalysis.h"
#include "ui/GuidePresets.h"

#include <algorithm>

class ScopeLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour&, bool isHighlighted, bool isDown) override
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = button.getToggleState();

        auto fill = on ? theme::accent : theme::panel;

        if (isDown)             fill = fill.brighter (0.08f);
        else if (isHighlighted) fill = fill.brighter (0.04f);

        const float corner = juce::jmin (bounds.getHeight() * 0.23f, 8.0f);

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, corner);

        g.setColour (on ? theme::accent : theme::panelLine.brighter (0.10f));
        g.drawRoundedRectangle (bounds, corner, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool, bool) override
    {
        g.setFont (theme::labelFont (juce::jlimit (9.0f, 14.0f, button.getHeight() * 0.40f)));

        g.setColour (button.getToggleState() ? theme::panelTop : theme::text);
        g.drawFittedText (button.getButtonText(), button.getLocalBounds(),
                          juce::Justification::centred, 1, 0.9f);
    }
};

static juce::Image tintLogo (const juce::Image& source, juce::Colour colour)
{
    if (! source.isValid())
        return {};

    juce::Image result (juce::Image::ARGB, source.getWidth(), source.getHeight(), true);
    juce::Image::BitmapData src (source, juce::Image::BitmapData::readOnly);
    juce::Image::BitmapData dst (result, juce::Image::BitmapData::writeOnly);

    for (int y = 0; y < source.getHeight(); ++y)
    {
        for (int x = 0; x < source.getWidth(); ++x)
        {
            const auto pixel = src.getPixelColour (x, y);
            dst.setPixelColour (x, y, colour.withAlpha (pixel.getAlpha()));
        }
    }

    return result;
}

void ReferenceSlotSwitch::setState (int activeSlot, bool slotAFilled, bool slotBFilled,
                                    bool slotCFilled, bool slotDFilled)
{
    const int newActive = juce::jlimit (0, 3, activeSlot);
    const std::array<bool, 4> newFilled { slotAFilled, slotBFilled, slotCFilled, slotDFilled };

    if (newActive == active && newFilled == filled)
        return;

    active = newActive;
    filled = newFilled;
    repaint();
}

void ReferenceSlotSwitch::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setColour (theme::panel);
    g.fillRoundedRectangle (bounds, 6.0f);

    const auto inner = bounds.reduced (2.0f);
    const float segmentGap = juce::jmax (1.0f, bounds.getHeight() * 0.08f);
    const float segmentWidth = (inner.getWidth() - segmentGap * 3.0f) * 0.25f;
    const char* letters[] = { "A", "B", "C", "D" };

    for (int i = 0; i < 4; ++i)
    {
        auto segment = inner.withWidth (segmentWidth).translated (i * (segmentWidth + segmentGap), 0.0f);
        const bool activeSegment = i == active;

        if (activeSegment)
        {
            g.setColour (theme::text);
            g.fillRoundedRectangle (segment, 4.0f);
        }

        g.setColour (activeSegment ? theme::bg
                                   : (filled[(size_t) i] ? theme::text.withAlpha (0.8f)
                                                         : theme::textDim));
        g.setFont (theme::monoFont (juce::jlimit (8.0f, 13.0f, getHeight() * 0.38f)));
        g.drawText (letters[i], segment, juce::Justification::centred);

        if (filled[(size_t) i] && ! activeSegment)
        {
            const float dot = juce::jmax (2.0f, getHeight() * 0.10f);
            g.setColour (theme::accent);
            g.fillEllipse (segment.getRight() - dot - 1.0f, segment.getBottom() - dot - 1.0f,
                           dot, dot);
        }
    }
}

void ReferenceSlotSwitch::mouseDown (const juce::MouseEvent& e)
{
    const int slot = juce::jlimit (0, 3, (e.x * 4) / juce::jmax (1, getWidth()));

    if (slot != active && onSelect)
        onSelect (slot);
}

void DonateButton::paintButton (juce::Graphics& g, bool isMouseOver, bool isMouseDown)
{
    const auto bounds = getLocalBounds().toFloat();

    const float corner = juce::jmin (bounds.getHeight() * 0.23f, 8.0f);

    g.setColour (isMouseDown ? theme::panelTop : theme::panel);
    g.fillRoundedRectangle (bounds, corner);
    g.setColour (theme::panelLine);
    g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.0f);

    const bool wide = bounds.getWidth() > 40.0f;
    const float cx = wide ? bounds.getX() + bounds.getHeight() * 0.54f
                          : bounds.getCentreX();
    const float cy = bounds.getCentreY();
    const float s = bounds.getHeight() * (wide ? 0.21f : 0.25f);

    juce::Path heart;
    heart.startNewSubPath (cx, cy + s * 0.85f);
    heart.cubicTo (cx - s * 1.5f, cy - s * 0.1f,
                   cx - s * 0.95f, cy - s * 1.25f,
                   cx, cy - s * 0.45f);
    heart.cubicTo (cx + s * 0.95f, cy - s * 1.25f,
                   cx + s * 1.5f, cy - s * 0.1f,
                   cx, cy + s * 0.85f);
    heart.closeSubPath();

    g.setColour (isMouseOver ? theme::hot.brighter (0.15f) : theme::hot);
    g.fillPath (heart);

    if (wide)
    {
        g.setColour (isMouseOver ? theme::text : theme::text.withAlpha (0.85f));
        g.setFont (theme::labelFont (juce::jlimit (8.0f, 12.0f, bounds.getHeight() * 0.35f)));
        g.drawText ("DONATE", bounds.withTrimmedLeft (bounds.getHeight() * 0.92f),
                    juce::Justification::centredLeft);
    }
}

LudensScopeEditor::LudensScopeEditor (LudensScopeProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p)
{
    theme::setTheme (theme::loadSavedTheme());

    lookAndFeel = std::make_unique<ScopeLookAndFeel>();
    setLookAndFeel (lookAndFeel.get());

    setOpaque (true);
    setWantsKeyboardFocus (true);
    logoSource = juce::ImageCache::getFromMemory (BinaryData::ludens_logo_png,
                                                  BinaryData::ludens_logo_pngSize);
    logoImage = tintLogo (logoSource, theme::text);
    tooltipWindow = std::make_unique<juce::TooltipWindow> (this, 700);
    setResizable (true, true);
    setResizeLimits (1060, 660, 2400, 1500);

    addAndMakeVisible (spectrumView);
    addAndMakeVisible (spectrogramView);
    addAndMakeVisible (meterPanel);
    addAndMakeVisible (vectorScopeView);
    addAndMakeVisible (oscilloscopeView);

    freezeButton.setClickingTogglesState (true);
    freezeButton.setColour (juce::TextButton::buttonColourId, theme::panel);
    freezeButton.setColour (juce::TextButton::buttonOnColourId, theme::accent);
    freezeButton.setColour (juce::TextButton::textColourOffId, theme::text);
    freezeButton.setColour (juce::TextButton::textColourOnId, theme::bg);
    freezeButton.onClick = [this]
    {
        frozen = freezeButton.getToggleState();
        oscilloscopeView.setData (scopeLeft.data(), scopeRight.data(), scopeSize, frozen);
        oscilloscopeView.repaint();
    };
    addAndMakeVisible (freezeButton);

    resetButton.setColour (juce::TextButton::buttonColourId, theme::panel);
    resetButton.setColour (juce::TextButton::textColourOffId, theme::text);
    resetButton.onClick = [this] { resetDisplay(); };
    addAndMakeVisible (resetButton);

    undoButton.setColour (juce::TextButton::buttonColourId, theme::panel);
    undoButton.setColour (juce::TextButton::textColourOffId, theme::textDim);
    undoButton.setColour (juce::TextButton::textColourOnId, theme::accent2);
    undoButton.onClick = [this] { undoReset(); };
    undoButton.setEnabled (false);
    undoButton.setTooltip ("Restore the meters, snapshot, reference and counters from before the last RESET");
    addAndMakeVisible (undoButton);

    slotSwitch.setTooltip ("Reference slots A-D");
    slotSwitch.onSelect = [this] (int slot) { selectReferenceSlot (slot); };
    addAndMakeVisible (slotSwitch);

    themeButton.setTooltip ("Interface colour theme");
    themeButton.onClick = [this] { showThemeMenu(); };
    addAndMakeVisible (themeButton);

    donateButton.setTooltip ("Donate via PayPal (0eskimo0@gmail.com)");
    donateButton.onClick = []
    {
        juce::URL ("https://www.paypal.com/donate/?business=0eskimo0%40gmail.com"
                   "&item_name=LudensScope&currency_code=USD").launchInDefaultBrowser();
    };
    addAndMakeVisible (donateButton);

    captureButton.setColour (juce::TextButton::buttonColourId, theme::panel);
    captureButton.setColour (juce::TextButton::textColourOffId, theme::text);
    captureButton.onClick = [this] { captureReference(); };
    addAndMakeVisible (captureButton);

    monoButton.setClickingTogglesState (true);
    monoButton.setColour (juce::TextButton::buttonColourId, theme::panel);
    monoButton.setColour (juce::TextButton::buttonOnColourId, theme::accent);
    monoButton.setColour (juce::TextButton::textColourOffId, theme::text);
    monoButton.setColour (juce::TextButton::textColourOnId, theme::bg);
    monoButton.onClick = [this] { monoActive = monoButton.getToggleState(); };
    addAndMakeVisible (monoButton);

    spanButton.setColour (juce::TextButton::buttonColourId, theme::panel);
    spanButton.setColour (juce::TextButton::textColourOffId, theme::text);
    spanButton.onClick = [this] { cycleSpan(); };
    addAndMakeVisible (spanButton);

    guidesButton.setColour (juce::TextButton::buttonColourId, theme::panel);
    guidesButton.setColour (juce::TextButton::buttonOnColourId, theme::accent);
    guidesButton.setColour (juce::TextButton::textColourOffId, theme::text);
    guidesButton.setColour (juce::TextButton::textColourOnId, theme::bg);
    guidesButton.onClick = [this] { showGuidesMenu(); };
    addAndMakeVisible (guidesButton);

    spectrumView.setGuideZones (guidePresets()[0].zones);

    spectrumView.onGuideSolo = [this] (int index) { toggleGuideSolo (index); };

    meterPanel.onTargetChange = [this] (float) { saveViewSettings(); };
    oscilloscopeView.onSettingsChanged = [this] { saveViewSettings(); };

    spectrogramView.setPushDivider (waterfallHistory);

    referenceButton.setColour (juce::TextButton::buttonColourId, theme::panel);
    referenceButton.setColour (juce::TextButton::textColourOffId, theme::text);
    referenceButton.onClick = [this] { chooseReferenceFile(); };
    addAndMakeVisible (referenceButton);

    fftButton.setColour (juce::TextButton::buttonColourId, theme::panel);
    fftButton.setColour (juce::TextButton::textColourOffId, theme::text);
    fftButton.onClick = [this] { showFftMenu(); };
    addAndMakeVisible (fftButton);

    fftButton.setTooltip ("FFT size, window, ballistics, tilt, smoothing");
    referenceButton.setTooltip ("Load a reference track; the transport appears on the spectrum");
    guidesButton.setTooltip ("Placement guides; Shift/Alt-click a zone to solo that band");
    spanButton.setTooltip ("Frequency span in octaves (wheel over the spectrum zooms)");
    monoButton.setTooltip ("Meters and scopes use the mono sum; the spectrum is always the mono sum");
    captureButton.setTooltip ("Store the averaged spectrum in the selected snapshot slot");
    freezeButton.setTooltip ("Freeze the spectrum, waterfall and meters");
    resetButton.setTooltip ("Reset meters, the loaded reference and the selected snapshot");

    spectrumView.onRangeChange = [this] (float lowHz, float highHz)
    {
        applyRange (lowHz, highHz);
    };

    spectrumView.onRangeReset = [this]
    {
        spanIndex = 0;
        applyRange (20.0f, 20000.0f);
    };

    spectrumView.onPlayToggle = [this] { toggleReferencePlay(); };
    spectrumView.onLoopToggle = [this] { toggleReferenceLoop(); };
    spectrumView.onAbToggle = [this] { toggleReferenceAb(); };
    spectrumView.onLevelMatchToggle = [this] { toggleLevelMatch(); };
    spectrumView.onMirrorToggle = [this] { toggleMirror(); };
    
    spectrumView.onReferenceSeek = [this] (double seconds)
    {
        processor.setReferencePositionSeconds (seconds);
    };

    spectrumView.onLoopRangeChange = [this] (double start, double end)
    {
        processor.setReferenceLoopRange (start, end);
    };

    spectrumView.setSnapshots (snapshots, snapshotFilled, snapshotSlot);

    spectrumView.onSnapshotSelect = [this] (int slot)
    {
        snapshotSlot = juce::jlimit (0, LudensScopeProcessor::snapshotSlots - 1, slot);
        processor.setSelectedSnapshot (snapshotSlot);
        spectrumView.setSnapshots (snapshots, snapshotFilled, snapshotSlot);
    };

    syncSnapshots();

    readLeft.resize (readChunk);
    readRight.resize (readChunk);

    preparedSampleRate = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
    analyzer.prepare (preparedSampleRate);
    sideAnalyzer.prepare (preparedSampleRate);
    oscilloscopeView.setSampleRate (preparedSampleRate);

    applyFftSettings (processor.getFftSettings());
    applyViewSettings (processor.getViewSettings());
    applyReferenceSettings();
    lastStateRevision = processor.stateRevision();

    setSize (1060, 700);
    startTimerHz (30);
    openEditors().add (this);
}

LudensScopeEditor::~LudensScopeEditor()
{
    auto& editors = openEditors();

    for (int i = editors.size(); --i >= 0;)
        if (editors[i] == nullptr || editors[i].getComponent() == this)
            editors.remove (i);

    referenceLoadGeneration.fetch_add (1, std::memory_order_relaxed);

    if (referenceLoadCancel != nullptr)
        referenceLoadCancel->store (true, std::memory_order_relaxed);

    // Never leave the host's mix filtered or replaced once the window closes.
    processor.setBandSolo (false, 0.0f, 0.0f);
    processor.setReferencePlaying (false);
    processor.setReferenceAudible (false);

    setLookAndFeel (nullptr);
}

void LudensScopeEditor::visibilityChanged()
{
    if (isVisible())
        return;

    processor.setBandSolo (false, 0.0f, 0.0f);
    processor.setReferencePlaying (false);
    processor.setReferenceAudible (false);

    if (soloZone >= 0)
    {
        soloZone = -1;
        spectrumView.setSoloZone (-1);
    }
}

juce::Array<juce::Component::SafePointer<LudensScopeEditor>>&
LudensScopeEditor::openEditors()
{
    static juce::Array<juce::Component::SafePointer<LudensScopeEditor>> editors;
    return editors;
}

void LudensScopeEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient::vertical (theme::bg, 0.0f,
                                                       theme::bgBottom, (float) getHeight()));
    g.fillAll();

    // Soft accent glow behind the spectrum area.
    juce::ColourGradient glow (theme::accent.withAlpha (0.05f),
                               getWidth() * 0.30f, -80.0f,
                               juce::Colours::transparentBlack,
                               getWidth() * 0.62f, getHeight() * 0.55f,
                               true);
    g.setGradientFill (glow);
    g.fillAll();

    if (! railBounds.isEmpty())
    {
        g.setColour (theme::panel.withAlpha (0.35f));
        g.fillRoundedRectangle (railBounds.toFloat(), 8.0f);
        g.setColour (theme::panelLine.withAlpha (0.6f));
        g.drawRoundedRectangle (railBounds.toFloat().reduced (0.5f), 8.0f, 1.0f);

        const int rowTop = railBounds.getBottom() - juce::roundToInt (8.0f * uiScale)
                           - juce::roundToInt (26.0f * uiScale);
        g.setColour (theme::panelLine.withAlpha (0.8f));
        g.fillRect (railBounds.getX() + 12, rowTop - 10, railBounds.getWidth() - 24, 1);
        g.setFont (theme::labelFont (juce::jlimit (7.0f, 10.0f, 8.0f * uiScale)));
        g.setColour (theme::textDim);
        g.drawText ("REF", railBounds.getX(), rowTop - 24, railBounds.getWidth(), 12,
                    juce::Justification::centred);
    }

    auto header = getLocalBounds().reduced (14).removeFromTop (36);

    auto brand = header.removeFromLeft (196);

    auto logoArea = brand.removeFromLeft (36).withSizeKeepingCentre (32, 32);

    if (logoImage.isValid())
        g.drawImage (logoImage, logoArea.toFloat(), juce::RectanglePlacement::centred, false);

    brand.removeFromLeft (8);

    g.setFont (theme::boldFont (19.0f));
    g.setColour (theme::text);
    g.drawText ("LudensScope", brand, juce::Justification::centredLeft);

    g.setFont (theme::labelFont (9.5f));
    g.setColour (theme::textDim);
    g.drawText ("STEREO ANALYZER", header, juce::Justification::centredLeft);

    g.setColour (theme::panelLine.withAlpha (0.6f));
    g.fillRect (14, 53, getWidth() - 28, 1);

    if (fileDragActive)
    {
        g.setColour (theme::bg.withAlpha (0.55f));
        g.fillRect (getLocalBounds());
        g.setColour (theme::accent);
        g.drawRect (getLocalBounds().reduced (3), 2);
        g.setFont (theme::labelFont (15.0f));
        g.drawText ("DROP REFERENCE AUDIO", getLocalBounds(), juce::Justification::centred);
    }
}

void LudensScopeEditor::resized()
{
    auto r = getLocalBounds().reduced (14);

    const float scaleW = (float) getWidth() / 1060.0f;
    const float scaleH = (float) getHeight() / 700.0f;
    uiScale = juce::jlimit (0.9f, 1.35f, juce::jmin (scaleW, scaleH));
    spectrumView.setUiScale (uiScale);
    meterPanel.setUiScale (uiScale);

    auto header = r.removeFromTop (36);
    const int themeWidth = juce::roundToInt (90.0f * uiScale);
    const int themeHeight = juce::jmin (34, juce::roundToInt (26.0f * uiScale));
    themeButton.setBounds (header.removeFromRight (themeWidth)
                               .withSizeKeepingCentre (themeWidth, themeHeight));
    r.removeFromTop (8);

    const int railWidth = juce::roundToInt (88.0f * uiScale);
    railBounds = r.removeFromRight (railWidth);
    r.removeFromRight (juce::roundToInt (10.0f * uiScale));

    auto rail = railBounds.withTrimmedLeft (2).withTrimmedRight (2)
                          .withTrimmedTop (juce::roundToInt (8.0f * uiScale));
    const int buttonHeight = juce::roundToInt (26.0f * uiScale);
    const int refRowHeight = buttonHeight;
    const int refMargin = juce::roundToInt (8.0f * uiScale);
    const int refCaption = juce::roundToInt (34.0f * uiScale);

    const int refTop = railBounds.getBottom() - refMargin - refRowHeight - refCaption;
    const int available = juce::jmax (0, refTop - rail.getY());
    const int minGap = juce::roundToInt (6.0f * uiScale);
    const int maxGap = juce::roundToInt (22.0f * uiScale);
    const int buttonGap = juce::jlimit (minGap, juce::jmax (minGap, maxGap),
                                        (available - buttonHeight * 10) / 9);

    const int stackHeight = buttonHeight * 10 + buttonGap * 9;
    auto stack = rail.withHeight (juce::jmin (available, stackHeight))
                      .translated (0, juce::jmax (0, (available - stackHeight) / 2));

    juce::Component* const buttons[] = {
        &fftButton, &referenceButton, &guidesButton, &spanButton, &monoButton,
        &captureButton, &freezeButton, &undoButton, &resetButton, &donateButton
    };

    for (auto* button : buttons)
    {
        button->setBounds (stack.removeFromTop (buttonHeight));
        stack.removeFromTop (buttonGap);
    }

    slotSwitch.setBounds (railBounds.getX() + juce::roundToInt (2.0f * uiScale),
                          railBounds.getBottom() - refMargin - refRowHeight,
                          railBounds.getWidth() - juce::roundToInt (4.0f * uiScale),
                          refRowHeight);

    const int gap = juce::roundToInt (10.0f * uiScale);
    const int bottomHeight = juce::jlimit (150, 230, (int) ((float) r.getHeight() * 0.34f));
    auto bottom = r.removeFromBottom (bottomHeight);
    r.removeFromBottom (gap);

    const int rightWidth = juce::jlimit (250, 340, (int) ((float) r.getWidth() * 0.28f));
    auto rightColumn = r.removeFromRight (rightWidth);
    r.removeFromRight (gap);

    leftColumnBounds = r;

    const int minSpectrumHeight = 140;
    const int maxWaterfallHeight = juce::jmax (0, r.getHeight() - minSpectrumHeight);
    const int waterfallHeight = juce::jlimit (
        0, maxWaterfallHeight,
        (int) std::lround (waterfallRatio * (float) r.getHeight()));

    auto waterfall = r.removeFromBottom (waterfallHeight);
    waterfallDivider = r.removeFromBottom (gap);

    spectrumView.setBounds (r);
    spectrogramView.setBounds (waterfall);
    meterPanel.setBounds (rightColumn);

    auto vectorBounds = bottom.removeFromLeft (bottomHeight);
    bottom.removeFromLeft (gap);

    vectorScopeView.setBounds (vectorBounds);
    oscilloscopeView.setBounds (bottom);
}

void LudensScopeEditor::timerCallback()
{
    const double sampleRate = processor.getSampleRate();

    if (sampleRate > 0.0 && std::abs (sampleRate - preparedSampleRate) > 1.0e-6)
    {
        preparedSampleRate = sampleRate;
        analyzer.prepare (sampleRate);
        sideAnalyzer.prepare (sampleRate);
        oscilloscopeView.setSampleRate (sampleRate);

        // Cached reference audio is at the old rate; reload it.
        processor.dropReferenceAudioCaches();

        if (processor.getReferenceSettings().valid)
            applyReferenceSettings();
    }

    const int numRead = processor.readSamples (readLeft.data(), readRight.data(), readChunk);

    // Mono check: sum the input for the analyzers without touching the DAW signal.
    if (numRead > 0 && monoActive)
    {
        for (int i = 0; i < numRead; ++i)
        {
            const float mono = 0.5f * (readLeft[(size_t) i] + readRight[(size_t) i]);
            readLeft[(size_t) i] = readRight[(size_t) i] = mono;
        }
    }

    float blockTruePeak = 0.0f;
    float blockSamplePeak = 0.0f;

    if (numRead > 0)
    {
        const float* left = readLeft.data();
        const float* right = readRight.data();

        blockTruePeak = std::max (truePeakLeft.process (left, numRead),
                                  truePeakRight.process (right, numRead));

        for (int i = 0; i < numRead; ++i)
        {
            blockSamplePeak = std::max (blockSamplePeak,
                                        std::max (std::abs (left[i]), std::abs (right[i])));
        }

        if (! frozen)
        {
            analyzer.push (left, right, numRead);
            appendScope (left, right, numRead);

            double sumLL = 0.0, sumRR = 0.0, sumLR = 0.0;

            for (int i = 0; i < numRead; ++i)
            {
                sumLL += (double) left[i] * (double) left[i];
                sumRR += (double) right[i] * (double) right[i];
                sumLR += (double) left[i] * (double) right[i];
            }

            const double denominator = std::sqrt (sumLL * sumRR);
            const float instant = denominator > 1.0e-12 ? (float) (sumLR / denominator) : 0.0f;
            correlation += 0.25f * (instant - correlation);
        }
    }

    const double referenceRate = preparedSampleRate > 0.0 ? preparedSampleRate : 48000.0;

    undoButton.setEnabled (processor.hasUndo());
    slotSwitch.setState (processor.getActiveReferenceSlot(),
                         processor.isReferenceSlotFilled (0),
                         processor.isReferenceSlotFilled (1),
                         processor.isReferenceSlotFilled (2),
                         processor.isReferenceSlotFilled (3));
    spectrumView.setReferenceModes (levelMatch, mirror);

    if (levelMatch && processor.hasReferenceAudio())
        updateReferenceGain();

    spectrumView.setTransport (processor.hasReferenceAudio(),
                               processor.isReferencePlaying(),
                               processor.isReferenceLooping(),
                               processor.isReferenceAudible(),
                               (double) processor.referencePositionSamples() / referenceRate,
                               (double) processor.referenceLengthSamples() / referenceRate,
                               processor.referenceLoopStartSeconds(),
                               processor.referenceLoopEndSeconds());

    if (processor.stateRevision() != lastStateRevision)
    {
        lastStateRevision = processor.stateRevision();
        applyFftSettings (processor.getFftSettings());
        applyViewSettings (processor.getViewSettings());
        applyReferenceSettings();
    }

    if (processor.referenceGeneration() != lastReferenceGeneration)
        applyReferenceSettings();

    if (processor.snapshotGeneration() != snapshotGeneration)
        syncSnapshots();

    if (frozen)
    {
        spectrumView.repaint();
        return;
    }

    if (blockTruePeak > 1.0f)
        ++truePeakOvers;

    constexpr float peakReleasePerTick = 0.35f;

    const float truePeakTarget = juce::Decibels::gainToDecibels (blockTruePeak, -100.0f);
    const float samplePeakTarget = juce::Decibels::gainToDecibels (blockSamplePeak, -100.0f);

    // Peak ballistics: instant attack, slow release; the "max" value is the
    // session maximum and never decays, so the readout stops jittering.
    truePeakDb = std::max (truePeakTarget, truePeakDb - peakReleasePerTick);
    samplePeakDb = std::max (samplePeakTarget, samplePeakDb - peakReleasePerTick);

    if (truePeakDb > truePeakHoldDb)
        truePeakHoldDb = truePeakDb;

    if (numRead > 0)
    {
        analyzer.update();
        spectrogramView.push (analyzer.levels().data(), SpectrumAnalyzer::numBins);
    }

    // Side-channel analyser mirrors the settings and runs only for M/S view.
    if (showSide)
    {
        sideAnalyzer.setFftSize (analyzer.getFftSize());
        sideAnalyzer.setWindow (analyzer.getWindow());
        sideAnalyzer.setTilt (analyzer.getTilt());
        sideAnalyzer.setOctaveSmoothing (analyzer.getOctaveSmoothing());
        sideAnalyzer.setOverlap (analyzer.getOverlap());

        if (timingPreset == 0)
            sideAnalyzer.setTiming (0.85f, 0.30f, 0.80f);
        else if (timingPreset == 1)
            sideAnalyzer.setTiming (0.55f, 0.12f, 0.35f);
        else
            sideAnalyzer.setTiming (0.25f, 0.03f, 0.12f);

        if (numRead > 0)
        {
            sideBuffer.resize ((size_t) numRead);

            for (int i = 0; i < numRead; ++i)
                sideBuffer[(size_t) i] = 0.5f * (readLeft[(size_t) i] - readRight[(size_t) i]);

            sideAnalyzer.pushMono (sideBuffer.data(), numRead);
            sideAnalyzer.update();
            spectrumView.setSideData (sideAnalyzer.levels().data(), sideAnalyzer.peaks().data());
        }
    }

    const float integrated = processor.integratedLufs();
    const float plr = (integrated > -70.0f && truePeakHoldDb > -99.0f)
                          ? truePeakHoldDb - integrated
                          : -100.0f;

    spectrumView.setData (analyzer.levels().data(), analyzer.peaks().data());
    meterPanel.setValues (processor.momentaryLufs(), processor.shortTermLufs(),
                          integrated, truePeakDb, samplePeakDb,
                          correlation, truePeakHoldDb, processor.lraLu(), plr,
                          processor.clippedSampleCount(), truePeakOvers);


    vectorScopeView.setData (scopeLeft.data() + (scopeSize - 1024),
                             scopeRight.data() + (scopeSize - 1024), 1024);
    oscilloscopeView.setMomentaryLufs (processor.momentaryLufs());
    oscilloscopeView.setData (scopeLeft.data(), scopeRight.data(), scopeSize, false);

    spectrumView.repaint();
    meterPanel.repaint();
    vectorScopeView.repaint();
    oscilloscopeView.repaint();
}

void LudensScopeEditor::appendScope (const float* left, const float* right, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    const int take = std::min (numSamples, scopeSize);
    const int keep = scopeSize - take;

    if (keep > 0)
    {
        std::memmove (scopeLeft.data(),  scopeLeft.data()  + take, sizeof (float) * (size_t) keep);
        std::memmove (scopeRight.data(), scopeRight.data() + take, sizeof (float) * (size_t) keep);
    }

    std::memcpy (scopeLeft.data()  + keep, left  + (numSamples - take), sizeof (float) * (size_t) take);
    std::memcpy (scopeRight.data() + keep, right + (numSamples - take), sizeof (float) * (size_t) take);
}

void LudensScopeEditor::chooseReferenceFile()
{
    referenceChooser = std::make_unique<juce::FileChooser> (
        "Load reference audio", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg");

    referenceChooser->launchAsync (juce::FileBrowserComponent::openMode
                                       | juce::FileBrowserComponent::canSelectFiles,
                                   [safe = juce::Component::SafePointer<LudensScopeEditor> (this)]
                                   (const juce::FileChooser& chooser)
                                   {
                                       if (safe == nullptr)
                                           return;

                                       const auto file = chooser.getResult();

                                       if (file.existsAsFile())
                                       {
                                           safe->pendingReferenceLoopStart = -1.0;
                                           safe->loadReferenceFile (file);
                                       }
                                   });
}

void LudensScopeEditor::loadReferenceFile (const juce::File& file)
{
    const int targetSlot = processor.getActiveReferenceSlot();
    const std::uint64_t generation = ++referenceLoadGeneration;

    auto cancel = std::make_shared<std::atomic<bool>> (false);

    if (referenceLoadCancel != nullptr)
        referenceLoadCancel->store (true, std::memory_order_relaxed);

    referenceLoadCancel = cancel;

    referenceButton.setButtonText ("LOADING...");
    referenceButton.setEnabled (false);

    const double targetRate = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
    juce::Component::SafePointer<LudensScopeEditor> safe (this);

    const bool started = juce::Thread::launch (
        [safe, file, targetRate, targetSlot, generation, cancel]
        {
            auto analysis = analyzeReferenceFile (
                file, [cancel] { return cancel->load (std::memory_order_relaxed); });

            if (analysis.cancelled || cancel->load (std::memory_order_relaxed))
                return;

            if (analysis.ok && analysis.audio != nullptr && targetRate > 0.0)
            {
                bool resampledTruncated = false;

                analysis.audio = resampleReference (*analysis.audio, analysis.sampleRate,
                                                    targetRate,
                                                    [cancel] { return cancel->load (std::memory_order_relaxed); },
                                                    &resampledTruncated);

                if (analysis.audio == nullptr)
                    return;

                analysis.truncated = analysis.truncated || resampledTruncated;
                analysis.sampleRate = targetRate;
            }

            auto shared = std::make_shared<ReferenceAnalysis> (std::move (analysis));

            juce::MessageManager::callAsync ([safe, file, targetSlot, generation, shared]
            {
                if (safe == nullptr)
                    return;

                if (safe->referenceLoadGeneration.load (std::memory_order_relaxed) != generation)
                    return;

                safe->handleReferenceLoaded (file, targetSlot, *shared);
            });
        });

    if (! started)
    {
        referenceLoadCancel.reset();
        referenceButton.setButtonText ("REF FILE");
        referenceButton.setEnabled (true);
    }
}

void LudensScopeEditor::handleReferenceLoaded (const juce::File& file, int targetSlot,
                                               ReferenceAnalysis& analysis)
{
    if (analysis.cancelled)
        return;

    referenceButton.setButtonText ("REF FILE");
    referenceButton.setEnabled (true);

    if (! analysis.ok)
    {
        spectrumView.setReferencePlaceholder ("COULD NOT READ REFERENCE FILE");
        juce::AlertWindow::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon,
            "Reference file",
            "Could not read " + file.getFileName()
                + ".\nSupported formats: WAV, AIFF, FLAC, OGG.");
        return;
    }

    if (processor.getActiveReferenceSlot() != targetSlot)
    {
        pendingReferenceLoopStart = -1.0;
        return;
    }

    if (analysis.audio != nullptr)
    {
        const auto newBytes = (juce::int64) analysis.audio->getNumSamples()
                            * analysis.audio->getNumChannels() * (juce::int64) sizeof (float);
        const auto oldBytes = processor.referenceSlotBytes (targetSlot);

        if (processor.referenceAudioBytes() - oldBytes + newBytes
            > LudensScopeProcessor::maxReferenceTotalBytes)
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "Reference file",
                "Reference memory limit reached (768 MiB across four slots). "
                "Load a shorter file or replace another slot.");
            return;
        }
    }

    referenceDb = analysis.levels;
    hasReference = true;
    spectrumView.setReference (referenceDb.data(), true);

    processor.setReferenceAudio (std::move (analysis.audio), analysis.lufs);
    processor.clearUndo();
    referencePath = file.getFullPathName();
    processor.setReferenceCurveInfo (referencePath, analysis.levels.data(), true);
    spectrumView.setReferenceWaveform (processor.referenceWaveform());

    if (analysis.truncated)
    {
        const double minutes = (double) processor.referenceLengthSamples()
                             / juce::jmax (1.0, processor.getSampleRate()) / 60.0;
        juce::AlertWindow::showMessageBoxAsync (
            juce::MessageBoxIconType::InfoIcon,
            "Reference file",
            "The reference was truncated to the first "
                + juce::String (minutes, 1) + " minutes (memory limit).");
    }

    if (pendingReferenceLoopStart >= 0.0)
    {
        processor.setReferenceLoopRange (pendingReferenceLoopStart, pendingReferenceLoopEnd);
        processor.setReferencePositionSeconds (pendingReferencePosition);
        pendingReferenceLoopStart = -1.0;
    }
}

void LudensScopeEditor::showFftMenu()
{
    juce::PopupMenu menu;

    const int sizes[] = { 2048, 4096, 8192, 16384 };

    menu.addSectionHeader ("FFT size");

    for (int i = 0; i < 4; ++i)
        menu.addItem (100 + i, juce::String (sizes[i]), true, analyzer.getFftSize() == sizes[i]);

    menu.addSeparator();
    menu.addSectionHeader ("Window");

    const char* windows[] = { "Hann", "Blackman-Harris", "Flat-top" };

    for (int i = 0; i < 3; ++i)
        menu.addItem (400 + i, windows[i], true, (int) analyzer.getWindow() == i);

    menu.addSeparator();
    menu.addSectionHeader ("Speed");
    menu.addItem (200, "Fast", true, timingPreset == 0);
    menu.addItem (201, "Medium", true, timingPreset == 1);
    menu.addItem (202, "Slow", true, timingPreset == 2);

    menu.addSeparator();
    menu.addSectionHeader ("Tilt");
    menu.addItem (300, "Off", true, analyzer.getTilt() == 0.0f);
    menu.addItem (301, "+3 dB/oct", true, analyzer.getTilt() == 3.0f);
    menu.addItem (302, "+4.5 dB/oct", true, analyzer.getTilt() == 4.5f);

    menu.addSeparator();
    menu.addSectionHeader ("Smoothing");

    const char* smoothing[] = { "Off", "1/1 octave", "1/3 octave", "1/6 octave" };

    for (int i = 0; i < 4; ++i)
        menu.addItem (800 + i, smoothing[i], true, smoothingPreset == i);

    menu.addSeparator();
    menu.addSectionHeader ("Waterfall");
    menu.addItem (700, "Fast (~3 s)", true, waterfallHistory == 1);
    menu.addItem (701, "Medium (~7 s)", true, waterfallHistory == 2);
    menu.addItem (702, "Slow (~14 s)", true, waterfallHistory == 4);

    menu.addSeparator();
    menu.addSectionHeader ("FFT overlap");
    menu.addItem (900, "Off", true, overlapPreset == 0);
    menu.addItem (901, "2x (50%)", true, overlapPreset == 1);
    menu.addItem (902, "4x (75%)", true, overlapPreset == 2);

    menu.addSeparator();
    menu.addItem (601, "Difference shading", true, showDifference);
    menu.addItem (610, "  vs snapshot", true, differenceSource == 0);
    menu.addItem (611, "  vs REF FILE", true, differenceSource == 1);
    menu.addItem (612, "  vs live", true, differenceSource == 2);
    menu.addItem (600, "Peak markers", true, showPeakMarkers);
    menu.addItem (602, "Side (M/S) curve", true, showSide);
    menu.addItem (603, "Note grid", true, showNoteGrid);

    bool anySnapshot = false;

    for (int slot = 0; slot < LudensScopeProcessor::snapshotSlots; ++slot)
        anySnapshot = anySnapshot || processor.isSnapshotFilled (slot);

    menu.addSeparator();
    menu.addItem (500, "Export snapshots (CSV)...", anySnapshot);
    menu.addItem (501, "Export view (PNG)...", true);

    menu.addSeparator();
    menu.addSectionHeader ("Curve presets");
    menu.addItem (1000, "Save current curve...", true, false);

    const auto& curves = curvePresets.getPresets();

    if (! curves.empty())
    {
        juce::PopupMenu loadMenu;
        juce::PopupMenu deleteMenu;

        for (int i = 0; i < (int) curves.size(); ++i)
        {
            loadMenu.addItem (1100 + i, curves[(size_t) i].name, true,
                              targetCurveActive && targetCurveName == curves[(size_t) i].name);
            deleteMenu.addItem (1200 + i, curves[(size_t) i].name, true, false);
        }

        menu.addSubMenu ("Load curve", loadMenu);
        menu.addSubMenu ("Delete curve", deleteMenu);
    }

    if (targetCurveActive)
        menu.addItem (1001, "Clear target", true, false);

    menu.addSeparator();
    menu.addItem (630, "Check for updates...", true, false);

    juce::Component::SafePointer<LudensScopeEditor> safe (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fftButton),
                        [safe] (int result)
                        {
                            if (safe != nullptr)
                                safe->applyFftMenu (result);
                        });
}

void LudensScopeEditor::applyFftMenu (int result)
{
    if (result >= 100 && result < 104)
    {
        const int sizes[] = { 2048, 4096, 8192, 16384 };
        const int size = sizes[result - 100];

        analyzer.setFftSize (size);
        analyzer.resetPeaks();
        analyzer.resetAverage();
        analyzer.update();

        fftButton.setButtonText ("FFT " + juce::String (size));
        spectrumView.setData (analyzer.levels().data(), analyzer.peaks().data());
        spectrumView.repaint();
        saveFftSettings();
    }
    else if (result >= 200 && result < 203)
    {
        timingPreset = result - 200;

        if (timingPreset == 0)
            analyzer.setTiming (0.85f, 0.30f, 0.80f);
        else if (timingPreset == 1)
            analyzer.setTiming (0.55f, 0.12f, 0.35f);
        else
            analyzer.setTiming (0.25f, 0.03f, 0.12f);

        saveFftSettings();
    }
    else if (result >= 300 && result < 303)
    {
        analyzer.setTilt (result == 300 ? 0.0f : (result == 301 ? 3.0f : 4.5f));
        saveFftSettings();
    }
    else if (result >= 400 && result < 403)
    {
        analyzer.setWindow (static_cast<SpectrumAnalyzer::Window> (result - 400));
        analyzer.resetPeaks();
        analyzer.resetAverage();
        saveFftSettings();
    }
    else if (result == 500)
    {
        exportSnapshots();
    }
    else if (result == 600)
    {
        showPeakMarkers = ! showPeakMarkers;
        spectrumView.setPeakMarkers (showPeakMarkers);
        saveViewSettings();
    }
    else if (result == 601)
    {
        showDifference = ! showDifference;
        spectrumView.setDifferenceShading (showDifference);
        saveViewSettings();
    }
    else if (result >= 610 && result < 613)
    {
        differenceSource = result - 610;
        spectrumView.setDifferenceSource (differenceSource);
        saveViewSettings();
    }
    else if (result == 602)
    {
        showSide = ! showSide;
        spectrumView.setShowSide (showSide);
        saveViewSettings();
    }
    else if (result == 603)
    {
        showNoteGrid = ! showNoteGrid;
        spectrumView.setNoteGrid (showNoteGrid);
        saveFftSettings();
    }
    else if (result >= 900 && result < 903)
    {
        overlapPreset = result - 900;
        analyzer.setOverlap (overlapPreset == 0 ? 1 : (overlapPreset == 1 ? 2 : 4));
        saveFftSettings();
    }
    else if (result == 501)
    {
        exportViewPng();
    }
    else if (result >= 700 && result < 703)
    {
        waterfallHistory = result == 700 ? 1 : (result == 701 ? 2 : 4);
        spectrogramView.setPushDivider (waterfallHistory);
        saveViewSettings();
    }
    else if (result >= 800 && result < 804)
    {
        smoothingPreset = result - 800;
        applySmoothing();
        saveFftSettings();
    }
    else if (result == 630)
    {
        checkForUpdates();
    }
    else if (result == 1000)
    {
        saveCurvePreset();
    }
    else if (result == 1001)
    {
        clearTargetCurve();
        saveViewSettings();
    }
    else if (result >= 1100 && result < 1200)
    {
        loadCurvePreset (result - 1100);
    }
    else if (result >= 1200 && result < 1300)
    {
        deleteCurvePreset (result - 1200);
    }
}

void LudensScopeEditor::applySmoothing()
{
    const float octaves = smoothingPreset == 0 ? 0.0f
                        : smoothingPreset == 1 ? 1.0f
                        : smoothingPreset == 2 ? 1.0f / 3.0f
                                               : 1.0f / 6.0f;

    analyzer.setOctaveSmoothing (octaves);
}

void LudensScopeEditor::exportViewPng()
{
    pngChooser = std::make_unique<juce::FileChooser> (
        "Export view",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile ("ludensscope.png"),
        "*.png");

    pngChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                 | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::warnAboutOverwriting,
                             [safe = juce::Component::SafePointer<LudensScopeEditor> (this)]
                             (const juce::FileChooser& chooser)
                             {
                                 if (safe == nullptr)
                                     return;

                                 const auto file = chooser.getResult();

                                 if (file == juce::File())
                                     return;

                                 file.deleteFile();

                                 auto stream = std::unique_ptr<juce::FileOutputStream> (
                                     file.createOutputStream());

                                 if (stream == nullptr)
                                     return;

                                 const auto image = safe->createComponentSnapshot (safe->getLocalBounds());
                                 juce::PNGImageFormat png;
                                 png.writeImageToStream (image, *stream);
                             });
}

void LudensScopeEditor::applyFftSettings (const LudensScopeProcessor::FftSettings& settings)
{
    timingPreset = juce::jlimit (0, 2, settings.timingPreset);

    analyzer.setFftSize (settings.fftSize);
    analyzer.setWindow (static_cast<SpectrumAnalyzer::Window> (
        juce::jlimit (0, 2, settings.windowType)));
    analyzer.setTilt (juce::jlimit (0.0f, 6.0f, settings.tiltDbPerOctave));

    if (timingPreset == 0)
        analyzer.setTiming (0.85f, 0.30f, 0.80f);
    else if (timingPreset == 1)
        analyzer.setTiming (0.55f, 0.12f, 0.35f);
    else
        analyzer.setTiming (0.25f, 0.03f, 0.12f);

    smoothingPreset = juce::jlimit (0, 3, settings.smoothingPreset);
    applySmoothing();

    overlapPreset = juce::jlimit (0, 2, settings.overlapPreset);
    analyzer.setOverlap (overlapPreset == 0 ? 1 : (overlapPreset == 1 ? 2 : 4));

    showNoteGrid = settings.noteGrid != 0;
    spectrumView.setNoteGrid (showNoteGrid);

    fftButton.setButtonText ("FFT " + juce::String (analyzer.getFftSize()));
}

void LudensScopeEditor::saveFftSettings()
{
    LudensScopeProcessor::FftSettings settings;
    settings.fftSize = analyzer.getFftSize();
    settings.timingPreset = timingPreset;
    settings.tiltDbPerOctave = analyzer.getTilt();
    settings.windowType = (int) analyzer.getWindow();
    settings.smoothingPreset = smoothingPreset;
    settings.overlapPreset = overlapPreset;
    settings.noteGrid = showNoteGrid ? 1 : 0;

    processor.setFftSettings (settings);
}

void LudensScopeEditor::exportSnapshots()
{
    bool anySnapshot = false;

    for (int slot = 0; slot < LudensScopeProcessor::snapshotSlots; ++slot)
        anySnapshot = anySnapshot || processor.isSnapshotFilled (slot);

    if (! anySnapshot)
        return;

    snapshotChooser = std::make_unique<juce::FileChooser> (
        "Export snapshots",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile ("ludensscope-snapshots.csv"),
        "*.csv");

    snapshotChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                      | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [safe = juce::Component::SafePointer<LudensScopeEditor> (this)]
                                  (const juce::FileChooser& chooser)
                                  {
                                      if (safe == nullptr)
                                          return;

                                      const auto file = chooser.getResult();

                                      if (file == juce::File())
                                          return;

                                      file.deleteFile();

                                      auto stream = std::unique_ptr<juce::FileOutputStream> (
                                          file.createOutputStream());

                                      if (stream == nullptr)
                                          return;

                                      juce::String csv = "bin,frequency_hz,A_dB,B_dB,C_dB,D_dB\n";

                                      for (int i = 0; i < SpectrumAnalyzer::numBins; ++i)
                                      {
                                          const double hz = 20.0 * std::pow (
                                              1000.0,
                                              (double) i / (double) (SpectrumAnalyzer::numBins - 1));

                                          csv += juce::String (i) + "," + juce::String (hz, 2);

                                          for (int slot = 0; slot < LudensScopeProcessor::snapshotSlots; ++slot)
                                          {
                                              csv += ",";

                                              if (safe->processor.isSnapshotFilled (slot))
                                                  csv += juce::String (safe->processor.snapshotLevels (slot)[i], 2);
                                          }

                                          csv += "\n";
                                      }

                                      stream->writeText (csv, false, false, "\n");
                                  });
}

void LudensScopeEditor::updateReferenceGain()
{
    float mixLufs = processor.integratedLufs();
    const float referenceLufs = processor.referenceLufs();

    if (mixLufs < -70.0f)
    {
        const float target = meterPanel.getTarget();

        if (target > -90.0f)
            mixLufs = target;
    }

    float gainDb = 0.0f;

    if (mixLufs > -70.0f && referenceLufs > -70.0f)
        gainDb = juce::jlimit (-24.0f, 24.0f, mixLufs - referenceLufs);

    processor.setReferenceGainDb (gainDb);
}

void LudensScopeEditor::toggleReferencePlay()
{
    if (! processor.hasReferenceAudio())
        return;

    const bool shouldPlay = ! processor.isReferencePlaying();

    if (shouldPlay)
    {
        if (levelMatch)
            updateReferenceGain();
        else
            processor.setReferenceGainDb (0.0f);

        processor.setReferenceAudible (true);
    }

    processor.setReferencePlaying (shouldPlay);
}

void LudensScopeEditor::toggleReferenceLoop()
{
    processor.setReferenceLoop (! processor.isReferenceLooping());
}

void LudensScopeEditor::toggleReferenceAb()
{
    if (! processor.hasReferenceAudio())
        return;

    const bool toReference = ! processor.isReferenceAudible();

    if (toReference)
    {
        if (levelMatch)
            updateReferenceGain();
        else
            processor.setReferenceGainDb (0.0f);

        if (! processor.isReferencePlaying())
            processor.setReferencePlaying (true);
    }

    processor.setReferenceAudible (toReference);
}

void LudensScopeEditor::applyViewSettings (const LudensScopeProcessor::ViewSettings& settings)
{
    applyingSettings = true;

    const auto& presets = guidePresets();
    guidePreset = juce::jlimit (0, (int) presets.size() - 1, settings.guidePreset);
    spectrumView.setGuideZones (presets[(size_t) guidePreset].zones);
    setGuidesEnabled (settings.guidesEnabled != 0);

    showSide = settings.sideCurve != 0;
    spectrumView.setShowSide (showSide);

    showPeakMarkers = settings.peakMarkers != 0;
    spectrumView.setPeakMarkers (showPeakMarkers);

    showDifference = settings.difference != 0;
    spectrumView.setDifferenceShading (showDifference);
    differenceSource = juce::jlimit (0, 2, settings.differenceSource);
    spectrumView.setDifferenceSource (differenceSource);

    if (settings.targetCurveName.isNotEmpty())
        applyTargetCurve (settings.targetCurveName);
    else
        clearTargetCurve();

    levelMatch = settings.levelMatch != 0;
    mirror = settings.mirror != 0;
    processor.setReferenceMirror (mirror);
    spectrumView.setReferenceModes (levelMatch, mirror);

    spanIndex = juce::jlimit (0, 5, settings.spanIndex);
    applyRange (juce::jlimit (20.0f, 20000.0f, settings.rangeLow),
                juce::jlimit (20.0f, 20000.0f, settings.rangeHigh));

    waterfallHistory = settings.waterfallHistory;
    spectrogramView.setPushDivider (waterfallHistory);

    const float newWaterfallRatio = juce::jlimit (0.18f, 0.60f, settings.waterfallRatio);

    if (std::abs (newWaterfallRatio - waterfallRatio) > 0.001f)
    {
        waterfallRatio = newWaterfallRatio;
        resized();
    }

    meterPanel.setTarget (settings.targetLufs);

    oscilloscopeView.setWindowSamples (settings.oscWindow);
    oscilloscopeView.setVerticalGain (settings.oscGain);

    applyingSettings = false;
}

void LudensScopeEditor::saveViewSettings()
{
    if (applyingSettings)
        return;

    LudensScopeProcessor::ViewSettings settings;
    settings.guidesEnabled = guidesEnabled ? 1 : 0;
    settings.guidePreset = guidePreset;
    settings.sideCurve = showSide ? 1 : 0;
    settings.peakMarkers = showPeakMarkers ? 1 : 0;
    settings.difference = showDifference ? 1 : 0;
    settings.differenceSource = differenceSource;
    settings.targetCurveName = targetCurveActive ? targetCurveName : juce::String();
    settings.levelMatch = levelMatch ? 1 : 0;
    settings.mirror = mirror ? 1 : 0;
    settings.spanIndex = spanIndex;
    settings.rangeLow = rangeLow;
    settings.rangeHigh = rangeHigh;
    settings.targetLufs = meterPanel.getTarget();
    settings.waterfallHistory = waterfallHistory;
    settings.waterfallRatio = waterfallRatio;
    settings.oscWindow = oscilloscopeView.getWindowSamples();
    settings.oscGain = oscilloscopeView.getVerticalGain();

    processor.setViewSettings (settings);
}

void LudensScopeEditor::setGuidesEnabled (bool enabled)
{
    guidesEnabled = enabled;
    guidesButton.setToggleState (enabled, juce::dontSendNotification);
    spectrumView.setGuides (enabled);

    if (! enabled)
        clearGuideSolo();
}

void LudensScopeEditor::toggleGuideSolo (int index)
{
    const auto& presets = guidePresets();

    if (guidePreset < 0 || guidePreset >= (int) presets.size())
        return;

    const auto& zones = presets[(size_t) guidePreset].zones;

    if (index < 0 || index >= (int) zones.size())
        return;

    if (soloZone == index)
    {
        clearGuideSolo();
        return;
    }

    soloZone = index;
    processor.setBandSolo (true, zones[(size_t) index].low, zones[(size_t) index].high);
    spectrumView.setSoloZone (soloZone);
}

void LudensScopeEditor::clearGuideSolo()
{
    soloZone = -1;
    processor.setBandSolo (false, 0.0f, 0.0f);
    spectrumView.setSoloZone (-1);
}

void LudensScopeEditor::showGuidesMenu()
{
    juce::PopupMenu menu;

    menu.addItem (1, "Off", true, ! guidesEnabled);
    menu.addSeparator();

    const auto& presets = guidePresets();

    for (int i = 0; i < (int) presets.size(); ++i)
        menu.addItem (10 + i, presets[(size_t) i].name, true,
                      guidesEnabled && guidePreset == i);

    juce::Component::SafePointer<LudensScopeEditor> safe (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&guidesButton),
                        [safe] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;

                            if (result == 1)
                            {
                                safe->setGuidesEnabled (false);
                                safe->saveViewSettings();
                                return;
                            }

                            const int index = result - 10;
                            const auto& list = guidePresets();

                            if (index < 0 || index >= (int) list.size())
                                return;

                            safe->guidePreset = index;
                            safe->clearGuideSolo();
                            safe->spectrumView.setGuideZones (list[(size_t) index].zones);
                            safe->setGuidesEnabled (true);
                            safe->saveViewSettings();
                        });
}

void LudensScopeEditor::captureReference()
{
    processor.clearUndo();

    processor.setSnapshot (snapshotSlot, analyzer.averageLevels().data());
    syncSnapshots();
}

void LudensScopeEditor::syncSnapshots()
{
    for (int slot = 0; slot < LudensScopeProcessor::snapshotSlots; ++slot)
    {
        snapshotFilled[(size_t) slot] = processor.isSnapshotFilled (slot);

        if (snapshotFilled[(size_t) slot])
            std::copy (processor.snapshotLevels (slot),
                       processor.snapshotLevels (slot) + LudensScopeProcessor::snapshotBins,
                       snapshots[(size_t) slot].begin());
    }

    snapshotSlot = juce::jlimit (0, LudensScopeProcessor::snapshotSlots - 1,
                                 processor.selectedSnapshot());
    snapshotGeneration = processor.snapshotGeneration();

    spectrumView.setSnapshots (snapshots, snapshotFilled, snapshotSlot);
}

void LudensScopeEditor::checkForUpdates()
{
    if (updateCheckRunning)
        return;

    updateCheckRunning = true;
    juce::Component::SafePointer<LudensScopeEditor> safe (this);

    const bool started = juce::Thread::launch ([safe]
    {
        const auto info = performUpdateCheck();

        juce::MessageManager::callAsync ([safe, info]
        {
            if (safe == nullptr)
                return;

            safe->updateCheckRunning = false;
            safe->handleUpdateResult (info);
        });
    });

    if (! started)
        updateCheckRunning = false;
}

void LudensScopeEditor::handleUpdateResult (const UpdateInfo& info)
{
    if (! info.error.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                "Update check", info.error);
        return;
    }

    if (! info.available)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                                                "LudensScope",
                                                "You are up to date (" + info.currentVersion + ").");
        return;
    }

    auto notes = info.notes;

    if (notes.length() > 900)
        notes = notes.substring (0, 900) + "...";

    juce::AlertWindow::showAsync (
        juce::MessageBoxOptions()
            .withIconType (juce::MessageBoxIconType::InfoIcon)
            .withTitle ("Update available")
            .withMessage ("LudensScope " + info.latestVersion + " is available (you have "
                          + info.currentVersion + ").\n\n" + notes)
            .withButton ("Download")
            .withButton ("Release page")
            .withButton ("Later"),
        [safe = juce::Component::SafePointer<LudensScopeEditor> (this), info] (int result)
        {
            if (safe == nullptr)
                return;

            if (result == 1)
                safe->downloadUpdate (info);
            else if (result == 2)
                juce::URL (info.releasePage).launchInDefaultBrowser();
        });
}

void LudensScopeEditor::downloadUpdate (const UpdateInfo& info)
{
    const auto target = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                            .getChildFile ("Downloads")
                            .getChildFile (info.assetName);
    target.getParentDirectory().createDirectory();

    auto* downloader = new UpdateDownloader (info, target);

    downloader->onFinished = [safe = juce::Component::SafePointer<LudensScopeEditor> (this)]
                              (bool succeeded, juce::String message)
    {
        if (safe != nullptr)
            safe->handleDownloadFinished (succeeded, message);
    };

    downloader->launchThread();
}

void LudensScopeEditor::handleDownloadFinished (bool succeeded, const juce::String& message)
{
    if (! succeeded)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                "Update download", message);
        return;
    }

    juce::AlertWindow::showAsync (
        juce::MessageBoxOptions()
            .withIconType (juce::MessageBoxIconType::InfoIcon)
            .withTitle ("Update downloaded")
            .withMessage ("Saved to:\n" + message
                          + "\n\nQuit the DAW, unzip the package and replace the installed VST3/AU.")
            .withButton ("Show in folder")
            .withButton ("OK"),
        [message] (int result)
        {
            if (result == 1)
                juce::File (message).revealToUser();
        });
}

void LudensScopeEditor::showThemeMenu()
{
    juce::PopupMenu menu;

    for (int i = 0; i < 7; ++i)
        menu.addItem (3000 + i, theme::themeName (i), true, theme::getTheme() == i);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (themeButton),
                        [safe = juce::Component::SafePointer<LudensScopeEditor> (this)] (int result)
                        {
                            if (safe == nullptr)
                                return;

                            if (result >= 3000)
                            {
                                theme::setTheme (result - 3000);
                                theme::saveTheme (theme::getTheme());

                                safe->applyTheme();

                                for (auto& other : openEditors())
                                    if (other != nullptr && other != safe.getComponent())
                                        other->applyTheme();

                                safe->saveViewSettings();
                            }
                        });
}

void LudensScopeEditor::applyTheme()
{
    logoImage = tintLogo (logoSource, theme::text);

    // The waterfall stores painted colours, so old rows cannot follow a new
    // palette; dropping the history keeps every pixel in the current theme.
    spectrogramView.clear();

    repaint();

    for (auto* child : getChildren())
        child->repaint();
}

void LudensScopeEditor::toggleLevelMatch()
{
    levelMatch = ! levelMatch;

    if (! levelMatch)
        processor.setReferenceGainDb (0.0f);
    else
        updateReferenceGain();

    spectrumView.setReferenceModes (levelMatch, mirror);
    saveViewSettings();
}

void LudensScopeEditor::toggleMirror()
{
    mirror = ! mirror;
    processor.setReferenceMirror (mirror);

    if (mirror)
        processor.setReferenceLoop (false);

    spectrumView.setReferenceModes (levelMatch, mirror);
    saveViewSettings();
}

void LudensScopeEditor::selectReferenceSlot (int slot)
{
    if (slot == processor.getActiveReferenceSlot())
        return;

    processor.switchReferenceSlot (slot);
    applyReferenceSettings();
    slotSwitch.setState (processor.getActiveReferenceSlot(),
                         processor.isReferenceSlotFilled (0),
                         processor.isReferenceSlotFilled (1),
                         processor.isReferenceSlotFilled (2),
                         processor.isReferenceSlotFilled (3));
}

void LudensScopeEditor::applyReferenceSettings()
{
    const auto reference = processor.getReferenceSettings();
    lastReferenceGeneration = processor.referenceGeneration();

    if (! reference.valid)
    {
        hasReference = false;
        referencePath.clear();
        spectrumView.setReference (nullptr, false);
        spectrumView.setReferenceWaveform ({});
        spectrumView.setReferencePlaceholder ("REF "
                                              + juce::String::charToString (
                                                    (juce::juce_wchar) ('A' + processor.getActiveReferenceSlot()))
                                              + " EMPTY — USE REF FILE");
        return;
    }

    spectrumView.setReferencePlaceholder ({});
    referenceDb = reference.curve;
    hasReference = true;
    spectrumView.setReference (referenceDb.data(), true);

    if (processor.hasReferenceAudio() && processor.getReferencePath() == reference.path)
    {
        referencePath = reference.path;
        processor.setReferenceLoopRange (reference.loopStart, reference.loopEnd);
        processor.setReferencePositionSeconds (reference.position);
        spectrumView.setReferenceWaveform (processor.referenceWaveform());
        return;
    }

    referencePath = reference.path;
    const juce::File file (reference.path);

    if (file.existsAsFile())
    {
        pendingReferenceLoopStart = reference.loopStart;
        pendingReferenceLoopEnd = reference.loopEnd;
        pendingReferencePosition = reference.position;
        loadReferenceFile (file);
    }
    else
    {
        if (processor.hasReferenceAudio())
            processor.unloadReferenceAudio();

        spectrumView.setReferenceWaveform ({});
    }
}

void LudensScopeEditor::saveCurvePreset()
{
    auto* window = new juce::AlertWindow ("Save FFT curve", "Preset name:",
                                          juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", targetCurveActive ? targetCurveName
                                                     : juce::String ("My curve"));
    window->addButton ("Save", 1);
    window->addButton ("Cancel", 0);

    juce::Component::SafePointer<LudensScopeEditor> safe (this);

    window->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe, window] (int result)
        {
            if (result != 1 || safe == nullptr)
                return;

            const auto name = window->getTextEditorContents ("name").trim();

            if (name.isEmpty())
                return;

            const auto& average = safe->analyzer.averageLevels();

            if (safe->curvePresets.save (name, average.data()))
            {
                safe->applyTargetCurve (name);
                safe->saveViewSettings();
            }
        }), true);
}

void LudensScopeEditor::loadCurvePreset (int index)
{
    const auto& curves = curvePresets.getPresets();

    if (index < 0 || index >= (int) curves.size())
        return;

    applyTargetCurve (curves[(size_t) index].name);
    saveViewSettings();
}

void LudensScopeEditor::deleteCurvePreset (int index)
{
    const auto& curves = curvePresets.getPresets();

    if (index < 0 || index >= (int) curves.size())
        return;

    const auto name = curves[(size_t) index].name;

    if (! curvePresets.remove (name))
        return;

    if (targetCurveActive && targetCurveName == name)
        clearTargetCurve();

    saveViewSettings();
}

void LudensScopeEditor::applyTargetCurve (const juce::String& name)
{
    const auto* preset = curvePresets.find (name);

    if (preset == nullptr)
    {
        clearTargetCurve();
        return;
    }

    targetCurve = preset->curve;
    targetCurveActive = true;
    targetCurveName = preset->name;
    spectrumView.setTargetCurve (targetCurve.data(), true, targetCurveName);
}

void LudensScopeEditor::clearTargetCurve()
{
    targetCurveActive = false;
    targetCurveName.clear();
    spectrumView.setTargetCurve (nullptr, false, {});
}

void LudensScopeEditor::undoReset()
{
    if (! processor.hasUndo())
        return;

    processor.requestUndoRestore();

    if (undoSnapshotValid)
    {
        processor.setSnapshot (undoSnapshotSlot, undoSnapshotLevels.data());
        processor.setSelectedSnapshot (undoSnapshotSlot);
    }

    if (undoReferenceValid && undoReferenceAudio != nullptr)
    {
        processor.restoreReferenceAudio (undoReferenceAudio, undoReferenceLufs,
                                         undoLoopStart, undoLoopEnd, undoPosition);
        processor.setReferenceCurveInfo (undoReferencePath, undoReferenceDb.data(), true);
        referencePath = undoReferencePath;
        hasReference = true;
        referenceDb = undoReferenceDb;
        spectrumView.setReference (referenceDb.data(), true);
        spectrumView.setReferenceWaveform (processor.referenceWaveform());
    }

    analyzer.restoreAverage (undoAverage.data());
    analyzer.restorePeaks (undoPeaks.data());

    truePeakOvers = undoTruePeakOvers;
    truePeakHoldDb = undoTruePeakHold;

    syncSnapshots();
}

void LudensScopeEditor::resetDisplay()
{
    // Capture what the reset is about to clear.
    undoSnapshotValid = processor.isSnapshotFilled (snapshotSlot);

    if (undoSnapshotValid)
    {
        undoSnapshotSlot = snapshotSlot;

        const float* levels = processor.snapshotLevels (snapshotSlot);

        if (levels != nullptr)
            std::copy (levels, levels + SpectrumAnalyzer::numBins, undoSnapshotLevels.begin());
    }

    undoAverage = analyzer.averageLevels();
    undoPeaks = analyzer.peaks();

    undoReferenceValid = processor.hasReferenceAudio();

    if (undoReferenceValid)
    {
        undoReferenceAudio = processor.copyReferenceAudio();
        undoReferenceLufs = processor.referenceLufs();
        undoLoopStart = processor.referenceLoopStartSeconds();
        undoLoopEnd = processor.referenceLoopEndSeconds();

        const double rate = preparedSampleRate > 0.0 ? preparedSampleRate : 48000.0;
        undoPosition = (double) processor.referencePositionSamples() / rate;
        undoReferenceDb = referenceDb;
        undoReferencePath = referencePath;
    }

    undoTruePeakOvers = truePeakOvers;
    undoTruePeakHold = truePeakHoldDb;

    // The audio thread snapshots the long-term loudness before applying reset.
    processor.requestUndoCapture();
    processor.requestReset();

    analyzer.resetPeaks();
    analyzer.resetAverage();
    truePeakLeft.reset();
    truePeakRight.reset();

    hasReference = false;
    referencePath.clear();
    spectrumView.setReference (nullptr, false);
    spectrogramView.clear();

    if (processor.isSnapshotFilled (snapshotSlot))
        processor.clearSnapshot (snapshotSlot);

    syncSnapshots();

    processor.clearReferenceAudio();
    spectrumView.setReferenceWaveform ({});
    spectrumView.setTransport (false, false, true, false, 0.0, 0.0, 0.0, 0.0);

    clearGuideSolo();

    truePeakDb = -100.0f;
    truePeakHoldDb = -100.0f;
    samplePeakDb = -100.0f;
    correlation = 0.0f;
    truePeakOvers = 0;

    scopeLeft.fill (0.0f);
    scopeRight.fill (0.0f);

    analyzer.update();

    spectrumView.setData (analyzer.levels().data(), analyzer.peaks().data());
    meterPanel.setValues (processor.momentaryLufs(), processor.shortTermLufs(),
                          processor.integratedLufs(), truePeakDb, samplePeakDb,
                          correlation, truePeakHoldDb, -1.0f, -100.0f, 0, 0);
    vectorScopeView.setData (scopeLeft.data() + (scopeSize - 1024),
                             scopeRight.data() + (scopeSize - 1024), 1024);
    oscilloscopeView.setData (scopeLeft.data(), scopeRight.data(), scopeSize, frozen);

    spectrumView.repaint();
    meterPanel.repaint();
    vectorScopeView.repaint();
    oscilloscopeView.repaint();
}

void LudensScopeEditor::cycleSpan()
{
    static constexpr int spans[] = { 10, 8, 6, 4, 2, 1 };

    spanIndex = (spanIndex + 1) % 6;

    if (spanIndex == 0)
    {
        applyRange (20.0f, 20000.0f);
        return;
    }

    const float octaves = (float) spans[spanIndex];
    const float centre = std::sqrt (rangeLow * rangeHigh);
    const float halfSpan = std::pow (2.0f, octaves * 0.5f);

    applyRange (centre / halfSpan, centre * halfSpan);
}

void LudensScopeEditor::applyRange (float lowHz, float highHz)
{
    lowHz = juce::jlimit (20.0f, 19801.0f, lowHz);
    highHz = juce::jlimit (lowHz * 1.01f, 20000.0f, highHz);

    if (std::abs (lowHz - rangeLow) < 0.5f && std::abs (highHz - rangeHigh) < 0.5f)
        return;

    rangeLow = lowHz;
    rangeHigh = highHz;

    spectrumView.setRange (lowHz, highHz);
    spectrogramView.setRange (lowHz, highHz);

    const int octaves = juce::jlimit (1, 10,
                                      (int) std::lround (std::log2 (highHz / lowHz)));
    spanButton.setButtonText ("SPAN " + juce::String (octaves));

    saveViewSettings();
}

bool LudensScopeEditor::keyPressed (const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown()
        || key.getModifiers().isAltDown())
    {
        return false;
    }

    if (key.getKeyCode() == juce::KeyPress::escapeKey)
    {
        clearGuideSolo();
        return true;
    }

    switch (key.getKeyCode())
    {
        case 'a':
        case 'A':
            toggleReferenceAb();
            return true;

        case 'f':
        case 'F':
            freezeButton.triggerClick();
            return true;

        default:
            break;
    }

    return false;
}

bool LudensScopeEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& path : files)
    {
        const auto extension = juce::File (path).getFileExtension().toLowerCase();

        if (extension == ".wav" || extension == ".aif" || extension == ".aiff"
            || extension == ".flac" || extension == ".ogg")
        {
            return true;
        }
    }

    return false;
}

void LudensScopeEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    fileDragActive = true;
    repaint();
}

void LudensScopeEditor::fileDragExit (const juce::StringArray&)
{
    fileDragActive = false;
    repaint();
}

void LudensScopeEditor::filesDropped (const juce::StringArray& files, int, int)
{
    fileDragActive = false;
    repaint();

    if (files.isEmpty())
        return;

    const juce::File file (files[0]);

    if (file.existsAsFile())
    {
        pendingReferenceLoopStart = -1.0;
        loadReferenceFile (file);
    }
}

void LudensScopeEditor::mouseMove (const juce::MouseEvent& e)
{    const bool overDivider = waterfallDivider.expanded (0, 4).contains (e.getPosition());

    setMouseCursor (overDivider ? juce::MouseCursor::UpDownResizeCursor
                                : juce::MouseCursor::NormalCursor);
}

void LudensScopeEditor::mouseDown (const juce::MouseEvent& e)
{
    draggingDivider = waterfallDivider.expanded (0, 4).contains (e.getPosition());
}

void LudensScopeEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (! draggingDivider)
        return;

    const int maximum = juce::jmax (0, leftColumnBounds.getHeight() - 140);
    const int height = juce::jlimit (0, maximum, leftColumnBounds.getBottom() - e.y);

    waterfallRatio = (float) height / (float) juce::jmax (1, leftColumnBounds.getHeight());
    resized();
    repaint();
}

void LudensScopeEditor::mouseUp (const juce::MouseEvent&)
{
    if (draggingDivider)
        saveViewSettings();

    draggingDivider = false;
}

void LudensScopeEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! waterfallDivider.expanded (0, 4).contains (e.getPosition()))
        return;

    waterfallRatio = 0.32f;
    resized();
    saveViewSettings();
}
