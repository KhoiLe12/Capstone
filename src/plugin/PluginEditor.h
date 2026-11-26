#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

/**
 * HybridSynthEditor — UI with Body Model selector, articulations, and rotary knobs:
 *   [Body Model Selector] [Palm Mute Toggle + LED] [Full Mute Toggle + LED]
 *   [Decay] [Brightness] [Pick Pos] [Stiffness] [Body Size] [Body Coupling] [Gain]
 */
class HybridSynthEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit HybridSynthEditor(HybridSynthProcessor& processor);
    ~HybridSynthEditor() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    HybridSynthProcessor& processorRef;

    // Mode Page Switcher (Acoustic vs Electric Page)
    juce::TextButton acousticPageBtn { "Acoustic" };
    juce::TextButton electricPageBtn { "Electric (DI)" };
    int currentPageMode = -1; // 0 = Acoustic, 1 = Electric
    int shownGuitarModel = -1;
    void updatePageMode(int mode, bool selectDefaultPreset = false);
    void refreshPickupChoices(int guitarModel);

    // Preset Selection Custom ComboBox (ensures first click registers reliably)
    class PresetBox : public juce::ComboBox
    {
    public:
        void showPopup() override
        {
            const auto currentText = getText();
            setSelectedId(0, juce::dontSendNotification);
            setText(currentText, juce::dontSendNotification);
            juce::ComboBox::showPopup();
        }
    };
    PresetBox   presetBox;
    juce::Label presetLabel;

    // Engine Model Selection ComboBox
    juce::ComboBox engineModelBox;
    juce::Label    engineModelLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> engineModelAttach;

    // String Material Selection ComboBox
    juce::ComboBox stringMaterialBox;
    juce::Label    stringMaterialLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> stringMaterialAttach;

    // Body Model Selection ComboBox
    juce::ComboBox bodyModelBox;
    juce::Label    bodyModelLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> bodyModelAttach;

    // Custom IR: load / clear menu next to the body selector
    juce::TextButton irButton { "IR..." };
    std::unique_ptr<juce::FileChooser> irChooser;
    juce::String shownCustomIRName { "-" };
    void showIRMenu();
    void chooseIRFile();
    void loadIRFile(const juce::File& file);
    void refreshCustomIRItem();

    // Palm Mute Articulation Toggle & Visual Status
    juce::ToggleButton palmMuteToggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> palmMuteAttach;
    bool isPalmVisuallyActive = false;
    juce::Rectangle<int> palmBadgeArea;

    // Full Mute Articulation Toggle & Visual Status
    juce::ToggleButton fullMuteToggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> fullMuteAttach;
    bool isFullVisuallyActive = false;
    juce::Rectangle<int> fullBadgeArea;

    // Legato (hammer-on / pull-off) toggle
    juce::ToggleButton legatoToggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> legatoAttach;

    // Strict fretboard toggle (drop physically unplayable chord notes)
    juce::ToggleButton strictToggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> strictAttach;

    // Strum 8th Articulation Toggle & Visual Status
    juce::ToggleButton strumToggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> strumAttach;
    bool isStrumVisuallyActive = false;
    juce::Rectangle<int> strumBadgeArea;

    // 8 Physical modeling knobs
    juce::Slider decayKnob, brightnessKnob, pickPosKnob,
                 stiffnessKnob, bodySizeKnob, bodyMixKnob, widthKnob, gainKnob, toneKnob;

    // Labels
    juce::Label  decayLabel, brightnessLabel, pickPosLabel,
                 stiffnessLabel, bodySizeLabel, bodyMixLabel, widthLabel, gainLabel, toneLabel;

    // Electric Guitar Mode Controls
    juce::ComboBox guitarModelBox;
    juce::Label    guitarModelLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> guitarModelAttach;

    juce::ComboBox pickupPosBox;
    juce::Label    pickupPosLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> pickupPosAttach;

    juce::ComboBox pickupTypeBox;
    juce::Label    pickupTypeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> pickupTypeAttach;

    juce::ToggleButton cabAirToggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> cabAirAttach;

    // APVTS Slider Attachments
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    SliderAttachment decayAttach, brightnessAttach, pickPosAttach,
                     stiffnessAttach, bodySizeAttach, bodyMixAttach, widthAttach, gainAttach, toneAttach;

    // Visual 6-String Fretboard State (polled lock-free at 30 Hz)
    struct DisplayString
    {
        int   midiNote = -1;
        int   fret     = -1;
        int   muteMode = 0;
        float energy   = 0.0f;
        bool  active   = false;
        bool  offBoard = false;
    };
    DisplayString displayStrings[6];
    uint32_t lastSeenNoticeSeq = 0;
    int      activeNoticeType  = 0;
    int      activeNoticeNote  = -1;
    int      noticeHoldTicks   = 0;
    juce::Rectangle<int> fretboardArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HybridSynthEditor)
};
