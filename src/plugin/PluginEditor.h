#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

/**
 * HybridSynthEditor — UI with Body Model selector and rotary knobs:
 *   [Body Model Selector]
 *   [Decay] [Brightness] [Pick Pos] [Stiffness] [Body Size] [Body Coupling] [Gain]
 */
class HybridSynthEditor : public juce::AudioProcessorEditor
{
public:
    explicit HybridSynthEditor(HybridSynthProcessor& processor);
    ~HybridSynthEditor() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    // Body Model Selection ComboBox
    juce::ComboBox bodyModelBox;
    juce::Label    bodyModelLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> bodyModelAttach;

    // 7 Physical modeling knobs
    juce::Slider decayKnob, brightnessKnob, pickPosKnob,
                 stiffnessKnob, bodySizeKnob, bodyMixKnob, gainKnob;

    // Labels
    juce::Label  decayLabel, brightnessLabel, pickPosLabel,
                 stiffnessLabel, bodySizeLabel, bodyMixLabel, gainLabel;

    // APVTS Slider Attachments
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    SliderAttachment decayAttach, brightnessAttach, pickPosAttach,
                     stiffnessAttach, bodySizeAttach, bodyMixAttach, gainAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HybridSynthEditor)
};
