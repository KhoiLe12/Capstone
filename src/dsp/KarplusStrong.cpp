#include "KarplusStrong.h"
#include <cmath>
#include <algorithm>

static constexpr float kPi = 3.14159265358979323846f;

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------

void KarplusStrong::init(float sr)
{
    sampleRate = sr;
    saddleBeta = std::exp(-2.0f * kPi * 4500.0f / sampleRate);
    // Pre-allocate for lowest expected pitch (~20 Hz = 2205 samples at 44100)
    const int maxLen = static_cast<int>(sr / 20.f) + 32;
    delayLineV.assign(static_cast<size_t>(maxLen), 0.f);
    delayLineH.assign(static_cast<size_t>(maxLen), 0.f);
    reset();
}

// ---------------------------------------------------------------------------
// Parameter Setters & Pitch Calibration (2D Dual-Polarization)
// ---------------------------------------------------------------------------

void KarplusStrong::setFrequency(float freqHz, float stiffness)
{
    // Clamp frequency to reasonable guitar range (60 Hz drop-B to Nyquist * 0.35)
    const float f0 = std::max(60.0f, std::min(freqHz, sampleRate * 0.35f));
    bridgeNormFactor = sampleRate / (2.0f * kPi * f0);

    // Map stiffness [0, 1] to dispersion allpass coefficient D in [-0.015, 0.0]
    // Nylon strings have negligible inharmonicity compared to steel.
    // Taming dispersion eliminates artificial metallic banjo/harpsichord "twang",
    // restoring pure acoustic harmonic integers.
    // Wound strings (E2..D3: ~82 - 150 Hz) have silver-plated copper wire coils over nylon core.
    // Plain strings (G3..E4+: 196+ Hz) are smooth extruded rectified nylon.
    woundFactor = std::clamp((196.0f - f0) / (196.0f - 82.0f), 0.0f, 1.0f);

    const float clampedStiffness = std::max(0.0f, std::min(stiffness, 1.0f));
    dispCoeff = (-0.006f - 0.009f * woundFactor) * clampedStiffness;

    // Frequency-adaptive vertical and horizontal loss filter coefficients.
    // Low notes have long delay lines where high harmonics circulate many more times.
    // S in [0.16, 0.32] provides deep, full-bodied low-end fundamental warmth.
    {
        const float t = std::max(0.0f, std::min((f0 - 60.0f) / (500.0f - 60.0f), 1.0f));
        baseSCoeffV = 0.32f - t * (0.32f - 0.16f);   // 0.32 (deep warm bass) → 0.16 (treble chime)
        baseSCoeffH = 0.25f - t * (0.25f - 0.12f);   // 0.25 (solid fundamental) → 0.12 (singing sustain)
        if (muteMode == MuteMode::Full)
        {
            sCoeffV_computed = 0.35f + woundFactor * 0.12f;
            sCoeffH_computed = 0.35f + woundFactor * 0.12f;
        }
        else if (muteMode == MuteMode::Palm)
        {
            sCoeffV_computed = std::min(0.48f, baseSCoeffV * (1.2f + 0.3f * woundFactor));
            sCoeffH_computed = std::min(0.48f, baseSCoeffH * (1.2f + 0.3f * woundFactor));
        }
        else
        {
            sCoeffV_computed = baseSCoeffV;
            sCoeffH_computed = baseSCoeffH;
        }
    }

    // DC group delay of the dispersion allpass filter: tau = (1 - D) / (1 + D)
    const float dispDelay = (1.0f - dispCoeff) / (1.0f + dispCoeff);

    // 1. Vertical Polarization (y-axis: perpendicular to soundboard, fundamental f0)
    const float totalDelayV = sampleRate / f0;
    // DC group delay of 1st-order FIR loss filter (1 - S) + S * z^-1 is exactly S samples
    float etaV = totalDelayV - sCoeffV_computed - dispDelay;
    if (etaV < 2.0f) etaV = 2.0f;

    delayLengthV = static_cast<int>(etaV);
    float fracV = etaV - static_cast<float>(delayLengthV); // in [0, 1)

    // Keep fractional delay away from 0 so allpass pole does not land on unit circle (z = -1)
    if (fracV < 0.2f && delayLengthV > 2)
    {
        delayLengthV -= 1;
        fracV += 1.0f;
    }

    if (delayLengthV + 8 > static_cast<int>(delayLineV.size()))
        delayLineV.resize(static_cast<size_t>(delayLengthV) + 32, 0.f);

    apCoeffV = (1.0f - fracV) / (1.0f + fracV);

    // 2. Horizontal Polarization (x-axis: parallel to soundboard, anisotropic micro-detuning ~0.18 Hz)
    const float fH = f0 + 0.18f;
    const float totalDelayH = sampleRate / fH;
    float etaH = totalDelayH - sCoeffH_computed - dispDelay;
    if (etaH < 2.0f) etaH = 2.0f;

    delayLengthH = static_cast<int>(etaH);
    float fracH = etaH - static_cast<float>(delayLengthH); // in [0, 1)

    if (fracH < 0.2f && delayLengthH > 2)
    {
        delayLengthH -= 1;
        fracH += 1.0f;
    }

    if (delayLengthH + 8 > static_cast<int>(delayLineH.size()))
        delayLineH.resize(static_cast<size_t>(delayLengthH) + 32, 0.f);

    apCoeffH = (1.0f - fracH) / (1.0f + fracH);

    currentFreq = f0;
    updateLoopGains();
}

