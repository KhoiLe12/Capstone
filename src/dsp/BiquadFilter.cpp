#include "BiquadFilter.h"
#include <cmath>
#include <algorithm>

static constexpr float kPi = 3.14159265358979323846f;

// ---------------------------------------------------------------------------
// Coefficient design
// ---------------------------------------------------------------------------

void BiquadFilter::setResonator(float freqHz, float Q, float gainDB, float sampleRate)
{
    // Peaking EQ biquad (Audio EQ Cookbook, R. Bristow-Johnson)
    //
    //   H(s) has a peak of gainDB dB at freqHz, bandwidth controlled by Q.
    //   Formulas below produce Direct-Form-I coefficients normalised by a0.
    //
    const float omega   = 2.f * kPi * freqHz / sampleRate;
    const float sinW    = std::sin(omega);
    const float cosW    = std::cos(omega);
    const float A       = std::pow(10.f, gainDB / 40.f);   // linear amplitude
    const float alpha   = sinW / (2.f * Q);

    const float a0_inv  = 1.f / (1.f + alpha / A);

    b0 = (1.f + alpha * A) * a0_inv;
    b1 = (-2.f * cosW)     * a0_inv;
    b2 = (1.f - alpha * A) * a0_inv;
    a1 = (-2.f * cosW)     * a0_inv;
    a2 = (1.f - alpha / A) * a0_inv;
}

void BiquadFilter::setModalResonator(float freqHz, float Q, float linearGain, float sampleRate) noexcept
{
    // Clamp frequency safely below Nyquist
    const float maxFreq = 0.49f * sampleRate;
    const float f = std::max(20.0f, std::min(freqHz, maxFreq));
    const float q = std::max(0.5f, Q);

    const float omega = 2.f * kPi * f / sampleRate;
    const float sinW  = std::sin(omega);
    const float cosW  = std::cos(omega);
    const float alpha = sinW / (2.f * q);

    // Constant 0 dB peak gain bandpass, scaled by linearGain
    const float a0_inv = 1.f / (1.f + alpha);

    b0 = (alpha * linearGain) * a0_inv;
    b1 = 0.f;
    b2 = -b0;
    a1 = (-2.f * cosW) * a0_inv;
    a2 = (1.f - alpha) * a0_inv;
}

void BiquadFilter::setCoefficients(float b0_, float b1_, float b2_,
                                    float a1_, float a2_) noexcept
{
    b0 = b0_; b1 = b1_; b2 = b2_;
    a1 = a1_; a2 = a2_;
}

// ---------------------------------------------------------------------------
// Per-sample processing
// ---------------------------------------------------------------------------

float BiquadFilter::process(float x) noexcept
{
    // Direct Form I difference equation:
    //   y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2]
    //                  - a1*y[n-1] - a2*y[n-2]
    const float y = b0 * x + b1 * x1 + b2 * x2
                            - a1 * y1 - a2 * y2;
    // Shift state registers
    x2 = x1;  x1 = x;
    y2 = y1;  y1 = y;
    return y;
}

void BiquadFilter::reset() noexcept
{
    x1 = x2 = y1 = y2 = 0.f;
}

