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
    for (int i = 0; i < NUM_VOICES; ++i)
    {
        voices[i].init(sr);
        voices[i].setPhysicalStringIndex(i);
    }
    body.init(sr, blockSize, paramBodySize, 1.0f);
    sympathetic.init(sr);
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
            {
                v.choke(velocity);
                activeCount++;
            }
        }

        // If no strings are actively vibrating, still produce an authentic fret slap!
        if (activeCount == 0)
        {
            voices[0].choke(velocity);
        }
        return;
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

    // If strumming is active (via parameter toggle or keyswitch),
    // held notes define the chord for the strumming engine.
    if (isStrumActive())
    {
        int heldCount = 0;
        for (int n = GUITAR_MIN_NOTE; n <= GUITAR_MAX_NOTE; ++n)
            if (heldNotes[n] > 0.001f)
                heldCount++;

        // If this is the start of a new chord (first note pressed), immediately trigger downstroke strum
        if (heldCount == 1)
        {
            const int samplesPer8th = std::max(100, static_cast<int>((30.0f / currentBpm) * sampleRate));
            strumSampleCounter = samplesPer8th;
            triggerStrum(true, velocity);
            nextStrumIsDown = false;
        }
        return;
    }

    const bool isFdtd = (paramEngineType >= 0.5f);
    const int s = assignStringForNote(midiNote);

    voices[s].setEngineType(isFdtd ? Voice::EngineType::BilbaoFdtd : Voice::EngineType::DigitalWaveguide);
    voices[s].setPhysicalStringIndex(s);

    const auto muteMode = getEffectiveMuteMode();
    voices[s].noteOn(midiNote, velocity,
                     paramBrightness, paramPickPos, paramDecay,
                     paramStiffness, muteMode, s);
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

    for (int s = 0; s < NUM_VOICES; ++s)
    {
        if (voices[s].getMidiNote() == midiNote && voices[s].isActive())
            voices[s].noteOff();
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

    // 1. Collect currently held notes from the keyboard (at most 6 notes on guitar)
    std::pair<int, float> chord[6];
    int chordSize = 0;

    for (int n = GUITAR_MIN_NOTE; n <= GUITAR_MAX_NOTE; ++n)
    {
        if (heldNotes[n] > 0.001f)
        {
            if (chordSize < 6)
            {
                chord[chordSize++] = { n, heldNotes[n] };
            }
        }
    }

    // Only strum when notes are actively held down on the keyboard.
    if (chordSize == 0)
        return;

    // 2. Clear any stale pending plucks from previous strums to prevent inter-chord collision
    for (int p = 0; p < MAX_PENDING_PLUCKS; ++p)
        pendingPlucks[p].active = false;

    // 3. Assign each note in the chord to a unique physical string (0..5)
    // Sort chord notes from lowest to highest pitch
    std::sort(chord, chord + chordSize, [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    static constexpr int openMidi[6] = { 40, 45, 50, 55, 59, 64 };
    bool stringUsed[6] = { false, false, false, false, false, false };
    int noteToString[6] = { -1, -1, -1, -1, -1, -1 };

    for (int i = 0; i < chordSize; ++i)
    {
        const int note = chord[i].first;
        int s = 0;
        if (note < 45)      s = 0;
        else if (note < 50) s = 1;
        else if (note < 55) s = 2;
        else if (note < 59) s = 3;
        else if (note < 64) s = 4;
        else                s = 5;

        if (!stringUsed[s])
        {
            stringUsed[s] = true;
            noteToString[i] = s;
        }
        else
        {
            // Search for an unused string within reachable fret span (fret <= 7)
            int bestAlt = -1;
            int bestFret = 999;
            for (int alt = 0; alt < 6; ++alt)
            {
                if (!stringUsed[alt])
                {
                    const int minOpen = (alt == 0) ? 38 : openMidi[alt];
                    if (note >= minOpen)
                    {
                        const int fret = note - minOpen;
                        if (fret <= 7 && fret < bestFret)
                        {
                            bestFret = fret;
                            bestAlt = alt;
                        }
                    }
                }
            }
            if (bestAlt >= 0)
            {
                stringUsed[bestAlt] = true;
                noteToString[i] = bestAlt;
            }
            else
            {
                noteToString[i] = s;
            }
        }
    }

    // 4. Immediately damp any currently ringing strings that are NOT part of this chord!
    // When switching chords, strings left behind are naturally muted.
    for (int s = 0; s < 6; ++s)
    {
        if (!stringUsed[s] && voices[s].isActive())
        {
            voices[s].noteOff();
        }
    }

    // 5. Directional acoustic physics & strum order:
    // Downstroke: lowest pitch / string to highest pitch / string (step = 0 .. chordSize - 1)
    // Upstroke: highest pitch / string to lowest pitch / string
    const float strokeVelScale    = isDownstroke ? 1.0f : 0.82f;
    const float strokeBrightScale = isDownstroke ? 1.0f : 1.15f;

    // Fast acoustic pick rake: ~2.5 ms (down) / ~2.0 ms (up)
    const float rakeTimeSec = isDownstroke ? 0.0025f : 0.0020f;
    const int rakeSamples = std::max(1, static_cast<int>(rakeTimeSec * sampleRate));

    for (int step = 0; step < chordSize; ++step)
    {
        const int i = isDownstroke ? step : (chordSize - 1 - step);
        const int s = noteToString[i];
        if (s < 0 || s >= 6) continue;

        float baseVel = (velOverride > 0.001f) ? velOverride : chord[i].second;
        float vel = std::clamp(baseVel * strokeVelScale, 0.05f, 1.0f);
        float brt = std::clamp(paramBrightness * strokeBrightScale, 0.05f, 1.0f);
        int delay = step * rakeSamples;

        schedulePluck(s, chord[i].first, vel, brt, delay);
    }
}

void SynthEngine::schedulePluck(int stringIdx, int midiNote, float velocity, float brightness, int delaySamples) noexcept
{
    const bool isFdtd = (paramEngineType >= 0.5f);
    const int s = std::clamp(stringIdx, 0, 5);

    if (delaySamples <= 0)
    {
        voices[s].setEngineType(isFdtd ? Voice::EngineType::BilbaoFdtd : Voice::EngineType::DigitalWaveguide);
        voices[s].setPhysicalStringIndex(s);

        const auto muteMode = getEffectiveMuteMode();
        voices[s].noteOn(midiNote, velocity,
                         brightness, paramPickPos, paramDecay,
                         paramStiffness, muteMode, s);
        return;
    }

    for (int p = 0; p < MAX_PENDING_PLUCKS; ++p)
    {
        if (!pendingPlucks[p].active)
        {
            pendingPlucks[p].stringIdx    = s;
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

        for (auto& v : voices)
            v.setMuteMode(currentMuteMode);

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
                    const bool isFdtd = (paramEngineType >= 0.5f);
                    const int s = std::clamp(pendingPlucks[p].stringIdx, 0, 5);

                    voices[s].setEngineType(isFdtd ? Voice::EngineType::BilbaoFdtd : Voice::EngineType::DigitalWaveguide);
                    voices[s].setPhysicalStringIndex(s);

                    const auto muteMode = getEffectiveMuteMode();
                    voices[s].noteOn(pendingPlucks[p].midiNote,
                                     pendingPlucks[p].velocity,
                                     pendingPlucks[p].brightness,
                                     paramPickPos, paramDecay,
                                     paramStiffness, muteMode, s);
                    pendingPlucks[p].active = false;
                }
            }
        }

        // Update open-string fretting damping:
        // If an active voice is fretted on physical string s, its open length is damped
        bool stringActive[6] = { false, false, false, false, false, false };
        for (int v = 0; v < NUM_VOICES; ++v)
        {
            if (voices[v].isActive())
            {
                const int sIdx = voices[v].getPhysicalStringIndex();
                if (sIdx >= 0 && sIdx < 6)
                    stringActive[sIdx] = true;
            }
        }
        for (int s = 0; s < 6; ++s)
            sympathetic.setStringFretted(s, stringActive[s]);

        sympathetic.setMuted(isPalmMuteActive(), isFullMuteActive());

        float totalForceV = 0.f;
        float totalForceH = 0.f;
        int activeVoices = 0;

        for (int v = 0; v < NUM_VOICES; ++v)
        {
            if (!voices[v].isActive())
                continue;

            activeVoices++;
            float fV = 0.0f, fH = 0.0f;
            voices[v].tickPolar(fV, fH);
            totalForceV += fV;
            totalForceH += fH;
        }

        // Cross-bridge sympathetic resonance:
        // Driving vertical bridge force excites unplayed open strings, scaled subtly by acoustic body coupling
        const float sympatheticForce = sympathetic.process(totalForceV);
        const float coupledForceV = totalForceV + sympatheticForce * (0.05f * paramBodyMix);

        // Polyphonic bridge impedance headroom:
        // Acoustic soundboard mechanical impedance distributes multi-string chord displacement (1 / sqrt(N))
        const float polyScale = (activeVoices > 1)
            ? (1.0f / std::sqrt(static_cast<float>(activeVoices)))
            : 1.0f;

        // Dual-Axis Saddle Polar Radiation:
        // Vertical force (V) pumps soundboard directly (100% into both channels in-phase).
        // Lateral force (H) rocks the saddle across the bridge plate, exciting asymmetrical
        // dipole modes (creating subtle anti-phase rocking moment in stereo soundboard field).
        float driveL = (coupledForceV + 0.35f * totalForceH) * polyScale;
        float driveR = (coupledForceV - 0.35f * totalForceH) * polyScale;

        // Soundboard Compliance Bloom / Soft Wood Saturation:
        // Under heavy strumming and multi-string plucks, physical spruce soundboard compliance
        // gently saturates, bloom-compressing raw bridge force transients before cavity resonance.
        auto soundboardBloom = [](float x) noexcept -> float
        {
            constexpr float knee = 1.10f;
            const float ax = std::abs(x);
            if (ax <= knee) return x;
            const float excess = ax - knee;
            const float sat = knee + 0.45f * std::tanh(excess / 0.45f);
            return (x > 0.0f ? 1.0f : -1.0f) * sat;
        };

        outputL[i] = soundboardBloom(driveL);
        outputR[i] = soundboardBloom(driveR);
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

        // Robust NaN/Inf safety guards: prevent corrupted audio samples from locking DC blocker state
        if (std::isnan(outL) || std::isinf(outL)) { outL = 0.f; dcX_L = 0.f; dcY_L = 0.f; }
        if (std::isnan(outR) || std::isinf(outR)) { outR = 0.f; dcX_R = 0.f; dcY_R = 0.f; }

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
    sympathetic.reset();
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
// Physical 6-String Guitar Allocation & Monophonic String Choking
// ---------------------------------------------------------------------------

int SynthEngine::assignStringForNote(int note) const noexcept
{
    // Acoustic guitar open string pitches (standard tuning: E2, A2, D3, G3, B3, E4)
    static constexpr int openMidi[6] = { 40, 45, 50, 55, 59, 64 };

    // 1. Natural first-position string:
    // String 0: E2..G#2 (MIDI 38..44)
    // String 1: A2..C#3 (MIDI 45..49)
    // String 2: D3..F#3 (MIDI 50..54)
    // String 3: G3..A#3 (MIDI 55..58)
    // String 4: B3..D#4 (MIDI 59..63)
    // String 5: E4..D6  (MIDI 64..86)
    int s = 0;
    if (note < 45)      s = 0;
    else if (note < 50) s = 1;
    else if (note < 55) s = 2;
    else if (note < 59) s = 3;
    else if (note < 64) s = 4;
    else                s = 5;

    // 2. If natural string is idle, or is already playing this exact note (repeated pluck), use it
    if (!voices[s].isActive() || voices[s].getMidiNote() == note)
        return s;

    // 3. String s is currently active.
    // Check if the note sounding on string s is physically held on the keyboard (i.e. part of a held chord):
    const int activeNoteOnS = voices[s].getMidiNote();
    const bool isChordHeld = (activeNoteOnS >= 0 && activeNoteOnS < 128 && heldNotes[activeNoteOnS] > 0.001f);

    if (isChordHeld)
    {
        // User is holding down multiple keys in a chord.
        // Search for an idle physical string that can comfortably voice this note in reachable hand reach (fret <= 5).
        int bestAlt = -1;
        int bestFret = 999;
        for (int alt = 0; alt < 6; ++alt)
        {
            if (!voices[alt].isActive())
            {
                const int minOpen = (alt == 0) ? 38 : openMidi[alt];
                if (note >= minOpen)
                {
                    const int fret = note - minOpen;
                    if (fret <= 5 && fret < bestFret)
                    {
                        bestFret = fret;
                        bestAlt = alt;
                    }
                }
            }
        }
        if (bestAlt >= 0)
            return bestAlt;
    }

    // 4. Strict physical guitar choking:
    // If the old note was already released (e.g. arpeggio or melodic line), or no free string is available,
    // the new note is fretted on string s, physically damping/choking the old vibration.
    return s;
}

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
