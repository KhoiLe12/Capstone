#include "KarplusStrong.h"
#include <cmath>
#include <algorithm>

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------

void KarplusStrong::init(float sr)
{
    sampleRate = sr;
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

    // Map stiffness [0, 1] to dispersion allpass coefficient D in [-0.55, 0.0]
    // D = 0: pure harmonic; D < 0: higher partials travel faster (stiff metal string)
    const float clampedStiffness = std::max(0.0f, std::min(stiffness, 1.0f));
    dispCoeff = -0.55f * clampedStiffness;

    // DC group delay of the dispersion allpass filter: tau = (1 - D) / (1 + D)
    const float dispDelay = (1.0f - dispCoeff) / (1.0f + dispCoeff);

    // 1. Vertical Polarization (y-axis: perpendicular to soundboard, fundamental f0)
    const float totalDelayV = sampleRate / f0;
    float etaV = totalDelayV - 0.5f - dispDelay;
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
    float etaH = totalDelayH - 0.5f - dispDelay;
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

void KarplusStrong::updateLoopGains() noexcept
{
    // Physics-based frequency-calibrated loop gains:
    // tau is the exponential decay time constant in seconds.
    // Vertical polarization (soundboard saddle attack thump): 0.10s .. 0.28s
    const float tauV = 0.10f + currentDecay * 0.18f;
    // Horizontal polarization (singing sustain floor): 0.80s .. 3.20s
    const float tauH = 0.80f + currentDecay * 2.40f;

    const float f0 = std::max(60.0f, currentFreq);

    // loopGain = exp(-1 / (f0 * tau))
    // Calibrates decay time in seconds across all pitches from low E to high E
    loopGainV = std::min(0.985f, std::exp(-1.0f / (f0 * tauV)));
    loopGainH = std::min(0.9990f, std::exp(-1.0f / (f0 * tauH)));
}

// ---------------------------------------------------------------------------
// Trigger & Dynamic Tension (45-degree pluck decomposition)
// ---------------------------------------------------------------------------

void KarplusStrong::trigger(const float* exciterBuf, int length, float velocity)
{
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

    // Hard plucks increase initial tension (shortens effective string length)
    // Settle time corresponds to ~50-80 ms
    const float vel = std::max(0.0f, std::min(velocity, 1.0f));
    tensionOffset = 0.85f * (vel * vel);

    // Start energy estimate high so voice stealer does not immediately reclaim
    energyEstimate = 1.0f;
}

// ---------------------------------------------------------------------------
// Per-Sample Tick: 2D Waveguide + Cross-Polarization Bridge Sum
// ---------------------------------------------------------------------------

float KarplusStrong::tick() noexcept
{
    if (delayLengthV <= 0 || delayLengthH <= 0) return 0.f;

    // Dynamic tension modulation (pitch gliss on hard plucks)
    float currentApCoeffV = apCoeffV;
    float currentApCoeffH = apCoeffH;
    if (tensionOffset > 0.001f)
    {
        const float pitchOffset = tensionOffset * 0.15f;
        currentApCoeffV = std::min(0.75f, apCoeffV + pitchOffset);
        currentApCoeffH = std::min(0.75f, apCoeffH + pitchOffset);
        tensionOffset *= tensionDecay;
    }

    // --- 1. Vertical Waveguide (y-axis: perpendicular to soundboard) ---
    const float xV = delayLineV[static_cast<size_t>(writeHeadV)];
    const float avgV = 0.5f * (xV + avgPrevV);
    avgPrevV = xV;
    const float gainedV = avgV * loopGainV;

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
    const float avgH = 0.5f * (xH + avgPrevH);
    avgPrevH = xH;
    const float gainedH = avgH * loopGainH;

    const float dispOutH = dispCoeff * gainedH + dispPrevInH - dispCoeff * dispPrevOutH;
    dispPrevInH  = gainedH;
    dispPrevOutH = dispOutH;

    const float apOutH = currentApCoeffH * dispOutH + apPrevInH - currentApCoeffH * apPrevOutH;
    apPrevInH  = dispOutH;
    apPrevOutH = apOutH;

    delayLineH[static_cast<size_t>(writeHeadH)] = apOutH;
    writeHeadH = (writeHeadH + 1) % delayLengthH;

    // --- 3. Soundboard Bridge Summing ---
    // Vertical vibration directly drives the bridge (1.0).
    // Horizontal vibration couples into bridge rocking/soundboard motion (~0.22).
    const float bridgeSignal = xV + 0.22f * xH;

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
    tensionOffset  = 0.f;
    energyEstimate = 0.f;
}
