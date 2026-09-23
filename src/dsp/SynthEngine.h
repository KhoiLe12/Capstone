#pragma once
#include "Voice.h"
#include "BodyResonance.h"

/**
 * SynthEngine — Multi-Port Digital Waveguide Orchestrator & Modal Body Coupler.
 *
 * Architecture:
 *   - 6 polyphonic voices with stiffness dispersion & dynamic tension
 *   - Energy-conserving Multi-Port Bridge Scattering Junction:
 *     Transfers downward string forces to the bridge and scatters mutual
 *     energy back into all strings (true physical sympathetic resonance).
 *   - IRCAM Modalys-style 32-mode parallel soundboard model.
 */
class SynthEngine
{
public:
    static constexpr int NUM_VOICES = 6;

    SynthEngine() = default;

    /** Initialise all voices and body resonance for the given sample rate. */
    void init(float sampleRate, int blockSize);

    /** Trigger a new note (MIDI note number + normalised velocity 0..1). */
    void noteOn(int midiNote, float velocity);

    /** Release a note (string continues to decay naturally). */
    void noteOff(int midiNote);

    /**
     * Fill outputL and outputR with numSamples of synthesised audio.
     * Output is mono-summed to both channels.
     */
    void process(float* outputL, float* outputR, int numSamples) noexcept;

    /** Hard-reset all voices and body resonance. */
    void reset();

    // -------------------------------------------------------------------
    // Physical Parameters — set by PluginProcessor
    // -------------------------------------------------------------------
    float paramDecay       = 0.80f;   ///< String sustain (0..1)
    float paramBrightness  = 0.50f;   ///< Exciter brightness (0..1)
    float paramPickPos     = 0.12f;   ///< Pick contact point (0.05..0.5)
    float paramBodyMix     = 0.70f;   ///< Body coupling (0 = solid electric, 1 = acoustic)
    float paramMasterGain  = 0.80f;   ///< Output master level (0..1)
    float paramStiffness   = 0.25f;   ///< Inharmonicity / metal stiffness (0..1)
    float paramBodySize    = 1.00f;   ///< Soundboard size / scale (0.6..1.8)

private:
    Voice         voices[NUM_VOICES];
    BodyResonance body;
    float         sampleRate = 44100.f;

    // Bridge scattering feedback memory for the 6 strings
    float lastBridgeReflections[NUM_VOICES] = {};

    int findFreeVoice() const noexcept;
    int findVoiceForNote(int midiNote) const noexcept;
};
