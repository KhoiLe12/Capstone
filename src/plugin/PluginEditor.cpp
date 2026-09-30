#include "plugin/PluginEditor.h"
#include "Version.h"

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

HybridSynthEditor::HybridSynthEditor(HybridSynthProcessor& p)
    : juce::AudioProcessorEditor(&p),
      processorRef    (p),
      decayAttach     (p.apvts, "decay",        decayKnob),
      brightnessAttach(p.apvts, "brightness",   brightnessKnob),
      pickPosAttach   (p.apvts, "pickPosition", pickPosKnob),
      stiffnessAttach (p.apvts, "stiffness",    stiffnessKnob),
      bodySizeAttach  (p.apvts, "bodySize",     bodySizeKnob),
      bodyMixAttach   (p.apvts, "bodyMix",      bodyMixKnob),
      widthAttach     (p.apvts, "stereoWidth",  widthKnob),
      gainAttach      (p.apvts, "masterGain",   gainKnob)
{
    // Engine Model Selection ComboBox
    engineModelBox.addItem("Digital Waveguide (DWG)", 1);
    engineModelBox.addItem("Bilbao FDTD (DAFx24)", 2);
    addAndMakeVisible(engineModelBox);

    engineModelAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        p.apvts, "engineModel", engineModelBox);

    engineModelLabel.setText("Engine:", juce::dontSendNotification);
    engineModelLabel.setFont(juce::FontOptions(12.f, juce::Font::bold));
    engineModelLabel.setColour(juce::Label::textColourId, juce::Colour(0xffa0c0e0));
    engineModelLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(engineModelLabel);

    // Body Model Selection ComboBox
    bodyModelBox.addItem("Classical Nylon (IR)", 1);
    bodyModelBox.addItem("Gibson Acoustic (IR)", 2);
    bodyModelBox.addItem("Modal Bank (32-Mode)", 3);
    addAndMakeVisible(bodyModelBox);

    bodyModelAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        p.apvts, "bodyModel", bodyModelBox);

    bodyModelLabel.setText("Body Model:", juce::dontSendNotification);
    bodyModelLabel.setFont(juce::FontOptions(12.f, juce::Font::bold));
    bodyModelLabel.setColour(juce::Label::textColourId, juce::Colour(0xffa0c0e0));
    bodyModelLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(bodyModelLabel);

    // Palm Mute Articulation Toggle (UI latch / MIDI keyswitch display C1 / FL: C2)
    palmMuteToggle.setButtonText("Palm Mute (C1 / FL: C2)");
    palmMuteToggle.setColour(juce::ToggleButton::textColourId, juce::Colour(0xfff0f0f0));
    palmMuteToggle.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xffe63946));
    addAndMakeVisible(palmMuteToggle);

    palmMuteAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        p.apvts, "palmMute", palmMuteToggle);

    // Full Mute Articulation Toggle (UI latch / MIDI keyswitch display D1 / FL: D2)
    fullMuteToggle.setButtonText("Full Mute (D1 / FL: D2)");
    fullMuteToggle.setColour(juce::ToggleButton::textColourId, juce::Colour(0xfff0f0f0));
    fullMuteToggle.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xffff2255));
    addAndMakeVisible(fullMuteToggle);

    fullMuteAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        p.apvts, "fullMute", fullMuteToggle);

    // Strum Pattern Articulation Toggle (UI latch / MIDI keyswitch display E1 / FL: E2)
    strumToggle.setButtonText("Strum 8th (E1 / FL: E2)");
    strumToggle.setColour(juce::ToggleButton::textColourId, juce::Colour(0xfff0f0f0));
    strumToggle.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xfff4a261));
    addAndMakeVisible(strumToggle);

    strumAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        p.apvts, "strumPattern", strumToggle);

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
    setupKnob(widthKnob,      widthLabel,      "Width");
    setupKnob(gainKnob,       gainLabel,       "Gain");

    startTimerHz(30);

    setSize(980, 250);
    setResizable(false, false);
}

// ---------------------------------------------------------------------------
// Timer Callback (Real-time mute status polling)
// ---------------------------------------------------------------------------

