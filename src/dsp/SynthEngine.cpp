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
        // 1. Advance all active string voices and sum bridge force
        float totalBridgeForce = 0.f;

        for (int v = 0; v < NUM_VOICES; ++v)
        {
            totalBridgeForce += voices[v].tick();
        }

        // 2. Drive the 32-mode IRCAM Modalys spruce soundboard
        // Analog soft-saturation prevents harsh digital clipping on multi-string chords
        // while preserving full unattenuated volume for single notes
        const float bridgeSignal = std::tanh(totalBridgeForce * 1.5f) * 0.75f;

        // 3. Excite the soundboard
        float outputSample = body.process(bridgeSignal);

        // 4. Master gain
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
