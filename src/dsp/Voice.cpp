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
    // Classical finger/palm damping time constant ~35ms: smooth, natural decay
    releaseCoeff = std::exp(-1.0f / (0.035f * sampleRate));
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
    releasing = false;
    releaseGain = 1.0f;
    fadeSamplesLeft = -1;

    const float freq = midiToFreq(note);
    string.setFrequency(freq, stiffness);
    string.setDecay(decay);

    // Exciter length matches one wavelength (delay-line size)
    const int exciterLen = std::min(static_cast<int>(sampleRate / freq),
                                    kMaxExciterLength);

    // Fill with velocity-dependent physical classical nylon finger pulse (flesh + nail)
    exciter.fill(exciterScratch, exciterLen,
                 velocity,
                 ExciterType::NYLON_FINGER_MODEL,
                 brightness,
                 pickPosition,
                 sampleRate,
                 freq);

    string.trigger(exciterScratch, exciterLen, velocity);
}

void Voice::noteOff() noexcept
{
    // Begin smooth acoustic release envelope
    releasing = true;
    string.damp();
}

// ---------------------------------------------------------------------------
// Per-Sample Processing
// ---------------------------------------------------------------------------

float Voice::tick() noexcept
{
    if (!active) return 0.f;

    float out = string.tick() * velocity;

    // Apply smooth exponential release envelope on note-off
    if (releasing)
    {
        out *= releaseGain;
        releaseGain *= releaseCoeff;

        // When release gain has decayed into near-inaudibility (-46 dB),
        // trigger an anti-click linear fade to zero
        if (releaseGain < 0.005f && fadeSamplesLeft < 0)
        {
            fadeSamplesLeft = 64;
        }
    }
    else if (string.getEnergy() < 1e-7f && fadeSamplesLeft < 0)
    {
        // Natural ring-out also fades out cleanly instead of hard-cutting
        fadeSamplesLeft = 64;
    }

    // 64-sample linear fade-out to guarantee zero DC or step click
    if (fadeSamplesLeft > 0)
    {
        const float fadeFactor = static_cast<float>(fadeSamplesLeft) / 64.0f;
        out *= fadeFactor;
        --fadeSamplesLeft;

        if (fadeSamplesLeft == 0)
        {
            active = false;
            releasing = false;
            midiNote = -1;
            return 0.f;
        }
    }

    return out;
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------

void Voice::reset() noexcept
{
    string.reset();
    active = false;
    releasing = false;
    releaseGain = 1.0f;
    fadeSamplesLeft = -1;
    midiNote = -1;
}

float Voice::midiToFreq(int note) noexcept
{
    // Equal temperament tuning: A4 = 440 Hz = MIDI 69
    return 440.f * std::pow(2.f, (note - 69) / 12.f);
}
