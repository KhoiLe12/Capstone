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
    // Cutoff maps: 0 -> 2.5 kHz (warm), 1 -> 22 kHz (crisp articulation)
    const float b     = std::max(0.0f, std::min(brightness, 1.0f));
    const float fc    = 2500.f + b * 19500.f;
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
                   float         sampleRate,
                   float         /*stringFreqHz*/)
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
        // Physical Plucked String Contact & Release Model:
        // Contact duration: 0.8 ms (hard strike) to 1.8 ms (soft strike)
        const float contactDurationSec = 0.0008f + (1.0f - vel) * 0.0010f;
        int contactSamples = static_cast<int>(contactDurationSec * sampleRate);
        contactSamples = std::max(6, std::min(contactSamples, length));

        // Asymmetric release point: 75% rise (displacement), 25% steep release snap
        const int releasePoint = static_cast<int>(contactSamples * 0.75f);

        float lastNoise = 0.f;

        for (int i = 0; i < contactSamples; ++i)
        {
            float displacement = 0.f;
            if (i <= releasePoint)
            {
                // Smooth rise ramp (string pulled aside)
                const float phase = static_cast<float>(i) / static_cast<float>(releasePoint);
                displacement = std::sin(0.5f * kPi * phase);
            }
            else
            {
                // Steep release edge (string slips free from nail/plectrum)
                const float phase = static_cast<float>(i - releasePoint) / static_cast<float>(contactSamples - releasePoint);
                displacement = std::cos(0.5f * kPi * phase);
            }

            // High-pass filtered micro-scrape (tactile string slip articulation: 3-10 kHz)
            const float noise = nextSample();
            const float scrape = noise - 0.85f * lastNoise;
            lastNoise = noise;

            // Release articulation burst is strongest during the steep slip
            const float scrapeWeight = (i >= releasePoint) ? 0.40f : 0.15f;

            outBuffer[i] = displacement * 0.75f + scrape * scrapeWeight * (0.8f + 0.6f * brightness);
        }

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
