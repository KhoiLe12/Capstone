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

    // Physical contact with palm/fret hand instantly absorbs string kinetic energy:
    // Damping time constant 1.8ms (chokeCoeff): string ringing drops to <6% in ~5ms
    chokeCoeff = std::exp(-1.0f / (0.0018f * sampleRate));
    // Transient impulse of ~45ms excites the acoustic body IR without synthetic bass boom
    chokeSamplesLeft = static_cast<int>(0.045f * sampleRate);

    const float freq = midiToFreq(midiNote);
    const float woundFactor = std::clamp((196.0f - freq) / (196.0f - 82.0f), 0.0f, 1.0f);

    // 1. Soundboard cavity air thud (A0 Helmholtz / lower bout mode):
    // Wound strings have higher mass impact -> deep, warm 105 Hz thud; plain strings ~145 Hz
    const float thudFreq = 105.0f + 40.0f * (1.0f - woundFactor);
    slapThudPhase = 0.f;
    slapThudPhaseInc = 2.0f * kPi * thudFreq / sampleRate;
    slapThudAmp = chokeVelocity * (0.07f + 0.07f * woundFactor);
    const float thudTime = 0.010f + 0.010f * woundFactor; // 10ms (plain) to 20ms (wound)
    slapThudDecay = std::exp(-1.0f / (thudTime * sampleRate));

    // 2. Fret-wire contact snap (sharp high-frequency mechanical transient):
    const float clickFreq = 1800.0f + 800.0f * (1.0f - woundFactor);
    slapClickPhase = 0.f;
    slapClickPhaseInc = 2.0f * kPi * clickFreq / sampleRate;
    slapClickAmp = chokeVelocity * (0.04f + 0.04f * (1.0f - woundFactor));
    const float clickTime = 0.003f + 0.003f * woundFactor; // 3ms to 6ms
    slapClickDecay = std::exp(-1.0f / (clickTime * sampleRate));
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

        // Once the string vibration has decayed into silence (< 0.01 = -40dB), reset the delay line
        // so no residual harmonic pitch can circulate or leak through.
        if (chokeGain < 0.01f && chokeGain > 0.0f)
        {
            string.reset();
            chokeGain = 0.0f;
        }

        float slapOut = 0.f;
        if (slapThudAmp > 1e-5f)
        {
            slapOut += slapThudAmp * std::sin(slapThudPhase);
            slapThudPhase += slapThudPhaseInc;
            if (slapThudPhase >= 2.0f * kPi) slapThudPhase -= 2.0f * kPi;
            slapThudAmp *= slapThudDecay;
        }

        if (slapClickAmp > 1e-5f)
        {
            slapOut += slapClickAmp * std::sin(slapClickPhase);
            slapClickPhase += slapClickPhaseInc;
            if (slapClickPhase >= 2.0f * kPi) slapClickPhase -= 2.0f * kPi;
            slapClickAmp *= slapClickDecay;
        }

        out += slapOut;

        --chokeSamplesLeft;
        if (chokeSamplesLeft <= 0 || (chokeGain <= 0.0f && slapThudAmp <= 1e-5f && slapClickAmp <= 1e-5f))
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
    slapThudAmp = 0.f;
    slapClickAmp = 0.f;
    releaseGain = 1.0f;
    fadeSamplesLeft = -1;
    midiNote = -1;
}

float Voice::midiToFreq(int note) noexcept
{
    // Equal temperament tuning: A4 = 440 Hz = MIDI 69
    return 440.f * std::pow(2.f, (note - 69) / 12.f);
}
