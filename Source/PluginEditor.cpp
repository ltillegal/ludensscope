#include "PluginEditor.h"

#include "report/ReportPdf.h"

#include <thread>
#include "BinaryData.h"

#include <cmath>

#include "dsp/ReferenceAnalysis.h"
#include "ui/GuidePresets.h"

#include <algorithm>

namespace
{
juce::File persistedUiStateFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("LudensScope")
             .getChildFile ("ui-state.bin");
}

// The standalone host writes its own JUCE filterState, but only on a clean
// close handshake that is unreliable in the kiosk session. Keep an independent
// copy next to the licence and refresh it on every UI setting change.
void persistUiState (juce::AudioProcessor& proc)
{
    juce::MemoryBlock data;
    proc.getStateInformation (data);

    const auto file = persistedUiStateFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithData (data.getData(), data.getSize());
}

void restorePersistedUiState (juce::AudioProcessor& proc)
{
    const auto file = persistedUiStateFile();
    if (! file.existsAsFile())
        return;

    juce::MemoryBlock data;
    if (file.loadFileAsData (data) && data.getSize() > 0)
        proc.setStateInformation (data.getData(), (int) data.getSize());
}
}

class ScopeLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void setUiScale (float scale) noexcept { uiScale = juce::jlimit (0.75f, 1.5f, scale); }

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

        if (button.hasKeyboardFocus (true))
        {
            g.setColour (theme::accent.withAlpha (0.9f));
            g.drawRoundedRectangle (bounds.reduced (2.0f), corner, 1.5f);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool, bool) override
    {
        g.setFont (theme::labelFont (juce::jlimit (9.0f, 14.0f * uiScale, button.getHeight() * 0.40f)));

        g.setColour (button.getToggleState() ? theme::panelTop : theme::text);
        g.drawFittedText (button.getButtonText(), button.getLocalBounds(),
                          juce::Justification::centred, 1, 0.9f);
    }

