#include "Voice.h"
#include <cmath>
#include <algorithm>

static constexpr float kPi = 3.14159265358979323846f;

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
                   float stiffness, KarplusStrong::MuteMode muteMode)
{
    midiNote = note;
    velocity = vel;
    active   = true;
    releasing = false;
    choking  = false;
    chokeGain = 1.0f;
    releaseGain = 1.0f;
    fadeSamplesLeft = -1;

    const float freq = midiToFreq(note);
    string.setFrequency(freq, stiffness);
    string.setMuteMode(muteMode);
    string.setDecay(decay);

    // Exciter length matches one wavelength (delay-line size)
    const int exciterLen = std::min(static_cast<int>(sampleRate / freq),
                                    kMaxExciterLength);

    // Register-dependent nylon string physics:
    // Wound strings have higher mass and tactile winding scrape
    const float woundFactor = std::clamp((196.0f - freq) / (196.0f - 82.0f), 0.0f, 1.0f);

    float effBrightness = brightness;
    if (muteMode == KarplusStrong::MuteMode::Full)
    {
        // Dead notes:
        // Wound bass strings have warm, wooden fundamental thumps (darker, fleshy)
        // Plain treble strings have crisp, papery clicks (higher frequency snap)
        effBrightness = brightness * (0.28f + 0.22f * (1.0f - woundFactor));
    }

    // Fill with velocity-dependent physical classical nylon finger pulse (flesh + nail)
    exciter.fill(exciterScratch, exciterLen,
                 velocity,
                 ExciterType::NYLON_FINGER_MODEL,
                 effBrightness,
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

void Voice::choke(float chokeVelocity) noexcept
{
    if (!active) return;

    choking = true;
    chokeGain = 1.0f;

    // Hand flesh rapidly absorbs string kinetic energy over ~8ms
    chokeCoeff = std::exp(-1.0f / (0.008f * sampleRate));
    chokeSamplesLeft = static_cast<int>(0.012f * sampleRate); // ~530 samples at 44.1k

    const float freq = midiToFreq(midiNote);
    const float woundFactor = std::clamp((196.0f - freq) / (196.0f - 82.0f), 0.0f, 1.0f);

    // Hand-slap transient:
    // Wound strings have heavier mass impact -> lower resonant frequency (~140 Hz) and thicker thud
    // Plain strings have lighter, crisper fret click (~2200 Hz)
    const float slapFreq = 140.0f + (1.0f - woundFactor) * 2000.0f;
    slapPhase = 0.f;
    slapPhaseInc = 2.0f * kPi * slapFreq / sampleRate;

    // Amplitude proportional to keyswitch velocity and string mass
    slapAmp = chokeVelocity * (0.08f + 0.12f * woundFactor);
    // Transient decays within ~4ms (plain) to ~8ms (wound)
    const float slapTime = 0.004f + 0.005f * woundFactor;
    slapDecay = std::exp(-1.0f / (slapTime * sampleRate));
}

// ---------------------------------------------------------------------------
// Per-Sample Processing
// ---------------------------------------------------------------------------

float Voice::tick() noexcept
{
    if (!active) return 0.f;

    float out = string.tick();

    // Physical acoustic choke handling (hand slapped on strings)
    if (choking)
    {
        out *= chokeGain;
        chokeGain *= chokeCoeff;

        if (slapAmp > 1e-5f)
        {
            const float slapSignal = slapAmp * std::sin(slapPhase);
            slapPhase += slapPhaseInc;
            if (slapPhase >= 2.0f * kPi) slapPhase -= 2.0f * kPi;
            slapAmp *= slapDecay;
            out += slapSignal;
        }

        --chokeSamplesLeft;
        if (chokeSamplesLeft <= 0 || chokeGain < 0.0005f)
        {
            active = false;
            choking = false;
            releasing = false;
            midiNote = -1;
            string.reset();
            return 0.f;
        }

        return out;
    }

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
            string.reset();
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
    choking = false;
    chokeGain = 1.0f;
    chokeSamplesLeft = 0;
    slapAmp = 0.f;
    releaseGain = 1.0f;
    fadeSamplesLeft = -1;
    midiNote = -1;
}

float Voice::midiToFreq(int note) noexcept
{
    // Equal temperament tuning: A4 = 440 Hz = MIDI 69
    return 440.f * std::pow(2.f, (note - 69) / 12.f);
}
