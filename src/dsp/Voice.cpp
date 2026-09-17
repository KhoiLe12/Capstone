#include "Voice.h"
#include <cmath>

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------

void Voice::init(float sr)
{
    sampleRate = sr;
    string.init(sr);
}

// ---------------------------------------------------------------------------
// Note events
// ---------------------------------------------------------------------------

void Voice::noteOn(int note, float vel,
                   float brightness, float pickPosition, float decay)
{
    midiNote = note;
    velocity = vel;
    active   = true;

    const float freq = midiToFreq(note);
    string.setFrequency(freq);
    string.setDecay(decay);

    // Clamp exciter length to the pre-allocated scratch buffer.
    // No heap allocation here — safe to call from the audio thread.
    const int exciterLen = std::min(static_cast<int>(sampleRate / freq),
                                    kMaxExciterLength);

    exciter.fill(exciterScratch, exciterLen,
                 ExciterType::PLECTRUM_MODEL,
                 brightness,
                 pickPosition,
                 sampleRate);

    string.trigger(exciterScratch, exciterLen);
}

void Voice::noteOff() noexcept
{
    // Karplus-Strong strings decay naturally; we simply let the energy drain.
    // A future enhancement could reduce loopGain here to simulate muting.
}

// ---------------------------------------------------------------------------
// Per-sample processing
// ---------------------------------------------------------------------------

float Voice::tick() noexcept
{
    if (!active) return 0.f;

    const float out = string.tick() * velocity;

    // Auto-deactivate when energy is negligibly small
    if (string.getEnergy() < kSilenceThreshold)
    {
        active   = false;
        midiNote = -1;
    }

    return out;
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------

void Voice::reset() noexcept
{
    string.reset();
    active   = false;
    midiNote = -1;
}

float Voice::midiToFreq(int note) noexcept
{
    // Standard equal-temperament tuning: A4 = 440 Hz = MIDI note 69
    return 440.f * std::pow(2.f, (note - 69) / 12.f);
}

