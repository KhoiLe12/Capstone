#pragma once
#include <cstdint>

/** The type of initial impulse to generate. */
enum class ExciterType
{
    WHITE_NOISE,     ///< Flat-spectrum noise burst
    PLECTRUM_MODEL   ///< Finite-duration asymmetric contact pulse + pick comb + brightness
};

/**
 * Exciter — generates the transient contact burst used to seed the waveguide.
 *
 * Models a physical plectrum:
 *   1. Finite contact duration pulse (~1-3 ms, velocity-dependent)
 *   2. Micro-friction texture against string windings
 *   3. Pick-position comb filter (H(z) = 1 - z^-M)
 *   4. Brightness lowpass filter
 */
class Exciter
{
public:
    Exciter() = default;

    /**
     * Fill outBuffer[0..length-1] with the exciter signal.
     *
     * @param outBuffer     Destination float array
     * @param length        Number of samples in the waveguide
     * @param velocity      Normalised velocity 0..1 (controls contact duration and energy)
     * @param type          WHITE_NOISE or PLECTRUM_MODEL
     * @param brightness    0 = dark (felt pick), 1 = bright (hard acrylic pick)
     * @param pickPosition  Fraction of string length where plectrum strikes (0.05 – 0.5)
     * @param sampleRate    Audio sample rate in Hz
     */
    void fill(float* outBuffer, int length,
              float velocity          = 0.8f,
              ExciterType type        = ExciterType::PLECTRUM_MODEL,
              float brightness        = 0.5f,
              float pickPosition      = 0.12f,
              float sampleRate        = 44100.f);

private:
    uint32_t prngState = 12345u;   ///< xorshift32 state

    /** Advance PRNG and return one sample in [-1, +1]. */
    float nextSample() noexcept;

    /** Apply 1-pole IIR lowpass to shape spectral brightness. */
    void applyBrightness(float* buf, int length,
                         float brightness, float sampleRate) noexcept;

    /** Comb filter H(z) = 1 - z^-M, M = round(pickPos * length). */
    void applyPickPosition(float* buf, int length, float pickPos) noexcept;
};
