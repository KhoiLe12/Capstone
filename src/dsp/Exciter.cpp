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
    // Cutoff frequency maps from 2.5 kHz (warm fingertip) to 22.0 kHz (crisp fingernail/pick)
    const float b     = std::max(0.0f, std::min(brightness, 1.0f));
    const float fc    = 2500.f + b * 19500.f;
    const float omega = 2.f * kPi * fc / sampleRate;
    const float alpha = 1.f - std::exp(-omega);

    // Smooth forward 1-pole lowpass filter (preserves tactile harmonics)
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
    if (length <= 2) return;

    // Effective brightness dynamically tracks velocity (harder plucks have more high-end bite)
    const float clampedVel = std::max(0.01f, std::min(velocity, 1.0f));
    const float effBrightness = std::clamp(brightness * (0.35f + 0.65f * clampedVel), 0.05f, 1.0f);

    if (type == ExciterType::WHITE_NOISE)
    {
        for (int i = 0; i < length; ++i)
            outBuffer[i] = nextSample();
    }
    else
    {
        // 1. Physical Triangular Plucked String Displacement (d'Alembert wave equation)
        // String pulled aside at strike position M, falling linearly to 0 at both bridge and nut
        const float clampedPick = std::max(0.06f, std::min(pickPosition, 0.48f));
        int M = static_cast<int>(std::round(clampedPick * static_cast<float>(length)));
        M = std::max(1, std::min(M, length - 1));

        const float invM = 1.0f / static_cast<float>(M);
        const float invTail = 1.0f / static_cast<float>(length - M);

        for (int i = 0; i < length; ++i)
        {
            if (i <= M)
                outBuffer[i] = static_cast<float>(i) * invM;
            else
                outBuffer[i] = static_cast<float>(length - i) * invTail;
        }

        // 2. Physical Fingernail/Plectrum Slip Snap & Tactile Scrape Texture
        // When the string slips off the fingernail edge, it injects a high-frequency
        // velocity release snap right at the release point (producing realistic string texture)
        const int snapSamples = std::max(3, std::min(length / 4,
            static_cast<int>((0.0004f + (1.0f - clampedVel) * 0.0008f) * sampleRate)));
        const float snapStrength = (0.35f + 0.65f * clampedVel) * (0.4f + 0.6f * effBrightness);

        float lastNoise = 0.f;
        for (int k = 0; k < snapSamples; ++k)
        {
            const int idx = (M + k) % length;
            const float phase = static_cast<float>(k) / static_cast<float>(snapSamples);
            const float win = std::sin(kPi * phase);

            // Tactile high-frequency nylon/wound scrape (3 - 12 kHz)
            const float noise = nextSample();
            const float scrape = noise - 0.80f * lastNoise;
            lastNoise = noise;

            outBuffer[idx] += win * snapStrength * (0.55f + 0.45f * scrape);
        }
    }

    // Shape spectral brightness with single-pole filter
    applyBrightness(outBuffer, length, effBrightness, sampleRate);

    // Peak-normalise and apply dynamic velocity scaling
    float peak = 0.f;
    for (int i = 0; i < length; ++i)
        peak = std::max(peak, std::abs(outBuffer[i]));

    if (peak > 1e-6f)
    {
        // Natural dynamic velocity response (vel^1.25 gives rich expressive piano-to-forte range)
        const float velScale = 0.20f + 0.80f * std::pow(clampedVel, 1.25f);
        const float scale = (0.95f * velScale) / peak;
        for (int i = 0; i < length; ++i)
            outBuffer[i] *= scale;
    }
}
