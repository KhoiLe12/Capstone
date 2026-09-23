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
    // Cutoff maps: 0 -> 1.5 kHz (soft felt), 1 -> 20 kHz (hard pick)
    const float b     = std::max(0.0f, std::min(brightness, 1.0f));
    const float fc    = 1500.f + b * 18500.f;
    const float omega = 2.f * kPi * fc / sampleRate;
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
                   float         sampleRate)
{
    std::fill(outBuffer, outBuffer + length, 0.f);

    const float vel = std::max(0.05f, std::min(velocity, 1.0f));

    if (type == ExciterType::WHITE_NOISE)
    {
        for (int i = 0; i < length; ++i)
            outBuffer[i] = nextSample();
    }
    else
    {
        // Plectrum Model:
        // Contact duration is 1.0 ms (hard snap) to 3.0 ms (gentle finger/felt)
        const float contactDurationSec = 0.0010f + (1.0f - vel) * 0.0020f;
        int contactSamples = static_cast<int>(contactDurationSec * sampleRate);
        contactSamples = std::max(4, std::min(contactSamples, length));

        // Generate smooth Hann-shaped contact pulse + winding friction texture
        for (int i = 0; i < contactSamples; ++i)
        {
            const float phase = static_cast<float>(i) / static_cast<float>(contactSamples);
            const float pulse = std::sin(kPi * phase); // half-sine window
            const float envelope = pulse * pulse;      // Hann pulse shape

            // 70% smooth displacement pulse + 30% winding micro-friction noise
            outBuffer[i] = envelope * (0.70f + 0.30f * nextSample());
        }

        // Apply pick-position comb filtering
        applyPickPosition(outBuffer, length, pickPosition);
    }

    // Shape spectral brightness
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
