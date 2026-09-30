#include "SynthEngine.h"
#include <limits>
#include <algorithm>
#include <cmath>
#include <vector>

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

    // 3. Intercept 8th-Note Strum Keyswitch E1 (MIDI 28 / FL: E2)
    if (midiNote == KEYSWITCH_STRUM_8TH)
    {
        keyswitchStrumActive = true;
        strumKeyswitchVelocity = velocity;
        const int samplesPer8th = std::max(100, static_cast<int>((30.0f / currentBpm) * sampleRate));
        strumSampleCounter = samplesPer8th;
        triggerStrum(true, velocity); // Trigger first downstroke immediately on noteOn
        nextStrumIsDown = false;      // Next 8th note will be upstroke
        return;
    }

    // 4. Strict Physical Guitar Range Validation
    // Standard acoustic guitar range: D2 (38, Drop D) / E2 (40) up to D6 (86, 22nd fret high E)
    // Any notes outside this range are rejected so keyswitches never produce unwanted audio
    if (midiNote < GUITAR_MIN_NOTE || midiNote > GUITAR_MAX_NOTE)
    {
        return;
    }

    heldNotes[midiNote] = velocity;

    const bool isFdtd = (paramEngineType >= 0.5f);
    int idx = -1;

    if (isFdtd)
    {
        // Authentic 6-string physical guitar assignment:
        // String 0: E2 (notes 38..44)
        // String 1: A2 (notes 45..49)
        // String 2: D3 (notes 50..54)
        // String 3: G3 (notes 55..58)
        // String 4: B3 (notes 59..63)
        // String 5: E4 (notes 64..86)
        int s = 0;
        if (midiNote < 45)      s = 0;
        else if (midiNote < 50) s = 1;
        else if (midiNote < 55) s = 2;
        else if (midiNote < 59) s = 3;
        else if (midiNote < 64) s = 4;
        else                    s = 5;

        idx = s;
        // If the primary string voice is already ringing with a different note,
        // allocate any available idle string voice (0..5) so chords ring naturally
        if (voices[idx].isActive() && voices[idx].getMidiNote() != midiNote)
        {
            for (int v = 0; v < 6; ++v)
            {
                if (!voices[v].isActive())
                {
                    idx = v;
                    break;
                }
            }
        }

        voices[idx].setEngineType(Voice::EngineType::BilbaoFdtd);
        voices[idx].setPhysicalStringIndex(idx);
    }
    else
    {
        idx = findVoiceForNote(midiNote);
        if (idx < 0)
            idx = findFreeVoice();

        voices[idx].setEngineType(Voice::EngineType::DigitalWaveguide);
    }

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

    // Intercept 8th-Note Strum Keyswitch E1 release
    if (midiNote == KEYSWITCH_STRUM_8TH)
    {
        keyswitchStrumActive = false;
        return;
    }

    // Discard note-off events outside the guitar range
    if (midiNote < GUITAR_MIN_NOTE || midiNote > GUITAR_MAX_NOTE)
    {
        return;
    }

    heldNotes[midiNote] = 0.0f;

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
// 8th-Note Strumming & Alternating Stroke Engine
// ---------------------------------------------------------------------------

