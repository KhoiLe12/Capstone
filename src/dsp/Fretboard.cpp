#include "Fretboard.h"
#include <algorithm>
#include <climits>

Fretboard::Assignment Fretboard::allocateNote(int midiNote,
                                              const std::array<bool, NUM_STRINGS>& stringActive) const noexcept
{
    // Clamp to playable guitar range (E2 = 40 to D6 = 86)
    const int note = std::max(kOpenStrings[0], std::min(midiNote, kOpenStrings[NUM_STRINGS - 1] + NUM_FRETS));

    // 1. Gather all strings physically capable of playing this pitch
    int bestString = -1;
    int bestFret   = 999;
    bool foundIdle = false;

    // Prefer idle strings first, and among those, pick the lowest fret (closest to open position)
    for (int s = 0; s < NUM_STRINGS; ++s)
    {
        const int fret = note - kOpenStrings[s];
        if (fret >= 0 && fret <= NUM_FRETS)
        {
            const bool isIdle = !stringActive[static_cast<size_t>(s)];

            if (isIdle && !foundIdle)
            {
                // Found first idle string
                foundIdle  = true;
                bestString = s;
                bestFret   = fret;
            }
            else if (isIdle && foundIdle)
            {
                // Both are idle, pick the one with lower fret (closer to open nut)
                if (fret < bestFret)
                {
                    bestString = s;
                    bestFret   = fret;
                }
            }
            else if (!isIdle && !foundIdle)
            {
                // Neither is idle, keep the one with lowest fret
                if (fret < bestFret)
                {
                    bestString = s;
                    bestFret   = fret;
                }
            }
        }
    }

    if (bestString >= 0)
        return { bestString, bestFret, stringActive[static_cast<size_t>(bestString)] };

    // Fallback: Low E or High E
    if (note < kOpenStrings[0])
        return { 0, 0, false };
    return { NUM_STRINGS - 1, NUM_FRETS, false };
}

int Fretboard::findStringPlayingNote(int midiNote, const std::array<int, NUM_STRINGS>& currentNotes) const noexcept
{
    for (int s = 0; s < NUM_STRINGS; ++s)
    {
        if (currentNotes[static_cast<size_t>(s)] == midiNote)
            return s;
    }
    return -1;
}

