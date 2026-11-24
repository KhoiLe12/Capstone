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
    polyScaleFilterCoeff = std::exp(-1.0f / (0.015f * sampleRate));
    legatoMinOnsetSamples = static_cast<int>(kLegatoChordWindowSec * sampleRate);
    smoothedPolyScale = 1.0f;
    cabFilter.setLowpass(4800.0f, 0.707f, sampleRate);
    reset();
}

// ---------------------------------------------------------------------------
// MIDI Event Handlers
// ---------------------------------------------------------------------------

void SynthEngine::noteOn(int midiNote, float velocity)
{
    noteOn(midiNote, velocity, -1.0f, -1.0f);
}

void SynthEngine::noteOn(int midiNote, float velocity, float brightnessOverride, float pickPosOverride)
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

    // Intercept Flamenco Golpe (Soundboard Thumb Knock) Keyswitch F1 (MIDI Note 29 / FL: F2)
    if (midiNote == KEYSWITCH_GOLPE)
    {
        triggerGolpe(velocity);
        return;
    }

    // 4. Strict Physical Guitar Range Validation
    // Standard acoustic guitar range: D2 (38, Drop D) / E2 (40) up to D6 (86, 22nd fret high E)
    // Any notes outside this range are rejected so keyswitches never produce unwanted audio
    if (midiNote < GUITAR_MIN_NOTE || midiNote > GUITAR_MAX_NOTE)
    {
        return;
    }

    // If strumming is active (via parameter toggle or keyswitch),
    // held notes define the chord for the strumming engine.
    if (isStrumActive())
    {
        heldNotes[midiNote] = velocity;
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

    // ── Legato Articulation Detection (DAW Piano Roll Overlapping Notes) ──
    // On acoustic guitar, a hammer-on or pull-off occurs ONLY when playing a monophonic lead line
    // along the SAME physical string, where the new note starts while the previous fret is still held.
    // Chords and non-overlapping notes MUST ALWAYS be played with fresh plectrum pick strikes!
    int legatoVoice = -1;
    if (paramLegatoEnabled && !isStrumActive() && !isFullMuteActive())
    {
        // Count how many notes are actively held down in the DAW before this new note arrived:
        int numCurrentlyHeld = 0;
        int heldVoiceIdx = -1;
        int heldMidiNote = -1;

        for (int v = 0; v < NUM_VOICES; ++v)
        {
            if (voices[v].isActive())
            {
                const int n = voices[v].getMidiNote();
                if (n > 0 && heldNotes[n] > 0.001f)
                {
                    heldVoiceIdx = v;
                    heldMidiNote = n;
                    ++numCurrentlyHeld;
                }
            }
        }

        // Legato is strictly valid ONLY for monophonic lead transitions where exactly 1 note is held:
        if (numCurrentlyHeld == 1 && heldVoiceIdx >= 0)
        {
            // The held note must have started at least kLegatoChordWindowSec (40 ms) earlier, so the
            // notes of a rolled / humanised chord (typically 10-40 ms apart) stay a chord:
            if (voices[heldVoiceIdx].getSamplesSinceNoteOn() >= legatoMinOnsetSamples)
            {
                const int interval = std::abs(midiNote - heldMidiNote);
                if (interval >= 1 && interval <= 12)
                {
                    const int heldStr = voices[heldVoiceIdx].getPhysicalStringIndex();
                    if (heldStr >= 0 && heldStr < 6)
                    {
                        static constexpr int openMidi[6] = { 40, 45, 50, 55, 59, 64 };
                        const int minOpen = (heldStr == 0) ? 38 : openMidi[heldStr];
                        const int fret = midiNote - minOpen;

                        // Physical Hammer-On / Pull-Off Invariant:
                        // A true acoustic legato transition occurs strictly along the SAME physical string.
                        // Notes on different natural strings belong to separate open/fretted strings, enabling
                        // rich polyphonic acoustic voicings (chords, dyads, double-stops) without dropping notes.
                        const bool samePhysicalString = (naturalStringForNote(midiNote) == heldStr);

                        if (samePhysicalString && fret >= 0 && fret <= kMaxFret)
                        {
                            legatoVoice = heldVoiceIdx;
                        }
                    }
                }
            }
        }
    }

    if (legatoVoice >= 0)
    {
        const int oldNote = voices[legatoVoice].getMidiNote();
        const bool isHammerOn = (midiNote > oldNote);
        heldNotes[oldNote] = 0.0f; // Hand position transferred to new note
        heldNotes[midiNote] = velocity;
        voices[legatoVoice].legatoTransition(midiNote, velocity, isHammerOn, paramStiffness);
        return;
    }

    heldNotes[midiNote] = velocity;
    const int s = assignStringForNote(midiNote, !paramStrictFretboard);
    startNoteOnString(midiNote, velocity, s, brightnessOverride, pickPosOverride);
}

