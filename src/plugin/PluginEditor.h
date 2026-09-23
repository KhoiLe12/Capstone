#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

/**
 * HybridSynthEditor — rotary knob UI for the 7 physical modeling parameters:
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

    // 7 Physical modeling knobs
    juce::Slider decayKnob, brightnessKnob, pickPosKnob,
                 stiffnessKnob, bodySizeKnob, bodyMixKnob, gainKnob;

    // Labels
    juce::Label  decayLabel, brightnessLabel, pickPosLabel,
                 stiffnessLabel, bodySizeLabel, bodyMixLabel, gainLabel;

    // APVTS Slider Attachments
    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    Attachment decayAttach, brightnessAttach, pickPosAttach,
               stiffnessAttach, bodySizeAttach, bodyMixAttach, gainAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HybridSynthEditor)
};
