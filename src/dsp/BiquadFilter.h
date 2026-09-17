#pragma once

/**
 * BiquadFilter — Direct Form I second-order IIR filter.
 *
 * Supports arbitrary coefficient loading and a convenience
 * resonator (peaking EQ) design method suitable for guitar
 * body modal synthesis.
 */
class BiquadFilter
{
public:
    BiquadFilter() = default;

    /**
     * Design a peaking resonator centred at freqHz with bandwidth
     * controlled by Q and peak gain gainDB (positive = boost).
     * Must be called whenever the sample rate or target frequency changes.
     */
    void setResonator(float freqHz, float Q, float gainDB, float sampleRate);

    /** Load raw biquad coefficients directly (b0, b1, b2, a1, a2 normalised). */
    void setCoefficients(float b0, float b1, float b2, float a1, float a2) noexcept;

    /** Process one input sample and return the filtered output. */
    float process(float x) noexcept;

    /** Zero all internal state registers without touching coefficients. */
    void reset() noexcept;

private:
    // Feed-forward coefficients
    float b0 = 0.f, b1 = 0.f, b2 = 0.f;
    // Feedback coefficients (sign convention: y[n] = ... - a1*y1 - a2*y2)
    float a1 = 0.f, a2 = 0.f;
    // Direct Form I state registers
    float x1 = 0.f, x2 = 0.f;   // delayed inputs
    float y1 = 0.f, y2 = 0.f;   // delayed outputs
};