void KarplusStrong::setDecay(float decay) noexcept
{
    currentDecay = std::max(0.0f, std::min(decay, 1.0f));
    updateLoopGains();
}

void KarplusStrong::setMuteMode(MuteMode mode) noexcept
{
    if (muteMode != mode)
    {
        muteMode = mode;
        isPalmMuted = (muteMode == MuteMode::Palm);
        updateLoopGains();
    }
}

void KarplusStrong::setPalmMute(bool muted) noexcept
{
    setMuteMode(muted ? MuteMode::Palm : MuteMode::Open);
}

void KarplusStrong::setFullMute(bool muted) noexcept
{
    setMuteMode(muted ? MuteMode::Full : MuteMode::Open);
}

void KarplusStrong::updateLoopGains() noexcept
{
    const float f0 = std::max(60.0f, currentFreq);
    woundFactor = std::clamp((196.0f - f0) / (196.0f - 82.0f), 0.0f, 1.0f);

    if (muteMode == MuteMode::Full)
    {
        // Physical Full Mute / Dead Notes ("X" notes):
        // Fretting hand rests across strings.
        // Wound strings (E2..D3): Heavy mass inertia gives a resonant, woody "thump" (~10ms to 18ms decay, loopGain ~0.42).
        // Plain strings (G3..E4+): Light nylon gives a dry, snappy papery "chick" (~3ms to 6ms decay, loopGain ~0.22).
        const float tauV = (0.003f + 0.006f * woundFactor) + currentDecay * (0.003f + 0.006f * woundFactor);
        const float tauH = (0.004f + 0.007f * woundFactor) + currentDecay * (0.004f + 0.007f * woundFactor);

        const float maxGainV = 0.22f + 0.20f * woundFactor; // 0.22 (plain treble) -> 0.42 (wound bass)
        const float maxGainH = 0.26f + 0.20f * woundFactor; // 0.26 (plain treble) -> 0.46 (wound bass)

        loopGainV = std::min(maxGainV, std::exp(-1.0f / (f0 * tauV)));
        loopGainH = std::min(maxGainH, std::exp(-1.0f / (f0 * tauH)));

        // Saddle mechanical impedance cutoff:
        // Low wound strings: saddle cutoff drops to 550 Hz (deep, warm, woody thump)
        // Plain treble strings: saddle cutoff 2400 Hz (crisp, snappy transient click)
        const float saddleCutoff = 2400.0f - woundFactor * 1850.0f; // 550 Hz to 2400 Hz
        saddleBeta = std::exp(-2.0f * kPi * saddleCutoff / sampleRate);

        sCoeffV_computed = 0.35f + woundFactor * 0.12f;
        sCoeffH_computed = 0.35f + woundFactor * 0.12f;
    }
    else if (muteMode == MuteMode::Palm)
    {
        // Palm Muting:
        // Fleshy edge of hand rests on bridge saddles.
        // Wound bass strings have deep, chunky low-end "chug" (~55-90ms)
        // Plain treble strings have light, crisp acoustic "plink" (~25-45ms)
        const float tauV = (0.025f + 0.020f * woundFactor) + currentDecay * (0.035f + 0.035f * woundFactor);
        const float tauH = (0.035f + 0.025f * woundFactor) + currentDecay * (0.045f + 0.045f * woundFactor);

        loopGainV = std::min(0.92f, std::exp(-1.0f / (f0 * tauV)));
        loopGainH = std::min(0.95f, std::exp(-1.0f / (f0 * tauH)));

        const float saddleCutoff = 2200.0f - woundFactor * 800.0f; // 1400 Hz (bass) -> 2200 Hz (treble)
        saddleBeta = std::exp(-2.0f * kPi * saddleCutoff / sampleRate);

        sCoeffV_computed = std::min(0.48f, baseSCoeffV * (1.2f + 0.3f * woundFactor));
        sCoeffH_computed = std::min(0.48f, baseSCoeffH * (1.2f + 0.3f * woundFactor));
    }
    else
    {
        // Normal open string decay:
        const float tauV = 0.25f + currentDecay * 0.60f;
        const float tauH = 1.20f + currentDecay * 3.30f;

        loopGainV = std::min(0.992f, std::exp(-1.0f / (f0 * tauV)));
        loopGainH = std::min(0.9994f, std::exp(-1.0f / (f0 * tauH)));

        saddleBeta = std::exp(-2.0f * kPi * 4500.0f / sampleRate);

        sCoeffV_computed = baseSCoeffV;
        sCoeffH_computed = baseSCoeffH;
    }
}

