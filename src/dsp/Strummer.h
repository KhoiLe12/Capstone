#pragma once
#include <array>
#include <algorithm>

/**
 * Strummer — Staggers chord plucks across the 6 physical strings.
 *
 * Physical behavior:
 *   A physical pick takes 10 to 35 ms to sweep across the 6 strings.
 *   - Downstroke: plucks lower-pitch strings first -> high strings last.
 *   - Upstroke: plucks higher-pitch strings first -> low strings last.
 *   - strumSpeed = 0: instantaneous keyboard chord.
 */
class Strummer
{
public:
    struct StrumEvent
    {
        int   stringIndex      = -1;
        int   midiNote         = -1;
        float velocity         = 0.f;
        int   samplesRemaining = 0;
        bool  active           = false;
    };

    static constexpr int MAX_PENDING = 12;

    Strummer() = default;

    void init(float sampleRate);

    /**
     * Schedule a note on a specific physical string.
     * @param stringIndex 0 (Low E) .. 5 (High E)
     * @param midiNote    Pitch number
     * @param velocity    Pluck velocity 0..1
     * @param strumSpeed  0.0 = instant, 1.0 = slow acoustic rake (~35 ms between strings)
     */
    void scheduleNote(int stringIndex, int midiNote, float velocity, float strumSpeed);

    /**
     * Advance one sample and return any event ready to trigger now.
     * Fills outString, outNote, outVelocity if an event triggered.
     * Returns true if a note was triggered on this sample.
     */
    bool tick(int& outString, int& outNote, float& outVelocity) noexcept;

    void reset() noexcept;

private:
    float sampleRate = 44100.f;
    std::array<StrumEvent, MAX_PENDING> queue{};
    bool lastWasDownstroke = false;
    int samplesSinceLastPluck = 99999;
    int chordNoteIndex = 0;
};

