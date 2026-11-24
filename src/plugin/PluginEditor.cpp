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
      gainAttach      (p.apvts, "masterGain",   gainKnob),
      toneAttach      (p.apvts, "toneKnob",     toneKnob)
{
    // Mode Page Switcher Buttons
    acousticPageBtn.setTooltip("Switch to Acoustic Guitar Modeling (classical, flamenco, steel acoustic, soundboards)");
    acousticPageBtn.onClick = [this]() {
        if (auto* param = processorRef.apvts.getParameter("instrumentMode"))
            param->setValueNotifyingHost(param->convertTo0to1(0.0f));
        updatePageMode(0);
    };
    addAndMakeVisible(acousticPageBtn);

    electricPageBtn.setTooltip("Switch to Electric Guitar Modeling (Strat, Les Paul, electromagnetic pickups, passive RLC tone, pure DI output)");
    electricPageBtn.onClick = [this]() {
        if (auto* param = processorRef.apvts.getParameter("instrumentMode"))
            param->setValueNotifyingHost(param->convertTo0to1(1.0f));
        updatePageMode(1);
    };
    addAndMakeVisible(electricPageBtn);

    // Preset Selection Dropdown
    presetBox.onChange = [this]() {
        const int comboIdx = presetBox.getSelectedItemIndex();
        if (comboIdx < 0) return;
        const int globalIdx = (currentPageMode == 1)
            ? (HybridSynthProcessor::kNumAcousticPresets + comboIdx)
            : comboIdx;
        processorRef.setCurrentProgram(globalIdx);
    };
    addAndMakeVisible(presetBox);

    presetLabel.setText("Preset:", juce::dontSendNotification);
    presetLabel.setFont(juce::FontOptions(11.5f, juce::Font::bold));
    presetLabel.setColour(juce::Label::textColourId, juce::Colour(0xfff1faee));
    presetLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(presetLabel);

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

    // String Material Selection ComboBox
    stringMaterialBox.addItem("Nylon Classical", 1);
    stringMaterialBox.addItem("Steel Acoustic", 2);
    addAndMakeVisible(stringMaterialBox);

    stringMaterialAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        p.apvts, "stringMaterial", stringMaterialBox);

    stringMaterialLabel.setText("String:", juce::dontSendNotification);
    stringMaterialLabel.setFont(juce::FontOptions(12.f, juce::Font::bold));
    stringMaterialLabel.setColour(juce::Label::textColourId, juce::Colour(0xffa0c0e0));
    stringMaterialLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(stringMaterialLabel);

    // Body Model Selection ComboBox
    bodyModelBox.addItem("Classical - Contreras 1971", 1);
    bodyModelBox.addItem("Legacy Body (IR)", 2);
    bodyModelBox.addItem("Modal Bank (32-Mode)", 3);
    bodyModelBox.addItem("Flamenco - Conde 1964", 4);
    bodyModelBox.addItem("Custom IR", 5);
    addAndMakeVisible(bodyModelBox);

    irButton.setTooltip("Load your own guitar body impulse response (WAV/AIFF/FLAC), or clear it. "
                        "The IR is saved inside your project.");
    bodyModelBox.setTooltip("Body. Classical (Contreras 1971) and Flamenco (Conde 1964) are measured IRs from "
                            "R. Mores, 'Archive for the acoustical documentation of ... guitars', Zenodo, "
                            "doi:10.5281/zenodo.4604577, CC BY 4.0 (derived: deconvolved, trimmed, normalised).");
    irButton.onClick = [this] { showIRMenu(); };
    addAndMakeVisible(irButton);

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

    // Legato toggle (hammer-ons / pull-offs on overlapping notes)
    legatoToggle.setButtonText("Legato (HO/PO)");
    legatoToggle.setTooltip("Overlapping notes on one string play as hammer-ons / pull-offs. "
                            "Notes within 40 ms of each other always play as a chord.");
    legatoToggle.setColour(juce::ToggleButton::textColourId, juce::Colour(0xfff0f0f0));
    legatoToggle.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xff8ecae6));
    addAndMakeVisible(legatoToggle);

    legatoAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        p.apvts, "legato", legatoToggle);

    // Strict fretboard toggle
    strictToggle.setButtonText("Strict Fretboard");
    strictToggle.setTooltip("On: chord notes with no real string/fret position (22 frets) are dropped, "
                            "as on a real guitar. Off: they play on a free string past the fretboard.");
    strictToggle.setColour(juce::ToggleButton::textColourId, juce::Colour(0xfff0f0f0));
    strictToggle.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xff8ecae6));
    addAndMakeVisible(strictToggle);

    strictAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        p.apvts, "strictFretboard", strictToggle);

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
    setupKnob(toneKnob,       toneLabel,       "Tone (RLC)");

    // Electric Guitar Model
    guitarModelBox.addItem("1962 Stratocaster (25.5\")", 1);
    guitarModelBox.addItem("1959 Les Paul (24.75\")", 2);
    addAndMakeVisible(guitarModelBox);
    guitarModelAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        p.apvts, "guitarModel", guitarModelBox);
    guitarModelLabel.setText("Guitar:", juce::dontSendNotification);
    guitarModelLabel.setFont(juce::FontOptions(12.f, juce::Font::bold));
    guitarModelLabel.setColour(juce::Label::textColourId, juce::Colour(0xffa0c0e0));
    guitarModelLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(guitarModelLabel);

    // Pickup Position
    pickupPosBox.addItem("Bridge", 1);
    pickupPosBox.addItem("Bridge + Middle", 2);
    pickupPosBox.addItem("Middle", 3);
    pickupPosBox.addItem("Neck + Middle", 4);
    pickupPosBox.addItem("Neck", 5);
    pickupPosBox.addItem("Bridge + Neck", 6);
    addAndMakeVisible(pickupPosBox);
    pickupPosAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        p.apvts, "pickupPosition", pickupPosBox);
    pickupPosLabel.setText("Pickup:", juce::dontSendNotification);
    pickupPosLabel.setFont(juce::FontOptions(12.f, juce::Font::bold));
    pickupPosLabel.setColour(juce::Label::textColourId, juce::Colour(0xffa0c0e0));
    pickupPosLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(pickupPosLabel);

    // Pickup Type
    pickupTypeBox.addItem("Single-Coil (Alnico V)", 1);
    pickupTypeBox.addItem("Humbucker (PAF Alnico II)", 2);
    addAndMakeVisible(pickupTypeBox);
    pickupTypeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        p.apvts, "pickupType", pickupTypeBox);
    pickupTypeLabel.setText("Type:", juce::dontSendNotification);
    pickupTypeLabel.setFont(juce::FontOptions(12.f, juce::Font::bold));
    pickupTypeLabel.setColour(juce::Label::textColourId, juce::Colour(0xffa0c0e0));
    pickupTypeLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(pickupTypeLabel);

    // Cab / Air Toggle
    cabAirToggle.setButtonText("Cab/Air Roll-off");
    cabAirToggle.setTooltip("Simulates gentle ~5kHz speaker cabinet air roll-off for headphone jamming. Off = pure DI.");
    cabAirToggle.setColour(juce::ToggleButton::textColourId, juce::Colour(0xfff0f0f0));
    cabAirToggle.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xff48cae4));
    addAndMakeVisible(cabAirToggle);
    cabAirAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        p.apvts, "cabAirFilter", cabAirToggle);

    refreshCustomIRItem();
    updatePageMode(p.apvts.getRawParameterValue("instrumentMode")->load() > 0.5f ? 1 : 0);
    startTimerHz(30);

    setSize(1140, 250);
    setResizable(false, false);
}

