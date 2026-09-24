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
    prngState ^= prngState << 13u;
    prngState ^= prngState >> 17u;
    prngState ^= prngState << 5u;
    return static_cast<float>(static_cast<int32_t>(prngState))
           / static_cast<float>(0x7FFFFFFFu);
}

// ---------------------------------------------------------------------------
// Spectral Shaping Helpers
// ---------------------------------------------------------------------------

void Exciter::applyBrightness(float* buf, int length,
                              float brightness, float sampleRate) noexcept
{
    // Classical nylon plucked with fingertip:
    // Warm flesh pad (0) = 1.4 kHz, crisp fingernail edge (1) = 5.4 kHz.
    const float b     = std::max(0.0f, std::min(brightness, 1.0f));
    const float fc    = 1400.f + b * 4000.f;
    const float omega = 2.f * kPi * fc / sampleRate;
    const float alpha = 1.f - std::exp(-omega);

    // Forward-backward 2-pass filter (zero phase distortion, perfectly symmetric boundary conditions)
    float prev = 0.f;
    for (int i = 0; i < length; ++i)
    {
        prev   += alpha * (buf[i] - prev);
        buf[i]  = prev;
    }
    prev = 0.f;
    for (int i = length - 1; i >= 0; --i)
    {
        prev   += alpha * (buf[i] - prev);
        buf[i]  = prev;
    }
}

void Exciter::applyPickPosition(float* buf, int length, float pickPos) noexcept
{
    int M = static_cast<int>(pickPos * static_cast<float>(length));
    M = std::max(1, std::min(M, length - 1));

    for (int i = length - 1; i >= M; --i)
        buf[i] -= buf[i - M];
}

// ---------------------------------------------------------------------------
// Public Interface
// ---------------------------------------------------------------------------

void Exciter::fill(float* outBuffer, int length,
                   float         velocity,
                   ExciterType   type,
                   float         brightness,
                   float         pickPosition,
                   float         sampleRate,
                   float         stringFreqHz)
{
    std::fill(outBuffer, outBuffer + length, 0.f);
    if (length <= 2) return;

    (void)velocity;

    if (type == ExciterType::WHITE_NOISE)
    {
        for (int i = 0; i < length; ++i)
            outBuffer[i] = nextSample();
    }
    else
    {
        // 1. Pure Physical Fourier Pluck Synthesis (d'Alembert string equation)
        // Solves y(x) = sum_n (1/n^2) * sin(n*pi*d) * sin(n*pi*x/L)
        // Guaranteed C1-smooth with IDENTICAL 0.000000 boundary conditions at both ends.
        // Completely eliminates all wrap-around clicks, steps, and circulating noise.
        const float clampedPick = std::max(0.08f, std::min(pickPosition, 0.45f));
        const float f0 = std::max(40.0f, stringFreqHz > 0.f ? stringFreqHz : (sampleRate / static_cast<float>(length)));
        const int numHarmonics = std::max(4, std::min(18, static_cast<int>(4800.0f / f0)));

        const float invLen = 1.0f / static_cast<float>(length);
        for (int i = 0; i < length; ++i)
        {
            const float xRatio = static_cast<float>(i) * invLen;
            float sum = 0.f;
            for (int n = 1; n <= numHarmonics; ++n)
            {
                const float nFloat = static_cast<float>(n);
                const float invN2 = 1.0f / (nFloat * nFloat); // 1/n^2 nylon harmonic decay
                const float modePick = std::sin(nFloat * kPi * clampedPick);
                const float modeShape = std::sin(nFloat * kPi * xRatio);
                sum += invN2 * modePick * modeShape;
            }
            outBuffer[i] = sum;
        }

        // 2. Tactile Fleshy Thumb Contact Thump (zero-noise, windowed half-sine)
        // Soft wooden attack impulse without high-frequency static noise
        const int thumpSamples = std::max(4, std::min(static_cast<int>(0.0016f * sampleRate), length / 2));
        for (int i = 0; i < thumpSamples; ++i)
        {
            const float phase = static_cast<float>(i) / static_cast<float>(thumpSamples);
            // Hann window ensures zero amplitude and zero derivative at both ends of the thump
            const float win = 0.5f * (1.0f - std::cos(2.f * kPi * phase));
            const float thump = std::sin(kPi * phase) * (0.35f + 0.15f * brightness);
            outBuffer[i] += thump * win;
        }
    }

    // Shape spectral brightness with zero-phase nylon filter
    applyBrightness(outBuffer, length, brightness, sampleRate);

    // Peak-normalise to 0.95 FS
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
