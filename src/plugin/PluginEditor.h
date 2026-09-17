#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

/**
 * HybridSynthEditor — minimal knob-based UI for the 5 synth parameters.
 *
 * Layout (500 × 220 px):
 *
 *   ┌─────────────────────────────────────────────────┐
 *   │      Hybrid Physical Modeling Synth              │
 *   │  [Decay] [Bright] [PickPos] [Body Mix] [Gain]   │
 *   │   (rotary knobs with value boxes below)          │
 *   └─────────────────────────────────────────────────┘
 *
 * All knobs are wired to the APVTS via SliderAttachment
 * (no manual listener boilerplate required).
 */
class HybridSynthEditor : public juce::AudioProcessorEditor
{
public:
    explicit HybridSynthEditor(HybridSynthProcessor& processor);
    ~HybridSynthEditor() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    HybridSynthProcessor& processorRef;

    // Knobs
    juce::Slider decayKnob, brightnessKnob, pickPosKnob, bodyMixKnob, gainKnob;

    // Labels below each knob
    juce::Label  decayLabel, brightnessLabel, pickPosLabel, bodyMixLabel, gainLabel;

    // APVTS attachments keep knob ↔ parameter in sync (thread-safe)
    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    Attachment decayAttach, brightnessAttach, pickPosAttach,
               bodyMixAttach, gainAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HybridSynthEditor)
};

