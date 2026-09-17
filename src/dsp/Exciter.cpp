#include "Exciter.h"
#include <cmath>
#include <algorithm>
#include <cstdint>

static constexpr float kPi = 3.14159265358979323846f;

// ---------------------------------------------------------------------------
// PRNG
// ---------------------------------------------------------------------------

float Exciter::nextSample() noexcept
{
    // xorshift32 — period 2^32 − 1, passes Diehard, safe on embedded targets
    prngState ^= prngState << 13u;
    prngState ^= prngState >> 17u;
    prngState ^= prngState << 5u;
    // Interpret bit pattern as signed, normalise to [-1, +1]
    return static_cast<float>(static_cast<int32_t>(prngState))
           / static_cast<float>(0x7FFFFFFFu);
}

// ---------------------------------------------------------------------------
// Spectral shaping helpers
// ---------------------------------------------------------------------------

void Exciter::applyBrightness(float* buf, int length,
                               float brightness, float sampleRate) noexcept
{
    // Single-pole IIR lowpass (bilinear approximation):
    //   y[n] = y[n-1] + alpha * (x[n] - y[n-1])
    //
    // Cutoff maps linearly: 0 → 2 kHz (dark),  1 → 20 kHz (bright)
    const float fc    = 2000.f + brightness * 18000.f;
    const float omega = 2.f * kPi * fc / sampleRate;
    // Exponential decay coefficient (keeps fc accurate for small angles)
    const float alpha = 1.f - std::exp(-omega);

    float prev = 0.f;
    for (int i = 0; i < length; ++i)
    {
        prev   += alpha * (buf[i] - prev);
        buf[i]  = prev;
    }
}

void Exciter::applyPickPosition(float* buf, int length, float pickPos) noexcept
{
    // Comb filter:  H(z) = 1 − z^{−M}
    //   y[i] = x[i] − x[i−M]
    //
    // Applied in-place, running backwards to avoid reading already-modified samples.
    int M = static_cast<int>(pickPos * static_cast<float>(length));
    M = std::max(1, std::min(M, length - 1));

    for (int i = length - 1; i >= M; --i)
        buf[i] -= buf[i - M];
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

void Exciter::fill(float* outBuffer, int length,
                   ExciterType   type,
                   float         brightness,
                   float         pickPosition,
                   float         sampleRate)
{
    // 1. Generate white noise burst
    for (int i = 0; i < length; ++i)
        outBuffer[i] = nextSample();

    // 2. Apply pick-position comb (plectrum model only)
    if (type == ExciterType::PLECTRUM_MODEL)
        applyPickPosition(outBuffer, length, pickPosition);

    // 3. Shape spectral brightness
    applyBrightness(outBuffer, length, brightness, sampleRate);

    // 4. Peak-normalise to 0.95 FS (prevent overload at delay-line input)
    float peak = 0.f;
    for (int i = 0; i < length; ++i)
        peak = std::max(peak, std::abs(outBuffer[i]));

    if (peak > 1e-6f)
    {
        const float scale = 0.95f / peak;
        for (int i = 0; i < length; ++i)
            outBuffer[i] *= scale;
    }
}