// ---------------------------------------------------------------------------
// Timer Callback (Real-time mute status polling)
// ---------------------------------------------------------------------------

void HybridSynthEditor::refreshCustomIRItem()
{
    const auto name = processorRef.getCustomIRName();
    if (name == shownCustomIRName)
        return;
    shownCustomIRName = name;
    bodyModelBox.changeItemText(HybridSynthProcessor::kCustomIRBodyIndex + 1,
                                name.isEmpty() ? juce::String("Custom IR (none loaded)") : "Custom IR: " + name);
}

void HybridSynthEditor::showIRMenu()
{
    juce::PopupMenu menu;
    const auto name = processorRef.getCustomIRName();
    menu.addSectionHeader(name.isEmpty() ? juce::String("No custom IR loaded") : "Loaded: " + name);
    menu.addItem(1, "Load IR file...");
    menu.addItem(2, "Clear custom IR", name.isNotEmpty());
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&irButton),
                       [safeThis = juce::Component::SafePointer<HybridSynthEditor>(this)](int result)
                       {
                           if (safeThis == nullptr) return;
                           if (result == 1)
                               safeThis->chooseIRFile();
                           else if (result == 2)
                           {
                               safeThis->processorRef.clearCustomIR();
                               safeThis->refreshCustomIRItem();
                           }
                       });
}

