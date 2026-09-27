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
                   float         stringFreqHz)
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
        // 1. Register-dependent nylon string physics:
        // Wound bass strings (E2..D3: ~82 - 150 Hz) have silver-plated copper wire coils
        // producing tactile winding friction and wider slip, while treble strings (G3..E4+: 196+ Hz)
        // are plain glassy extruded nylon with crisp, instantaneous slip snap.
        const float bassFactor = std::clamp((196.0f - stringFreqHz) / (196.0f - 82.0f), 0.0f, 1.0f);

        // Subtle human stroke micro-variation: pick position jitter (+/- 1.5%) prevents robotic repetition
        const float pickJitter = 0.015f * nextSample();
        const float clampedPick = std::clamp(pickPosition + pickJitter, 0.06f, 0.48f);

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

        // 2. Physical Fingernail / Plectrum Slip Snap & Tactile Scrape Texture
        // Wound strings have longer slip over winding ridges; plain treble strings have crisp fast snap
        const float baseSnapSec = 0.00035f + bassFactor * 0.00045f + (1.0f - clampedVel) * 0.0005f;
        const int snapSamples = std::max(3, std::min(length / 3, static_cast<int>(baseSnapSec * sampleRate)));
        const float snapStrength = (0.40f + 0.60f * clampedVel) * (0.45f + 0.55f * effBrightness);

        // Wound strings exhibit more metallic silver friction scrape; plain strings have clean snappy pop
        const float scrapeMix = 0.35f + bassFactor * 0.45f;
        const float hfFilter  = 0.70f + bassFactor * 0.20f; // High-pass differentiation for winding ridges

        float lastNoise = 0.f;
        for (int k = 0; k < snapSamples; ++k)
        {
            const int idx = (M + k) % length;
            const float phase = static_cast<float>(k) / static_cast<float>(snapSamples);
            const float win = std::sin(kPi * phase);

            const float noise = nextSample();
            const float scrape = noise - hfFilter * lastNoise;
            lastNoise = noise;

            const float tactilePulse = (1.0f - scrapeMix) + scrapeMix * scrape;
            outBuffer[idx] += win * snapStrength * tactilePulse;
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
        // Natural dynamic velocity response (vel^1.15 gives rich expressive piano-to-forte range)
        const float velScale = 0.30f + 0.70f * std::pow(clampedVel, 1.15f);
        const float scale = (0.95f * velScale) / peak;
        for (int i = 0; i < length; ++i)
            outBuffer[i] *= scale;
    }
}
