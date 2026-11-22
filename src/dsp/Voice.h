#pragma once
#include "Exciter.h"
#include "KarplusStrong.h"
#include "FdtdString.h"

/**
 * Voice — one self-contained plucked-string voice with:
 *   - Physical exciter (finite contact pulse with wound vs plain string textures)
 *   - 1D waveguide with sub-sample pitch tuning
 *   - Inharmonicity / stiffness dispersion
 *   - Kirchhoff-Carrier tension modulation (FDTD engine)
 *   - Multi-port bridge velocity coupling
 *   - Physical acoustic choke with hand slap & fret contact transient
 *   - Stefan Bilbao FDTD (DAFx24) physical wave equation string model
 */
class Voice
{
public:
    enum class EngineType
    {
        DigitalWaveguide = 0,
        BilbaoFdtd       = 1
    };

    enum class StringMaterial
    {
        NylonClassical = 0,
        SteelAcoustic  = 1
    };

    Voice() = default;

    /** Allocate KS delay line and FDTD buffers for this sample rate. Call once at startup. */
    void init(float sampleRate);

    /** Select string engine mode. */
    void setEngineType(EngineType type) noexcept { engineType = type; }
    EngineType getEngineType() const noexcept    { return engineType; }

    /** Select string material (Nylon Classical vs Steel Acoustic). */
    void setStringMaterial(int material) noexcept { stringMaterial = material; }
    int getStringMaterial() const noexcept        { return stringMaterial; }

    /** Physical guitar string index (0 = E2, 1 = A2, 2 = D3, 3 = G3, 4 = B3, 5 = E4). */
    void setPhysicalStringIndex(int idx) noexcept { physicalStringIndex = idx; }
    int getPhysicalStringIndex() const noexcept   { return physicalStringIndex; }

    /**
     * Trigger a new note.
     * @param midiNote       MIDI note number 0-127
     * @param velocity       Normalised velocity 0..1
     * @param brightness     Exciter brightness 0..1
     * @param pickPosition   Plectrum contact point, fraction of string length
     * @param stiffness      String stiffness / inharmonicity 0..1
     * @param muteMode       Articulation mode (Open, Palm, or Full mute dead notes)
     * @param stringIdx      Physical guitar string index (0..5)
     * @param stringMaterial String material (0 = Nylon Classical, 1 = Steel Acoustic)
     * @param geometryString String whose gauge/material is used for the sound (-1 = stringIdx).
     *                       Differs from stringIdx only for notes placed past the fretboard.
     */
    void noteOn(int midiNote, float velocity,
                float brightness, float pickPosition, float decay,
                float stiffness = 0.25f,
                KarplusStrong::MuteMode muteMode = KarplusStrong::MuteMode::Open,
                int stringIdx = -1,
                int stringMaterial = 0,
                int geometryString = -1);

    /**
     * Legato transition (Hammer-On or Pull-Off) without plectrum snap.
     * Retunes the active string while preserving existing acoustic momentum.
     */
    void legatoTransition(int newMidiNote, float newVelocity, bool isHammerOn, float stiffness = 0.25f) noexcept;

    /** Signal note release (string continues natural decay). */
    void noteOff() noexcept;

    /**
     * Physical acoustic choke with hand slap & fret contact transient.
     * @param chokeVelocity Velocity of the keyswitch press (scales slap transient intensity)
     */
    void choke(float chokeVelocity = 0.8f) noexcept;

    /** Articulation mode setters and getters. */
    void setMuteMode(KarplusStrong::MuteMode mode) noexcept;
    void setPalmMute(bool muted) noexcept { setMuteMode(muted ? KarplusStrong::MuteMode::Palm : KarplusStrong::MuteMode::Open); }
    void setFullMute(bool muted) noexcept { setMuteMode(muted ? KarplusStrong::MuteMode::Full : KarplusStrong::MuteMode::Open); }
    KarplusStrong::MuteMode getMuteMode() const noexcept { return currentMuteMode; }
    bool isPalmMuted() const noexcept { return currentMuteMode == KarplusStrong::MuteMode::Palm; }
    bool isFullMuted() const noexcept { return currentMuteMode == KarplusStrong::MuteMode::Full; }

