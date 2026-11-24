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
        int   bodyModel;      // 0 = Classical IR, 1 = Legacy IR, 2 = Modal Bank, 3 = Flamenco IR (4 = Custom IR)
        float stereoWidth;
        float masterGain;
        bool  palmMute;
        bool  fullMute;
        bool  strumPattern;
        // Electric guitar parameters (defaults for acoustic presets)
        int   instrumentMode = 0; // 0 = Acoustic, 1 = Electric
        int   guitarModel    = 0; // 0 = Strat 1962, 1 = Les Paul 1959
        int   pickupPosition = 0; // 0 = Bridge, 1 = Bridge+Mid, 2 = Mid, 3 = Neck+Mid, 4 = Neck, 5 = Bridge+Neck
        int   pickupType     = 0; // 0 = Single-Coil, 1 = Humbucker
        float toneKnob       = 1.0f;
        bool  cabAirFilter   = false;
    };

    static const std::vector<FactoryPreset>& getFactoryPresets() noexcept;
    static constexpr int kNumAcousticPresets = 8;
    static constexpr int kNumElectricPresets = 4;

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

    // -----------------------------------------------------------------------
    // Custom body IR (message thread). The IR audio is embedded in the plugin state, so projects
    // reopen with the same IR on any machine even if the original file has moved.
    // -----------------------------------------------------------------------
    /** Load an audio file (WAV/AIFF/FLAC/...) as the Custom IR. Returns an error message, or {} on success. */
    juce::String loadCustomIRFile(const juce::File& file);
    /** Plausibility check before loading: a user-facing warning if the file does not look like an IR, else {}. */
    juce::String getCustomIRFileWarning(const juce::File& file) const;
    void clearCustomIR();
    bool hasCustomIR() const;
    juce::String getCustomIRName() const;

    /** Index of the Custom IR choice in the "bodyModel" parameter. */
    static constexpr int kCustomIRBodyIndex = 4;

    SynthEngine& getSynth() noexcept { return synth; }
    const SynthEngine& getSynth() const noexcept { return synth; }

    // -----------------------------------------------------------------------
    // Parameter tree — public so the editor can attach to it
    // -----------------------------------------------------------------------
    juce::AudioProcessorValueTreeState apvts;

private:
    SynthEngine synth;
    int currentProgram = 0;

    // Custom IR copy (at most BodyResonance::kMaxCustomIRSeconds), guarded for message-thread use
    mutable juce::CriticalSection customIRLock;
    juce::AudioBuffer<float> customIRBuffer;
    double customIRSampleRate = 0.0;
    juce::String customIRName;
    std::atomic<bool> isPrepared { false };
    void applyCustomIRToEngine(bool prepareNow);

    // Fixed-capacity timestamped MIDI event queue for sample-accurate processing (no allocation)
    static constexpr int kMaxEventsPerSegment = 512;
    std::array<SynthEngine::Event, kMaxEventsPerSegment> eventQueue {};

    /** Build the parameter layout used to construct the APVTS. */
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HybridSynthProcessor)
};