void HybridSynthEditor::chooseIRFile()
{
    irChooser = std::make_unique<juce::FileChooser>("Load guitar body impulse response",
                                                    juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg");
    irChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                           [safeThis = juce::Component::SafePointer<HybridSynthEditor>(this)](const juce::FileChooser& fc)
                           {
                               if (safeThis == nullptr) return;
                               const auto file = fc.getResult();
                               if (file == juce::File()) return; // cancelled

                               const auto warning = safeThis->processorRef.getCustomIRFileWarning(file);
                               if (warning.isEmpty())
                               {
                                   safeThis->loadIRFile(file);
                                   return;
                               }
                               juce::AlertWindow::showAsync(
                                   juce::MessageBoxOptions()
                                       .withIconType(juce::MessageBoxIconType::WarningIcon)
                                       .withTitle("This doesn't look like a body IR")
                                       .withMessage("\"" + file.getFileName() + "\": " + warning
                                                    + "\n\nAs a body IR it would likely ring or sound like feedback. "
                                                      "(Its level is capped, so it cannot overload.)")
                                       .withButton("Load anyway")
                                       .withButton("Cancel"),
                                   [safeThis, file](int result)
                                   {
                                       if (safeThis != nullptr && result == 1)
                                           safeThis->loadIRFile(file);
                                   });
                           });
}

void HybridSynthEditor::loadIRFile(const juce::File& file)
{
    const auto error = processorRef.loadCustomIRFile(file);
    if (error.isNotEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Could not load IR", error);
        return;
    }
    // Select the Custom IR body so the new IR is heard immediately
    if (auto* param = processorRef.apvts.getParameter("bodyModel"))
        param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(HybridSynthProcessor::kCustomIRBodyIndex)));
    refreshCustomIRItem();
}

void HybridSynthEditor::updatePageMode(int mode)
{
    currentPageMode = mode;
    const bool isElectric = (mode == 1);

    presetBox.clear(juce::dontSendNotification);
    const auto& presets = HybridSynthProcessor::getFactoryPresets();
    if (!isElectric)
    {
        for (int i = 0; i < HybridSynthProcessor::kNumAcousticPresets; ++i)
            presetBox.addItem(presets[static_cast<size_t>(i)].name, i + 1);

        int cur = processorRef.getCurrentProgram();
        if (cur >= HybridSynthProcessor::kNumAcousticPresets)
        {
            processorRef.setCurrentProgram(0);
            cur = 0;
        }
        presetBox.setSelectedItemIndex(cur, juce::dontSendNotification);
    }
    else
    {
        for (int i = 0; i < HybridSynthProcessor::kNumElectricPresets; ++i)
        {
            const size_t pIdx = static_cast<size_t>(HybridSynthProcessor::kNumAcousticPresets + i);
            presetBox.addItem(presets[pIdx].name, i + 1);
        }

        int cur = processorRef.getCurrentProgram();
        if (cur < HybridSynthProcessor::kNumAcousticPresets)
        {
            processorRef.setCurrentProgram(HybridSynthProcessor::kNumAcousticPresets);
            cur = HybridSynthProcessor::kNumAcousticPresets;
        }
        presetBox.setSelectedItemIndex(cur - HybridSynthProcessor::kNumAcousticPresets, juce::dontSendNotification);
    }

    stringMaterialBox.setVisible(!isElectric);
    stringMaterialLabel.setVisible(!isElectric);
    bodyModelBox.setVisible(!isElectric);
    bodyModelLabel.setVisible(!isElectric);
    irButton.setVisible(!isElectric);
    bodySizeKnob.setVisible(!isElectric);
    bodySizeLabel.setVisible(!isElectric);
    bodyMixKnob.setVisible(!isElectric);
    bodyMixLabel.setVisible(!isElectric);

    guitarModelBox.setVisible(isElectric);
    guitarModelLabel.setVisible(isElectric);
    pickupPosBox.setVisible(isElectric);
    pickupPosLabel.setVisible(isElectric);
    pickupTypeBox.setVisible(isElectric);
    pickupTypeLabel.setVisible(isElectric);
    cabAirToggle.setVisible(isElectric);
    toneKnob.setVisible(isElectric);
    toneLabel.setVisible(isElectric);

    // Style page switcher buttons
    if (!isElectric)
    {
        acousticPageBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a4d69));
        acousticPageBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        electricPageBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff182232));
        electricPageBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff8a99ad));
    }
    else
    {
        acousticPageBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff182232));
        acousticPageBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff8a99ad));
        electricPageBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff0077b6));
        electricPageBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    }

    resized();
    repaint();
}