void SynthEngine::triggerStrum(bool isDownstroke, float velOverride) noexcept
{
    lastStrumWasDown = isDownstroke;

    // 1. Collect currently held notes from the keyboard
    std::vector<std::pair<int, float>> chord;
    chord.reserve(12);

    for (int n = GUITAR_MIN_NOTE; n <= GUITAR_MAX_NOTE; ++n)
    {
        if (heldNotes[n] > 0.001f)
            chord.push_back({ n, heldNotes[n] });
    }

    // 2. If no keys are physically held, fallback to any actively ringing voices
    if (chord.empty())
    {
        for (const auto& v : voices)
        {
            if (v.isActive() && v.getMidiNote() >= GUITAR_MIN_NOTE && v.getMidiNote() <= GUITAR_MAX_NOTE)
            {
                int note = v.getMidiNote();
                bool found = false;
                for (const auto& p : chord)
                {
                    if (p.first == note) { found = true; break; }
                }
                if (!found)
                    chord.push_back({ note, 0.75f });
            }
        }
    }

    if (chord.empty())
        return; // Nothing to strum

    // 3. Sort notes by stroke direction
    if (isDownstroke)
    {
        // Downstroke: sweep from lowest pitch to highest pitch
        std::sort(chord.begin(), chord.end(), [](const auto& a, const auto& b) {
            return a.first < b.first;
        });
    }
    else
    {
        // Upstroke: sweep from highest pitch to lowest pitch
        std::sort(chord.begin(), chord.end(), [](const auto& a, const auto& b) {
            return a.first > b.first;
        });
    }

    // 4. Directional acoustic physics:
    // Downstroke: standard velocity and brightness
    // Upstroke: slightly lighter velocity (~82%), brighter glancing edge (~115%)
    const float strokeVelScale    = isDownstroke ? 1.0f : 0.82f;
    const float strokeBrightScale = isDownstroke ? 1.0f : 1.15f;

    // Fast acoustic pick rake: ~2.5 ms (down) / ~2.0 ms (up)
    const float rakeTimeSec = isDownstroke ? 0.0025f : 0.0020f;
    const int rakeSamples = std::max(1, static_cast<int>(rakeTimeSec * sampleRate));

    for (size_t i = 0; i < chord.size(); ++i)
    {
        float baseVel = (velOverride > 0.001f) ? velOverride : chord[i].second;
        float vel = std::clamp(baseVel * strokeVelScale, 0.05f, 1.0f);
        float brt = std::clamp(paramBrightness * strokeBrightScale, 0.05f, 1.0f);
        int delay = static_cast<int>(i) * rakeSamples;

        schedulePluck(chord[i].first, vel, brt, delay);
    }
}

void SynthEngine::schedulePluck(int midiNote, float velocity, float brightness, int delaySamples) noexcept
{
    if (delaySamples <= 0)
    {
        int idx = findVoiceForNote(midiNote);
        if (idx < 0)
            idx = findFreeVoice();

        const auto muteMode = getEffectiveMuteMode();
        voices[idx].noteOn(midiNote, velocity,
                           brightness, paramPickPos, paramDecay,
                           paramStiffness, muteMode);
        return;
    }

    for (int p = 0; p < MAX_PENDING_PLUCKS; ++p)
    {
        if (!pendingPlucks[p].active)
        {
            pendingPlucks[p].midiNote     = midiNote;
            pendingPlucks[p].velocity     = velocity;
            pendingPlucks[p].brightness   = brightness;
            pendingPlucks[p].delaySamples = delaySamples;
            pendingPlucks[p].active       = true;
            return;
        }
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

    // 8th-note interval in samples based on DAW host tempo
    const int samplesPer8th = std::max(100, static_cast<int>((30.0f / currentBpm) * sampleRate));

    // 1. Advance all active string voices and sum bridge force into output buffers.
    // All voices are summed to centre (mono mix-bus before the body IR).
    // Stereo width is applied downstream via the M/S spatializer and body IR cross-delay,
    // which create natural, pitch-independent spatial spread controlled by the Width knob.
    for (int i = 0; i < numSamples; ++i)
    {
        // Advance 8th-note strum interval timer
        if (keyswitchStrumActive || paramStrum)
        {
            --strumSampleCounter;
            if (strumSampleCounter <= 0)
            {
                strumSampleCounter = samplesPer8th;
                triggerStrum(nextStrumIsDown, strumKeyswitchVelocity);
                nextStrumIsDown = !nextStrumIsDown;
            }
        }

        // Fire any pending rake plucks whose countdown has reached zero
        for (int p = 0; p < MAX_PENDING_PLUCKS; ++p)
        {
            if (pendingPlucks[p].active)
            {
                --pendingPlucks[p].delaySamples;
                if (pendingPlucks[p].delaySamples <= 0)
                {
                    int idx = findVoiceForNote(pendingPlucks[p].midiNote);
                    if (idx < 0)
                        idx = findFreeVoice();
                    const auto muteMode = getEffectiveMuteMode();
                    voices[idx].noteOn(pendingPlucks[p].midiNote,
                                       pendingPlucks[p].velocity,
                                       pendingPlucks[p].brightness,
                                       paramPickPos, paramDecay,
                                       paramStiffness, muteMode);
                    pendingPlucks[p].active = false;
                }
            }
        }

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
    keyswitchStrumActive    = false;
    strumSampleCounter      = 0;
    nextStrumIsDown         = true;
    lastStrumWasDown        = true;
    std::fill(std::begin(heldNotes), std::end(heldNotes), 0.f);
    for (int p = 0; p < MAX_PENDING_PLUCKS; ++p)
        pendingPlucks[p].active = false;
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
