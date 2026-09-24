#pragma once
#include <cstdint>

/** The type of initial impulse to generate. */
enum class ExciterType
{
    WHITE_NOISE,
    PLECTRUM_MODEL,
    NYLON_FINGER_MODEL   ///< Flesh compression + crisp fingernail release transient
};

/**
 * Exciter — generates the transient contact burst used to seed the waveguide.
 *
 * Models classical nylon finger plucks and plectrums:
 *   1. Flesh pad compression ramp + fingernail release snap
 *   2. Wound string silver wire texture vs plain nylon glassy response
 *   3. Pick/finger position comb filter (H(z) = 1 - z^-M)
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
     * @param type          WHITE_NOISE, PLECTRUM_MODEL, or NYLON_FINGER_MODEL
     * @param brightness    0 = dark (flesh only), 1 = bright (hard polished fingernail)
     * @param pickPosition  Fraction of string length where finger strikes (0.05 – 0.5)
     * @param sampleRate    Audio sample rate in Hz
     * @param stringFreqHz  Fundamental string frequency in Hz (distinguishes wound bass vs plain nylon treble)
     */
    void fill(float* outBuffer, int length,
              float velocity          = 0.8f,
              ExciterType type        = ExciterType::NYLON_FINGER_MODEL,
              float brightness        = 0.55f,
              float pickPosition      = 0.14f,
              float sampleRate        = 44100.f,
              float stringFreqHz      = 196.0f);

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
