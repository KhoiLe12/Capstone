#pragma once
#include "BiquadFilter.h"

/**
 * BodyResonance — models acoustic guitar body coloration using a series
 * chain of peaking biquad filters tuned to known plate and air resonance modes.
 *
 * Physical basis:
 *   Real acoustic guitar bodies have several well-studied resonant modes
 *   (Helmholtz/air mode A0, top-plate dipole T(1,1), cross-dipole T(2,1), etc.)
 *   that collectively give the instrument its characteristic warm, woody timbre.
 *   Running biquads in series means each stage sees the spectrally-shaped output
 *   of the previous stage, producing cumulative tonal character similar to what
 *   a real acoustic body imparts on the string vibration it radiates.
 *
 * Usage:
 *   Call init() once, then pass the summed voice output through process()
 *   sample-by-sample.  Adjust dryMix/wetMix at runtime to blend in resonance.
 */
class BodyResonance
{
public:
    /** Number of resonant body modes modelled. */
    static constexpr int N_MODES = 8;

    BodyResonance() = default;

    /** Design all filter coefficients for the given sample rate. */
    void init(float sampleRate);

    /**
     * Process one sample through the body resonance chain.
     * Returns  dryMix*input + wetMix*(series-filtered output).
     */
    float process(float input) noexcept;

    /** Zero all filter state (call on transport stop / reset). */
    void reset() noexcept;

    float dryMix = 0.3f;   ///< Weight of unprocessed signal (0..1)
    float wetMix = 0.7f;   ///< Weight of body-filtered signal (0..1)

private:
    BiquadFilter filters[N_MODES];
};

