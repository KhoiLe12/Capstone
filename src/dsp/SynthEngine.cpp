#include "SynthEngine.h"
#include <limits>
#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------

void SynthEngine::init(float sr, int blockSize)
{
    sampleRate = sr;
    for (auto& v : voices)
        v.init(sr);
    body.init(sr, blockSize, paramBodySize, 1.0f);
    reset();
}

// ---------------------------------------------------------------------------
// MIDI Event Handlers
// ---------------------------------------------------------------------------

void SynthEngine::noteOn(int midiNote, float velocity)
{
    if (velocity <= 0.0001f)
    {
        noteOff(midiNote);
        return;
    }

    int idx = findVoiceForNote(midiNote);
    if (idx < 0)
        idx = findFreeVoice();

    voices[idx].noteOn(midiNote, velocity,
                       paramBrightness, paramPickPos, paramDecay,
                       paramStiffness);
}

void SynthEngine::noteOff(int midiNote)
{
    for (auto& v : voices)
    {
        if (v.getMidiNote() == midiNote && v.isActive())
            v.noteOff();
    }
}

// ---------------------------------------------------------------------------
// Pure Acoustic String & Modalys Soundboard Processing
// ---------------------------------------------------------------------------

void SynthEngine::process(float* outputL, float* outputR, int numSamples) noexcept
{
    // Update body parameters (size, damping, coupling, bodyType)
    const int bodyType = static_cast<int>(std::round(paramBodyType));
    body.setParameters(paramBodySize, 1.0f, paramBodyMix, bodyType);

    // 1. Advance all active string voices and sum bridge force into output buffers
    for (int i = 0; i < numSamples; ++i)
    {
        float totalBridgeForceL = 0.f;
        float totalBridgeForceR = 0.f;

        for (int v = 0; v < NUM_VOICES; ++v)
        {
            if (!voices[v].isActive())
                continue;

            const float s = voices[v].tick();
            const int note = voices[v].getMidiNote();

            // Physical string position across the bridge saddle (55mm width):
            // Low E (MIDI 40) sits on the bass side (-0.28 pan)
            // High E (MIDI 64) sits on the treble side (+0.28 pan)
            float stringPan = 0.0f;
            if (note > 0)
            {
                stringPan = std::clamp((static_cast<float>(note) - 52.0f) / 24.0f, -1.0f, 1.0f) * 0.28f;
            }

            totalBridgeForceL += s * (1.0f - stringPan);
            totalBridgeForceR += s * (1.0f + stringPan);
        }

        outputL[i] = totalBridgeForceL * 1.0f;
        outputR[i] = totalBridgeForceR * 1.0f;
    }

    // 2. Drive the acoustic body soundboard (IR convolution or Modal Bank)
    body.processBlock(outputL, outputR, numSamples);

    // 3. DC Blocker, Master Gain, and Soft Limiter
    constexpr float R = 0.9974f;
    for (int i = 0; i < numSamples; ++i)
    {
        float outL = outputL[i];
        float outR = outputR[i];

        // 18 Hz Acoustic DC Blocker (guarantees zero DC offset and true acoustic centering)
        const float dcOutL = outL - dcX_L + R * dcY_L;
        dcX_L = outL;
        dcY_L = dcOutL;
        outL = dcOutL;

        const float dcOutR = outR - dcX_R + R * dcY_R;
        dcX_R = outR;
        dcY_R = dcOutR;
        outR = dcOutR;

        // Master gain
        outL *= paramMasterGain;
        outR *= paramMasterGain;

        // Transparent soft-limiter: guarantees audio never hard-clips against the 0 dBFS ceiling
        if (std::abs(outL) > 0.88f)
        {
            const float sign = outL > 0.f ? 1.f : -1.f;
            outL = sign * (0.88f + 0.10f * std::tanh((std::abs(outL) - 0.88f) / 0.10f));
        }
        if (std::abs(outR) > 0.88f)
        {
            const float sign = outR > 0.f ? 1.f : -1.f;
            outR = sign * (0.88f + 0.10f * std::tanh((std::abs(outR) - 0.88f) / 0.10f));
        }

        outputL[i] = outL;
        outputR[i] = outR;
    }
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

void SynthEngine::reset()
{
    for (auto& v : voices)
        v.reset();
    body.reset();
    dcX_L = 0.f; dcY_L = 0.f;
    dcX_R = 0.f; dcY_R = 0.f;
}

// ---------------------------------------------------------------------------
// Voice Allocation
// ---------------------------------------------------------------------------

int SynthEngine::findFreeVoice() const noexcept
{
    // 1. Look for an idle voice
    for (int i = 0; i < NUM_VOICES; ++i)
        if (!voices[i].isActive())
            return i;

    // 2. Otherwise steal the quietest (lowest RMS energy) voice
    int   lowestIdx    = 0;
    float lowestEnergy = std::numeric_limits<float>::max();

    for (int i = 0; i < NUM_VOICES; ++i)
    {
        const float e = voices[i].getEnergy();
        if (e < lowestEnergy)
        {
            lowestEnergy = e;
            lowestIdx    = i;
        }
    }
    return lowestIdx;
}

int SynthEngine::findVoiceForNote(int midiNote) const noexcept
{
    for (int i = 0; i < NUM_VOICES; ++i)
        if (voices[i].getMidiNote() == midiNote && voices[i].isActive())
            return i;
    return -1;
}