void HybridSynthEditor::timerCallback()
{
    const bool currentPalm = processorRef.getSynth().isPalmMuteActive()
                          || (processorRef.apvts.getRawParameterValue("palmMute")->load() > 0.5f);
    const bool currentFull = processorRef.getSynth().isFullMuteActive()
                          || (processorRef.apvts.getRawParameterValue("fullMute")->load() > 0.5f);
    const bool currentStrum = processorRef.getSynth().isStrumActive()
                           || (processorRef.apvts.getRawParameterValue("strumPattern")->load() > 0.5f);

    bool needRepaint = false;
    if (currentPalm != isPalmVisuallyActive)
    {
        isPalmVisuallyActive = currentPalm;
        needRepaint = true;
    }
    if (currentFull != isFullVisuallyActive)
    {
        isFullVisuallyActive = currentFull;
        needRepaint = true;
    }
    if (currentStrum != isStrumVisuallyActive)
    {
        isStrumVisuallyActive = currentStrum;
        needRepaint = true;
    }

    if (needRepaint)
    {
        if (!palmBadgeArea.isEmpty())
            repaint(palmBadgeArea.expanded(2));
        if (!fullBadgeArea.isEmpty())
            repaint(fullBadgeArea.expanded(2));
        if (!strumBadgeArea.isEmpty())
            repaint(strumBadgeArea.expanded(2));
    }
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
                                   juce::Colour(0xff0d2b45), static_cast<float>(getWidth()), 0.f, false);
    g.setGradientFill(titleGrad);
    g.fillRect(0, 0, getWidth(), 36);

    // Title text
    g.setColour(juce::Colour(0xfff0f0f0));
    g.setFont(juce::FontOptions(13.5f, juce::Font::bold));
    g.drawText("Hybrid Physical Modeling Synthesizer — Acoustic IR & Modal Engine",
               juce::Rectangle<int>(14, 0, getWidth() - 145, 36),
               juce::Justification::centredLeft);

    // Prominent Version Badge in top-right
    juce::Rectangle<int> badgeArea(getWidth() - 118, 6, 104, 24);
    g.setColour(juce::Colour(0xff1d3557));
    g.fillRoundedRectangle(badgeArea.toFloat(), 4.f);
    g.setColour(juce::Colour(0xff457b9d));
    g.drawRoundedRectangle(badgeArea.toFloat(), 4.f, 1.2f);
    g.setColour(juce::Colour(0xffe63946)); // eye-catching coral red/amber dot
    g.fillEllipse(static_cast<float>(badgeArea.getX() + 8), static_cast<float>(badgeArea.getY() + 8), 7.f, 7.f);
    g.setColour(juce::Colour(0xfff1faee));
    g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
    g.drawText(HYBRID_SYNTH_VERSION_STRING, badgeArea.withTrimmedLeft(16), juce::Justification::centred);

    // Helper to draw articulation LED badges
    auto drawLedBadge = [&](const juce::Rectangle<int>& area, bool active,
                            juce::Colour activeBg, juce::Colour activeBorder, juce::Colour activeDot,
                            const char* activeText)
    {
        if (area.isEmpty()) return;

        g.setColour(active ? activeBg : juce::Colour(0xff182232));
        g.fillRoundedRectangle(area.toFloat(), 4.f);

        g.setColour(active ? activeBorder : juce::Colour(0xff2d4059));
        g.drawRoundedRectangle(area.toFloat(), 4.f, 1.2f);

        const float dotSize = 7.f;
        const float dotX = static_cast<float>(area.getX() + 6);
        const float dotY = static_cast<float>(area.getCentreY()) - dotSize * 0.5f;

        g.setColour(active ? activeDot : juce::Colour(0xff4a5568));
        g.fillEllipse(dotX, dotY, dotSize, dotSize);

        g.setColour(active ? juce::Colours::white : juce::Colour(0xff8a99ad));
        g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        g.drawText(active ? activeText : "OPEN",
                   area.withTrimmedLeft(16),
                   juce::Justification::centred);
    };

    // Palm Mute LED Badge
    drawLedBadge(palmBadgeArea, isPalmVisuallyActive,
                 juce::Colour(0xff5c1d24), juce::Colour(0xffe63946), juce::Colour(0xffff4d6d),
                 "PALM");

    // Full Mute / Choke LED Badge
    drawLedBadge(fullBadgeArea, isFullVisuallyActive,
                 juce::Colour(0xff500e20), juce::Colour(0xffff2255), juce::Colour(0xffff4070),
                 "CHOKE");

    // Strum 8th LED Badge
    const char* strumText = processorRef.getSynth().isStrumDownstroke() ? "STRUM v" : "STRUM ^";
    drawLedBadge(strumBadgeArea, isStrumVisuallyActive,
                 juce::Colour(0xff5c3a1d), juce::Colour(0xfff4a261), juce::Colour(0xffffb703),
                 strumText);

    // Subtle separator lines
    g.setColour(juce::Colour(0xff203a58));
    g.drawHorizontalLine(36, 0.f, static_cast<float>(getWidth()));
    g.drawHorizontalLine(74, 0.f, static_cast<float>(getWidth()));
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void HybridSynthEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop(38); // title bar

    // Engine Model, Body Model & Articulation Strip
    auto strip = area.removeFromTop(34).reduced(8, 3);

    const int engLabelW = 54;
    engineModelLabel.setBounds(strip.removeFromLeft(engLabelW));
    strip.removeFromLeft(4);
    engineModelBox.setBounds(strip.removeFromLeft(155));

    strip.removeFromLeft(8);
    const int labelW = 68;
    bodyModelLabel.setBounds(strip.removeFromLeft(labelW));
    strip.removeFromLeft(4);
    bodyModelBox.setBounds(strip.removeFromLeft(140));

    strip.removeFromLeft(8);
    palmMuteToggle.setBounds(strip.removeFromLeft(125));
    palmBadgeArea = strip.removeFromLeft(48).reduced(0, 2);

    strip.removeFromLeft(6);
    fullMuteToggle.setBounds(strip.removeFromLeft(125));
    fullBadgeArea = strip.removeFromLeft(50).reduced(0, 2);

    strip.removeFromLeft(6);
    strumToggle.setBounds(strip.removeFromLeft(130));
    strumBadgeArea = strip.removeFromLeft(60).reduced(0, 2);

    // Rotary Knobs Section
    const int numKnobs  = 8;
    const int knobW     = area.getWidth() / numKnobs;
    const int labelH    = 22;

    auto layoutKnob = [&](juce::Slider& knob, juce::Label& label)
    {
        auto col = area.removeFromLeft(knobW).reduced(4, 4);
        label.setBounds(col.removeFromBottom(labelH));
        knob.setBounds(col);
    };

    layoutKnob(decayKnob,      decayLabel);
    layoutKnob(brightnessKnob, brightnessLabel);
    layoutKnob(pickPosKnob,    pickPosLabel);
    layoutKnob(stiffnessKnob,  stiffnessLabel);
    layoutKnob(bodySizeKnob,   bodySizeLabel);
    layoutKnob(bodyMixKnob,    bodyMixLabel);
    layoutKnob(widthKnob,      widthLabel);
    layoutKnob(gainKnob,       gainLabel);
}
