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
    delayLine.assign(static_cast<size_t>(maxLen), 0.f);
    reset();
}

// ---------------------------------------------------------------------------
// Parameter Setters & Pitch Calibration
// ---------------------------------------------------------------------------

void KarplusStrong::setFrequency(float freqHz, float stiffness)
{
    // Clamp frequency to reasonable guitar range (20 Hz to Nyquist / 3)
    const float f0 = std::max(20.0f, std::min(freqHz, sampleRate * 0.35f));

    // Map stiffness [0, 1] to dispersion allpass coefficient D in [-0.55, 0.0]
    // D = 0: pure harmonic; D < 0: higher partials travel faster (stiff metal string)
    const float clampedStiffness = std::max(0.0f, std::min(stiffness, 1.0f));
    dispCoeff = -0.55f * clampedStiffness;

    // DC group delay of the dispersion allpass filter: tau = (1 - D) / (1 + D)
    const float dispDelay = (1.0f - dispCoeff) / (1.0f + dispCoeff);

    // Total desired loop delay in samples: T = fs / f0
    const float totalDelay = sampleRate / f0;

    // Subtract 1st-order averaging LPF delay (~0.5 samples) and dispersion delay
    float eta = totalDelay - 0.5f - dispDelay;
    if (eta < 2.0f) eta = 2.0f;

    delayLength = static_cast<int>(eta);
    float frac = eta - static_cast<float>(delayLength); // in [0, 1)

    // Keep fractional delay away from 0 so allpass pole does not land on the unit circle (z = -1)
    if (frac < 0.2f && delayLength > 2)
    {
        delayLength -= 1;
        frac += 1.0f;
    }

    // Ensure circular buffer has sufficient capacity
    if (delayLength + 8 > static_cast<int>(delayLine.size()))
        delayLine.resize(static_cast<size_t>(delayLength) + 32, 0.f);

    // Allpass coefficient: C = (1 - frac) / (1 + frac)
    apCoeff = (1.0f - frac) / (1.0f + frac);
}

void KarplusStrong::setDecay(float decay) noexcept
{
    // Map decay 0..1 to loop gain 0.980..0.9985
    // Strictly bound below 1.0 to preserve numerical passivity
    const float d = std::max(0.0f, std::min(decay, 1.0f));
    loopGain = 0.980f + d * 0.0185f;
}

// ---------------------------------------------------------------------------
// Trigger & Dynamic Tension
// ---------------------------------------------------------------------------

void KarplusStrong::trigger(const float* exciterBuf, int length, float velocity)
{
    reset();

    const int copyLen = std::min(length, delayLength);
    for (int i = 0; i < copyLen; ++i)
        delayLine[static_cast<size_t>(i)] = exciterBuf[i];

    writeHead = 0;

    // Hard plucks increase initial tension (shortens effective string length)
    // Settle time corresponds to ~50-80 ms
    const float vel = std::max(0.0f, std::min(velocity, 1.0f));
    tensionOffset = 0.85f * (vel * vel);

    // Start energy estimate high so voice stealer does not immediately reclaim
    energyEstimate = 1.0f;
}

// ---------------------------------------------------------------------------
// Per-Sample Tick with Multi-Port Bridge Coupling
// ---------------------------------------------------------------------------

float KarplusStrong::tick(float bridgeInjection) noexcept
{
    if (delayLength <= 0) return 0.f;

    // 1. Read the wave currently arriving at the bridge
    const float x = delayLine[static_cast<size_t>(writeHead)];

    // 2. Averaging lowpass filter (high-frequency air and internal friction loss)
    const float averaged = 0.5f * (x + avgPrev);
    avgPrev = x;

    // 3. Loop gain (overall sustain)
    const float gained = averaged * loopGain;

    // 4. Stiffness dispersion allpass filter (inharmonicity)
    //    y[n] = D*x[n] + x[n-1] - D*y[n-1]
    const float dispOut = dispCoeff * gained + dispPrevIn - dispCoeff * dispPrevOut;
    dispPrevIn  = gained;
    dispPrevOut = dispOut;

    // 5. Dynamic tension modulation (adjust allpass coefficient slightly on pluck onset)
    float currentApCoeff = apCoeff;
    if (tensionOffset > 0.001f)
    {
        // Pitch shift: decrease fractional delay -> increase C (clamped to prevent Nyquist resonance)
        currentApCoeff = std::min(0.75f, apCoeff + tensionOffset * 0.15f);
        tensionOffset *= tensionDecay;
    }

    // 6. Sub-sample pitch tuning allpass filter
    //    y[n] = C*x[n] + x[n-1] - C*y[n-1]
    const float apOut = currentApCoeff * dispOut + apPrevIn - currentApCoeff * apPrevOut;
    apPrevIn  = dispOut;
    apPrevOut = apOut;

    // 7. Inject bridge motion (scattered reflection from mutual string coupling)
    //    Passive coupling: bridge motion reflection loss ensures ||S|| < 1.0 (strictly dissipative)
    constexpr float kBridgeCouplingLoss = 0.003f;
    const float toWrite = (1.0f - kBridgeCouplingLoss) * apOut + bridgeInjection;

    // 8. Write filtered, coupled sample back into delay line
    delayLine[static_cast<size_t>(writeHead)] = toWrite;
    writeHead = (writeHead + 1) % delayLength;

    // 9. Update leaky RMS energy estimate
    energyEstimate = 0.9999f * energyEstimate + 0.0001f * x * x;

    return x;
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

void KarplusStrong::reset() noexcept
{
    std::fill(delayLine.begin(), delayLine.end(), 0.f);
    writeHead      = 0;
    avgPrev        = 0.f;
    apPrevIn       = 0.f;
    apPrevOut      = 0.f;
    dispPrevIn     = 0.f;
    dispPrevOut    = 0.f;
    tensionOffset  = 0.f;
    energyEstimate = 0.f;
}