// ---------------------------------------------------------------------------
// Trigger & Dynamic Tension (45-degree pluck decomposition)
// ---------------------------------------------------------------------------

void KarplusStrong::trigger(const float* exciterBuf, int length, float velocity)
{
    (void)velocity;
    reset();
    updateLoopGains();

    // Pluck angle ~45 degrees decomposes energy into orthogonal planes:
    // v_V = sin(45 deg) * exciter, v_H = cos(45 deg) * exciter
    constexpr float kPluckDecomp = 0.70710678f; // 1 / sqrt(2)

    const int copyLenV = std::min(length, delayLengthV);
    for (int i = 0; i < copyLenV; ++i)
        delayLineV[static_cast<size_t>(i)] = exciterBuf[i] * kPluckDecomp;

    const int copyLenH = std::min(length, delayLengthH);
    for (int i = 0; i < copyLenH; ++i)
        delayLineH[static_cast<size_t>(i)] = exciterBuf[i] * kPluckDecomp;

    writeHeadV = 0;
    writeHeadH = 0;

    // Dynamic tension modulation disabled for classical nylon string
    // to maintain rock-solid acoustic tuning and avoid laser/synthesizer pitch artifacts.
    tensionOffset = 0.f;

    // Start energy estimate high so voice stealer does not immediately reclaim
    energyEstimate = 1.0f;
}

// ---------------------------------------------------------------------------
// Per-Sample Tick: 2D Waveguide + Cross-Polarization Bridge Sum
// ---------------------------------------------------------------------------

