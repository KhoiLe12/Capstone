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
    const int maxLen = static_cast<int>(sr / 20.f) + 8;
    delayLine.assign(static_cast<size_t>(maxLen), 0.f);
    reset();
}

// ---------------------------------------------------------------------------
// Parameter setters
// ---------------------------------------------------------------------------

void KarplusStrong::setFrequency(float freqHz)
{
    // Total desired loop delay (samples): T = fs / f0
    const float T = sampleRate / freqHz;

    // The 1st-order averaging filter contributes ~0.5 samples of delay.
    // Subtract that so the allpass only corrects the remainder.
    float eta = T - 0.5f;                   // target delay for integer + allpass
    delayLength = static_cast<int>(eta);     // floor → integer part

    // Grow buffer if needed (e.g., after a very low note)
    if (delayLength + 4 > static_cast<int>(delayLine.size()))
        delayLine.resize(static_cast<size_t>(delayLength) + 8, 0.f);

    // Fractional remainder that the allpass must provide
    float frac = eta - static_cast<float>(delayLength);   // in [0, 1)

    // 1st-order allpass group delay at DC = (1 − C) / (1 + C) = frac
    //  → C = (1 − frac) / (1 + frac)
    apCoeff = (1.f - frac) / (1.f + frac);
}

void KarplusStrong::setDecay(float decay) noexcept
{
    // Map 0..1 → loopGain 0.990..0.9999
    // (Higher gain = longer sustain because each loop loses less energy)
    loopGain = 0.990f + decay * 0.0099f;
}

// ---------------------------------------------------------------------------
// Trigger
// ---------------------------------------------------------------------------

void KarplusStrong::trigger(const float* exciterBuf, int length)
{
    reset();

    // Copy exciter content into delay line (clamp to delayLength)
    const int copyLen = std::min(length, delayLength);
    for (int i = 0; i < copyLen; ++i)
        delayLine[static_cast<size_t>(i)] = exciterBuf[i];

    writeHead = 0;   // start reading from the beginning of the seeded buffer

    // Start energy estimate HIGH so a freshly-triggered voice is never treated
    // as the "quietest" candidate by the voice stealer.  The leaky integrator
    // will naturally pull this value toward the actual signal level over time.
    energyEstimate = 1.0f;
}

// ---------------------------------------------------------------------------
// Per-sample tick
// ---------------------------------------------------------------------------

float KarplusStrong::tick() noexcept
{
    if (delayLength <= 0) return 0.f;

    // 1. Read the oldest sample (the one we are about to overwrite)
    const float x = delayLine[static_cast<size_t>(writeHead)];

    // 2. 1st-order averaging lowpass — simulates high-frequency string loss
    //    y[n] = 0.5 * (x[n] + x[n-1])
    const float averaged = 0.5f * (x + avgPrev);
    avgPrev = x;

    // 3. Loop gain — controls overall decay rate
    const float gained = averaged * loopGain;

    // 4. 1st-order allpass interpolator — provides fractional delay for tuning
    //    y[n] = C*x[n] + x[n-1] - C*y[n-1]
    const float apOut = apCoeff * gained + apPrevIn - apCoeff * apPrevOut;
    apPrevIn  = gained;
    apPrevOut = apOut;

    // 5. Write filtered sample back (overwriting the oldest entry)
    delayLine[static_cast<size_t>(writeHead)] = apOut;

    // 6. Advance circular write head
    writeHead = (writeHead + 1) % delayLength;

    // 7. Update leaky energy estimate (for voice stealing)
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
    energyEstimate = 0.f;
}