private:
    float uiScale = 1.0f;
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
    const float cx = wide ? bounds.getX() + bounds.getWidth() * 0.20f
                          : bounds.getCentreX();
    const float cy = bounds.getCentreY();
    const float s = wide ? juce::jmin (bounds.getHeight() * 0.20f, bounds.getWidth() * 0.16f)
                         : bounds.getHeight() * 0.25f;

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
        g.setFont (theme::labelFont (juce::jlimit (8.0f, 11.0f, bounds.getHeight() * 0.30f)));
        g.drawText ("DONATE", bounds.withTrimmedLeft (bounds.getWidth() * 0.36f),
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
    addAndMakeVisible (vuMeterView);
    addAndMakeVisible (oscilloscopeView);

    freezeButton.setClickingTogglesState (true);
    freezeButton.setColour (juce::TextButton::buttonColourId, theme::panel);
    freezeButton.setColour (juce::TextButton::buttonOnColourId, theme::accent);
    freezeButton.setColour (juce::TextButton::textColourOffId, theme::text);
    freezeButton.setColour (juce::TextButton::textColourOnId, theme::bg);
    freezeButton.onClick = [this]
    {
        frozen = freezeButton.getToggleState();
        oscilloscopeView.setData (scopeLeft.data(), scopeRight.data(), scopeSize, false);
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

    addAndMakeVisible (referenceStrip);
    referenceStrip.onPlay = [this] { if (! isFull()) { showUnlockDialog(); return; } toggleReferencePlay(); };
    referenceStrip.onLoop = [this] { if (! isFull()) { showUnlockDialog(); return; } toggleReferenceLoop(); };
    referenceStrip.onAb = [this] { if (! isFull()) { showUnlockDialog(); return; } toggleReferenceAb(); };
    referenceStrip.onMatch = [this] { if (! isFull()) { showUnlockDialog(); return; } toggleLevelMatch(); };
    referenceStrip.onMirror = [this] { if (! isFull()) { showUnlockDialog(); return; } toggleMirror(); };
    referenceStrip.onRefFile = [this] { if (! isFull()) { showUnlockDialog(); return; } chooseReferenceFile(); };
    referenceStrip.onSeek = [this] (double seconds)
    {
        processor.setReferencePositionSeconds (seconds);
    };
    referenceStrip.onLoopRange = [this] (double start, double end)
    {
        processor.setReferenceLoopRange (start, end);
    };

    auto styleSourceButton = [this] (juce::TextButton& button)
    {
        button.setColour (juce::TextButton::buttonColourId, theme::panel);
        button.setColour (juce::TextButton::buttonOnColourId, theme::accent);
        button.setColour (juce::TextButton::textColourOffId, theme::textDim);
        button.setColour (juce::TextButton::textColourOnId, theme::bg);
        button.setClickingTogglesState (true);
        button.setRadioGroupId (7001);
        addAndMakeVisible (button);
    };

    styleSourceButton (origButton);
    styleSourceButton (refButton);
    origButton.setTooltip ("Analyse and play the DAW input");
    refButton.setTooltip ("Analyse and play the loaded reference file");
    origButton.setToggleState (true, juce::dontSendNotification);
    origButton.onClick = [this] { selectSource (false); };
    refButton.onClick = [this] { selectSource (true); };

    themeButton.setTooltip ("Interface colour theme (PRO: 7 themes)");
    themeButton.onClick = [this] { showThemeMenu(); };
    addAndMakeVisible (themeButton);

    unlockButton.setColour (juce::TextButton::buttonColourId, theme::accent);
    unlockButton.setColour (juce::TextButton::textColourOffId, theme::bg);
    unlockButton.onClick = [this] { showUnlockDialog(); };
    addAndMakeVisible (unlockButton);

    donateButton.setTooltip ("Donate via PayPal");
    donateButton.onClick = []
    {
        juce::URL ("https://www.paypal.com/donate/?business=0eskimo0%40gmail.com&item_name=LudensScope&currency_code=USD").launchInDefaultBrowser();
    };
    addAndMakeVisible (donateButton);

    captureButton.setColour (juce::TextButton::buttonColourId, theme::panel);
    captureButton.setColour (juce::TextButton::textColourOffId, theme::text);
    captureButton.onClick = [this] { if (! isFull()) { showUnlockDialog(); return; } captureReference(); };
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
    meterPanel.onResetIntegrated = [this] { processor.requestIntegratedReset(); };
    oscilloscopeView.onSettingsChanged = [this] { saveViewSettings(); };

    spectrogramView.setPushDivider (waterfallHistory);

    fftButton.setColour (juce::TextButton::buttonColourId, theme::panel.darker (0.20f));
    fftButton.setColour (juce::TextButton::textColourOffId, theme::text);
    fftButton.onClick = [this] { showFftMenu(); };
    addAndMakeVisible (fftButton);

    fftButton.setTooltip ("FFT size/window, ballistics, tilt, smoothing, overlap, "
                          "note grid, CSV, updates  -  v" + juce::String (LUDENSCOPE_VERSION_STRING));

   #if ! defined (LUDENSCOPE_TEST_HOOKS)
    juce::Timer::callAfterDelay (4000, [safe = juce::Component::SafePointer<LudensScopeEditor> (this)]
    {
        if (safe != nullptr)
            safe->runAutomaticUpdateCheck();
    });
   #endif
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

    spectrumView.onToggleExpand = [this] { toggleFftExpanded(); };

    spectrumView.setSnapshots (snapshots, snapshotFilled, snapshotSlot);

    spectrumView.onSnapshotSelect = [this] (int slot)
    {
        if (! isFull())
        {
            showUnlockDialog();
            return;
        }

        snapshotSlot = juce::jlimit (0, LudensScopeProcessor::snapshotSlots - 1, slot);
        processor.setSelectedSnapshot (snapshotSlot);
        spectrumView.setSnapshots (snapshots, snapshotFilled, snapshotSlot);
    };

    // Our own state file is authoritative in the kiosk session.
    restorePersistedUiState (processor);

    syncSnapshots();

    readLeft.resize (readChunk);
    readRight.resize (readChunk);

    preparedSampleRate = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
    analyzer.prepare (preparedSampleRate);
    sideAnalyzer.prepare (preparedSampleRate);
    stereoAnalyzer.prepare (preparedSampleRate);
    oscilloscopeView.setSampleRate (preparedSampleRate);

    applyFftSettings (processor.getFftSettings());
    applyViewSettings (processor.getViewSettings());
    applyReferenceSettings();
    lastStateRevision = processor.stateRevision();

    // Restore the saved UI scale together with a window that can hold it.
    if (uiScalePercent > 0)
        applyUiScalePercent (uiScalePercent);
    else
        setSize (1060, 700);

    applyEdition();

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
    // The whole window uses the same corner radius as the panels, so no
    // surface inside it ends up with square corners next to rounded ones.
    g.setColour (theme::bg);
    g.fillAll();

    const auto windowBounds = getLocalBounds().toFloat().reduced (0.5f);

    g.setGradientFill (juce::ColourGradient::vertical (theme::bg, 0.0f,
                                                       theme::bgBottom, (float) getHeight()));
    g.fillRoundedRectangle (windowBounds, theme::panelRadius);

    g.setColour (theme::panelLine.withAlpha (0.6f));
    g.drawRoundedRectangle (windowBounds.reduced (0.5f), theme::panelRadius, 1.0f);

    // Soft accent glow behind the spectrum area, kept inside the rounded edge.
    {
        juce::Graphics::ScopedSaveState saved (g);
        juce::Path roundedWindow;
        roundedWindow.addRoundedRectangle (windowBounds, theme::panelRadius);
        g.reduceClipRegion (roundedWindow);

        juce::ColourGradient glow (theme::accent.withAlpha (0.05f),
                                   getWidth() * 0.30f, -80.0f,
                                   juce::Colours::transparentBlack,
                                   getWidth() * 0.62f, getHeight() * 0.55f,
                                   true);
        g.setGradientFill (glow);
        g.fillAll();
    }

    if (! railBounds.isEmpty())
    {
        g.setColour (theme::panel.withAlpha (0.35f));
        g.fillRoundedRectangle (railBounds.toFloat(), theme::panelRadius);
        g.setColour (theme::panelLine.withAlpha (0.6f));
        g.drawRoundedRectangle (railBounds.toFloat().reduced (0.5f), theme::panelRadius, 1.0f);

    }

    auto header = getLocalBounds().reduced (14)
                      .removeFromTop (juce::roundToInt (36.0f * uiScale));

    auto brand = header.removeFromLeft (juce::roundToInt (196.0f * uiScale));

    const int logoSize = juce::roundToInt (32.0f * uiScale);
    auto logoArea = brand.removeFromLeft (juce::roundToInt (36.0f * uiScale))
                        .withSizeKeepingCentre (logoSize, logoSize);

    if (logoImage.isValid())
        g.drawImage (logoImage, logoArea.toFloat(), juce::RectanglePlacement::centred, false);

    brand.removeFromLeft (juce::roundToInt (8.0f * uiScale));

    g.setFont (theme::boldFont (19.0f * uiScale));
    g.setColour (theme::text);
    g.drawText ("LudensScope", brand, juce::Justification::centredLeft);

    g.setFont (theme::detailFont (9.5f, uiScale));
    g.setColour (theme::textDim);
    g.drawText ("STEREO ANALYZER", header, juce::Justification::centredLeft);

    if (isFull())
    {
        const auto email = LudensScopeLicence::licensedEmail();
        const auto licence = LudensScopeLicence::isLegacy()
            ? juce::String ("LICENSED TO LEGACY (0.4.x)")
            : (email.isNotEmpty() ? "LICENSED TO " + email.toUpperCase()
                                  : juce::String ("LICENSED TO PRO"));

        g.setFont (theme::detailFont (8.5f, uiScale));
        g.setColour (theme::accent.withAlpha (0.78f));
        g.drawText (licence,
                    header.withTrimmedRight (juce::roundToInt (190.0f * uiScale)),
                    juce::Justification::centredRight);
    }

    g.setColour (theme::panelLine.withAlpha (0.6f));
    g.fillRect (14, 14 + juce::roundToInt (36.0f * uiScale) + 3, getWidth() - 28, 1);

    if (fileDragActive)
    {
        g.setColour (theme::bg.withAlpha (0.55f));
        g.fillRoundedRectangle (windowBounds, theme::panelRadius);
        g.setColour (theme::accent);
        g.drawRoundedRectangle (windowBounds.reduced (2.5f), theme::panelRadius - 2.0f, 2.0f);
        g.setFont (theme::detailFont (15.0f, uiScale));
        g.drawText ("DROP REFERENCE AUDIO", getLocalBounds(), juce::Justification::centred);
    }
}

void LudensScopeEditor::resized()
{
    auto r = getLocalBounds().reduced (14);

    const float scaleW = (float) getWidth() / 1060.0f;
    const float scaleH = (float) getHeight() / 700.0f;
    const float autoScale = juce::jlimit (0.9f, 1.35f, juce::jmin (scaleW, scaleH));
    const float requestedScale = uiScalePercent > 0 ? (float) uiScalePercent / 100.0f : autoScale;
    uiScale = juce::jlimit (0.75f, 1.5f, requestedScale);

    // A forced scale that the window cannot hold would squeeze individual
    // panels, so fall back to the largest scale the window actually fits.
    if (! forceUiScale)
        uiScale = juce::jmax (0.75f,
                              juce::jmin (uiScale,
                                          (float) getWidth() / 1060.0f,
                                          (float) getHeight() / 660.0f));

    // Every view owns its scale; nothing global leaks between editor instances.
    lookAndFeel->setUiScale (uiScale);
    spectrumView.setUiScale (uiScale);
    spectrogramView.setUiScale (uiScale);
    vectorScopeView.setUiScale (uiScale);
    oscilloscopeView.setUiScale (uiScale);
    referenceStrip.setUiScale (uiScale);
    vuMeterView.setUiScale (uiScale);
    spectrumView.setExpanded (fftExpanded);

    const int gap = juce::roundToInt (10.0f * uiScale);
    const int railWidth = juce::roundToInt (88.0f * uiScale);
    const int stripHeight = juce::jlimit (80, 160, juce::roundToInt (100.0f * uiScale));

    // The right rail stays in both layouts. Keeping its sizing in one place
    // makes the fullscreen FFT indistinguishable from the normal button row.
    auto layOutRail = [this, gap, railWidth] (juce::Rectangle<int> area,
                                              int reclaimedHeight) -> juce::Rectangle<int>
    {
        railBounds = area.removeFromRight (railWidth);
        area.removeFromRight (gap);

        auto rail = railBounds.withTrimmedLeft (2).withTrimmedRight (2)
                              .withTrimmedTop (juce::roundToInt (8.0f * uiScale));
        // Buttons keep their PRO size; only the analysis panels grow in Lite.
        const int available = juce::jmax (0, rail.getHeight() - reclaimedHeight);

        juce::Component* const buttons[] = {
            &fftButton, &guidesButton, &spanButton, &monoButton,
            &captureButton, &freezeButton, &undoButton, &resetButton, &donateButton
        };

        int buttonCount = 0;
        for (auto* button : buttons)
            if (button->isVisible())
                ++buttonCount;

        buttonCount = juce::jmax (1, buttonCount);

        const int buttonGap = juce::roundToInt (2.0f * uiScale);
        const int buttonHeight = juce::jmax (
            juce::roundToInt (22.0f * uiScale),
            (available - buttonGap * (buttonCount - 1)) / buttonCount);

        const int stackHeight = buttonHeight * buttonCount + buttonGap * (buttonCount - 1);
        auto stack = rail.withHeight (juce::jmin (available, stackHeight));

        for (auto* button : buttons)
        {
            if (! button->isVisible())
                continue;

            button->setBounds (stack.removeFromTop (buttonHeight));
            stack.removeFromTop (buttonGap);
        }

        return area;
    };

    if (fftExpanded)
    {
        auto expandedArea = layOutRail (getLocalBounds().reduced (14), 0);

        spectrumView.setBounds (expandedArea);
        spectrogramView.setBounds ({});
        meterPanel.setBounds ({});
        vectorScopeView.setBounds ({});
        vuMeterView.setBounds ({});
        oscilloscopeView.setBounds ({});
        // These sit above the spectrum in z-order. Clearing their bounds keeps
        // the FFT alone in the window and leaves the corner hotspot clickable
        // in fullscreen too; the normal layout below restores them.
        referenceStrip.setBounds ({});
        origButton.setBounds ({});
        refButton.setBounds ({});
        themeButton.setBounds ({});
        unlockButton.setBounds ({});
        waterfallDivider = {};
        leftColumnBounds = expandedArea;
        return;
    }

    auto header = r.removeFromTop (juce::roundToInt (36.0f * uiScale));
    const int themeWidth = juce::roundToInt (90.0f * uiScale);
    const int unlockWidth = juce::roundToInt (86.0f * uiScale);
    const int themeHeight = juce::jlimit (24, 42, juce::roundToInt (26.0f * uiScale));
    themeButton.setBounds (header.removeFromRight (themeWidth)
                               .withSizeKeepingCentre (themeWidth, themeHeight));
    header.removeFromRight (juce::roundToInt (6.0f * uiScale));
    unlockButton.setBounds (header.removeFromRight (unlockWidth)
                                .withSizeKeepingCentre (unlockWidth, themeHeight));
    r.removeFromTop (8);

    // Lite has no reference band: its height is handed to the analysis row
    // above (FFT/waterfall and loudness), while the scope row below keeps its
    // size and drops into the freed space.
    int reclaimedHeight = 0;

    if (referenceStrip.isVisible())
    {
        auto stripBand = r.removeFromBottom (stripHeight);
        auto sourceBand = stripBand.removeFromRight (railWidth);
        referenceStrip.setBounds (stripBand);

        // ORIG/REF span the whole height of the reference panel.
        auto sourceRow = sourceBand.reduced (juce::roundToInt (2.0f * uiScale), 0);
        const int sourceGap = juce::roundToInt (3.0f * uiScale);
        origButton.setBounds (sourceRow.removeFromLeft ((sourceRow.getWidth() - sourceGap) / 2));
        sourceRow.removeFromLeft (sourceGap);
        refButton.setBounds (sourceRow);

        r.removeFromBottom (gap);
    }
    else
    {
        reclaimedHeight = stripHeight + gap;
    }

    r = layOutRail (r, reclaimedHeight);

    // The scope row is sized from the height the window had with the strip,
    // so reclaimed space never inflates VU, stereo image or oscilloscope.
    const int baseHeight = juce::jmax (0, r.getHeight() - reclaimedHeight);
    const int bottomHeight = juce::jlimit (115, 160, (int) ((float) baseHeight * 0.22f));
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
    meterPanel.setUiScale (juce::jmin (uiScale,
        (float) rightColumn.getHeight() / 373.0f));

    auto vectorBounds = bottom.removeFromLeft (bottomHeight);
    bottom.removeFromLeft (gap);

    vectorScopeView.setBounds (vectorBounds);

    // The VU meter keeps its narrower width, but shares the full row height
    // with the stereo image and the oscilloscope. Only its width is shrunk.
    constexpr float vuShrink = 0.85f;
    const int fullVuWidth = juce::jlimit (240, 480,
                                          juce::jmin (juce::roundToInt (400.0f * uiScale),
                                                      juce::roundToInt (bottom.getWidth() * 0.50f)));
    const int vuWidth = juce::jmax (200, juce::roundToInt ((float) fullVuWidth * vuShrink));
    const int vuHeight = bottom.getHeight();
    auto vuBounds = bottom.removeFromLeft (vuWidth)
                          .withSizeKeepingCentre (vuWidth, vuHeight);
    bottom.removeFromLeft (gap);

    vuMeterView.setBounds (vuBounds);
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
        stereoAnalyzer.prepare (sampleRate);
        oscilloscopeView.setSampleRate (sampleRate);

        // Cached reference audio is at the old rate; reload it.
        processor.dropReferenceAudioCaches();

        if (processor.getReferenceSettings().valid)
            applyReferenceSettings();
    }

    const int numRead = processor.readSamples (readLeft.data(), readRight.data(), readChunk);
    const bool audioActive = numRead > 0;

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

        // FREEZE holds the spectrum and waterfall only; scopes and meters keep
        // following the audio.
        if (! frozen)
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

    const double referenceRate = preparedSampleRate > 0.0 ? preparedSampleRate : 48000.0;

    undoButton.setEnabled (processor.hasUndo());

    ReferenceStrip::State stripState;
    stripState.waveform = processor.referenceWaveform();
    stripState.duration = referenceRate > 0.0
                              ? (double) processor.referenceLengthSamples() / referenceRate : 0.0;
    stripState.position = referenceRate > 0.0
                              ? (double) processor.referencePositionSamples() / referenceRate : 0.0;
    stripState.loopStart = processor.referenceLoopStartSeconds();
    stripState.loopEnd = processor.referenceLoopEndSeconds();
    stripState.playing = processor.isReferencePlaying();
    stripState.looping = processor.isReferenceLooping();
    stripState.audible = processor.isReferenceAudible();
    stripState.match = levelMatch;
    stripState.mirror = mirror;
    stripState.activeSlot = processor.getActiveReferenceSlot();

    for (int i = 0; i < 4; ++i)
        stripState.filled[(size_t) i] = processor.isReferenceSlotFilled (i);

    stripState.name = referencePath.isNotEmpty()
                          ? juce::File (referencePath).getFileName() : juce::String();
    stripState.lufs = processor.referenceLufs();
    referenceStrip.setState (std::move (stripState));

    if (processor.isReferenceMonitoring() != refButton.getToggleState())
    {
        origButton.setToggleState (! processor.isReferenceMonitoring(), juce::dontSendNotification);
        refButton.setToggleState (processor.isReferenceMonitoring(), juce::dontSendNotification);
    }

    if (levelMatch && processor.hasReferenceAudio())
        updateReferenceGain();

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

    const float integrated = processor.integratedLufs();
    const float plr = (integrated > -70.0f && truePeakHoldDb > -99.0f)
                          ? truePeakHoldDb - integrated
                          : -100.0f;

    const bool meterChanged = meterPanel.setValues (processor.momentaryLufs(),
                          processor.shortTermLufs(), integrated, truePeakDb, samplePeakDb,
                          correlation, truePeakHoldDb, processor.lraLu(), plr,
                          processor.clippedSampleCount(), truePeakOvers);

    float vuL = 0.0f, vuR = 0.0f;

    if (numRead > 0)
    {
        double sumL = 0.0, sumR = 0.0;

        for (int i = 0; i < numRead; ++i)
        {
            const double l = readLeft[(size_t) i];
            const double r = readRight[(size_t) i];
            sumL += l * l;
            sumR += r * r;
        }

        vuL = (float) std::sqrt (sumL / (double) numRead);
        vuR = (float) std::sqrt (sumR / (double) numRead);
        lastVuRmsL = vuL;
        lastVuRmsR = vuR;
        vuGapTicks = 0;
    }
    else if (vuGapTicks < vuHoldTicks)
    {
        // A host may call the editor timer more often than it delivers audio
        // blocks. Treating those ticks as silence drives the input smoother to
        // -100 VU, which pins the needle to its rest stop while the digits jump.
        // Hold the last RMS for a short while, then fall back to silence.
        ++vuGapTicks;
        vuL = lastVuRmsL;
        vuR = lastVuRmsR;
    }

    vuMeterView.setUiScale (uiScale);
    vuMeterView.setLevels (vuL, vuR, (float) vuZeroDbfs, 1.0 / 30.0);

    // The scopes only change when new samples arrive (plus one final frame
    // when audio stops). Repainting them forever with a stale buffer is what
    // made the window shimmer after a while in silence.
    if (audioActive || wasAudioActive)
    {
        vectorScopeView.setData (scopeLeft.data() + (scopeSize - 1024),
                                 scopeRight.data() + (scopeSize - 1024), 1024);
        oscilloscopeView.setMomentaryLufs (processor.momentaryLufs());
        oscilloscopeView.setData (scopeLeft.data(), scopeRight.data(), scopeSize, false);

        vectorScopeView.repaint();
        oscilloscopeView.repaint();
    }

    if (meterChanged)
        meterPanel.repaint();

    wasAudioActive = audioActive;

    if (frozen)
        return;

    if (numRead > 0)
    {
        analyzer.update();
        spectrogramView.push (analyzer.levels().data(), SpectrumAnalyzer::numBins);

        if (showMatch && hasReference && ++matchTick >= 15)
        {
            matchTick = 0;
            updateMatchSuggestions();
        }
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

    // Per-band stereo image. Correlation needs both channels and a single
    // window, so it is a separate analyser from the mono spectrum.
    if (showStereoField)
    {
        stereoAnalyzer.setFftSize (analyzer.getFftSize());
        stereoAnalyzer.setWindow ((int) analyzer.getWindow());

        if (timingPreset == 0)
            stereoAnalyzer.setTiming (0.85f, 0.30f);
        else if (timingPreset == 1)
            stereoAnalyzer.setTiming (0.55f, 0.12f);
        else
            stereoAnalyzer.setTiming (0.25f, 0.03f);

        if (numRead > 0)
        {
            stereoAnalyzer.push (readLeft.data(), readRight.data(), numRead);
            stereoAnalyzer.update();
            spectrumView.setStereoField (stereoAnalyzer.correlation().data(),
                                         stereoAnalyzer.sideMid().data());
        }
    }

    if (audioActive)
    {
        spectrumView.setData (analyzer.levels().data(), analyzer.peaks().data());
        spectrumView.repaint();
    }
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
    if (! isFull())
    {
        showUnlockDialog();
        return;
    }

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
    if (! isFull())
    {
        showUnlockDialog();
        return;
    }

    const int targetSlot = processor.getActiveReferenceSlot();
    const std::uint64_t generation = ++referenceLoadGeneration;

    auto cancel = std::make_shared<std::atomic<bool>> (false);

    if (referenceLoadCancel != nullptr)
        referenceLoadCancel->store (true, std::memory_order_relaxed);

    referenceLoadCancel = cancel;


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
    }
}