float KarplusStrong::tick() noexcept
{
    if (delayLengthV <= 0 || delayLengthH <= 0) return 0.f;

    // Stable sub-sample allpass coefficients (zero chirp)
    const float currentApCoeffV = apCoeffV;
    const float currentApCoeffH = apCoeffH;

    // --- 1. Vertical Waveguide (y-axis: perpendicular to soundboard) ---
    const float xV = delayLineV[static_cast<size_t>(writeHeadV)];
    // Frequency-adaptive viscoelastic nylon loss filter (warmer at bass, crisper at treble)
    const float lossV = (1.0f - sCoeffV_computed) * xV + sCoeffV_computed * avgPrevV;
    avgPrevV = xV;
    const float gainedV = lossV * loopGainV;

    const float dispOutV = dispCoeff * gainedV + dispPrevInV - dispCoeff * dispPrevOutV;
    dispPrevInV  = gainedV;
    dispPrevOutV = dispOutV;

    const float apOutV = currentApCoeffV * dispOutV + apPrevInV - currentApCoeffV * apPrevOutV;
    apPrevInV  = dispOutV;
    apPrevOutV = apOutV;

    delayLineV[static_cast<size_t>(writeHeadV)] = apOutV;
    writeHeadV = (writeHeadV + 1) % delayLengthV;

    // --- 2. Horizontal Waveguide (x-axis: parallel to soundboard) ---
    const float xH = delayLineH[static_cast<size_t>(writeHeadH)];
    // Calibrated nylon loss filter (tames metallic buzz on lower strings)
    const float lossH = (1.0f - sCoeffH_computed) * xH + sCoeffH_computed * avgPrevH;
    avgPrevH = xH;
    const float gainedH = lossH * loopGainH;

    const float dispOutH = dispCoeff * gainedH + dispPrevInH - dispCoeff * dispPrevOutH;
    dispPrevInH  = gainedH;
    dispPrevOutH = dispOutH;

    const float apOutH = currentApCoeffH * dispOutH + apPrevInH - currentApCoeffH * apPrevOutH;
    apPrevInH  = dispOutH;
    apPrevOutH = apOutH;

    delayLineH[static_cast<size_t>(writeHeadH)] = apOutH;
    writeHeadH = (writeHeadH + 1) % delayLengthH;

    // --- 3. Soundboard Bridge Summing (Physical Bridge Force with Saddle Impedance) ---
    // Physical force on the soundboard saddle is proportional to string spatial slope at termination,
    // computed from velocity (dx/dt) normalized by (fs / 2pi f0).
    // The bone saddle termination introduces mechanical impedance, rolling off high frequencies
    // above 4.5 kHz to eliminate differentiator hash and prevent body mode clipping.
    const float velV = (xV - prevXV) * bridgeNormFactor;
    prevXV = xV;
    saddleFilterV = (1.0f - saddleBeta) * velV + saddleBeta * saddleFilterV;

    const float velH = (xH - prevXH) * bridgeNormFactor;
    prevXH = xH;
    saddleFilterH = (1.0f - saddleBeta) * velH + saddleBeta * saddleFilterH;

    // Direct vertical soundboard drive (1.0) + horizontal rocking saddle couple (~0.35)
    const float bridgeSignal = saddleFilterV + 0.35f * saddleFilterH;

    // Update leaky RMS energy estimate
    energyEstimate = 0.9999f * energyEstimate + 0.0001f * (bridgeSignal * bridgeSignal);

    return bridgeSignal;
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

void KarplusStrong::reset() noexcept
{
    std::fill(delayLineV.begin(), delayLineV.end(), 0.f);
    std::fill(delayLineH.begin(), delayLineH.end(), 0.f);
    writeHeadV     = 0;
    writeHeadH     = 0;
    avgPrevV       = 0.f;
    avgPrevH       = 0.f;
    apPrevInV      = 0.f;
    apPrevOutV     = 0.f;
    apPrevInH      = 0.f;
    apPrevOutH     = 0.f;
    dispPrevInV    = 0.f;
    dispPrevOutV   = 0.f;
    dispPrevInH    = 0.f;
    dispPrevOutH   = 0.f;
    prevXV         = 0.f;
    prevXH         = 0.f;
    saddleFilterV  = 0.f;
    saddleFilterH  = 0.f;
    tensionOffset  = 0.f;
    energyEstimate = 0.f;
}
