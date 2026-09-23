#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

/**
 * HybridSynthEditor — Clean 2-Row Physical Modeling Interface:
 *
 *   Row 1 [Strings & Strum]: Decay, Brightness, Pick Pos, Stiffness, Strum
 *   Row 2 [Tone & Body]:    Pickup, Tone, Body Size, Body Coupl, Master Gain
 */
class HybridSynthEditor : public juce::AudioProcessorEditor
{
public:
    explicit HybridSynthEditor(HybridSynthProcessor& processor);
    ~HybridSynthEditor() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    // Row 1: String & Strum Knobs
    juce::Slider decayKnob, brightnessKnob, pickPosKnob, stiffnessKnob, strumKnob;
    juce::Label  decayLabel, brightnessLabel, pickPosLabel, stiffnessLabel, strumLabel;

    // Row 2: Electronics & Body Knobs
    juce::Slider pickupKnob, toneKnob, bodySizeKnob, bodyMixKnob, gainKnob;
    juce::Label  pickupLabel, toneLabel, bodySizeLabel, bodyMixLabel, gainLabel;

    // APVTS Attachments
    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    Attachment decayAttach, brightnessAttach, pickPosAttach, stiffnessAttach, strumAttach,
               pickupAttach, toneAttach, bodySizeAttach, bodyMixAttach, gainAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HybridSynthEditor)
};