void SynthEngine::startNoteOnString(int midiNote, float velocity, int s, float brightness, float pickPos)
{
    const bool isFdtd = (paramEngineType >= 0.5f);
    voices[s].setEngineType(isFdtd ? Voice::EngineType::BilbaoFdtd : Voice::EngineType::DigitalWaveguide);
    voices[s].setPhysicalStringIndex(s);
    const bool isElectric = (paramInstrumentMode >= 0.5f);
    voices[s].setInstrumentMode(isElectric ? 1 : 0);
    voices[s].setGuitarModel(static_cast<int>(std::round(paramGuitarModel)));

    // On-board: the string's own gauge. Off-board (past fret kMaxFret or below the open pitch):
    // sound with the gauge of the note's natural string so tone and level match a fretted note.
    const int fret = midiNote - stringMinOpen(s);
    const int geometry = (fret >= 0 && fret <= kMaxFret) ? s : naturalStringForNote(midiNote);

    const auto muteMode = getEffectiveMuteMode();
    const int mat = static_cast<int>(paramStringMaterial + 0.5f);
    voices[s].noteOn(midiNote, velocity,
                     (brightness >= 0.0f ? brightness : paramBrightness),
                     (pickPos >= 0.0f ? pickPos : paramPickPos),
                     paramDecay,
                     paramStiffness, muteMode, s, mat, geometry);
}

void SynthEngine::noteOnChord(const Event* const* chordEvents, int count)
{
    // Auto-strum: held notes define the chord; the strum engine does its own assignment
    if (isStrumActive())
    {
        for (int i = 0; i < count; ++i)
            dispatch(*chordEvents[i]);
        return;
    }

    // Sort by pitch so the solver's output order is deterministic
    const Event* sorted[NUM_VOICES];
    for (int i = 0; i < count; ++i)
        sorted[i] = chordEvents[i];
    std::sort(sorted, sorted + count, [](const Event* a, const Event* b) { return a->note < b->note; });

    int notes[NUM_VOICES];
    int strings[NUM_VOICES];
    for (int i = 0; i < count; ++i)
    {
        notes[i] = sorted[i]->note;
        heldNotes[notes[i]] = sorted[i]->velocity;
    }

    // Simultaneous notes are a chord, never a legato transition
    assignChordStrings(notes, count, !paramStrictFretboard, false, strings);

    for (int i = 0; i < count; ++i)
        if (strings[i] >= 0)
            startNoteOnString(notes[i], sorted[i]->velocity, strings[i], sorted[i]->brightness, sorted[i]->pickPos);
}

void SynthEngine::dispatchGroup(const Event* events, int count)
{
    // Within one sample: note-offs, all-notes-off and keyswitches first (in arrival order), so a
    // keyswitch on the same tick as a chord applies to it and a note ending where the next begins
    // releases the old note instead of killing the new one. Guitar-range note-ons form a chord.
    const Event* chord[NUM_VOICES];
    int chordCount = 0;
    for (int i = 0; i < count; ++i)
    {
        const Event& e = events[i];
        const bool isChordNote = e.type == Event::Type::NoteOn && e.velocity > 0.0001f
                              && e.note >= GUITAR_MIN_NOTE && e.note <= GUITAR_MAX_NOTE;
        if (!isChordNote)
            dispatch(e);
    }
    for (int i = 0; i < count; ++i)
    {
        const Event& e = events[i];
        const bool isChordNote = e.type == Event::Type::NoteOn && e.velocity > 0.0001f
                              && e.note >= GUITAR_MIN_NOTE && e.note <= GUITAR_MAX_NOTE;
        if (!isChordNote)
            continue;
        if (chordCount < NUM_VOICES)
            chord[chordCount++] = &e;
        else
            dispatch(e); // more than 6 simultaneous notes: extra notes take the single-note path
    }

    if (chordCount == 1)
        dispatch(*chord[0]);
    else if (chordCount > 1)
        noteOnChord(chord, chordCount);
}

