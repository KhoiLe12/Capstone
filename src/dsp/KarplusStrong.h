#pragma once
#include <vector>

/**
 * KarplusStrong — 1-D digital waveguide implementing the Karplus-Strong
 * plucked-string algorithm with accurate pitch tuning.
 *
 * Signal flow per tick():
 *
 *   delayLine[readHead]
 *        │
 *        ▼
 *   1st-order averaging LPF   ← high-frequency decay each loop
 *        │
 *        ▼
 *   loop gain g                ← sustain / overall decay rate
 *        │
 *        ▼
 *   1st-order allpass          ← fractional delay for sub-sample pitch accuracy
 *        │
 *        ▼
 *   delayLine[writeHead]       (= readHead, overwrite oldest sample)
 *
 * The output returned by tick() is the raw sample read before filtering,
 * which avoids the delay of the loop filter appearing in the output signal.
 */
class KarplusStrong
{
public:
    KarplusStrong() = default;

    /** Allocate internal buffers for a given sample rate. Call once at startup. */
    void init(float sampleRate);

    /**
     * Set the fundamental frequency of the string.
     * Computes integer delay-line length N and allpass coefficient C
     * so that the total loop delay equals fs/f0 exactly (to sub-sample).
     */
    void setFrequency(float freqHz);

    /**
     * Set the sustain (loop gain scaling).
     * decay = 0 → very short staccato.
     * decay = 1 → very long sustain (~10 s at 440 Hz, 44100 Hz).
     */
    void setDecay(float decay) noexcept;

    /**
     * Seed the delay line with exciter content and reset state.
     * @param exciterBuf  Source buffer (should have length == delay-line size)
     * @param length      Number of samples to copy
     */
    void trigger(const float* exciterBuf, int length);

    /** Advance one sample and return the string output. */
    float tick() noexcept;

    /** Zero the delay line and all filter state. */
    void reset() noexcept;

    /** Leaky RMS energy estimate — used by the voice-stealer. */
    float getEnergy() const noexcept { return energyEstimate; }

private:
    float sampleRate   = 44100.f;
    float loopGain     = 0.9980f;

    std::vector<float> delayLine;
    int   writeHead    = 0;
    int   delayLength  = 0;

    // 1st-order averaging LPF state
    float avgPrev      = 0.f;

    // 1st-order allpass interpolator (fractional delay correction)
    //   y[n] = C*x[n] + x[n-1] - C*y[n-1]
    float apCoeff      = 0.f;   ///< Allpass coefficient C
    float apPrevIn     = 0.f;   ///< x[n-1]
    float apPrevOut    = 0.f;   ///< y[n-1]

    float energyEstimate = 0.f;
};

