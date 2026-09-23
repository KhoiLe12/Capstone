#include "plugin/PluginEditor.h"

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

HybridSynthEditor::HybridSynthEditor(HybridSynthProcessor& p)
    : juce::AudioProcessorEditor(&p),
      decayAttach      (p.apvts, "decay",        decayKnob),
      brightnessAttach (p.apvts, "brightness",   brightnessKnob),
      pickPosAttach    (p.apvts, "pickPosition", pickPosKnob),
      stiffnessAttach  (p.apvts, "stiffness",    stiffnessKnob),
      strumAttach      (p.apvts, "strumSpeed",   strumKnob),
      pickupAttach     (p.apvts, "pickupPos",    pickupKnob),
      toneAttach       (p.apvts, "tone",         toneKnob),
      bodySizeAttach   (p.apvts, "bodySize",     bodySizeKnob),
      bodyMixAttach    (p.apvts, "bodyMix",      bodyMixKnob),
      gainAttach       (p.apvts, "masterGain",   gainKnob)
{
    auto setupKnob = [this](juce::Slider& knob, juce::Label& label,
                            const juce::String& name)
    {
        knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 16);
        addAndMakeVisible(knob);

        label.setText(name, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setFont(juce::FontOptions(11.f));
        addAndMakeVisible(label);
    };

    // Row 1
    setupKnob(decayKnob,      decayLabel,      "Decay");
    setupKnob(brightnessKnob, brightnessLabel, "Brightness");
    setupKnob(pickPosKnob,    pickPosLabel,    "Pick Pos");
    setupKnob(stiffnessKnob,  stiffnessLabel,  "Stiffness");
    setupKnob(strumKnob,      strumLabel,      "Strum");

    // Row 2
    setupKnob(pickupKnob,     pickupLabel,     "Pickup Pos");
    setupKnob(toneKnob,       toneLabel,       "Tone");
    setupKnob(bodySizeKnob,   bodySizeLabel,   "Body Size");
    setupKnob(bodyMixKnob,    bodyMixLabel,    "Body Coupl");
    setupKnob(gainKnob,       gainLabel,       "Gain");

    setSize(640, 290);
    setResizable(false, false);
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void HybridSynthEditor::paint(juce::Graphics& g)
{
    // Deep midnight slate background
    g.fillAll(juce::Colour(0xff10131a));

    // Title bar gradient
    juce::ColourGradient titleGrad(juce::Colour(0xff182232), 0.f, 0.f,
                                   juce::Colour(0xff0c243a), 640.f, 0.f, false);
    g.setGradientFill(titleGrad);
    g.fillRect(0, 0, getWidth(), 34);

    // Title text
    g.setColour(juce::Colour(0xffe8edf5));
    g.setFont(juce::FontOptions(13.f, juce::Font::bold));
    g.drawText("Hybrid Physical Modeling Synthesizer — Fretboard & Strum Engine",
               juce::Rectangle<int>(0, 0, getWidth(), 34),
               juce::Justification::centred);

    // Separator line under title
    g.setColour(juce::Colour(0xff223850));
    g.drawHorizontalLine(34, 0.f, static_cast<float>(getWidth()));

    // Subtle row divider line
    g.setColour(juce::Colour(0xff1a2230));
    g.drawHorizontalLine(162, 10.f, static_cast<float>(getWidth() - 10));
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void HybridSynthEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop(38); // Title bar margin

    const int rowHeight = (area.getHeight() - 4) / 2;

    auto row1 = area.removeFromTop(rowHeight);
    auto row2 = area;

    const int knobW1 = row1.getWidth() / 5;
    const int knobW2 = row2.getWidth() / 5;
    const int labelH = 18;

    auto layoutKnob = [&](juce::Rectangle<int>& row, int w, juce::Slider& knob, juce::Label& label)
    {
        auto col = row.removeFromLeft(w).reduced(4, 2);
        label.setBounds(col.removeFromBottom(labelH));
        knob.setBounds(col);
    };

    // Row 1
    layoutKnob(row1, knobW1, decayKnob,      decayLabel);
    layoutKnob(row1, knobW1, brightnessKnob, brightnessLabel);
    layoutKnob(row1, knobW1, pickPosKnob,    pickPosLabel);
    layoutKnob(row1, knobW1, stiffnessKnob,  stiffnessLabel);
    layoutKnob(row1, knobW1, strumKnob,      strumLabel);

    // Row 2
    layoutKnob(row2, knobW2, pickupKnob,     pickupLabel);
    layoutKnob(row2, knobW2, toneKnob,       toneLabel);
    layoutKnob(row2, knobW2, bodySizeKnob,   bodySizeLabel);
    layoutKnob(row2, knobW2, bodyMixKnob,    bodyMixLabel);
    layoutKnob(row2, knobW2, gainKnob,       gainLabel);
}
