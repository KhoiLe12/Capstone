#pragma once
#include "Voice.h"
#include "BodyResonance.h"
#include "Fretboard.h"
#include "PickupModel.h"
#include "Strummer.h"

/**
 * SynthEngine — Complete Physical Guitar Engine:
 *   - 6 Physical Strings (E2, A2, D3, G3, B3, E4) x 22 Frets
 *   - Intelligent Fretboard Allocation (ergonomic hand position & monophonic strings)
 *   - Strummer Engine (time-staggered pick sweeping across strings)
 *   - Multi-Port Bridge Scattering Network (sympathetic resonance)
 *   - Virtual Magnetic Pickups (spatial comb filter Bridge <-> Neck + RLC tone circuit)
 *   - IRCAM Modalys 32-Mode Parallel Soundboard Model
 */
class SynthEngine
{
public:
    static constexpr int NUM_STRINGS = 6;

    SynthEngine() = default;

    /** Initialise all physical modules for the given sample rate. */
    void init(float sampleRate, int blockSize);

    /** Trigger a new note. */
    void noteOn(int midiNote, float velocity);

    /** Release a note. */
    void noteOff(int midiNote);

    /** Fill output buffers with synthesized audio. */
    void process(float* outputL, float* outputR, int numSamples) noexcept;

    /** Hard-reset all physical states. */
    void reset();

    // -------------------------------------------------------------------
    // Physical Parameters
    // -------------------------------------------------------------------
    float paramDecay       = 0.80f;   ///< String sustain (0..1)
    float paramBrightness  = 0.50f;   ///< Plectrum hardness (0..1)
    float paramPickPos     = 0.12f;   ///< Pick contact point (0.05..0.5)
    float paramStiffness   = 0.25f;   ///< Inharmonicity / metal stiffness (0..1)
    float paramBodySize    = 1.00f;   ///< Soundboard scale (0.6..1.8)
    float paramBodyMix     = 0.70f;   ///< Electric (0) vs. Acoustic (1)
    float paramMasterGain  = 0.80f;   ///< Master output level (0..1)
    float paramPickupPos   = 0.35f;   ///< Virtual pickup (0 = Bridge, 1 = Neck)
    float paramTone        = 0.85f;   ///< Guitar tone pot (0 = Dark rolled off, 1 = Open)
    float paramStrumSpeed  = 0.20f;   ///< Strum sweep speed (0 = Instant, 1 = Slow rake)

private:
    Voice         voices[NUM_STRINGS];
    BodyResonance body;
    Fretboard     fretboard;
    PickupModel   pickup;
    Strummer      strummer;

    float         sampleRate = 44100.f;
    float         lastBridgeReflections[NUM_STRINGS] = {};
};
