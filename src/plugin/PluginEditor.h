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

    // Engine Model Selection ComboBox
    juce::ComboBox engineModelBox;
    juce::Label    engineModelLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> engineModelAttach;

    // Body Model Selection ComboBox
    juce::ComboBox bodyModelBox;
    juce::Label    bodyModelLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> bodyModelAttach;

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

    // Strum 8th Articulation Toggle & Visual Status
    juce::ToggleButton strumToggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> strumAttach;
    bool isStrumVisuallyActive = false;
    juce::Rectangle<int> strumBadgeArea;

    // 8 Physical modeling knobs
    juce::Slider decayKnob, brightnessKnob, pickPosKnob,
                 stiffnessKnob, bodySizeKnob, bodyMixKnob, widthKnob, gainKnob;

    // Labels
    juce::Label  decayLabel, brightnessLabel, pickPosLabel,
                 stiffnessLabel, bodySizeLabel, bodyMixLabel, widthLabel, gainLabel;

    // APVTS Slider Attachments
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    SliderAttachment decayAttach, brightnessAttach, pickPosAttach,
                     stiffnessAttach, bodySizeAttach, bodyMixAttach, widthAttach, gainAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HybridSynthEditor)
};
