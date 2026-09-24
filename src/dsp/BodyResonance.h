#pragma once
#include "BiquadFilter.h"

/**
 * BodyResonance — IRCAM Modalys-style parallel modal soundboard model.
 *
 * Physical basis:
 *   An acoustic guitar body (top plate, back plate, ribs, and soundhole air cavity)
 *   is represented as a parallel sum of 32 distinct mechanical eigenmodes.
 *   Unlike an arbitrary EQ effect, each mode corresponds to a physical resonance
 *   with its own eigenfrequency, quality factor Q (damping), and modal coupling amplitude.
 *
 * Macro Controls:
 *   - bodySize:     Scales modal frequencies (0.7 = small parlor/mandolin, 1.0 = dreadnought, 1.4 = jumbo).
 *   - bodyDamping:  Scales mode Q factors (low = softer cedar/mahogany, high = resonant maple/spruce).
 *   - bodyCoupling: Controls bridge energy transfer (0.0 = rigid bridge solid-body electric with infinite sustain,
 *                   1.0 = responsive lightweight acoustic soundboard).
 */
class BodyResonance
{
public:
    static constexpr int N_MODES = 32;

    BodyResonance() = default;

    /** Design all modal filter coefficients for the current sample rate and macro parameters. */
    void init(float sampleRate, float bodySize = 1.0f, float bodyDamping = 1.0f);

    /** Update macro parameters dynamically without clicks. */
    void setParameters(float bodySize, float bodyDamping, float bodyCoupling);

    /**
     * Process one sample of stereo bridge excitation through the parallel modal bank.
     * Radiates true acoustic soundboard velocity in stereo.
     */
    void processStereo(float bridgeForceL, float bridgeForceR, float& outL, float& outR) noexcept;

    /** Mono convenience overload. */
    float process(float bridgeForce) noexcept
    {
        float l = 0.f, r = 0.f;
        processStereo(bridgeForce, bridgeForce, l, r);
        return 0.5f * (l + r);
    }

    /** Zero all filter states. */
    void reset() noexcept;

    float getBodyCoupling() const noexcept { return currentCoupling; }

private:
    BiquadFilter filters[N_MODES];
    float sampleRate      = 44100.f;
    float currentSize     = 1.0f;
    float currentDamping  = 1.0f;
    float currentCoupling = 0.7f;

    // Cross-plate soundboard acoustic diffusion (Haas delay ~0.3ms)
    static constexpr int CROSS_DELAY_LEN = 32;
    float crossDelayL[CROSS_DELAY_LEN] = {};
    float crossDelayR[CROSS_DELAY_LEN] = {};
    int   crossDelayIdx = 0;

    void updateFilters();
};