    /** Advance one sample and return voice output at bridge. */
    float tick() noexcept;

    /** Advance one sample and return dual-axis bridge forces (vertical pumping V, lateral rocking H). */
    void tickPolar(float& outV, float& outH) noexcept;

    /**
     * Direct airborne plectrum radiation — the high-frequency pick click (~4–12 kHz burst)
     * that travels through open air from plectrum to microphone, bypassing the soundboard
     * body resonator entirely. Generates energy for ~8–14 ms following the pluck moment.
     * Call this in a separate pass AFTER body.processBlock() and sum directly into stereo out.
     */
    float tickPickClick() noexcept;

    bool  isActive()    const noexcept { return active; }
    int   getMidiNote() const noexcept { return midiNote; }
    int   getSamplesSinceNoteOn() const noexcept { return samplesSinceNoteOn; }
    float getEnergy()   const noexcept
    {
        float eng = (engineType == EngineType::BilbaoFdtd) ? fdtdString.getEnergy() : string.getEnergy();
        if (engineType == EngineType::BilbaoFdtd && (fdtdString.isPlectrumContactActive() || samplesSinceNoteOn < static_cast<int>(0.075f * sampleRate)))
            eng = std::max(eng, 1.0f);
        return eng * (releasing ? releaseGain : (choking ? chokeGain : 1.0f));
    }

    /** Hard-stop and reset. */
    void reset() noexcept;

private:
    Exciter       exciter;
    KarplusStrong string;
    FdtdString    fdtdString;

    EngineType    engineType = EngineType::DigitalWaveguide;
    int           stringMaterial = 0; // 0 = Nylon Classical, 1 = Steel Acoustic
    int           physicalStringIndex = -1;
    int           geometryStringIndex = -1; ///< String gauge used for the sound (see noteOn)

    int   midiNote   = -1;
    float velocity   = 1.f;
    bool  active     = false;
    float sampleRate = 44100.f;
    KarplusStrong::MuteMode currentMuteMode = KarplusStrong::MuteMode::Open;
    float currentDecay = 0.80f;

    // Smooth acoustic release envelope
    bool  releasing       = false;
    float releaseGain     = 1.0f;
    float releaseCoeff    = 0.999f;
    int   fadeSamplesLeft = -1;
    int   samplesSinceNoteOn = 0;

    // Physical acoustic choke & hand slap transient state
    bool  choking          = false;
    float chokeGain        = 1.0f;
    float chokeCoeff       = 0.99f;
    int   chokeSamplesLeft = 0;

    // Choke hand slap mechanical dipole impulse into bridge saddle
    bool     slapActive            = false;
    float    slapPulseAmp          = 0.0f;
    float    slapPulseSigma        = 48.0f;
    float    slapPulseCenter       = 40.0f;
    int      slapPulseElapsed      = 0;
    int      slapPulseDuration     = 0;

    // Direct airborne plectrum physical radiation (v50.f)
    // Physical elastodynamic model of acoustic dipole shock wave + dual flexural eigenmodes + wire ridge micro-texture
    float    pickClickAmp          = 0.0f;  ///< Peak amplitude of the snap
    int      pickClickSamplesLeft  = 0;     ///< Remaining samples for click lifespan
    int      pickClickElapsed      = 0;     ///< Elapsed samples since release

    // Acoustic dipole shock wave (Mexican Hat / Ricker wavelet pressure impulse)
    float    pickDipoleSigma       = 8.0f;  ///< Width of dipole shock wave in samples
    float    pickDipoleCenter      = 12.0f; ///< Peak center in samples

    // Flexural Resonator 1 (Plectrum body flex / fingernail plate mode)
    float    pickRes1_b0           = 0.0f;
    float    pickRes1_a1           = 0.0f;
    float    pickRes1_a2           = 0.0f;
    float    pickRes1_x1           = 0.0f;
    float    pickRes1_x2           = 0.0f;
    float    pickRes1_y1           = 0.0f;
    float    pickRes1_y2           = 0.0f;

