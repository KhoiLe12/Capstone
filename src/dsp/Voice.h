#pragma once
#include "Exciter.h"
#include "KarplusStrong.h"

/**
 * Voice — one self-contained plucked-string voice with:
 *   - Physical exciter (finite contact pulse)
 *   - 1D waveguide with sub-sample pitch tuning
 *   - Inharmonicity / stiffness dispersion
 *   - Dynamic tension modulation
 *   - Multi-port bridge velocity coupling
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
     * @param velocity     Normalised velocity 0..1
     * @param brightness   Exciter brightness 0..1
     * @param pickPosition Plectrum contact point, fraction of string length
     * @param decay        KS loop-gain decay 0..1
     * @param stiffness    String stiffness / inharmonicity 0..1
     */
    void noteOn(int midiNote, float velocity,
                float brightness, float pickPosition, float decay,
                float stiffness = 0.25f);

    /** Signal note release (string continues natural decay). */
    void noteOff() noexcept;

    /** Advance one sample and return voice output at bridge. */
    float tick() noexcept;

    bool  isActive()    const noexcept { return active; }
    int   getMidiNote() const noexcept { return midiNote; }
    float getEnergy()   const noexcept { return string.getEnergy(); }

    /** Hard-stop and reset. */
    void reset() noexcept;

private:
    Exciter       exciter;
    KarplusStrong string;

    int   midiNote   = -1;
    float velocity   = 1.f;
    bool  active     = false;
    float sampleRate = 44100.f;

    static float midiToFreq(int note) noexcept;
    static constexpr float kSilenceThreshold = 1e-9f;

    static constexpr int kMaxExciterLength = 2216;
    float exciterScratch[kMaxExciterLength] = {};
};