namespace
{
    struct ChordSearch
    {
        const int* notes;
        int count;
        bool allowOffBoard;
        bool ringing[6];
        int minOpen[6];
        int maxFret;

        int current[6];
        int best[6];
        // Score: physical count, placed count, ringing strings kept, -hand span, -fret sum
        int bestScore[5];
        bool haveBest = false;

        void evaluate()
        {
            int physical = 0, placed = 0, ringingUsed = 0, fretSum = 0;
            int lo = 1000, hi = -1;
            for (int i = 0; i < count; ++i)
            {
                const int s = current[i];
                if (s < 0) continue;
                ++placed;
                if (ringing[s]) ++ringingUsed;
                const int fret = notes[i] - minOpen[s];
                if (fret >= 0 && fret <= maxFret)
                {
                    ++physical;
                    fretSum += fret;
                    if (fret > 0) { lo = std::min(lo, fret); hi = std::max(hi, fret); }
                }
            }
            const int span = (hi >= 0) ? (hi - lo) : 0;
            // Ergonomic physical priority: physical fret count, placed note count, compact hand span, low fretboard register, then tie-break with ringing strings kept
            const int score[5] = { physical, placed, -span, -fretSum, -ringingUsed };
            bool better = !haveBest;
            for (int k = 0; k < 5 && !better; ++k)
            {
                if (score[k] != bestScore[k]) { better = score[k] > bestScore[k]; break; }
            }
            if (better)
            {
                haveBest = true;
                std::copy(score, score + 5, bestScore);
                std::copy(current, current + count, best);
            }
        }

        void search(int i, unsigned usedMask)
        {
            if (i == count) { evaluate(); return; }
            current[i] = -1; // leave this note unplaced
            search(i + 1, usedMask);
            for (int s = 0; s < 6; ++s)
            {
                if (usedMask & (1u << s)) continue;
                const int fret = notes[i] - minOpen[s];
                if (!allowOffBoard && (fret < 0 || fret > maxFret)) continue;
                current[i] = s;
                search(i + 1, usedMask | (1u << s));
            }
        }
    };
}

