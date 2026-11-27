#include "MatchEditor.h"
#include "MatchProcessor.h"

#include "ui/Theme.h"

#include <utility>

namespace
{
    constexpr int editorWidth = 980;
    constexpr int editorHeight = 620;
}

LudensMatchEditor::LudensMatchEditor (LudensMatchProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setSize (editorWidth, editorHeight);

    addAndMakeVisible (spectrum);
    spectrum.setRange (20.0f, 20000.0f);

    auto setup = [this] (juce::Slider& slider, juce::Label& caption, const juce::String& text)
    {
        addAndMakeVisible (slider);
        addAndMakeVisible (caption);

        styleSlider (slider);
        caption.setText (text, juce::dontSendNotification);
        caption.setFont (theme::labelFont (10.5f));
        caption.setColour (juce::Label::textColourId, theme::textDim);
        caption.setJustificationType (juce::Justification::centredLeft);
    };

    setup (amount, amountCaption, "AMOUNT");
    setup (mix, mixCaption, "MIX");
    setup (output, outputCaption, "OUTPUT");

    auto& state = processor.parameters();
    amountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, "amount", amount);
    mixAttachment    = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, "mix", mix);
    outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, "output", output);

    readLeft.resize (4096);
    readRight.resize (4096);

    startTimerHz (30);
}

void LudensMatchEditor::styleSlider (juce::Slider& slider) const
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 54, 18);
    slider.setColour (juce::Slider::textBoxTextColourId, theme::text);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::trackColourId, theme::accent.withAlpha (0.75f));
    slider.setColour (juce::Slider::backgroundColourId, theme::grid);
    slider.setColour (juce::Slider::thumbColourId, theme::accent);
}

void LudensMatchEditor::timerCallback()
{
    const int numRead = processor.readSamples (readLeft.data(), readRight.data(),
                                               (int) readLeft.size());

    if (numRead <= 0)
        return;

    analyzer.push (readLeft.data(), readRight.data(), numRead);
    analyzer.update();
    spectrum.setData (analyzer.levels().data(), analyzer.peaks().data());
    spectrum.repaint();
}

void LudensMatchEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);

    const auto bounds = getLocalBounds().toFloat();

    g.setFont (theme::boldFont (19.0f));
    g.setColour (theme::text);
    g.drawText ("LudensMatch", bounds.reduced (18.0f, 0.0f).removeFromTop (44.0f),
                juce::Justification::centredLeft);

    g.setFont (theme::labelFont (10.0f));
    g.setColour (theme::textDim);
    g.drawText ("REFERENCE MATCHING EQ", bounds.reduced (18.0f, 0.0f).removeFromTop (44.0f),
                juce::Justification::centredRight);
}

void LudensMatchEditor::resized()
{
    auto area = getLocalBounds().reduced (18);

    auto header = area.removeFromTop (44);
    juce::ignoreUnused (header);

    auto controls = area.removeFromBottom (92);
    area.removeFromBottom (10);

    spectrum.setBounds (area);

    const int sliderWidth = controls.getWidth() / 3;

    const std::pair<juce::Slider*, juce::Label*> cells[] = {
        { &amount, &amountCaption },
        { &mix, &mixCaption },
        { &output, &outputCaption },
    };

    for (const auto& cellPair : cells)
    {
        auto cell = controls.removeFromLeft (sliderWidth).reduced (6, 4);
        cellPair.second->setBounds (cell.removeFromTop (18));
        cellPair.first->setBounds (cell.removeFromTop (34));
    }
}
