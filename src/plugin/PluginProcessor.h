#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/SynthEngine.h"
#include "Version.h"

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
    const juce::String getName() const override { return "Hybrid Synth (" HYBRID_SYNTH_VERSION_STRING ")"; }
    bool  acceptsMidi()  const override { return true;  }
    bool  producesMidi() const override { return false; }
    bool  isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    // Presets & Programs
    struct FactoryPreset {
        const char* name;
        float decay;
        float brightness;
        float pickPos;
        float stiffness;
        float bodySize;
        float bodyMix;
        int   engineModel;    // 0 = DWG, 1 = FDTD
        int   stringMaterial; // 0 = Nylon, 1 = Steel
        int   bodyModel;      // 0 = Classical Nylon IR, 1 = Gibson Acoustic IR, 2 = Modal Bank
        float stereoWidth;
        float masterGain;
        bool  palmMute;
        bool  fullMute;
        bool  strumPattern;
    };

    static const std::vector<FactoryPreset>& getFactoryPresets() noexcept;

    int  getNumPrograms()   override;
    int  getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    // Editor
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // State persistence
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    SynthEngine& getSynth() noexcept { return synth; }
    const SynthEngine& getSynth() const noexcept { return synth; }

    // -----------------------------------------------------------------------
    // Parameter tree — public so the editor can attach to it
    // -----------------------------------------------------------------------
    juce::AudioProcessorValueTreeState apvts;

private:
    SynthEngine synth;
    int currentProgram = 0;

    /** Build the parameter layout used to construct the APVTS. */
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HybridSynthProcessor)
};