void LudensScopeEditor::handleReferenceLoaded (const juce::File& file, int targetSlot,
                                               ReferenceAnalysis& analysis)
{
    if (! isFull())
        return;

    if (analysis.cancelled)
        return;


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
    referenceLow = analysis.levelsLow;
    referenceHigh = analysis.levelsHigh;
    hasReferenceBand = true;
    referenceBandPath = file.getFullPathName();
    hasReference = true;
    spectrumView.setReference (referenceDb.data(), true,
                               referenceLow.data(), referenceHigh.data());

    processor.setReferenceAudio (std::move (analysis.audio), analysis.lufs);
    processor.clearUndo();
    referencePath = file.getFullPathName();
    processor.setReferenceCurveInfo (referencePath, analysis.levels.data(), true);

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

    const bool full = isFull();

    const auto waterfallLabel = [this] (const juce::String& name, int divider)
    {
        const double seconds = spectrogramView.historySecondsFor (divider);
        return seconds > 0.0 ? name + " (~" + juce::String (seconds, 1) + " s)" : name;
    };

    // Item IDs stay exactly as before: applyFftMenu() is independent of the
    // grouping, so moving an entry between submenus cannot change its action.
    juce::PopupMenu fftMenu;
    const int sizes[] = { 2048, 4096, 8192, 16384 };

    fftMenu.addSectionHeader ("FFT size");

    for (int i = 0; i < 4; ++i)
        fftMenu.addItem (100 + i, juce::String (sizes[i]), true, analyzer.getFftSize() == sizes[i]);

    fftMenu.addSeparator();
    fftMenu.addSectionHeader ("Window");

    const char* windows[] = { "Hann", "Blackman-Harris", "Flat-top" };

    for (int i = 0; i < 3; ++i)
        fftMenu.addItem (400 + i, windows[i], true, (int) analyzer.getWindow() == i);

    fftMenu.addSeparator();
    fftMenu.addSectionHeader ("Speed");
    fftMenu.addItem (200, "Fast", true, timingPreset == 0);
    fftMenu.addItem (201, "Medium", true, timingPreset == 1);
    fftMenu.addItem (202, "Slow", true, timingPreset == 2);

    fftMenu.addSeparator();
    fftMenu.addSectionHeader (full ? "Tilt" : "Tilt (PRO)");
    fftMenu.addItem (300, "Off", full, analyzer.getTilt() == 0.0f);
    fftMenu.addItem (301, "+3 dB/oct", full, analyzer.getTilt() == 3.0f);
    fftMenu.addItem (302, "+4.5 dB/oct", full, analyzer.getTilt() == 4.5f);

    fftMenu.addSeparator();
    fftMenu.addSectionHeader (full ? "Smoothing" : "Smoothing (PRO)");

    const char* smoothing[] = { "Off", "1/1 octave", "1/3 octave", "1/6 octave" };

    for (int i = 0; i < 4; ++i)
        fftMenu.addItem (800 + i, smoothing[i], full, smoothingPreset == i);

    fftMenu.addSeparator();
    fftMenu.addSectionHeader (full ? "FFT overlap" : "FFT overlap (PRO)");
    fftMenu.addItem (900, "Off", full, overlapPreset == 0);
    fftMenu.addItem (901, "2x (50%)", full, overlapPreset == 1);
    fftMenu.addItem (902, "4x (75%)", full, overlapPreset == 2);

    menu.addSubMenu ("FFT", fftMenu);

    juce::PopupMenu displayMenu;
    displayMenu.addSectionHeader ("Waterfall");
    displayMenu.addItem (700, waterfallLabel ("Fast", 1), true, waterfallHistory == 1);
    displayMenu.addItem (701, waterfallLabel ("Medium", 2), true, waterfallHistory == 2);
    displayMenu.addItem (702, waterfallLabel ("Slow", 4), true, waterfallHistory == 4);

    displayMenu.addSeparator();
    displayMenu.addSectionHeader ("VU input smoothing");
    displayMenu.addItem (740, "Fast (40 ms)", true, vuInputSmoothingMs == 40);
    displayMenu.addItem (741, "Smooth (80 ms)", true, vuInputSmoothingMs == 80);
    displayMenu.addItem (742, "Extra smooth (150 ms)", true, vuInputSmoothingMs == 150);

    displayMenu.addSeparator();
    displayMenu.addItem (601, full ? "Difference shading" : "Difference shading [PRO]", full, showDifference);
    displayMenu.addItem (610, "  vs snapshot", full, differenceSource == 0);
    displayMenu.addItem (611, "  vs REF FILE", full, differenceSource == 1);
    displayMenu.addItem (612, "  vs live", full, differenceSource == 2);
    displayMenu.addItem (600, full ? "Peak markers" : "Peak markers [PRO]", full, showPeakMarkers);
    displayMenu.addItem (602, full ? "Side (M/S) curve" : "Side (M/S) curve [PRO]", full, showSide);
    displayMenu.addItem (603, full ? "Note grid" : "Note grid [PRO]", full, showNoteGrid);
    displayMenu.addItem (604, full ? "Stereo field by band" : "Stereo field by band [PRO]", full, showStereoField);

    displayMenu.addSeparator();
    displayMenu.addSectionHeader (full ? "Gain staging" : "Gain staging (PRO)");
    displayMenu.addItem (720, "Reference lines", full, gainStaging);
    displayMenu.addItem (721, "0 VU = -18 dBFS (EBU)", full, vuZeroDbfs == -18);
    displayMenu.addItem (722, "0 VU = -20 dBFS (SMPTE)", full, vuZeroDbfs == -20);

    menu.addSubMenu ("Display", displayMenu);

    juce::PopupMenu referenceMenu;
    referenceMenu.addItem (730, full ? "Suggested EQ (REF)" : "Suggested EQ (REF) [PRO]",
                           full && hasReference, showMatch);
    referenceMenu.addSeparator();
    referenceMenu.addSectionHeader (full ? "Curve presets" : "Curve presets (PRO)");
    referenceMenu.addItem (1000, "Save current curve...", full, false);

    const auto& curves = curvePresets.getPresets();

    if (! curves.empty())
    {
        juce::PopupMenu loadMenu;
        juce::PopupMenu deleteMenu;

        for (int i = 0; i < (int) curves.size(); ++i)
        {
            loadMenu.addItem (1100 + i, curves[(size_t) i].name, full,
                              targetCurveActive && targetCurveName == curves[(size_t) i].name);
            deleteMenu.addItem (1200 + i, curves[(size_t) i].name, full, false);
        }

        referenceMenu.addSubMenu ("Load curve", loadMenu);
        referenceMenu.addSubMenu ("Delete curve", deleteMenu);
    }

    if (targetCurveActive)
        referenceMenu.addItem (1001, "Clear target", full, false);

    menu.addSubMenu ("Reference", referenceMenu);

    bool anySnapshot = false;

    for (int slot = 0; slot < LudensScopeProcessor::snapshotSlots; ++slot)
        anySnapshot = anySnapshot || processor.isSnapshotFilled (slot);

    juce::PopupMenu exportMenu;
    exportMenu.addItem (500, full ? "Export snapshots (CSV)..." : "Export snapshots (CSV) [PRO]",
                        full && anySnapshot);
    exportMenu.addItem (501, "Export view (PNG)...", true);
    exportMenu.addItem (502, full ? "Track report (PDF)..." : "Track report (PDF) [PRO]",
                        full);
    menu.addSubMenu ("Export", exportMenu);

    menu.addSeparator();
    menu.addItem (630, pendingUpdateVersion.isNotEmpty()
                            ? "Update to " + pendingUpdateVersion + "..."
                            : juce::String ("Check for updates..."),
                  true, false);

    if (! full)
        menu.addItem (640, "Unlock PRO - EUR 19 (was EUR 29)...", true, false);

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
    if (result == 640)
    {
        showUnlockDialog();
        return;
    }

    if (! isFull())
    {
        const bool fullOnly = (result >= 300 && result <= 302)
            || result == 500
            || (result >= 600 && result <= 603)
            || (result >= 610 && result <= 612)
            || (result >= 720 && result <= 722)
            || result == 730
            || (result >= 800 && result <= 803)
            || (result >= 900 && result <= 902)
            || result == 1000 || result == 1001
            || (result >= 1100 && result <= 1299);

        if (fullOnly)
        {
            showUnlockDialog();
            return;
        }
    }

    if (result == 720)
    {
        gainStaging = ! gainStaging;
        applyGainStaging();
        saveViewSettings();
        return;
    }

    if (result == 721 || result == 722)
    {
        vuZeroDbfs = result == 722 ? -20 : -18;
        applyGainStaging();
        saveViewSettings();
        return;
    }

    if (result == 730)
    {
        showMatch = ! showMatch;
        spectrumView.setShowMatch (showMatch);

        if (showMatch)
            updateMatchSuggestions();

        saveViewSettings();
        return;
    }

    if (result >= 740 && result < 743)
    {
        vuInputSmoothingMs = result == 740 ? 40 : (result == 741 ? 80 : 150);
        vuMeterView.setInputSmoothingMs (vuInputSmoothingMs);
        saveViewSettings();
        return;
    }

    if (result >= 100 && result < 104)
    {
        const int sizes[] = { 2048, 4096, 8192, 16384 };
        const int size = sizes[result - 100];

        analyzer.setFftSize (size);
        analyzer.resetPeaks();
        analyzer.resetAverage();
        analyzer.update();
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
    else if (result == 604)
    {
        showStereoField = ! showStereoField;
        spectrumView.setShowStereoField (showStereoField);
        saveViewSettings();
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
    else if (result == 502)
    {
        auto chooser = std::make_shared<juce::FileChooser> (
            "Audio file to analyse", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
            "*.wav;*.aif;*.aiff;*.flac;*.mp3");

        chooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [this, chooser] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();

                                  if (file.existsAsFile())
                                      exportTrackReport (file);
                              });
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
    analyzer.setTilt (isFull() ? juce::jlimit (0.0f, 6.0f, settings.tiltDbPerOctave) : 0.0f);

    if (timingPreset == 0)
        analyzer.setTiming (0.85f, 0.30f, 0.80f);
    else if (timingPreset == 1)
        analyzer.setTiming (0.55f, 0.12f, 0.35f);
    else
        analyzer.setTiming (0.25f, 0.03f, 0.12f);

    smoothingPreset = isFull() ? juce::jlimit (0, 3, settings.smoothingPreset) : 0;
    applySmoothing();

    overlapPreset = isFull() ? juce::jlimit (0, 2, settings.overlapPreset) : 0;
    analyzer.setOverlap (overlapPreset == 0 ? 1 : (overlapPreset == 1 ? 2 : 4));

    showNoteGrid = isFull() && settings.noteGrid != 0;
    spectrumView.setNoteGrid (showNoteGrid);
}

void LudensScopeEditor::saveFftSettings()
{
    const bool full = isFull();
    const auto preserved = processor.getFftSettings();

    LudensScopeProcessor::FftSettings settings;
    settings.fftSize = analyzer.getFftSize();
    settings.timingPreset = timingPreset;
    settings.tiltDbPerOctave = full ? analyzer.getTilt() : preserved.tiltDbPerOctave;
    settings.windowType = (int) analyzer.getWindow();
    settings.smoothingPreset = full ? smoothingPreset : preserved.smoothingPreset;
    settings.overlapPreset = full ? overlapPreset : preserved.overlapPreset;
    settings.noteGrid = full ? (showNoteGrid ? 1 : 0) : preserved.noteGrid;

    processor.setFftSettings (settings);
    persistUiState (processor);
}

void LudensScopeEditor::exportSnapshots()
{
    if (! isFull())
    {
        showUnlockDialog();
        return;
    }

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

void LudensScopeEditor::selectSource (bool reference)
{
    if (reference && ! isFull())
    {
        showUnlockDialog();
        return;
    }

    processor.setReferenceMonitor (reference);

    origButton.setToggleState (! reference, juce::dontSendNotification);
    refButton.setToggleState (reference, juce::dontSendNotification);

    if (reference && processor.hasReferenceAudio())
        processor.setReferencePlaying (true);
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

void LudensScopeEditor::applyUiScalePercent (int percent)
{
    uiScalePercent = juce::jlimit (0, 150, percent);
    forceUiScale = false;

    if (uiScalePercent == 0)
    {
        setResizeLimits (1060, 660, 2400, 1500);
        resized();
        return;
    }

    // The design is 1060x660 at 100%. A larger scale needs a larger window,
    // otherwise individual panels would have to shrink instead of the layout.
    const float scale = (float) uiScalePercent / 100.0f;
    const int minWidth = juce::roundToInt (1060.0f * scale);
    const int minHeight = juce::roundToInt (660.0f * scale);

    setResizeLimits (minWidth, minHeight,
                     juce::jmax (2400, minWidth), juce::jmax (1500, minHeight));
    setSize (juce::jmax (getWidth(), minWidth), juce::jmax (getHeight(), minHeight));
    resized();
}

void LudensScopeEditor::applyViewSettings (const LudensScopeProcessor::ViewSettings& settings)
{
    applyingSettings = true;

    const bool full = isFull();

    const auto& presets = guidePresets();
    guidePreset = juce::jlimit (0, (int) presets.size() - 1, settings.guidePreset);
    spectrumView.setGuideZones (presets[(size_t) guidePreset].zones);
    setGuidesEnabled (settings.guidesEnabled != 0);

    showSide = full && settings.sideCurve != 0;
    spectrumView.setShowSide (showSide);

    showStereoField = full && settings.stereoField != 0;
    spectrumView.setShowStereoField (showStereoField);

    gainStaging = full && settings.gainStaging != 0;
    vuZeroDbfs = settings.vuZeroDbfs == -20 ? -20 : -18;
    vuInputSmoothingMs = (settings.vuInputSmoothing == 40 || settings.vuInputSmoothing == 150)
                             ? settings.vuInputSmoothing : 80;
    vuMeterView.setInputSmoothingMs (vuInputSmoothingMs);
    applyGainStaging();

    showMatch = full && settings.showMatch != 0;
    spectrumView.setShowMatch (showMatch);

    uiScalePercent = settings.uiScalePercent;
    forceUiScale = false;
    resized();


    showPeakMarkers = full && settings.peakMarkers != 0;
    spectrumView.setPeakMarkers (showPeakMarkers);

    showDifference = full && settings.difference != 0;
    spectrumView.setDifferenceShading (showDifference);
    differenceSource = juce::jlimit (0, 2, settings.differenceSource);
    spectrumView.setDifferenceSource (differenceSource);

    if (full && settings.targetCurveName.isNotEmpty())
        applyTargetCurve (settings.targetCurveName);
    else
        clearTargetCurve();

    levelMatch = full && settings.levelMatch != 0;
    mirror = full && settings.mirror != 0;
    processor.setReferenceMirror (mirror);

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

    const bool full = isFull();
    const auto preserved = processor.getViewSettings();

    LudensScopeProcessor::ViewSettings settings;
    settings.guidesEnabled = guidesEnabled ? 1 : 0;
    settings.guidePreset = guidePreset;
    settings.sideCurve = full ? (showSide ? 1 : 0) : preserved.sideCurve;
    settings.stereoField = full ? (showStereoField ? 1 : 0) : preserved.stereoField;
    settings.peakMarkers = full ? (showPeakMarkers ? 1 : 0) : preserved.peakMarkers;
    settings.difference = full ? (showDifference ? 1 : 0) : preserved.difference;
    settings.differenceSource = full ? differenceSource : preserved.differenceSource;
    settings.targetCurveName = full ? (targetCurveActive ? targetCurveName : juce::String())
                                    : preserved.targetCurveName;
    settings.levelMatch = full ? (levelMatch ? 1 : 0) : preserved.levelMatch;
    settings.mirror = full ? (mirror ? 1 : 0) : preserved.mirror;
    settings.spanIndex = spanIndex;
    settings.rangeLow = rangeLow;
    settings.rangeHigh = rangeHigh;
    settings.targetLufs = meterPanel.getTarget();
    settings.waterfallHistory = waterfallHistory;
    settings.waterfallRatio = waterfallRatio;
    settings.gainStaging = full ? (gainStaging ? 1 : 0) : preserved.gainStaging;
    settings.vuZeroDbfs = full ? vuZeroDbfs : preserved.vuZeroDbfs;
    settings.vuInputSmoothing = vuInputSmoothingMs;
    settings.showMatch = full ? (showMatch ? 1 : 0) : preserved.showMatch;
    settings.uiScalePercent = uiScalePercent;
    settings.oscWindow = oscilloscopeView.getWindowSamples();
    settings.oscGain = oscilloscopeView.getVerticalGain();

    processor.setViewSettings (settings);
    persistUiState (processor);
}

void LudensScopeEditor::setShowStereoField (bool shouldShow)
{
    showStereoField = shouldShow && isFull();
    spectrumView.setShowStereoField (showStereoField);
    repaint();
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
    if (! isFull())
    {
        showUnlockDialog();
        return;
    }

    processor.clearUndo();

    processor.setSnapshot (snapshotSlot, analyzer.averageLevels().data());
    syncSnapshots();
}

void LudensScopeEditor::syncSnapshots()
{
    for (int slot = 0; slot < LudensScopeProcessor::snapshotSlots; ++slot)
    {
        snapshotFilled[(size_t) slot] = isFull() && processor.isSnapshotFilled (slot);

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

void LudensScopeEditor::runAutomaticUpdateCheck()
{
    auto state = updateState::load();

    // A release found during an earlier session is shown immediately.
    if (isNewerVersion (state.latestVersion, LUDENSCOPE_VERSION_STRING))
    {
        pendingUpdateVersion = state.latestVersion;
        refreshUpdateCue();
    }

    if (! updateState::isStale (state) || updateCheckRunning)
        return;

    updateCheckRunning = true;

    juce::Component::SafePointer<LudensScopeEditor> safe (this);

    const bool started = juce::Thread::launch ([safe]
    {
        const auto info = performUpdateCheck();

        juce::MessageManager::callAsync ([safe, info]
        {
            auto state = updateState::load();
            state.lastCheckSeconds = juce::Time::currentTimeMillis() / 1000;

            if (info.error.isEmpty())
                state.latestVersion = info.available ? info.latestVersion : juce::String();

            updateState::save (state);

            if (safe == nullptr)
                return;

            safe->updateCheckRunning = false;

            // Automatic checks stay quiet: no dialogs, ever.
            if (info.error.isEmpty() && info.available)
            {
                safe->pendingUpdateVersion = info.latestVersion;
                safe->refreshUpdateCue();
            }
        });
    });

    if (! started)
        updateCheckRunning = false;
}

void LudensScopeEditor::refreshUpdateCue()
{
    const juce::String base ("FFT size/window, ballistics, tilt, smoothing, overlap, "
                             "note grid, CSV, updates  -  v"
                             + juce::String (LUDENSCOPE_VERSION_STRING));

    fftButton.setTooltip (pendingUpdateVersion.isEmpty()
                              ? base
                              : base + " - LudensScope " + pendingUpdateVersion + " is available");
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
                          + "\n\nQuit the DAW, run the downloaded installer and follow its steps.")
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
    if (! isFull())
    {
        showUnlockDialog();
        return;
    }

    juce::PopupMenu menu;

    for (int i = 0; i < 7; ++i)
        menu.addItem (3000 + i, theme::themeName (i), true, theme::getTheme() == i);

    menu.addSeparator();
    menu.addSectionHeader ("UI scale");
    menu.addItem (3100, "Auto", true, uiScalePercent == 0);
    menu.addItem (3101, "100%", true, uiScalePercent == 100);
    menu.addItem (3102, "125%", true, uiScalePercent == 125);
    menu.addItem (3103, "150%", true, uiScalePercent == 150);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (themeButton),
                        [safe = juce::Component::SafePointer<LudensScopeEditor> (this)] (int result)
                        {
                            if (safe == nullptr)
                                return;

                            if (result >= 3100)
                            {
                                static const int percents[] = { 0, 100, 125, 150 };
                                safe->applyUiScalePercent (percents[juce::jlimit (0, 3, result - 3100)]);
                                safe->saveViewSettings();
                                safe->repaint();
                                return;
                            }

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

void LudensScopeEditor::showUnlockDialog()
{
    if (isFull())
    {
        const juce::String status = LudensScopeLicence::statusText();

        juce::AlertWindow::showMessageBoxAsync (
            juce::MessageBoxIconType::InfoIcon,
            "LudensScope " + status,
            LudensScopeLicence::isLegacy()
                ? "Grandfathered PRO licence from a 0.4.x installation.\n\n"
                  "Thank you for supporting LudensScope."
                : "Licensed to: " + LudensScopeLicence::licensedEmail() + "\n\n"
                  "Offline licence, no account and no telemetry.\n"
                  "Thank you for supporting LudensScope.");
        return;
    }

    juce::String message =
        "PRO adds the ORIG/REF reference analysis and transport, snapshot slots, "
        "gain staging lines, all 7 themes, advanced FFT options, target curves "
        "and CSV export.\n\n"
        "Holiday offer: EUR 19 (was EUR 29) via PayPal. After payment email "
        "alek.ludens@gmail.com and you will receive a licence key. The licence "
        "is offline, includes 1.x updates and, for keys issued from 1.0.4, is "
        "activated on up to 2 machines.";

    if (LudensScopeLicence::isMachineLimitReached())
        message += "\n\n" + LudensScopeLicence::activationMessage();

    auto* aw = new juce::AlertWindow (
        "Unlock LudensScope PRO", message, juce::MessageBoxIconType::NoIcon);

    aw->addTextEditor ("key", LudensScopeLicence::load(), "Licence key");
    aw->addButton ("Unlock", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Buy PRO - EUR 19 (was EUR 29)", 2);
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    aw->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, aw] (int result)
        {
            if (result == 1)
            {
                juce::String error;

                if (LudensScopeLicence::saveAndVerify (aw->getTextEditorContents ("key"), error))
                {
                    applyEdition();

                    juce::AlertWindow::showMessageBoxAsync (
                        juce::MessageBoxIconType::InfoIcon,
                        "LudensScope PRO",
                        "Thank you! PRO features are now unlocked.");
                }
                else
                {
                    juce::AlertWindow::showMessageBoxAsync (
                        juce::MessageBoxIconType::WarningIcon,
                        "Licence not accepted",
                        error);
                }
            }
            else if (result == 2)
            {
                juce::URL ("https://www.paypal.com/ncp/payment/9LQYYZMR7TW7G")
                    .launchInDefaultBrowser();
            }
        }), true);
}

void LudensScopeEditor::applyEdition()
{
    const bool full = isFull();
    const bool legacy = LudensScopeLicence::isLegacy();

    donateButton.setVisible (! full);

    // Lite has no reference workflow, so it does not reserve a bottom strip
    // for the player or the ORIG/REF source switch. PRO keeps both unchanged.
    referenceStrip.setVisible (full);
    origButton.setVisible (full);
    refButton.setVisible (full);

    unlockButton.setButtonText (full ? "PRO" : "UNLOCK");
    unlockButton.setEnabled (! full);
    unlockButton.setTooltip (full
        ? (legacy ? "Grandfathered PRO licence (0.4.x)" : "LudensScope PRO licence")
        : "Unlock LudensScope PRO - EUR 19 (was EUR 29) via PayPal");

    if (! full)
    {
        theme::setTheme (0);
        applyTheme();

        showSide = false;
        showStereoField = false;
        spectrumView.setShowStereoField (false);
        showPeakMarkers = false;
        showDifference = false;
        showNoteGrid = false;
        showMatch = false;
        gainStaging = false;
        levelMatch = false;
        mirror = false;

        spectrumView.setShowSide (false);
        spectrumView.setPeakMarkers (false);
        spectrumView.setDifferenceShading (false);
        spectrumView.setNoteGrid (false);
        spectrumView.setShowMatch (false);
        spectrumView.setDifferenceSource (0);

        processor.setReferenceMirror (false);
        processor.setReferenceGainDb (0.0f);

        clearTargetCurve();
        applyGainStaging();
    }
    else
    {
        applyFftSettings (processor.getFftSettings());
        applyViewSettings (processor.getViewSettings());
    }

    referenceStrip.setLocked (! full);
    captureButton.setEnabled (full);
    captureButton.setTooltip (full
        ? "Store the averaged spectrum in the selected snapshot slot"
        : "Snapshot slots are a PRO feature");

    syncSnapshots();
    applyReferenceSettings();

    resized();
    repaint();
}

void LudensScopeEditor::updateMatchSuggestions()
{
    matchLines.clear();

    if (! hasReference)
    {
        spectrumView.setMatchSuggestions (matchLines, showMatch);
        return;
    }

    const auto& mix = analyzer.averageLevels();
    const double centres[] = { 63.0, 125.0, 250.0, 500.0, 1000.0,
                               2000.0, 4000.0, 8000.0, 16000.0 };

    struct Suggestion { float diff; double centre; };
    std::vector<Suggestion> found;

    for (const double centre : centres)
    {
        const double low = centre / 1.41421356237;
        const double high = centre * 1.41421356237;
        double mixSum = 0.0, refSum = 0.0;
        int count = 0;

        for (int i = 0; i < SpectrumAnalyzer::numBins; ++i)
        {
            const double hz = 20.0 * std::pow (1000.0,
                                               (double) i / (double) (SpectrumAnalyzer::numBins - 1));

            if (hz >= low && hz <= high)
            {
                mixSum += (double) mix[(size_t) i];
                refSum += (double) referenceDb[(size_t) i];
                ++count;
            }
        }

        if (count > 0)
        {
            const float diff = (float) ((mixSum - refSum) / (double) count);

            if (std::abs (diff) >= 0.8f)
                found.push_back ({ diff, centre });
        }
    }

    std::sort (found.begin(), found.end(),
               [] (const Suggestion& a, const Suggestion& b)
               {
                   return std::abs (a.diff) > std::abs (b.diff);
               });

    for (int i = 0; i < (int) found.size() && i < 4; ++i)
    {
        const auto& suggestion = found[(size_t) i];
        const auto hz = suggestion.centre >= 1000.0
                            ? juce::String (suggestion.centre / 1000.0, 1) + " kHz"
                            : juce::String ((int) suggestion.centre) + " Hz";
        matchLines.add ((suggestion.diff > 0.0f ? "CUT " : "BOOST ")
                        + juce::String (std::abs (suggestion.diff), 1) + " dB @ " + hz);
    }

    spectrumView.setMatchSuggestions (matchLines, showMatch);
}

void LudensScopeEditor::applyGainStaging()
{
    if (! isFull())
        gainStaging = false;

    spectrumView.setGainStaging (gainStaging, (float) vuZeroDbfs);
    oscilloscopeView.setGainStaging (gainStaging, (float) vuZeroDbfs);
    vuMeterView.setZeroDbfs ((float) vuZeroDbfs);
}

void LudensScopeEditor::toggleLevelMatch()
{
    levelMatch = ! levelMatch;

    if (! levelMatch)
        processor.setReferenceGainDb (0.0f);
    else
        updateReferenceGain();

    saveViewSettings();
}

void LudensScopeEditor::toggleMirror()
{
    mirror = ! mirror;
    processor.setReferenceMirror (mirror);

    if (mirror)
        processor.setReferenceLoop (false);

    saveViewSettings();
}

void LudensScopeEditor::selectReferenceSlot (int slot)
{
    if (! isFull())
    {
        showUnlockDialog();
        return;
    }

    if (slot == processor.getActiveReferenceSlot())
        return;

    processor.switchReferenceSlot (slot);
    applyReferenceSettings();
}

void LudensScopeEditor::applyReferenceSettings()
{
    if (! isFull())
    {
        lastReferenceGeneration = processor.referenceGeneration();
        hasReference = false;
        hasReferenceBand = false;
        referencePath.clear();
        showMatch = false;
        levelMatch = false;
        mirror = false;
        matchLines.clear();

        spectrumView.setShowMatch (false);
        spectrumView.setMatchSuggestions (matchLines, false);
        spectrumView.setReference (nullptr, false);
        spectrumView.setReferencePlaceholder ("REFERENCE - PRO FEATURE");

        processor.setReferenceMirror (false);
        processor.setReferenceGainDb (0.0f);
        return;
    }

    const auto reference = processor.getReferenceSettings();
    lastReferenceGeneration = processor.referenceGeneration();

    if (! reference.valid)
    {
        hasReference = false;
        referencePath.clear();
        spectrumView.setReference (nullptr, false);
        spectrumView.setReferencePlaceholder ("REF EMPTY - USE REF FILE");
        return;
    }

    spectrumView.setReferencePlaceholder ({});
    referenceDb = reference.curve;
    hasReference = true;

    const bool band = hasReferenceBand && reference.path == referenceBandPath;

    if (! band)
        hasReferenceBand = false;

    spectrumView.setReference (referenceDb.data(), true,
                               band ? referenceLow.data() : nullptr,
                               band ? referenceHigh.data() : nullptr);

    if (processor.hasReferenceAudio() && processor.getReferencePath() == reference.path)
    {
        referencePath = reference.path;
        processor.setReferenceLoopRange (reference.loopStart, reference.loopEnd);
        processor.setReferencePositionSeconds (reference.position);
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
    if (! isFull())
    {
        clearTargetCurve();
        return;
    }

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
    }

    analyzer.restoreAverage (undoAverage.data());
    analyzer.restorePeaks (undoPeaks.data());

    truePeakOvers = undoTruePeakOvers;
    truePeakHoldDb = undoTruePeakHold;

    syncSnapshots();
}

void LudensScopeEditor::resetDisplay()
{
    meterPanel.clearHistory();
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

void LudensScopeEditor::toggleFftExpanded()
{
    fftExpanded = ! fftExpanded;
    spectrumView.setExpanded (fftExpanded);
    resized();
    repaint();
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
        if (fftExpanded)
            toggleFftExpanded();
        else
            clearGuideSolo();

        return true;
    }

    switch (key.getKeyCode())
    {
        case 'a':
        case 'A':
            if (! isFull())
            {
                showUnlockDialog();
                return true;
            }

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

    if (! isFull())
    {
        showUnlockDialog();
        return;
    }

    if (files.isEmpty())
        return;

    const juce::File file (files[0]);

    if (! file.existsAsFile())
        return;

    juce::PopupMenu dropMenu;
    dropMenu.addSectionHeader (file.getFileName());
    dropMenu.addItem (1, "Load as reference");
    dropMenu.addItem (2, "Track report (PDF)...");

    dropMenu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [safe = juce::Component::SafePointer<LudensScopeEditor> (this), file] (int choice)
                            {
                                if (safe == nullptr || choice < 1)
                                    return;

                                safe->handleDroppedFile (file, choice == 2);
                            });
}

void LudensScopeEditor::handleDroppedFile (const juce::File& file, bool asReport)
{
    if (! file.existsAsFile())
        return;

    if (asReport)
    {
        exportTrackReport (file);
        return;
    }

    pendingReferenceLoopStart = -1.0;
    loadReferenceFile (file);
}

void LudensScopeEditor::exportTrackReport (const juce::File& file)
{
    const float target = meterPanel.getTarget();
    const bool hasTarget = targetCurveActive;
    const auto curve = targetCurve;
    const auto name = file.getFileNameWithoutExtension();

    juce::Component::SafePointer<LudensScopeEditor> safe (this);

    juce::MouseCursor::showWaitCursor();

    // Decoding and analysing a whole track takes seconds; keep the message
    // thread free.
    std::thread ([safe, file, target, hasTarget, curve, name]
    {
        auto report = analyzeTrackFile (file, target);

        juce::MessageManager::callAsync ([safe, report, hasTarget, curve, name]
        {
            juce::MouseCursor::hideWaitCursor();

            if (safe == nullptr)
                return;

            if (! report.ok)
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        "Track report",
                                                        "Could not read " + report.fileName);
                return;
            }

            auto suggested = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                                 .getChildFile (name + "-report.pdf");

            auto chooser = std::make_shared<juce::FileChooser> ("Save track report",
                                                                suggested, "*.pdf");

            chooser->launchAsync (juce::FileBrowserComponent::saveMode
                                      | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [chooser, report, hasTarget, curve] (const juce::FileChooser& fc)
                                  {
                                      auto result = fc.getResult();

                                      if (result == juce::File())
                                          return;

                                      if (result.getFileExtension().isEmpty())
                                          result = result.withFileExtension ("pdf");

                                      if (writeTrackReportPdf (report, hasTarget ? &curve : nullptr,
                                                               result, LUDENSCOPE_VERSION_STRING))
                                      {
                                          result.revealToUser();
                                      }
                                      else
                                      {
                                          juce::AlertWindow::showMessageBoxAsync (
                                              juce::MessageBoxIconType::WarningIcon,
                                              "Track report",
                                              "Could not write " + result.getFullPathName());
                                      }
                                  });
        });
    }).detach();
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
