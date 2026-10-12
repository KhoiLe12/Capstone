#pragma once
#include <cmath>
#include <algorithm>

/**
 * SympatheticResonance — Acoustic Inter-String Cross-Bridge Sympathetic Coupling.
 *
 * Physical Principle:
 * When any string on an acoustic guitar is plucked, the oscillating bridge force
 * drives the soundboard and saddle. This motion excites the other unplayed (open)
 * strings at their natural modal frequencies, especially when the played notes share
 * common harmonics (octaves, 5ths, 4ths, major 3rds) with the open strings.
 *
 * The 6 Physical Open Strings (Standard Tuning):
 *   String 0: E2  (82.4069 Hz)
 *   String 1: A2  (110.0000 Hz)
 *   String 2: D3  (146.8324 Hz)
 *   String 3: G3  (196.0000 Hz)
 *   String 4: B3  (246.9417 Hz)
 *   String 5: E4  (329.6276 Hz)
 *
 * Each open string is modeled with 3 prominent harmonic modes (fundamental + 2 overtones)
 * utilizing Audio EQ Cookbook constant 0 dB peak gain bandpass resonators with natural nylon Q.
 *
 * Fretting & Mute Dynamics:
 * - If string s is fretted/played, its open-string resonator is dynamically bypassed/damped.
 * - Under palm mute or full mute, sympathetic coupling across all strings is attenuated.
 */
class SympatheticResonance
{
public:
    static constexpr int NUM_STRINGS = 6;
    static constexpr int NUM_HARMONICS = 3;

    struct Resonator
    {
        // 2-pole constant 0 dB peak gain BPF:
        // H(s) = (s*w0/Q) / (s^2 + s*w0/Q + w0^2)
        // Normalized discrete direct-form 1 coefficients:
        float b0 = 0.0f;
        float b2 = 0.0f;
        float a1 = 0.0f;
        float a2 = 0.0f;

        // Filter delay states
        float x1 = 0.0f;
        float x2 = 0.0f;
        float y1 = 0.0f;
        float y2 = 0.0f;

        void setup(float freq, float Q, float sampleRate) noexcept
        {
            if (freq >= sampleRate * 0.48f)
            {
                b0 = b2 = a1 = a2 = 0.0f;
                return;
            }

            const float w0 = 2.0f * 3.14159265358979323846f * freq / sampleRate;
            const float cosw = std::cos(w0);
            const float sinw = std::sin(w0);
            const float alpha = sinw / (2.0f * std::max(1.0f, Q));

            const float a0 = 1.0f + alpha;
            const float invA0 = 1.0f / a0;

            b0 = (alpha) * invA0;
            b2 = (-alpha) * invA0;
            a1 = (-2.0f * cosw) * invA0;
            a2 = (1.0f - alpha) * invA0;
        }

        inline float process(float in) noexcept
        {
            float out = b0 * in + b2 * x2 - a1 * y1 - a2 * y2;

            // Anti-denormal protection
            if (std::abs(out) < 1.0e-15f)
                out = 0.0f;

            x2 = x1;
            x1 = in;
            y2 = y1;
            y1 = out;
            return out;
        }

        void reset() noexcept
        {
            x1 = x2 = y1 = y2 = 0.0f;
        }
    };

    SympatheticResonance() = default;

    void init(float sr) noexcept
    {
        sampleRate = sr;

        // Open string fundamental frequencies in Hz (E2, A2, D3, G3, B3, E4)
        static constexpr float openFreqs[NUM_STRINGS] = {
            82.4069f, 110.0000f, 146.8324f, 196.0000f, 246.9417f, 329.6276f
        };

        // Nylon string inharmonicity factors per string
        static constexpr float B_coeff[NUM_STRINGS] = {
            0.000025f, 0.000028f, 0.000032f, 0.000045f, 0.000060f, 0.000085f
        };

        // Resonator Q factors (fundamental has longest decay ~2.0s, upper harmonics decay faster)
        static constexpr float Q_harmonics[NUM_HARMONICS] = {
            140.0f, 100.0f, 75.0f
        };

        for (int s = 0; s < NUM_STRINGS; ++s)
        {
            stringDamping[s] = 1.0f;
            for (int k = 0; k < NUM_HARMONICS; ++k)
            {
                const float harmonicNumber = static_cast<float>(k + 1);
                // Stiff string frequency formula: fk = k * f0 * sqrt(1 + B * k^2)
                const float inharm = std::sqrt(1.0f + B_coeff[s] * harmonicNumber * harmonicNumber);
                const float fk = harmonicNumber * openFreqs[s] * inharm;
                resonators[s][k].setup(fk, Q_harmonics[k], sampleRate);
            }
        }

        reset();
    }

    void reset() noexcept
    {
        for (int s = 0; s < NUM_STRINGS; ++s)
        {
            stringDamping[s] = 1.0f;
            for (int k = 0; k < NUM_HARMONICS; ++k)
                resonators[s][k].reset();
        }
        muteDamping = 1.0f;
    }

    /**
     * Mark which physical strings are fretted/held.
     * When fretted, the open length is damped by the finger.
     */
    void setStringFretted(int stringIdx, bool isFretted) noexcept
    {
        if (stringIdx >= 0 && stringIdx < NUM_STRINGS)
        {
            // Smooth target: fretted strings are damped
            targetDamping[stringIdx] = isFretted ? 0.0f : 1.0f;
        }
    }

    /** Set whether palm or full mute is active across the bridge. */
    void setMuted(bool palmMute, bool fullMute) noexcept
    {
        if (fullMute)
            targetMuteDamping = 0.0f;
        else if (palmMute)
            targetMuteDamping = 0.15f;
        else
            targetMuteDamping = 1.0f;
    }

    /**
     * Process a sample of bridge drive force.
     * Returns the sympathetic resonance force from all vibrating open strings.
     */
    inline float process(float bridgeForce) noexcept
    {
        // Smooth mute damping towards target (per-sample slew ~5ms)
        muteDamping += 0.005f * (targetMuteDamping - muteDamping);

        if (muteDamping < 0.001f && targetMuteDamping < 0.001f)
            return 0.0f;

        float totalResonance = 0.0f;

        // Weights for harmonics: fundamental is strongest, overtones drop off
        static constexpr float harmonicWeights[NUM_HARMONICS] = { 0.55f, 0.30f, 0.15f };

        for (int s = 0; s < NUM_STRINGS; ++s)
        {
            // Smooth string damping
            stringDamping[s] += 0.008f * (targetDamping[s] - stringDamping[s]);

            if (stringDamping[s] < 0.001f)
                continue;

            float stringOut = 0.0f;
            for (int k = 0; k < NUM_HARMONICS; ++k)
            {
                stringOut += resonators[s][k].process(bridgeForce) * harmonicWeights[k];
            }

            totalResonance += stringOut * stringDamping[s];
        }

        return totalResonance * muteDamping;
    }

private:
    float sampleRate = 44100.0f;
    Resonator resonators[NUM_STRINGS][NUM_HARMONICS];
    float stringDamping[NUM_STRINGS] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    float targetDamping[NUM_STRINGS] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    float muteDamping = 1.0f;
    float targetMuteDamping = 1.0f;
};

