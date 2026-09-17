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
    body.init(sr);
}

// ---------------------------------------------------------------------------
// MIDI event handlers
// ---------------------------------------------------------------------------

void SynthEngine::noteOn(int midiNote, float velocity)
{
    // Retrigger if this note is already sounding (re-use same voice slot)
    int idx = findVoiceForNote(midiNote);

    // Otherwise grab a free or steal-worthy slot
    if (idx < 0)
        idx = findFreeVoice();

    voices[idx].noteOn(midiNote, velocity,
                       paramBrightness, paramPickPos, paramDecay);
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
// Audio processing
// ---------------------------------------------------------------------------

void SynthEngine::process(float* outputL, float* outputR, int numSamples) noexcept
{
    // Sync body wet/dry from parameters
    body.dryMix = 1.f - paramBodyMix;
    body.wetMix = paramBodyMix;

    for (int i = 0; i < numSamples; ++i)
    {
        // Sum all active voices
        float mix = 0.f;
        for (auto& v : voices)
            mix += v.tick();

        // Normalise by voice count to keep headroom constant regardless
        // of how many strings are sounding simultaneously
        mix /= static_cast<float>(NUM_VOICES);

        // Apply guitar body coloration
        mix = body.process(mix);

        // Master gain
        mix *= paramMasterGain;

        // Write to both channels (mono instrument → identical L/R)
        outputL[i] = mix;
        outputR[i] = mix;
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
// Voice allocation helpers
// ---------------------------------------------------------------------------

int SynthEngine::findFreeVoice() const noexcept
{
    // Priority 1: idle voice
    for (int i = 0; i < NUM_VOICES; ++i)
        if (!voices[i].isActive())
            return i;

    // Priority 2: steal the quietest (lowest energy) active voice
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

