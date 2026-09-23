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

    /**
     * Set sustain (loop gain scaling).
     * decay = 0 -> very short staccato, 1 -> long singing sustain.
     */
    void setDecay(float decay) noexcept;

    /**
     * Seed the delay line with exciter content and trigger tension envelope.
     * @param exciterBuf Source buffer (length == delay-line size)
     * @param length     Number of samples to copy
     * @param velocity   Normalised velocity 0..1 (sets tension pitch gliss)
     */
    void trigger(const float* exciterBuf, int length, float velocity = 0.8f);

    /**
     * Advance one sample and return the string output at the bridge.
     * @param bridgeInjection Velocity/force scattered back from the bridge junction
     */
    float tick(float bridgeInjection = 0.f) noexcept;

    /** Zero delay line and all filter registers. */
    void reset() noexcept;

    /** Leaky RMS energy estimate — used by voice-stealer and active checks. */
    float getEnergy() const noexcept { return energyEstimate; }

private:
    float sampleRate   = 44100.f;
    float loopGain     = 0.9980f;

    std::vector<float> delayLine;
    int   writeHead    = 0;
    int   delayLength  = 0;

    // 1st-order averaging LPF state (high frequency damping per cycle)
    float avgPrev      = 0.f;

    // 1st-order allpass interpolator (fractional delay sub-sample tuning)
    //   y[n] = C*x[n] + x[n-1] - C*y[n-1]
    float apCoeff      = 0.f;
    float apPrevIn     = 0.f;
    float apPrevOut    = 0.f;

    // 1st-order allpass dispersion filter (stiffness / inharmonicity)
    //   y[n] = D*x[n] + x[n-1] - D*y[n-1]
    float dispCoeff    = 0.f;   ///< D in [-0.6, 0.0]
    float dispPrevIn   = 0.f;
    float dispPrevOut  = 0.f;

    // Dynamic tension modulation (pitch gliss on hard plucks)
    float tensionOffset = 0.f;
    float tensionDecay  = 0.9995f;

    float energyEstimate = 0.f;
};
