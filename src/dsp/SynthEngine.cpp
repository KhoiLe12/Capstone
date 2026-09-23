#include "SynthEngine.h"
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
    pickup.init(sr);
    strummer.init(sr);
    reset();
}

// ---------------------------------------------------------------------------
// MIDI Event Handlers with Physical Fretboard & Strummer
// ---------------------------------------------------------------------------

void SynthEngine::noteOn(int midiNote, float velocity)
{
    if (velocity <= 0.0001f)
    {
        noteOff(midiNote);
        return;
    }

    // 1. Gather current active state of the 6 physical strings
    std::array<bool, NUM_STRINGS> activeMap;
    for (int s = 0; s < NUM_STRINGS; ++s)
        activeMap[static_cast<size_t>(s)] = voices[s].isActive();

    // 2. Query the Fretboard Matrix for optimal physical string & fret
    const auto alloc = fretboard.allocateNote(midiNote, activeMap);

    if (alloc.stringIndex >= 0)
    {
        // 3. Schedule the pluck via the Strummer engine
        strummer.scheduleNote(alloc.stringIndex, midiNote, velocity, paramStrumSpeed);
    }
}

void SynthEngine::noteOff(int midiNote)
{
    // Find which string is currently playing this pitch
    std::array<int, NUM_STRINGS> noteMap;
    for (int s = 0; s < NUM_STRINGS; ++s)
        noteMap[static_cast<size_t>(s)] = voices[s].getMidiNote();

    const int s = fretboard.findStringPlayingNote(midiNote, noteMap);
    if (s >= 0)
        voices[s].noteOff();
}

// ---------------------------------------------------------------------------
// Real-Time Processing Loop
// ---------------------------------------------------------------------------

void SynthEngine::process(float* outputL, float* outputR, int numSamples) noexcept
{
    body.setParameters(paramBodySize, 1.0f, paramBodyMix);

    // Sympathetic coupling strength between physical strings (Woodhouse / Karjalainen model)
    // Strictly bounded to maintain passive L2 dissipative contractivity:
    // maximum eigenvalue lambda_max = (1 - kBridgeCouplingLoss) + sympatheticStrength < 1.0
    const float sympatheticStrength = 0.0018f * paramBodyMix;
    constexpr float kMutualNorm = 1.0f / static_cast<float>(NUM_STRINGS - 1);

    for (int i = 0; i < numSamples; ++i)
    {
        // 1. Check if the Strummer has any scheduled plucks ready to strike
        int sIndex = -1;
        int sNote  = -1;
        float sVel = 0.f;
        while (strummer.tick(sIndex, sNote, sVel))
        {
            if (sIndex >= 0 && sIndex < NUM_STRINGS)
            {
                voices[sIndex].noteOn(sNote, sVel,
                                      paramBrightness, paramPickPos, paramDecay,
                                      paramStiffness);
            }
        }

        // 2. Advance all 6 strings and accumulate downward bridge forces
        float stringWaves[NUM_STRINGS];
        float totalBridgeForce = 0.f;

        for (int v = 0; v < NUM_STRINGS; ++v)
        {
            stringWaves[v] = voices[v].tick(lastBridgeReflections[v]);
            totalBridgeForce += stringWaves[v];
        }

        // 3. Multi-Port Bridge Scattering: mutual sympathetic reflections
        // Normalized by (NUM_STRINGS - 1) so total coupled force into any single string
        // is guaranteed strictly passive
        for (int v = 0; v < NUM_STRINGS; ++v)
        {
            const float mutualForce = (totalBridgeForce - stringWaves[v]) * kMutualNorm;
            lastBridgeReflections[v] = sympatheticStrength * mutualForce;
        }

        // Dynamic analog soft-saturation:
        // Single melody notes pass unattenuated for full acoustic volume and rich modal resonance.
        // Full chords are smoothly saturated (analog wood compression) to protect digital headroom.
        const float bridgeSignal = std::tanh(totalBridgeForce * 1.8f) * 0.75f;

        // 4. Virtual Magnetic Pickup (Electric tone with spatial comb & tone circuit)
        const float pickupSignal = pickup.process(bridgeSignal, paramPickupPos, paramTone);

        // 5. Modalys 32-Mode Soundboard (Acoustic body radiation)
        const float acousticSignal = body.process(bridgeSignal);

        // 6. Blend between Electric Pickup and Acoustic Soundboard
        float mixed = (1.0f - paramBodyMix) * pickupSignal + paramBodyMix * acousticSignal;

        // Master gain
        mixed *= paramMasterGain;

        outputL[i] = mixed;
        outputR[i] = mixed;
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
    pickup.reset();
    strummer.reset();
    std::fill(std::begin(lastBridgeReflections), std::end(lastBridgeReflections), 0.f);
}
