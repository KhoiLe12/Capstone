#include "Strummer.h"
#include <cmath>

void Strummer::init(float sr)
{
    sampleRate = sr;
    reset();
}

void Strummer::scheduleNote(int stringIndex, int midiNote, float velocity, float strumSpeed)
{
    const float speed = std::max(0.0f, std::min(strumSpeed, 1.0f));

    // If strumming is disabled, schedule for immediate trigger (0 delay)
    if (speed < 0.01f)
    {
        for (auto& ev : queue)
        {
            if (!ev.active)
            {
                ev = { stringIndex, midiNote, velocity, 0, true };
                return;
            }
        }
        return;
    }

    // Chord clustering window: notes arriving within 35 ms of each other belong to the same strum stroke
    const int chordWindowSamples = static_cast<int>(0.035f * sampleRate);
    if (samplesSinceLastPluck > chordWindowSamples)
    {
        // New stroke begins: the first note always strikes with 0 delay (instant melody/first note)
        chordNoteIndex        = 0;
        samplesSinceLastPluck = 0;
        lastWasDownstroke     = !lastWasDownstroke;
    }

    // Stagger delay between successive strings in the chord (6 ms to 30 ms)
    const float gapSec = 0.006f + speed * 0.024f;
    const float delaySec = static_cast<float>(chordNoteIndex) * gapSec;
    const int delaySamples = static_cast<int>(delaySec * sampleRate);

    ++chordNoteIndex;

    // Insert into queue
    for (auto& ev : queue)
    {
        if (!ev.active)
        {
            ev = { stringIndex, midiNote, velocity, delaySamples, true };
            return;
        }
    }
}

bool Strummer::tick(int& outString, int& outNote, float& outVelocity) noexcept
{
    if (samplesSinceLastPluck < 100000)
        ++samplesSinceLastPluck;

    // Check if any scheduled note is ready
    for (auto& ev : queue)
    {
        if (ev.active)
        {
            if (--ev.samplesRemaining <= 0)
            {
                outString   = ev.stringIndex;
                outNote     = ev.midiNote;
                outVelocity = ev.velocity;
                ev.active   = false;
                return true;
            }
        }
    }

    return false;
}

void Strummer::reset() noexcept
{
    for (auto& ev : queue)
        ev.active = false;
    lastWasDownstroke     = false;
    samplesSinceLastPluck = 99999;
    chordNoteIndex        = 0;
}

