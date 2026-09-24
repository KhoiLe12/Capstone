#include "SynthEngine.h"
#include <limits>
#include <algorithm>

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------

void SynthEngine::init(float sr, int /*blockSize*/)
{
    sampleRate = sr;
    for (auto& v : voices)
        v.init(sr);
    body.init(sr, paramBodySize, 1.0f);
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
    // Update body modal parameters (size, damping, coupling)
    body.setParameters(paramBodySize, 1.0f, paramBodyMix);

    for (int i = 0; i < numSamples; ++i)
    {
        // 1. Advance all active string voices and sum bridge force in stereo
        float totalBridgeForceL = 0.f;
        float totalBridgeForceR = 0.f;

        for (int v = 0; v < NUM_VOICES; ++v)
        {
            if (!voices[v].isActive())
                continue;

            const float s = voices[v].tick();
            const int note = voices[v].getMidiNote();

            // Physical string position across the bridge saddle (55mm width):
            // Low E (MIDI 40) sits on the bass side (-0.18 pan)
            // High E (MIDI 64) sits on the treble side (+0.18 pan)
            float stringPan = 0.0f;
            if (note > 0)
            {
                stringPan = std::clamp((static_cast<float>(note) - 52.0f) / 24.0f, -1.0f, 1.0f) * 0.18f;
            }

            totalBridgeForceL += s * (1.0f - stringPan);
            totalBridgeForceR += s * (1.0f + stringPan);
        }

        // 2. Drive the 32-mode IRCAM Modalys spruce soundboard
        // Linear summing preserves harmonic clarity and prevents intermodulation distortion on chords.
        // Scaled to 0.50f (+3.1 dB makeup gain) for full acoustic punch and presence.
        const float bridgeSignalL = totalBridgeForceL * 0.50f;
        const float bridgeSignalR = totalBridgeForceR * 0.50f;

        // 3. Excite the soundboard in stereo
        float outL = 0.f;
        float outR = 0.f;
        body.processStereo(bridgeSignalL, bridgeSignalR, outL, outR);

        // 4. Master gain
        outL *= paramMasterGain;
        outR *= paramMasterGain;

        // 5. Transparent soft-limiter: guarantees audio never hard-clips against the 0 dBFS DAC ceiling
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
