#pragma once
#include <cstdint>

/** The type of initial impulse to generate. */
enum class ExciterType
{
    WHITE_NOISE,     ///< Flat-spectrum noise burst — neutral, full-range excitation
    PLECTRUM_MODEL   ///< Pick-position comb + brightness shaping — more guitarlike
};

/**
 * Exciter — generates the transient burst used to seed the Karplus-Strong delay line.
 *
 * Uses an xorshift32 PRNG (deterministic, fast, no stdlib rand() dependency)
 * and applies two optional spectral shaping stages:
 *   1. Pick-position comb filter  (PlectrumModel only)
 *   2. Brightness single-pole lowpass
 */
class Exciter
{
public:
    Exciter() = default;

    /**
     * Fill outBuffer[0..length-1] with the exciter signal.
     *
     * @param outBuffer     Destination float array (caller-allocated, length >= length)
     * @param length        Number of samples to generate (should equal delay-line length)
     * @param type          WHITE_NOISE or PLECTRUM_MODEL
     * @param brightness    0 = dark (fc ≈ 2 kHz), 1 = bright (fc ≈ 20 kHz)
     * @param pickPosition  Fractional position of plectrum along string (0.05 – 0.5)
     * @param sampleRate    Audio sample rate in Hz
     */
    void fill(float* outBuffer, int length,
              ExciterType type        = ExciterType::PLECTRUM_MODEL,
              float brightness        = 0.5f,
              float pickPosition      = 0.12f,
              float sampleRate        = 44100.f);

private:
    uint32_t prngState = 12345u;   ///< xorshift32 state (never 0)

    /** Advance PRNG and return one sample in [-1, +1]. */
    float nextSample() noexcept;

    /**
     * Apply 1-pole IIR lowpass to shape spectral brightness.
     * Higher brightness → higher cutoff → more high-frequency content.
     */
    void applyBrightness(float* buf, int length,
                         float brightness, float sampleRate) noexcept;

    /**
     * Comb filter H(z) = 1 − z^{−M}, M = round(pickPos × length).
     * Suppresses harmonics at integer multiples of 1/pickPos,
     * mimicking a plectrum contacting the string at that point.
     */
    void applyPickPosition(float* buf, int length, float pickPos) noexcept;
};