void SynthEngine::assignChordStrings(const int* notes, int count, bool allowOffBoard, bool avoidRinging,
                                     int* outString) const noexcept
{
    count = std::min(count, NUM_VOICES);
    ChordSearch cs;
    cs.notes = notes;
    cs.count = count;
    cs.allowOffBoard = allowOffBoard;
    cs.maxFret = kMaxFret;
    for (int s = 0; s < NUM_VOICES; ++s)
    {
        cs.ringing[s] = avoidRinging && voices[s].isActive();
        cs.minOpen[s] = stringMinOpen(s);
    }
    cs.search(0, 0u);
    for (int i = 0; i < count; ++i)
        outString[i] = cs.haveBest ? cs.best[i] : -1;
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

    // Intercept Golpe Keyswitch F1 release (momentary percussive knock)
    if (midiNote == KEYSWITCH_GOLPE)
    {
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
// Flamenco Golpe & Soundboard Thumb Knock Engine
// ---------------------------------------------------------------------------

void SynthEngine::triggerGolpe(float velocity) noexcept
{
    const float clampedVel = std::clamp(velocity, 0.05f, 1.0f);
    // Contact duration for thumb impact on spruce soundboard:
    // ~2.4 ms at high velocity to ~3.4 ms at gentle tap (viscoelastic flesh compliance)
    const float durationSec = 0.0034f * (1.0f - 0.28f * clampedVel);
    const int numSamples = std::max(16, static_cast<int>(durationSec * sampleRate));
    golpeTotalSamples = numSamples;
    golpeSamplesLeft  = numSamples;
    golpeSampleIdx    = 0;
    golpeVelocity     = clampedVel;
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

    // Joint best-fit string assignment (same solver as simultaneous note-ons). The strum re-picks
    // every chord string, so ringing strings are not avoided.
    int chordNotes[6];
    int noteToString[6] = { -1, -1, -1, -1, -1, -1 };
    for (int i = 0; i < chordSize; ++i)
        chordNotes[i] = chord[i].first;
    assignChordStrings(chordNotes, chordSize, !paramStrictFretboard, false, noteToString);

    bool stringUsed[6] = { false, false, false, false, false, false };
    for (int i = 0; i < chordSize; ++i)
        if (noteToString[i] >= 0)
            stringUsed[noteToString[i]] = true;

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
    const int s = std::clamp(stringIdx, 0, 5);

    if (delaySamples <= 0)
    {
        startNoteOnString(midiNote, velocity, s, brightness, -1.0f);
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

void SynthEngine::dispatch(const Event& e)
{
    switch (e.type)
    {
        case Event::Type::NoteOn:      noteOn(e.note, e.velocity, e.brightness, e.pickPos); break;
        case Event::Type::NoteOff:     noteOff(e.note);            break;
        case Event::Type::AllNotesOff: allNotesOff();              break;
    }
}

void SynthEngine::allNotesOff() noexcept
{
    for (auto& v : voices)
        v.reset();
    sympathetic.reset();
    passiveElectronics.reset();
    cabFilter.reset();
    keyswitchPalmMuteActive = false;
    keyswitchFullMuteActive = false;
    keyswitchStrumActive    = false;
    strumSampleCounter      = 0;
    nextStrumIsDown         = true;
    golpeSamplesLeft        = 0;
    golpeTotalSamples       = 0;
    golpeSampleIdx          = 0;
    golpeVelocity           = 0.0f;
    std::fill(std::begin(heldNotes), std::end(heldNotes), 0.f);
    for (int p = 0; p < MAX_PENDING_PLUCKS; ++p)
        pendingPlucks[p].active = false;
    prevMuteMode = getEffectiveMuteMode();
}

void SynthEngine::process(float* outputL, float* outputR, int numSamples,
                          const Event* events, int numEvents) noexcept
{
    int nextEvent = 0;

    // Update body parameters (size, damping, coupling, bodyType)
    const bool isElectric = (paramInstrumentMode >= 0.5f);
    const auto guitarModelEnum = (paramGuitarModel >= 0.5f) ? ElectricGuitar::GuitarModel::LesPaul1959 : ElectricGuitar::GuitarModel::Stratocaster1962;
    if (isElectric)
    {
        passiveElectronics.update(guitarModelEnum, paramToneKnob, sampleRate);
    }
    else
    {
        const int bodyType = static_cast<int>(std::round(paramBodyType));
        body.setParameters(paramBodySize, 1.0f, paramBodyMix, bodyType);
    }

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

    // Symmetrical acoustic soundboard reflection delays (d1 = 5.2ms, d2 = 9.6ms)
    const int d1 = std::clamp(static_cast<int>(sampleRate * 0.0052f), 4, REFLECTION_BUF_SIZE - 1);
    const int d2 = std::clamp(static_cast<int>(sampleRate * 0.0096f), 8, REFLECTION_BUF_SIZE - 1);
    const float widthParam = std::clamp(paramStereoWidth, 0.0f, 1.0f);

    // 1. Advance all active string voices and sum bridge force into output buffers.
    for (int i = 0; i < numSamples; ++i)
    {
        // Sample-accurate MIDI: apply every event scheduled at (or, if mis-sorted, before) sample i
        if (nextEvent < numEvents && events[nextEvent].sampleOffset <= i)
        {
            int groupEnd = nextEvent;
            while (groupEnd < numEvents && events[groupEnd].sampleOffset <= i)
                ++groupEnd;
            dispatchGroup(events + nextEvent, groupEnd - nextEvent);
            nextEvent = groupEnd;
        }

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
                    startNoteOnString(pendingPlucks[p].midiNote, pendingPlucks[p].velocity,
                                      std::clamp(pendingPlucks[p].stringIdx, 0, 5),
                                      pendingPlucks[p].brightness, -1.0f);
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
        int activeVoices = 0;
        float totalClick = 0.f;
        float totalPickupVoltage = 0.0f;

        const auto pickupPosEnum = static_cast<ElectricGuitar::PickupPosition>(std::clamp(static_cast<int>(std::round(paramPickupPosition)), 0, 5));
        const int pickupTypeOverride = static_cast<int>(std::round(paramPickupType));

        for (int v = 0; v < NUM_VOICES; ++v)
        {
            if (!isElectric)
                totalClick += voices[v].tickPickClick();

            if (!voices[v].isActive())
                continue;

            activeVoices++;
            float fV = 0.0f, fH = 0.0f;
            voices[v].tickPolar(fV, fH);

            if (isElectric)
            {
                int sIdx = voices[v].getPhysicalStringIndex();
                if (sIdx < 0 || sIdx >= 6) sIdx = v;
                const auto pCfg = ElectricGuitar::getPickupConfig(guitarModelEnum, pickupPosEnum, sIdx, pickupTypeOverride);
                float v_ind = pCfg.mixGain * voices[v].getPickupVelocity(pCfg.distBridge1, pCfg.isHumbucker1, pCfg.poleSpacing1);
                if (pCfg.numActivePickups > 1)
                    v_ind += pCfg.mixGain * voices[v].getPickupVelocity(pCfg.distBridge2, pCfg.isHumbucker2, pCfg.poleSpacing2);
                totalPickupVoltage += v_ind;
            }
            else
            {
                // Vertical (soundboard pumping) drives the saddle directly; the horizontal polarization
                // couples through lateral saddle rocking at ~35% (same ratio as the waveguide engine).
                // The slightly detuned H plane is what produces the natural two-stage decay and beating.
                // All string fundamentals are centered (constant spatial location for every note).
                totalForceV += fV + kHorizontalSaddleCoupling * fH;
            }
        }

        // Flamenco Golpe / Soundboard Thumb Knock Physical Mechanics:
        // Transmits mechanical compressive force pulse into top plate, air cavity, and bridge
        float golpeBodyForce = 0.0f;
        float golpeDirectForce = 0.0f;
        if (golpeSamplesLeft > 0)
        {
            const float tau = static_cast<float>(golpeSampleIdx) / static_cast<float>(golpeTotalSamples);
            // Hertzian compressive contact pulse: sin^2 gives zero onset/offset discontinuity
            // Normalized so peak amplitude = 1.0 (occurring at tau ~ 0.39)
            const float sinTau = std::sin(3.14159265f * tau);
            const float contactPulse = 2.67f * sinTau * sinTau * std::exp(-2.2f * tau);

            // 1. Mechanical normal force driving the soundboard wood and internal air cavity:
            // High-energy acoustic punch driving A0 (~100 Hz) air pump and T(1,1) (~200 Hz) wood dipole
            golpeBodyForce = 4.8f * std::pow(golpeVelocity, 1.20f) * contactPulse;

            // 2. Localized near-field acoustic flexural and wood-tap transient:
            // Combines deep localized top-plate flexure (~180 Hz) and crisp spruce contact crackle (~1400 Hz)
            const float flexPhase = 2.0f * 3.14159265f * 180.0f * (static_cast<float>(golpeSampleIdx) / sampleRate);
            const float tapPhase  = 2.0f * 3.14159265f * 1400.0f * (static_cast<float>(golpeSampleIdx) / sampleRate);
            const float flexWave  = std::sin(flexPhase) * (1.0f - tau);
            const float tapClick  = 0.45f * std::sin(tapPhase) * std::exp(-8.0f * tau);
            golpeDirectForce = 0.65f * std::pow(golpeVelocity, 1.10f) * (flexWave + tapClick) * sinTau;

            ++golpeSampleIdx;
            --golpeSamplesLeft;
        }

        if (i < CLICK_BUF_SIZE)
            clickBuf[i] = totalClick + golpeDirectForce;

        // Cross-bridge sympathetic resonance:
        // Driving vertical bridge force (including top-plate golpe shockwave) excites unplayed open strings
        const float sympatheticForce = sympathetic.process(totalForceV + golpeBodyForce);
        const float coupledSymp = sympatheticForce * (0.12f * paramBodyMix);
        const float stringBridgeForce = totalForceV + coupledSymp;

        // Polyphonic bridge impedance headroom:
        // Acoustic soundboard mechanical impedance distributes multi-string chord displacement at the saddle.
        // Real-time smooth slewing (tau ~ 15ms) eliminates zipper/stepping discontinuities when notes are strummed.
        const float targetPolyScale = (activeVoices > 1)
            ? (1.0f / std::sqrt(1.0f + 0.70f * static_cast<float>(activeVoices - 1)))
            : 1.0f;
        smoothedPolyScale = targetPolyScale + polyScaleFilterCoeff * (smoothedPolyScale - targetPolyScale);

        // Soundboard mechanical drive:
        // Vibrating strings deliver force through the bridge saddle (scaled by polyphonic impedance).
        // Soundboard golpe delivers mechanical impact force directly into the spruce plate, unaffected
        // by the number of active vibrating strings on the fretboard!
        const float monoBridge = stringBridgeForce * smoothedPolyScale + golpeBodyForce;

        if (isElectric)
        {
            // Calibrate physical string transverse velocity (v ~ 1.0 m/s) to standard instrument DI voltage
            // (-12 to -6 dBFS). This guarantees the +8 dB resonant peak from the passive RLC tone biquad
            // (Q = 2.6) never hits the soft-limiter threshold (0.85), eliminating all clipping fuzz and
            // delivering a clean, uncompressed DI output directly into DAW amp modelers.
            float diSample = totalPickupVoltage * smoothedPolyScale * 0.22f;
            diSample = passiveElectronics.process(diSample);
            if (paramCabAirFilter)
                diSample = cabFilter.process(diSample);

            if (widthParam > 0.001f)
            {
                reflectionBuf[reflectionIdx] = diSample;
                const int read1 = (reflectionIdx - d1 + REFLECTION_BUF_SIZE) % REFLECTION_BUF_SIZE;
                const int read2 = (reflectionIdx - d2 + REFLECTION_BUF_SIZE) % REFLECTION_BUF_SIZE;
                reflectionIdx = (reflectionIdx + 1) % REFLECTION_BUF_SIZE;

                const float ref1 = reflectionBuf[read1];
                const float ref2 = reflectionBuf[read2];
                const float rL =  0.20f * ref1 - 0.15f * ref2;
                const float rR = -0.15f * ref1 + 0.20f * ref2;
                outputL[i] = diSample + widthParam * rL;
                outputR[i] = diSample + widthParam * rR;
            }
            else
            {
                outputL[i] = diSample;
                outputR[i] = diSample;
            }
        }
        else
        {
            // Symmetrical Acoustic Soundboard Reflection Network:
            // Symmetrical reflection taps guarantee bit-exact attack centering (0.00 dB L/R difference across all registers).
            reflectionBuf[reflectionIdx] = monoBridge;
            const int read1 = (reflectionIdx - d1 + REFLECTION_BUF_SIZE) % REFLECTION_BUF_SIZE;
            const int read2 = (reflectionIdx - d2 + REFLECTION_BUF_SIZE) % REFLECTION_BUF_SIZE;
            reflectionIdx = (reflectionIdx + 1) % REFLECTION_BUF_SIZE;

            const float ref1 = reflectionBuf[read1];
            const float ref2 = reflectionBuf[read2];

            // Symmetrical mirrored diffuse aura:
            const float rL =  0.22f * ref1 - 0.16f * ref2;
            const float rR = -0.16f * ref1 + 0.22f * ref2;

            float driveL = monoBridge + widthParam * rL;
            float driveR = monoBridge + widthParam * rR;

            // Pure Linear Bridge Force Radiation into Acoustic Soundboard:
            // Delivering clean linear force into the body IR/modal resonators eliminates all inharmonic
            // intermodulation distortion (IMD) on chords and prevents discontinuous knee-switching crackles.
            outputL[i] = driveL;
            outputR[i] = driveR;
        }
    }

    // Events with offsets past the end of the block (out-of-range timestamps) still take effect
    if (nextEvent < numEvents)
        dispatchGroup(events + nextEvent, numEvents - nextEvent);

    if (!isElectric)
    {
        // 2. Drive the acoustic body soundboard (Stereo IR convolution or 32-Mode Modal Bank)
        body.processBlock(outputL, outputR, numSamples);

        // 3. Direct Airborne Plectrum Radiation:
        // Injects the direct high-frequency pick snap (~4–12 kHz) directly into both channels,
        // bypassing the soundboard body lowpass filter while preserving master gain and DC blocking.
        for (int i = 0; i < numSamples; ++i)
        {
            const float click = (i < CLICK_BUF_SIZE) ? clickBuf[i] : 0.0f;
            outputL[i] += click;
            outputR[i] += click;
        }
    }

    // 4. DC Blocker, Master Gain, and Transparent Soft Saturation
    constexpr float R = 0.9974f;

    // Musical soft-limiter: transparent analog-style saturation only at extreme levels
    auto softLimit = [](float x) noexcept -> float
    {
        constexpr float threshold = 0.85f;
        constexpr float ceiling   = 0.99f;
        const float absX = std::abs(x);
        if (absX <= threshold)
            return x;
        const float excess = absX - threshold;
        const float compressed = threshold + (ceiling - threshold) * std::tanh(excess / (ceiling - threshold));
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
        outL *= (paramMasterGain * 0.78f);
        outR *= (paramMasterGain * 0.78f);

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
    passiveElectronics.reset();
    cabFilter.reset();
    dcX_L = 0.f; dcY_L = 0.f;
    dcX_R = 0.f; dcY_R = 0.f;
    std::fill_n(reflectionBuf, REFLECTION_BUF_SIZE, 0.f);
    std::fill_n(clickBuf, CLICK_BUF_SIZE, 0.f);
    reflectionIdx = 0;
    keyswitchPalmMuteActive = false;
    keyswitchFullMuteActive = false;
    keyswitchStrumActive    = false;
    strumSampleCounter      = 0;
    nextStrumIsDown         = true;
    golpeSamplesLeft        = 0;
    golpeTotalSamples       = 0;
    golpeSampleIdx          = 0;
    golpeVelocity           = 0.0f;
    lastStrumWasDown        = true;
    std::fill(std::begin(heldNotes), std::end(heldNotes), 0.f);
    for (int p = 0; p < MAX_PENDING_PLUCKS; ++p)
        pendingPlucks[p].active = false;
    prevMuteMode            = KarplusStrong::MuteMode::Open;
    smoothedPolyScale       = 1.0f;
}

// ---------------------------------------------------------------------------
// Physical 6-String Guitar Allocation & Monophonic String Choking
// ---------------------------------------------------------------------------

int SynthEngine::assignStringForNote(int note, bool allowOffBoard) const noexcept
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

    // 2. If natural string is completely idle, use it immediately
    if (!voices[s].isActive())
        return s;

    // 2b. If the note on natural string s is not currently held down (i.e. merely decaying in release),
    // fretting string s with the new note naturally stops the release tail and sounds the note at its
    // authentic physical position instead of contorting the melody onto high frets of other strings.
    const int oldNote = voices[s].getMidiNote();
    const bool oldHeld = (oldNote >= 0 && oldNote < 128 && heldNotes[oldNote] > 0.001f);
    if (!oldHeld)
        return s;

    // 3. String s is currently physically held down by the player.
    // Search for an idle physical string that can voice this note (fret <= kMaxFret).
    // This preserves ringing chord tails and prevents violent buffer re-trigger clobbering clicks!
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
                if (fret <= kMaxFret && fret < bestFret)
                {
                    bestFret = fret;
                    bestAlt = alt;
                }
            }
        }
    }
    if (bestAlt >= 0)
        return bestAlt;

    // 3b. No physical position free: unless the fretboard is strict, use an idle string past the
    // fretboard (exact pitch) rather than re-plucking a ringing string.
    if (allowOffBoard)
    {
        for (int alt = 5; alt >= 0; --alt)
            if (!voices[alt].isActive() && note >= stringMinOpen(alt))
                return alt;
    }

    // 4. If all alternative strings are occupied, reuse string s
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
