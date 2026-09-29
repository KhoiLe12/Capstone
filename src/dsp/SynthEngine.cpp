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

    // 1. Intercept Palm Mute Keyswitch C1 (MIDI 24 / FL: C2)
    if (midiNote == KEYSWITCH_PALM_MUTE)
    {
        keyswitchPalmMuteActive = true;
        prevMuteMode = getEffectiveMuteMode();
        // Physically clamp all currently ringing strings into palm mute if not already full-muted
        if (!isFullMuteActive())
        {
            for (auto& v : voices)
                if (v.isActive())
                    v.setMuteMode(KarplusStrong::MuteMode::Palm);
        }
        return; // Keyswitch produces 0 audio itself
    }

    // 2. Intercept Full Mute Keyswitch D1 (MIDI 26 / FL: D2)
    if (midiNote == KEYSWITCH_FULL_MUTE)
    {
        keyswitchFullMuteActive = true;
        prevMuteMode = getEffectiveMuteMode();

        // If any strings are vibrating, physically choke them with authentic hand slap
        int activeCount = 0;
        for (auto& v : voices)
        {
            if (v.isActive())
                activeCount++;
        }

        if (activeCount > 0)
        {
            for (auto& v : voices)
            {
                if (v.isActive())
                    v.choke(velocity);
            }
        }
        return; // Keyswitch alone is silent if no notes were ringing
    }

    // 3. Strict Physical Guitar Range Validation
    // Standard acoustic guitar range: D2 (38, Drop D) / E2 (40) up to D6 (86, 22nd fret high E)
    // Any notes outside this range are rejected so keyswitches never produce unwanted audio
    if (midiNote < GUITAR_MIN_NOTE || midiNote > GUITAR_MAX_NOTE)
    {
        return;
    }

    int idx = findVoiceForNote(midiNote);
    if (idx < 0)
        idx = findFreeVoice();

    const auto muteMode = getEffectiveMuteMode();
    voices[idx].noteOn(midiNote, velocity,
                       paramBrightness, paramPickPos, paramDecay,
                       paramStiffness, muteMode);
}

void SynthEngine::noteOff(int midiNote)
{
    // Intercept Palm Mute Keyswitch C1 release
    if (midiNote == KEYSWITCH_PALM_MUTE)
    {
        keyswitchPalmMuteActive = false;
        syncVoiceMuteModes();
        return;
    }

    // Intercept Full Mute Keyswitch D1 release
    if (midiNote == KEYSWITCH_FULL_MUTE)
    {
        keyswitchFullMuteActive = false;
        syncVoiceMuteModes();
        return;
    }

    // Discard note-off events outside the guitar range
    if (midiNote < GUITAR_MIN_NOTE || midiNote > GUITAR_MAX_NOTE)
    {
        return;
    }

    for (auto& v : voices)
    {
        if (v.getMidiNote() == midiNote && v.isActive())
            v.noteOff();
    }
}

void SynthEngine::syncVoiceMuteModes() noexcept
{
    const auto mode = getEffectiveMuteMode();
    prevMuteMode = mode;
    for (auto& v : voices)
    {
        if (v.isActive())
            v.setMuteMode(mode);
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

    // Sync mute state if changed via DAW automation parameter or UI toggle
    const auto currentMuteMode = getEffectiveMuteMode();
    if (currentMuteMode != prevMuteMode)
    {
        if (currentMuteMode == KarplusStrong::MuteMode::Full)
        {
            for (auto& v : voices)
                if (v.isActive())
                    v.choke(0.8f);
        }
        else
        {
            for (auto& v : voices)
                if (v.isActive())
                    v.setMuteMode(currentMuteMode);
        }
        prevMuteMode = currentMuteMode;
    }

    // 1. Advance all active string voices and sum bridge force into output buffers.
    // All voices are summed to centre (mono mix-bus before the body IR).
    // Stereo width is applied downstream via the M/S spatializer and body IR cross-delay,
    // which create natural, pitch-independent spatial spread controlled by the Width knob.
    for (int i = 0; i < numSamples; ++i)
    {
        float totalBridgeForce = 0.f;
        int activeVoices = 0;

        for (int v = 0; v < NUM_VOICES; ++v)
        {
            if (!voices[v].isActive())
                continue;

            activeVoices++;
            totalBridgeForce += voices[v].tick();
        }

        // Polyphonic bridge impedance headroom:
        // Acoustic soundboard mechanical impedance distributes multi-string chord displacement (1 / sqrt(N))
        const float polyScale = (activeVoices > 1)
            ? (1.0f / std::sqrt(static_cast<float>(activeVoices)))
            : 1.0f;

        outputL[i] = totalBridgeForce * polyScale;
        outputR[i] = totalBridgeForce * polyScale;
    }

    // 2. Drive the acoustic body soundboard (Stereo IR convolution or 32-Mode Modal Bank)
    body.processBlock(outputL, outputR, numSamples);

    // 3. Virtual Stereo Microphone Field & Mid/Side Spatializer:
    // Continuous adjustment from pure mono (0.0) to natural stereo (0.7) to wide studio space (1.0).
    const float sideGain = std::clamp(paramStereoWidth * 1.42f, 0.0f, 1.60f);
    for (int i = 0; i < numSamples; ++i)
    {
        const float mid  = 0.5f * (outputL[i] + outputR[i]);
        const float side = 0.5f * (outputL[i] - outputR[i]);
        outputL[i] = mid + side * sideGain;
        outputR[i] = mid - side * sideGain;
    }

    // 4. DC Blocker, Master Gain, and Transparent Soft Saturation
    constexpr float R = 0.9974f;

    // Smooth acoustic saturation curve (prevents harsh square-wave fuzz on heavy chord plucks)
    auto softLimit = [](float x) noexcept -> float
    {
        constexpr float threshold = 0.75f;
        constexpr float ceiling   = 0.98f;
        const float absX = std::abs(x);
        if (absX <= threshold)
            return x;
        const float excess = (absX - threshold) / (1.6f - threshold);
        const float compressed = threshold + (ceiling - threshold) * std::tanh(excess);
        return (x > 0.f ? 1.f : -1.f) * std::min(compressed, ceiling);
    };

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

        // Calibrated master gain matching concert acoustic references with clean chord headroom
        outL *= (paramMasterGain * 0.92f);
        outR *= (paramMasterGain * 0.92f);

        // Musical soft-limiter: guarantees audio never hard-clips or fuzzes against the ceiling
        outL = softLimit(outL);
        outR = softLimit(outR);

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
    keyswitchPalmMuteActive = false;
    keyswitchFullMuteActive = false;
    prevMuteMode            = KarplusStrong::MuteMode::Open;
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
