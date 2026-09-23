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

    // A chord cluster window is ~50 ms. If new notes arrive after 50 ms, it's a new strum stroke.
    const int newStrokeThreshold = static_cast<int>(0.050f * sampleRate);
    if (samplesSinceLastPluck > newStrokeThreshold)
    {
        lastWasDownstroke = !lastWasDownstroke;
        samplesSinceLastPluck = 0;
    }

    // Delay per string: 3 ms to 18 ms per string depending on speed
    const float gapSec = 0.003f + speed * 0.015f;
    float delaySec = 0.f;

    if (lastWasDownstroke)
    {
        // Downstroke: Low E (string 0) plucks first -> High E (string 5) last
        delaySec = static_cast<float>(stringIndex) * gapSec;
    }
    else
    {
        // Upstroke: High E (string 5) plucks first -> Low E (string 0) last
        delaySec = static_cast<float>(5 - stringIndex) * gapSec;
    }

    const int delaySamples = static_cast<int>(delaySec * sampleRate);

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
}

