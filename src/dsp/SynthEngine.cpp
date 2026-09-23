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
// Multi-Port Bridge Scattering & Audio Processing
// ---------------------------------------------------------------------------

void SynthEngine::process(float* outputL, float* outputR, int numSamples) noexcept
{
    // Update body modal parameters (size, damping, coupling)
    body.setParameters(paramBodySize, 1.0f, paramBodyMix);

    // Sympathetic coupling strength between strings at the bridge
    // Bounded between 0.005 and 0.025 depending on body coupling
    const float sympatheticStrength = 0.005f + 0.020f * paramBodyMix;

    for (int i = 0; i < numSamples; ++i)
    {
        // 1. Advance all 6 strings, feeding each the bridge reflection from previous sample
        float stringWaves[NUM_VOICES];
        float totalBridgeForce = 0.f;

        for (int v = 0; v < NUM_VOICES; ++v)
        {
            stringWaves[v] = voices[v].tick(lastBridgeReflections[v]);
            totalBridgeForce += stringWaves[v];
        }

        // 2. Multi-Port Scattering: compute mutual sympathetic reflection for each string
        for (int v = 0; v < NUM_VOICES; ++v)
        {
            // Each string receives a fraction of the force exerted by the other 5 strings
            const float mutualForce = totalBridgeForce - stringWaves[v];
            lastBridgeReflections[v] = sympatheticStrength * mutualForce;
        }

        // 3. Normalise by voice count to preserve headroom
        float bridgeSignal = totalBridgeForce / static_cast<float>(NUM_VOICES);

        // 4. Excite the IRCAM Modalys 32-mode soundboard
        float outputSample = body.process(bridgeSignal);

        // 5. Master gain
        outputSample *= paramMasterGain;

        outputL[i] = outputSample;
        outputR[i] = outputSample;
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
    std::fill(std::begin(lastBridgeReflections), std::end(lastBridgeReflections), 0.f);
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
