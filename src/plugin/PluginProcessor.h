#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/SynthEngine.h"

/**
 * HybridSynthProcessor — JUCE AudioProcessor wrapper for the HybridSynth engine.
 *
 * Responsibilities:
 *   - Implement the JUCE plugin interface (prepareToPlay, processBlock, etc.)
 *   - Own the AudioProcessorValueTreeState (APVTS) for all automatable parameters
 *   - Parse incoming MidiBuffer events and route them to SynthEngine
 *   - Pass audio output buffers to SynthEngine::process()
 *   - Serialize/deserialize state for DAW project save/load
 */
class HybridSynthProcessor : public juce::AudioProcessor
{
public:
    HybridSynthProcessor();
    ~HybridSynthProcessor() override = default;

    // -----------------------------------------------------------------------
    // AudioProcessor interface
    // -----------------------------------------------------------------------
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // Plugin metadata
    const juce::String getName() const override { return "HybridSynth"; }
    bool  acceptsMidi()  const override { return true;  }
    bool  producesMidi() const override { return false; }
    bool  isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    // Programs (not used — single preset)
    int  getNumPrograms()   override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    // Editor
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // State persistence
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // -----------------------------------------------------------------------
    // Parameter tree — public so the editor can attach to it
    // -----------------------------------------------------------------------
    juce::AudioProcessorValueTreeState apvts;

private:
    SynthEngine synth;

    /** Build the parameter layout used to construct the APVTS. */
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HybridSynthProcessor)
};