    // Flexural Resonator 2 (Plectrum tip snap / keratin nail edge mode)
    float    pickRes2_b0           = 0.0f;
    float    pickRes2_a1           = 0.0f;
    float    pickRes2_a2           = 0.0f;
    float    pickRes2_x1           = 0.0f;
    float    pickRes2_x2           = 0.0f;
    float    pickRes2_y1           = 0.0f;
    float    pickRes2_y2           = 0.0f;

    // Wound-wire pre-release winding ridge micro-texture
    float    pickRidgeAmp          = 0.0f;
    float    pickRidgePhase        = 0.0f;
    float    pickRidgePhaseInc     = 0.0f;
    float    pickRidgeChirpRate    = 0.0f;
    int      pickRidgeDurSamples   = 0;

    // Airborne edge sizzle layer (8–16 kHz air bite & cavitation)
    float    pickEdgeHPState       = 0.0f;
    float    pickEdgeHPAlpha       = 0.65f;
    float    pickEdgeDecayRate     = 0.005f;

    uint32_t pickClickRng          = 0xC0FFEE17u; ///< Xorshift32 PRNG for organic roughness micro-texture

    static float midiToFreq(int note) noexcept;

    /**
     * Physical FDTD string parameters for a note on physical string s (shared by picked notes and
     * legato so both get identical tuning, damping, fret clearance and coupling). Also records the
     * note's open / palm damping and saddle brightness for later mute changes and legato.
     */
    FdtdString::StringParams makeFdtdParams(int note, int s, float stiffness, float decay, float brightness) noexcept;

    /** Pitch exponent of the FDTD frequency-independent damping sigma0 ~ (f / 82.4 Hz)^p. */
    static constexpr float kFdtdSteelRegisterExponent = 0.0f;
    static constexpr float kFdtdNylonRegisterExponent = 0.0f;
    float fdtdOpenSigma0 = 1.0f;
    float fdtdOpenSigma1 = 4.5e-4f;
    float fdtdBrightness = 0.60f;

    /**
     * FDTD ring-out detection. Uses the string's total (kinetic + tension-potential) energy, which
     * is smooth over each vibration cycle, relative to the note's own peak, checked every 32 samples.
     * Before v65.f a fixed threshold on displacement energy sum(u^2) was used: sum(u^2) dips towards
     * zero twice per cycle (all energy kinetic), and short treble strings hold little displacement,
     * so high notes were cut while loud (D6 nylon at -15 dB re. peak after 0.39 s; E4 at -38 dB).
     */
    static constexpr float kFdtdSilenceRatio = 1.0e-8f; ///< -80 dB re. the note's peak energy
    static constexpr int   kFdtdSilenceCheckInterval = 32;
    float fdtdPeakEnergy = 0.0f;
    int   fdtdSilenceCheckCounter = 0;
    bool fdtdIsSilent() noexcept
    {
        if (fdtdString.isPlectrumContactActive() || samplesSinceNoteOn < static_cast<int>(0.075f * sampleRate))
            return false;
        if (++fdtdSilenceCheckCounter < kFdtdSilenceCheckInterval)
            return false;
        fdtdSilenceCheckCounter = 0;
        const float energy = fdtdString.computeDiscreteEnergy();
        fdtdPeakEnergy = std::max(fdtdPeakEnergy, energy);
        if (fdtdPeakEnergy < 1e-9f)
            return false;
        return energy <= fdtdPeakEnergy * kFdtdSilenceRatio;
    }
    void resetFdtdSilenceReference() noexcept
    {
        fdtdPeakEnergy = fdtdString.computeDiscreteEnergy();
        fdtdSilenceCheckCounter = 0;
    }

    static constexpr float kSilenceThreshold = 1e-9f;

    static constexpr int kMaxExciterLength = 2216;
    float exciterScratch[kMaxExciterLength] = {};

    uint32_t humanRngState = 0x5a17e105u;
    float humanRand() noexcept
    {
        humanRngState ^= humanRngState << 13u;
        humanRngState ^= humanRngState >> 17u;
        humanRngState ^= humanRngState << 5u;
        return static_cast<float>(static_cast<int32_t>(humanRngState)) / static_cast<float>(0x7FFFFFFFu);
    }
};
