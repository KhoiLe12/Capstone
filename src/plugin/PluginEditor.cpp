#include "plugin/PluginEditor.h"

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

HybridSynthEditor::HybridSynthEditor(HybridSynthProcessor& p)
    : juce::AudioProcessorEditor(&p),
      processorRef(p),
      // Wire each knob to its APVTS parameter by ID
      decayAttach     (p.apvts, "decay",        decayKnob),
      brightnessAttach(p.apvts, "brightness",   brightnessKnob),
      pickPosAttach   (p.apvts, "pickPosition", pickPosKnob),
      bodyMixAttach   (p.apvts, "bodyMix",      bodyMixKnob),
      gainAttach      (p.apvts, "masterGain",   gainKnob)
{
    // Helper to configure each knob + label pair
    auto setupKnob = [this](juce::Slider& knob, juce::Label& label,
                            const juce::String& name)
    {
        knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 18);
        addAndMakeVisible(knob);

        label.setText(name, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setFont(juce::FontOptions(12.f));
        addAndMakeVisible(label);
    };

    setupKnob(decayKnob,      decayLabel,      "Decay");
    setupKnob(brightnessKnob, brightnessLabel, "Brightness");
    setupKnob(pickPosKnob,    pickPosLabel,    "Pick Pos");
    setupKnob(bodyMixKnob,    bodyMixLabel,    "Body Mix");
    setupKnob(gainKnob,       gainLabel,       "Gain");

    setSize(500, 220);
    setResizable(false, false);
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void HybridSynthEditor::paint(juce::Graphics& g)
{
    // Dark navy background
    g.fillAll(juce::Colour(0xff1a1a2e));

    // Title bar gradient
    juce::ColourGradient titleGrad(juce::Colour(0xff16213e), 0.f, 0.f,
                                   juce::Colour(0xff0f3460), 500.f, 0.f, false);
    g.setGradientFill(titleGrad);
    g.fillRect(0, 0, getWidth(), 36);

    // Title text
    g.setColour(juce::Colour(0xffe0e0e0));
    g.setFont(juce::FontOptions(15.f, juce::Font::bold));
    g.drawText("Hybrid Physical Modeling Synth",
               juce::Rectangle<int>(0, 0, getWidth(), 36),
               juce::Justification::centred);

    // Subtle separator line
    g.setColour(juce::Colour(0xff0f3460));
    g.drawHorizontalLine(36, 0.f, static_cast<float>(getWidth()));
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void HybridSynthEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop(40);   // title bar

    const int knobW     = area.getWidth() / 5;
    const int labelH    = 22;

    // Lay knobs side-by-side, label below each
    auto layoutKnob = [&](juce::Slider& knob, juce::Label& label)
    {
        auto col = area.removeFromLeft(knobW).reduced(4, 4);
        label.setBounds(col.removeFromBottom(labelH));
        knob.setBounds(col);
    };

    layoutKnob(decayKnob,      decayLabel);
    layoutKnob(brightnessKnob, brightnessLabel);
    layoutKnob(pickPosKnob,    pickPosLabel);
    layoutKnob(bodyMixKnob,    bodyMixLabel);
    layoutKnob(gainKnob,       gainLabel);
}

