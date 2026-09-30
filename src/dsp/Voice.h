#pragma once
#include "Exciter.h"
#include "KarplusStrong.h"
#include "FdtdString.h"

/**
 * Voice — one self-contained plucked-string voice with:
 *   - Physical exciter (finite contact pulse with wound vs plain string textures)
 *   - 1D waveguide with sub-sample pitch tuning
 *   - Inharmonicity / stiffness dispersion
 *   - Dynamic tension modulation
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

    Voice() = default;

    /** Allocate KS delay line and FDTD buffers for this sample rate. Call once at startup. */
    void init(float sampleRate);

    /** Select string engine mode. */
    void setEngineType(EngineType type) noexcept { engineType = type; }
    EngineType getEngineType() const noexcept    { return engineType; }

    /** Physical guitar string index (0 = E2, 1 = A2, 2 = D3, 3 = G3, 4 = B3, 5 = E4). */
    void setPhysicalStringIndex(int idx) noexcept { physicalStringIndex = idx; }
    int getPhysicalStringIndex() const noexcept   { return physicalStringIndex; }

    /**
     * Trigger a new note.
     * @param midiNote     MIDI note number 0-127
     * @param velocity     Normalised velocity 0..1
     * @param brightness   Exciter brightness 0..1
     * @param pickPosition Plectrum contact point, fraction of string length
     * @param decay        KS loop-gain decay 0..1
     * @param stiffness    String stiffness / inharmonicity 0..1
     * @param muteMode     Articulation mode (Open, Palm, or Full mute dead notes)
     */
    void noteOn(int midiNote, float velocity,
                float brightness, float pickPosition, float decay,
                float stiffness = 0.25f,
                KarplusStrong::MuteMode muteMode = KarplusStrong::MuteMode::Open);

    /** Signal note release (string continues natural decay). */
    void noteOff() noexcept;

    /**
     * Physical acoustic choke with hand slap & fret contact transient.
     * @param chokeVelocity Velocity of the keyswitch press (scales slap transient intensity)
     */
    void choke(float chokeVelocity = 0.8f) noexcept;

    /** Articulation mode setters and getters. */
    void setMuteMode(KarplusStrong::MuteMode mode) noexcept { string.setMuteMode(mode); }
    void setPalmMute(bool muted) noexcept                   { string.setPalmMute(muted); }
    void setFullMute(bool muted) noexcept                   { string.setFullMute(muted); }
    KarplusStrong::MuteMode getMuteMode() const noexcept    { return string.getMuteMode(); }
    bool isPalmMuted() const noexcept                       { return string.getMuteMode() == KarplusStrong::MuteMode::Palm; }
    bool isFullMuted() const noexcept                       { return string.getMuteMode() == KarplusStrong::MuteMode::Full; }

    /** Advance one sample and return voice output at bridge. */
    float tick() noexcept;

    bool  isActive()    const noexcept { return active; }
    int   getMidiNote() const noexcept { return midiNote; }
    float getEnergy()   const noexcept
    {
        const float eng = (engineType == EngineType::BilbaoFdtd) ? fdtdString.getEnergy() : string.getEnergy();
        return eng * (releasing ? releaseGain : (choking ? chokeGain : 1.0f));
    }

    /** Hard-stop and reset. */
    void reset() noexcept;

private:
    Exciter       exciter;
    KarplusStrong string;
    FdtdString    fdtdString;

    EngineType    engineType = EngineType::DigitalWaveguide;
    int           physicalStringIndex = -1;

    int   midiNote   = -1;
    float velocity   = 1.f;
    bool  active     = false;
    float sampleRate = 44100.f;

    // Smooth acoustic release envelope
    bool  releasing       = false;
    float releaseGain     = 1.0f;
    float releaseCoeff    = 0.999f;
    int   fadeSamplesLeft = -1;

    // Physical acoustic choke & hand slap transient state
    bool  choking          = false;
    float chokeGain        = 1.0f;
    float chokeCoeff       = 0.99f;
    int   chokeSamplesLeft = 0;

    // Dual-component acoustic slap:
    // 1. Low-end soundboard cavity thud (A0 Helmholtz / lower bout mode)
    float slapThudAmp      = 0.f;
    float slapThudPhase    = 0.f;
    float slapThudPhaseInc = 0.f;
    float slapThudDecay    = 0.99f;

    // 2. Fret-wire contact click (snappy metal/wood contact)
    float slapClickAmp      = 0.f;
    float slapClickPhase    = 0.f;
    float slapClickPhaseInc = 0.f;
    float slapClickDecay    = 0.99f;

    static float midiToFreq(int note) noexcept;
    static constexpr float kSilenceThreshold = 1e-9f;

    static constexpr int kMaxExciterLength = 2216;
    float exciterScratch[kMaxExciterLength] = {};
};
