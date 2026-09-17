#pragma once
#include "Voice.h"
#include "BodyResonance.h"

/**
 * SynthEngine — top-level DSP orchestrator.
 *
 * Manages a pool of NUM_VOICES independent plucked-string voices and routes
 * their summed output through the shared BodyResonance processor.
 *
 * Voice allocation policy:
 *   1. Look for an already-idle voice.
 *   2. If all voices are active, steal the voice with the lowest energy
 *      (the quietest currently-sounding string).
 *
 * This class is JUCE-agnostic; the PluginProcessor wrapper converts
 * juce::MidiBuffer events into noteOn()/noteOff() calls and passes
 * raw float* output buffers to process().
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
     * Output is mono-summed to both channels (mono instrument).
     */
    void process(float* outputL, float* outputR, int numSamples) noexcept;

    /** Hard-reset all voices and body resonance (e.g., on transport stop). */
    void reset();

    // -------------------------------------------------------------------
    // Parameters — set directly by the PluginProcessor each block
    // -------------------------------------------------------------------
    float paramDecay       = 0.80f;   ///< String sustain  (0..1)
    float paramBrightness  = 0.50f;   ///< Exciter brightness (0..1)
    float paramPickPos     = 0.12f;   ///< Pick position (0.05..0.5)
    float paramBodyMix     = 0.70f;   ///< Body resonance wet mix (0..1)
    float paramMasterGain  = 0.80f;   ///< Output master gain (0..1)

private:
    Voice         voices[NUM_VOICES];
    BodyResonance body;
    float         sampleRate = 44100.f;

    /**
     * Find the best voice index for the next note.
     * Returns an idle voice if available; otherwise steals the quietest.
     */
    int findFreeVoice() const noexcept;

    /** Return the index of the voice currently playing midiNote, or -1. */
    int findVoiceForNote(int midiNote) const noexcept;
};