void HybridSynthEditor::timerCallback()
{
    refreshCustomIRItem();

    // Sync mode page if changed via APVTS automation or preset load
    const float modeVal = processorRef.apvts.getRawParameterValue("instrumentMode")->load();
    const int targetMode = (modeVal >= 0.5f) ? 1 : 0;
    if (targetMode != currentPageMode)
        updatePageMode(targetMode);

    const int curProg = processorRef.getCurrentProgram();
    const int expectedIdx = (currentPageMode == 1)
        ? (curProg - HybridSynthProcessor::kNumAcousticPresets)
        : curProg;
    if (expectedIdx >= 0 && expectedIdx < presetBox.getNumItems()
        && presetBox.getSelectedItemIndex() != expectedIdx)
    {
        presetBox.setSelectedItemIndex(expectedIdx, juce::dontSendNotification);
    }

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
    g.drawText(currentPageMode == 1 ? "Hybrid Electric Guitar (Pure DI)" : "Hybrid Acoustic Guitar",
               juce::Rectangle<int>(230, 0, 230, 36),
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
    acousticPageBtn.setBounds(14, 6, 95, 24);
    electricPageBtn.setBounds(115, 6, 105, 24);

    strictToggle.setBounds(getWidth() - 680, 6, 140, 24);
    legatoToggle.setBounds(getWidth() - 535, 6, 130, 24);
    presetLabel.setBounds(getWidth() - 395, 6, 52, 24);
    presetBox.setBounds(getWidth() - 340, 6, 210, 24);

    auto area = getLocalBounds();
    area.removeFromTop(38); // title bar

    // Middle Strip
    auto strip = area.removeFromTop(34).reduced(8, 3);

    const int engLabelW = 50;
    engineModelLabel.setBounds(strip.removeFromLeft(engLabelW));
    strip.removeFromLeft(4);
    engineModelBox.setBounds(strip.removeFromLeft(135));
    strip.removeFromLeft(8);

    if (currentPageMode == 0) // Acoustic Mode Page
    {
        const int matLabelW = 44;
        stringMaterialLabel.setBounds(strip.removeFromLeft(matLabelW));
        strip.removeFromLeft(4);
        stringMaterialBox.setBounds(strip.removeFromLeft(115));

        strip.removeFromLeft(8);
        const int labelW = 44;
        bodyModelLabel.setBounds(strip.removeFromLeft(labelW));
        strip.removeFromLeft(4);
        bodyModelBox.setBounds(strip.removeFromLeft(140));
        strip.removeFromLeft(4);
        irButton.setBounds(strip.removeFromLeft(44));
    }
    else // Electric Mode Page
    {
        const int gtrLabelW = 44;
        guitarModelLabel.setBounds(strip.removeFromLeft(gtrLabelW));
        strip.removeFromLeft(4);
        guitarModelBox.setBounds(strip.removeFromLeft(150));

        strip.removeFromLeft(8);
        const int pickLabelW = 46;
        pickupPosLabel.setBounds(strip.removeFromLeft(pickLabelW));
        strip.removeFromLeft(4);
        pickupPosBox.setBounds(strip.removeFromLeft(125));

        strip.removeFromLeft(8);
        const int typeLabelW = 38;
        pickupTypeLabel.setBounds(strip.removeFromLeft(typeLabelW));
        strip.removeFromLeft(4);
        pickupTypeBox.setBounds(strip.removeFromLeft(135));
    }

    strip.removeFromLeft(8);
    palmMuteToggle.setBounds(strip.removeFromLeft(115));
    palmBadgeArea = strip.removeFromLeft(46).reduced(0, 2);

    strip.removeFromLeft(6);
    fullMuteToggle.setBounds(strip.removeFromLeft(115));
    fullBadgeArea = strip.removeFromLeft(48).reduced(0, 2);

    strip.removeFromLeft(6);
    strumToggle.setBounds(strip.removeFromLeft(118));
    strumBadgeArea = strip.removeFromLeft(56).reduced(0, 2);

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

    if (currentPageMode == 0) // Acoustic Knobs
    {
        layoutKnob(bodySizeKnob,   bodySizeLabel);
        layoutKnob(bodyMixKnob,    bodyMixLabel);
    }
    else // Electric Knobs
    {
        layoutKnob(toneKnob,       toneLabel);
        auto colCab = area.removeFromLeft(knobW).reduced(4, 4);
        cabAirToggle.setBounds(colCab.withSizeKeepingCentre(110, 30));
    }

    layoutKnob(widthKnob,      widthLabel);
    layoutKnob(gainKnob,       gainLabel);
}
