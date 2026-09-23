#include "plugin/PluginEditor.h"

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

HybridSynthEditor::HybridSynthEditor(HybridSynthProcessor& p)
    : juce::AudioProcessorEditor(&p),
      decayAttach     (p.apvts, "decay",        decayKnob),
      brightnessAttach(p.apvts, "brightness",   brightnessKnob),
      pickPosAttach   (p.apvts, "pickPosition", pickPosKnob),
      stiffnessAttach (p.apvts, "stiffness",    stiffnessKnob),
      bodySizeAttach  (p.apvts, "bodySize",     bodySizeKnob),
      bodyMixAttach   (p.apvts, "bodyMix",      bodyMixKnob),
      gainAttach      (p.apvts, "masterGain",   gainKnob)
{
    auto setupKnob = [this](juce::Slider& knob, juce::Label& label,
                            const juce::String& name)
    {
        knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 18);
        addAndMakeVisible(knob);

        label.setText(name, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setFont(juce::FontOptions(11.f));
        addAndMakeVisible(label);
    };

    setupKnob(decayKnob,      decayLabel,      "Decay");
    setupKnob(brightnessKnob, brightnessLabel, "Brightness");
    setupKnob(pickPosKnob,    pickPosLabel,    "Pick Pos");
    setupKnob(stiffnessKnob,  stiffnessLabel,  "Stiffness");
    setupKnob(bodySizeKnob,   bodySizeLabel,   "Body Size");
    setupKnob(bodyMixKnob,    bodyMixLabel,    "Body Coupl");
    setupKnob(gainKnob,       gainLabel,       "Gain");

    setSize(680, 220);
    setResizable(false, false);
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void HybridSynthEditor::paint(juce::Graphics& g)
{
    // Deep navy background
    g.fillAll(juce::Colour(0xff121224));

    // Title bar gradient
    juce::ColourGradient titleGrad(juce::Colour(0xff1a233a), 0.f, 0.f,
                                   juce::Colour(0xff0d2b45), 680.f, 0.f, false);
    g.setGradientFill(titleGrad);
    g.fillRect(0, 0, getWidth(), 36);

    // Title text
    g.setColour(juce::Colour(0xfff0f0f0));
    g.setFont(juce::FontOptions(14.f, juce::Font::bold));
    g.drawText("Hybrid Physical Modeling Synthesizer — Modalys & Multi-Port Engine",
               juce::Rectangle<int>(0, 0, getWidth(), 36),
               juce::Justification::centred);

    // Subtle separator line
    g.setColour(juce::Colour(0xff203a58));
    g.drawHorizontalLine(36, 0.f, static_cast<float>(getWidth()));
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void HybridSynthEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop(40); // title bar

    const int numKnobs  = 7;
    const int knobW     = area.getWidth() / numKnobs;
    const int labelH    = 22;

    auto layoutKnob = [&](juce::Slider& knob, juce::Label& label)
    {
        auto col = area.removeFromLeft(knobW).reduced(3, 4);
        label.setBounds(col.removeFromBottom(labelH));
        knob.setBounds(col);
    };

    layoutKnob(decayKnob,      decayLabel);
    layoutKnob(brightnessKnob, brightnessLabel);
    layoutKnob(pickPosKnob,    pickPosLabel);
    layoutKnob(stiffnessKnob,  stiffnessLabel);
    layoutKnob(bodySizeKnob,   bodySizeLabel);
    layoutKnob(bodyMixKnob,    bodyMixLabel);
    layoutKnob(gainKnob,       gainLabel);
}
