#include "Voice.h"
#include <cmath>
#include <algorithm>

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------

void Voice::init(float sr)
{
    sampleRate = sr;
    string.init(sr);
}

// ---------------------------------------------------------------------------
// Note Events
// ---------------------------------------------------------------------------

void Voice::noteOn(int note, float vel,
                   float brightness, float pickPosition, float decay,
                   float stiffness)
{
    midiNote = note;
    velocity = vel;
    active   = true;

    const float freq = midiToFreq(note);
    string.setFrequency(freq, stiffness);
    string.setDecay(decay);

    // Exciter length matches one wavelength (delay-line size)
    const int exciterLen = std::min(static_cast<int>(sampleRate / freq),
                                    kMaxExciterLength);

    // Fill with velocity-dependent physical plectrum pulse
    exciter.fill(exciterScratch, exciterLen,
                 velocity,
                 ExciterType::PLECTRUM_MODEL,
                 brightness,
                 pickPosition,
                 sampleRate);

    string.trigger(exciterScratch, exciterLen, velocity);
}

void Voice::noteOff() noexcept
{
    // Natural Karplus-Strong string decay continues
}

// ---------------------------------------------------------------------------
// Per-Sample Processing
// ---------------------------------------------------------------------------

float Voice::tick(float bridgeInjection) noexcept
{
    if (!active) return 0.f;

    const float out = string.tick(bridgeInjection) * velocity;

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
    // Equal temperament tuning: A4 = 440 Hz = MIDI 69
    return 440.f * std::pow(2.f, (note - 69) / 12.f);
}
