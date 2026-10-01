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
    static constexpr int NUM_VOICES = 6; ///< 6 physical guitar strings: 0=E2, 1=A2, 2=D3, 3=G3, 4=B3, 5=E4

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
    static constexpr int KEYSWITCH_STRUM_8TH = 28; ///< E1 (MIDI Note 28 / FL: E2) 8th-Note Strum Keyswitch
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
    float paramEngineType  = 0.0f;    ///< String Engine Model (0 = Digital Waveguide, 1 = Bilbao FDTD DAFx24)
    float paramStereoWidth = 0.70f;   ///< Stereo soundboard / microphone imaging (0 = mono, 0.7 = natural, 1.0 = wide studio)
    bool  paramPalmMute    = false;   ///< Palm Mute DAW parameter / UI toggle
    bool  paramFullMute    = false;   ///< Full Mute DAW parameter / UI toggle
    bool  paramStrum       = false;   ///< Strum 8th DAW parameter / UI toggle

    void setBpm(float bpm) noexcept
    {
        if (bpm >= 20.f && bpm <= 400.f)
            currentBpm = bpm;
    }

    bool isStrumActive() const noexcept
    {
        return paramStrum || keyswitchStrumActive;
    }

    bool isStrumDownstroke() const noexcept
    {
        return lastStrumWasDown;
    }

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
    float         currentBpm = 120.0f;

    bool          keyswitchPalmMuteActive = false;
    bool          keyswitchFullMuteActive = false;
    bool          keyswitchStrumActive    = false;
    float         strumKeyswitchVelocity  = 0.8f;
    int           strumSampleCounter      = 0;
    bool          nextStrumIsDown         = true;
    bool          lastStrumWasDown        = true;
    KarplusStrong::MuteMode prevMuteMode  = KarplusStrong::MuteMode::Open;

    // Track physically held notes in guitar range
    float         heldNotes[128]          = { 0.f };

    // Pending scheduled plucks for inter-string strum rake
    static constexpr int MAX_PENDING_PLUCKS = 16;
    struct StrumPluck
    {
        int   midiNote     = -1;
        float velocity     = 0.f;
        float brightness   = 0.f;
        int   delaySamples = 0;
        bool  active       = false;
    };
    StrumPluck pendingPlucks[MAX_PENDING_PLUCKS];

    void triggerStrum(bool isDownstroke, float velOverride = -1.0f) noexcept;
    void schedulePluck(int midiNote, float velocity, float brightness, int delaySamples) noexcept;

    void syncVoiceMuteModes() noexcept;

    int assignStringForNote(int midiNote) const noexcept;
    int findFreeVoice() const noexcept;
    int findVoiceForNote(int midiNote) const noexcept;

    // 18 Hz Acoustic DC Blocker state (removes sub-bass DC bias)
    float dcX_L = 0.f;
    float dcY_L = 0.f;
    float dcX_R = 0.f;
    float dcY_R = 0.f;
};
