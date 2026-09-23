#pragma once
#include <array>

/**
 * Fretboard — Models the physical 6-string guitar neck and ergonomic fingering.
 *
 * Physical layout (Standard EADGBE tuning):
 *   String 0: E2 (MIDI 40, ~82.4 Hz)
 *   String 1: A2 (MIDI 45, ~110.0 Hz)
 *   String 2: D3 (MIDI 50, ~146.8 Hz)
 *   String 3: G3 (MIDI 55, ~196.0 Hz)
 *   String 4: B3 (MIDI 59, ~246.9 Hz)
 *   String 5: E4 (MIDI 64, ~329.6 Hz)
 *
 * Each string has 22 frets. A note on a string is physically monophonic:
 * fretting a new note on a string naturally stops any previous note on that string.
 */
class Fretboard
{
public:
    static constexpr int NUM_STRINGS = 6;
    static constexpr int NUM_FRETS   = 22;

    static constexpr int kOpenStrings[NUM_STRINGS] = { 40, 45, 50, 55, 59, 64 };

    struct Assignment
    {
        int stringIndex = -1; // 0..5, or -1 if impossible
        int fret        = -1; // 0..22
        bool isRepluck  = false;
    };

    Fretboard() = default;

    /**
     * Find the best physical string and fret for an incoming MIDI note.
     * @param midiNote      Incoming note number (40..86)
     * @param stringActive  Array indicating which of the 6 strings are currently ringing
     */
    Assignment allocateNote(int midiNote, const std::array<bool, NUM_STRINGS>& stringActive) const noexcept;

    /** Returns which string is currently playing midiNote, or -1. */
    int findStringPlayingNote(int midiNote, const std::array<int, NUM_STRINGS>& currentNotes) const noexcept;
};

