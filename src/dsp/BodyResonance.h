#pragma once
#include "BiquadFilter.h"
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <atomic>

/**
 * BodyResonance — acoustic guitar body stage (bridge force -> radiated sound).
 *
 * Body types (the order is the saved parameter index; do not reorder):
 *   0. Classical (IR)  - Manuel Contreras 1971 classical guitar, measured (bridge force -> near-field mics)
 *   1. Legacy Body (IR) - the original embedded IR, origin unknown (kept for older presets)
 *   2. Modal Bank      - 32-mode Modalys-style parallel biquad soundboard (hand-authored modes)
 *   3. Flamenco (IR)   - Faustino Conde 1964 flamenca negra, measured
 *   Measured IRs: R. Mores, Zenodo 10.5281/zenodo.4604577, CC BY 4.0 (resources/ir/ATTRIBUTION.md)
 *   4. Custom IR       - user-loaded impulse response (falls back to Classical until loaded)
 *
 * Every IR is energy-normalised (white-input power) to the same reference, so switching
 * bodies keeps the level comparable. Switching crossfades over 20 ms; only audible engines run.
 *
 * Macro Controls:
 *   - bodyCoupling: blends direct bridge signal (0.0 = DI pickup) and radiated body (1.0).
 *   - bodySize / bodyDamping: modal frequency / Q scaling (Modal Bank only).
 */
class BodyResonance
{
public:
    static constexpr int N_MODES = 32;

    enum BodyType
    {
        ClassicalIR        = 0,
        LegacyIR           = 1,
        ModalResonatorBank = 2,
        FlamencoIR         = 3,
        CustomIR           = 4
    };
    static constexpr int NUM_BODY_TYPES = 5;

    /** Longest custom IR accepted; longer files are truncated with a short fade-out. */
    static constexpr double kMaxCustomIRSeconds = 2.0;

    BodyResonance();

    /** Design all modal filter coefficients and prepare convolution for the current sample rate. */
    void init(float sampleRate, int blockSize = 512, float bodySize = 1.0f, float bodyDamping = 1.0f);

    /** Update macro parameters dynamically without clicks. */
    void setParameters(float bodySize, float bodyDamping, float bodyCoupling, int bodyType = 0);

    /**
     * Process an audio block in-place (stereo).
     * Radiates true acoustic soundboard velocity in stereo.
     */
    void processBlock(float* channelL, float* channelR, int numSamples) noexcept;

    /** Per-sample stereo fallback. */
    void processStereo(float bridgeForceL, float bridgeForceR, float& outL, float& outR) noexcept;

    /** Mono convenience overload. */
    float process(float bridgeForce) noexcept
    {
        float l = 0.f, r = 0.f;
        processStereo(bridgeForce, bridgeForce, l, r);
        return 0.5f * (l + r);
    }

    /** Zero all filter and convolution states. */
    void reset() noexcept;

    float getBodyCoupling() const noexcept { return currentCoupling; }
    int   getBodyType() const noexcept     { return currentBodyType; }

    /**
     * Install a user impulse response into the Custom IR slot (any channel count; 1 = mono,
     * 2+ = first two channels as stereo). Call from the message thread or an offline context,
     * never the audio thread. The IR is truncated to kMaxCustomIRSeconds and energy-normalised.
     * With prepareNow = false, JUCE loads it on its background thread and swaps it in while
     * audio keeps running; with prepareNow = true (init / offline) it is installed synchronously.
     * An empty buffer clears the slot.
     */
    void setCustomIR(const juce::AudioBuffer<float>& ir, double irSampleRate, bool prepareNow);
    bool hasCustomIR() const noexcept { return customIRLoaded.load(); }

    /** Scale an IR in place to the reference energy used for all bodies. */
    static void normaliseIREnergy(juce::AudioBuffer<float>& ir);

    /**
     * Heuristic check that a file is plausibly an impulse response rather than a recording.
     * Returns a user-facing explanation if it looks wrong (level does not decay, or the peak comes
     * late), or an empty string if it looks fine. Message thread only.
     */
    static juce::String describeIRProblems(const juce::AudioBuffer<float>& ir, double irSampleRate);

    /**
     * Largest custom-IR peak magnitude response allowed, relative to the loudest built-in body's peak.
     * Real body IRs measured -3.0 to -0.6 dB on this scale (6 Mores guitars); a recording of the
     * phrase used as an "IR" measured +21.4 dB. 3 dB leaves real IRs untouched.
     */
    static constexpr float kCustomIRPeakHeadroomDb = 3.0f;

private:
    BiquadFilter filters[N_MODES];
    float sampleRate      = 44100.f;
    int   maxBlockSize    = 512;
    float currentSize     = 1.0f;
    float currentDamping  = 1.0f;
    float currentCoupling = 0.7f;
    int   currentBodyType = 0; // effective body type (BodyType); Custom falls back to Classical until loaded

    // Cross-plate soundboard acoustic diffusion (Haas delay ~0.3ms) for modal bank
    static constexpr int CROSS_DELAY_LEN = 32;
    float crossDelayL[CROSS_DELAY_LEN] = {};
    float crossDelayR[CROSS_DELAY_LEN] = {};
    int   crossDelayIdx = 0;

    // Wood viscosity & high-frequency air dispersion filter (gentle 10 kHz acoustic roll-off),
    // one state pair per body type so two bodies can run side by side during a crossfade.
    float airFilterL[NUM_BODY_TYPES] = {};
    float airFilterR[NUM_BODY_TYPES] = {};
    float airFilterBeta = 0.0f;

    // Zero-latency partitioned stereo convolution, one engine per IR body type (index = body
    // type; the Modal Bank slot is unused). Built-in IRs load in init() (prepareToPlay); the
    // custom IR loads via setCustomIR(). Only the audible engine(s) are processed each block.
    juce::dsp::Convolution convolutions[NUM_BODY_TYPES] {
        juce::dsp::Convolution { juce::dsp::Convolution::Latency { 0 } },
        juce::dsp::Convolution { juce::dsp::Convolution::Latency { 0 } },
        juce::dsp::Convolution { juce::dsp::Convolution::Latency { 0 } },
        juce::dsp::Convolution { juce::dsp::Convolution::Latency { 0 } },
        juce::dsp::Convolution { juce::dsp::Convolution::Latency { 0 } }
    };
    std::atomic<bool> customIRLoaded { false };

    // Scratch buffers: dry input copy + one wet buffer pair per IR engine
    std::vector<float> dryBufferL, dryBufferR;
    std::vector<float> convBufferL[NUM_BODY_TYPES];
    std::vector<float> convBufferR[NUM_BODY_TYPES];

    // Body model crossfade (~20 ms) when the body type changes
    int   previousBodyType = 0;
    int   fadeSamplesTotal = 960;
    int   fadeSamplesLeft  = 0;
    bool  freshStart       = true; ///< No block processed since reset(): body changes apply instantly

    void updateFilters();
    void loadInternalIRs();
    void prepareConvolution(int type);
    static bool isIRBody(int type) noexcept { return type != ModalResonatorBank; }
    void renderBody(int type, int i, float inL, float inR, float& outL, float& outR) noexcept;
};
