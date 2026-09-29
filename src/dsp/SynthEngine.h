#pragma once
#include "Voice.h"
#include "BodyResonance.h"

/**
 * SynthEngine — Multi-Port Digital Waveguide Orchestrator & Modal Body Coupler.
 *
 * Architecture:
 *   - 12 polyphonic voices with physical string differentiation (wound vs plain)
 *   - Energy-conserving Multi-Port Bridge Scattering Junction
 *   - IRCAM Modalys-style 32-mode parallel soundboard model + IR engine
 *   - Articulations: Palm Muting (C1 / FL: C2), Full Muting & Fret Slap Choke (D1 / FL: D2)
 */
class SynthEngine
{
public:
    static constexpr int NUM_VOICES = 12;

    SynthEngine() = default;

    /** Initialise all voices and body resonance for the given sample rate. */
    void init(float sampleRate, int blockSize);

    /** Trigger a new note (MIDI note number + normalised velocity 0..1). */
    void noteOn(int midiNote, float velocity);

    /** Release a note (string continues to decay naturally). */
    void noteOff(int midiNote);

    /**
     * Fill outputL and outputR with numSamples of synthesised audio.
     * Output is mono-summed to both channels with stereo spatial spreading.
     */
    void process(float* outputL, float* outputR, int numSamples) noexcept;

    /** Hard-reset all voices and body resonance. */
    void reset();

    BodyResonance& getBodyResonance() noexcept { return body; }

    // -------------------------------------------------------------------
    // Physical Parameters & Articulations — set by PluginProcessor
    // -------------------------------------------------------------------
    static constexpr int KEYSWITCH_PALM_MUTE = 24; ///< C1 (MIDI Note 24 / FL: C2) Hold Keyswitch
    static constexpr int KEYSWITCH_FULL_MUTE = 26; ///< D1 (MIDI Note 26 / FL: D2) Hold/Choke Keyswitch
    static constexpr int GUITAR_MIN_NOTE     = 38; ///< D2 (Standard Drop-D lowest note; standard E2 is 40)
    static constexpr int GUITAR_MAX_NOTE     = 86; ///< D6 (22nd fret high E string)

    float paramDecay       = 0.80f;   ///< String sustain (0..1)
    float paramBrightness  = 0.60f;   ///< Exciter brightness / nail polish (0..1)
    float paramPickPos     = 0.13f;   ///< Pick/finger strike position (0.05..0.5)
    float paramBodyMix     = 0.70f;   ///< Body coupling (0 = solid electric, 1 = acoustic)
    float paramMasterGain  = 0.80f;   ///< Output master level (0..1)
    float paramStiffness   = 0.05f;   ///< Inharmonicity / acoustic string dispersion (0..1)
    float paramBodySize    = 1.00f;   ///< Soundboard size / scale (0.6..1.8)
    float paramBodyType    = 0.0f;    ///< Body Model (0 = Classical Nylon IR, 1 = Gibson Acoustic IR, 2 = Modal Bank)
    bool  paramPalmMute    = false;   ///< Palm Mute DAW parameter / UI toggle
    bool  paramFullMute    = false;   ///< Full Mute DAW parameter / UI toggle

    KarplusStrong::MuteMode getEffectiveMuteMode() const noexcept
    {
        if (paramFullMute || keyswitchFullMuteActive)
            return KarplusStrong::MuteMode::Full;
        if (paramPalmMute || keyswitchPalmMuteActive)
            return KarplusStrong::MuteMode::Palm;
        return KarplusStrong::MuteMode::Open;
    }

    bool isPalmMuteActive() const noexcept
    {
        return (paramPalmMute || keyswitchPalmMuteActive) && !isFullMuteActive();
    }

    bool isFullMuteActive() const noexcept
    {
        return paramFullMute || keyswitchFullMuteActive;
    }

private:
    Voice         voices[NUM_VOICES];
    BodyResonance body;
    float         sampleRate = 44100.f;

    bool          keyswitchPalmMuteActive = false;
    bool          keyswitchFullMuteActive = false;
    KarplusStrong::MuteMode prevMuteMode  = KarplusStrong::MuteMode::Open;

    void syncVoiceMuteModes() noexcept;

    int findFreeVoice() const noexcept;
    int findVoiceForNote(int midiNote) const noexcept;

    // 18 Hz Acoustic DC Blocker state (removes sub-bass DC bias)
    float dcX_L = 0.f;
    float dcY_L = 0.f;
    float dcX_R = 0.f;
    float dcY_R = 0.f;
};
