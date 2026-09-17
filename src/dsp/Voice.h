#pragma once
#include "Exciter.h"
#include "KarplusStrong.h"

/**
 * Voice — one self-contained plucked-string voice.
 *
 * Owns an Exciter and a KarplusStrong waveguide.
 * Activated by noteOn(), produces audio via tick(),
 * and silently deactivates itself once its energy drops
 * below a threshold (no explicit noteOff() is required
 * for natural string decay, but noteOff() is provided
 * for future use e.g. damping or muting).
 */
class Voice
{
public:
    Voice() = default;

    /** Allocate KS delay line for this sample rate. Call once at startup. */
    void init(float sampleRate);

    /**
     * Trigger a new note.
     * @param midiNote     MIDI note number 0-127
     * @param velocity     Normalised velocity 0..1 (scales output amplitude)
     * @param brightness   Exciter brightness 0..1
     * @param pickPosition Plectrum contact point, fraction of string length
     * @param decay        KS loop-gain decay 0..1
     */
    void noteOn(int midiNote, float velocity,
                float brightness, float pickPosition, float decay);

    /** Signal that the key has been released (string may still ring). */
    void noteOff() noexcept;

    /** Advance one sample and return the voice output. Returns 0 if inactive. */
    float tick() noexcept;

    bool  isActive()  const noexcept { return active; }
    int   getMidiNote() const noexcept { return midiNote; }
    float getEnergy() const noexcept { return string.getEnergy(); }

    /** Hard-stop and reset (used by voice stealer). */
    void reset() noexcept;

private:
    Exciter       exciter;
    KarplusStrong string;

    int   midiNote   = -1;
    float velocity   = 1.f;
    bool  active     = false;
    float sampleRate = 44100.f;

    /** MIDI note number → fundamental frequency in Hz. */
    static float midiToFreq(int note) noexcept;

    /** Energy threshold below which the voice considers itself silent. */
    static constexpr float kSilenceThreshold = 1e-9f;

    /**
     * Maximum delay-line length at 44100 Hz for the lowest note (~20 Hz).
     * Pre-allocated so noteOn() never calls malloc on the audio thread.
     */
    static constexpr int kMaxExciterLength = 2216;
    float exciterScratch[kMaxExciterLength] = {};
};

