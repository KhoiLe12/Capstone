#pragma once
#include <vector>

/**
 * KarplusStrong — Advanced 1-D digital waveguide with:
 *   - Sub-sample allpass pitch tuning
 *   - Inharmonicity / stiffness dispersion allpass filter (metallic steel twang)
 *   - Dynamic tension modulation (pitch settle on hard plucks)
 *   - Multi-port bridge velocity injection for sympathetic resonance
 *
 * Signal flow per tick(bridgeInjection):
 *
 *   delayLine[readHead] ──► Averaging LPF ──► Loop Gain (Decay)
 *                                │
 *                                ▼
 *                     Stiffness Dispersion Allpass (Inharmonicity)
 *                                │
 *                                ▼
 *                     Pitch-Tuning Allpass (+ Tension Modulation)
 *                                │
 *                                ▼
 *                      [ + bridgeInjection ] (Sympathetic coupling)
 *                                │
 *                                ▼
 *                      delayLine[writeHead]
 */
class KarplusStrong
{
public:
    KarplusStrong() = default;

    /** Allocate internal buffers for a given sample rate. Call once at startup. */
    void init(float sampleRate);

    /**
     * Set fundamental frequency and string stiffness (inharmonicity).
     * @param freqHz    Fundamental pitch in Hz
     * @param stiffness 0 = pure harmonic nylon, 1 = stiff metallic steel
     */
    void setFrequency(float freqHz, float stiffness = 0.25f);

    /** String articulation damping modes. */
    enum class MuteMode {
        Open = 0,
        Palm = 1,
        Full = 2
    };

    /** Set sustain (loop gain scaling).
     * decay = 0 -> very short staccato, 1 -> long singing sustain.
     */
    void setDecay(float decay) noexcept;

    /** Set mute mode (Open, Palm, or Full). */
    void setMuteMode(MuteMode mode) noexcept;

    /** Enable or disable palm muting on the string. */
    void setPalmMute(bool muted) noexcept;

    /** Enable or disable full muting / dead notes on the string. */
    void setFullMute(bool muted) noexcept;

    MuteMode getMuteMode() const noexcept { return muteMode; }
    float getWoundFactor() const noexcept { return woundFactor; }

    /**
     * Seed the delay line with exciter content and trigger tension envelope.
     * @param exciterBuf Source buffer (length == delay-line size)
     * @param length     Number of samples to copy
     * @param velocity   Normalised velocity 0..1 (sets tension pitch gliss)
     */
    void trigger(const float* exciterBuf, int length, float velocity = 0.8f);

    /** Advance one sample and return the string output at the bridge. */
    float tick() noexcept;

    /** Apply physical finger/palm damping on note release. */
    void damp() noexcept
    {
        // Kept no-op inside the circulating delay line: instantaneous loopGain
        // changes inject step discontinuities that circulate and crackle.
        // Note-off release is cleanly handled by Voice's acoustic release envelope.
    }

    /** Zero delay lines and all filter registers. */
    void reset() noexcept;

    /** Leaky RMS energy estimate — used by voice-stealer and active checks. */
    float getEnergy() const noexcept { return energyEstimate; }

    float sampleRate      = 44100.f;
    float currentFreq     = 196.0f;
    float currentDecay    = 0.80f;
    float currentVelocity = 0.80f;
    float woundFactor     = 0.0f;  ///< 1.0 = heavy wound bass string (E2), 0.0 = plain nylon treble (G3+)
    MuteMode muteMode     = MuteMode::Open;
    bool  isPalmMuted     = false;

    uint32_t humanRngState = 987654321u;
    float humanRand() noexcept
    {
        humanRngState ^= humanRngState << 13u;
        humanRngState ^= humanRngState >> 17u;
        humanRngState ^= humanRngState << 5u;
        return static_cast<float>(static_cast<int32_t>(humanRngState)) / static_cast<float>(0x7FFFFFFFu);
    }

    void updateLoopGains() noexcept;

    // Double-decay loop gains (Vertical = fast attack, Horizontal = singing sustain)
    float loopGainV  = 0.965f;
    float loopGainH  = 0.994f;

    // Base loss coefficients before palm damping
    float baseSCoeffV = 0.32f;
    float baseSCoeffH = 0.25f;

    // Nylon string viscoelastic loss filter coefficients S in [0.05, 0.48]
    // y[n] = (1 - S) * x[n] + S * x[n-1]
    // Both vertical and horizontal coefficients are frequency-adaptive to warmly
    // damp high harmonics at low pitches while keeping treble notes crisp.
    float sCoeffV_computed = 0.32f;
    float sCoeffH_computed = 0.25f;

    // --- Vertical Polarization (y: perpendicular to top plate) ---
    std::vector<float> delayLineV;
    int   writeHeadV   = 0;
    int   delayLengthV = 0;
    float avgPrevV     = 0.f;
    float apCoeffV     = 0.f;
    float apPrevInV    = 0.f;
    float apPrevOutV   = 0.f;
    float dispPrevInV  = 0.f;
    float dispPrevOutV = 0.f;

    // --- Horizontal Polarization (x: parallel to top plate) ---
    std::vector<float> delayLineH;
    int   writeHeadH   = 0;
    int   delayLengthH = 0;
    float avgPrevH     = 0.f;
    float apCoeffH     = 0.f;
    float apPrevInH    = 0.f;
    float apPrevOutH   = 0.f;
    float dispPrevInH  = 0.f;
    float dispPrevOutH = 0.f;

    // Shared inharmonicity dispersion allpass coefficient (stiffness D)
    float dispCoeff    = 0.f;

    // Dynamic tension modulation (pitch gliss on hard plucks)
    float tensionOffset = 0.f;
    float tensionDecay  = 0.9995f;

    // Physical bridge force & saddle terminating impedance filter states
    float prevXV            = 0.f;
    float prevXH            = 0.f;
    float saddleFilterV     = 0.f;
    float saddleFilterH     = 0.f;
    float saddleBeta        = 0.f;
    float bridgeNormFactor  = 1.f;

    float energyEstimate = 0.f;
};
