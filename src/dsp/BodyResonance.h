#pragma once
#include "BiquadFilter.h"
#include <juce_dsp/juce_dsp.h>
#include <vector>

/**
 * BodyResonance — Dual-Engine Acoustic Guitar Body Model.
 *
 * Engines:
 *   1. Classical Nylon (Studio Mic IR) - High-resolution zero-latency partitioned
 *      convolution with stereo small-diaphragm condenser microphone acoustic field.
 *   2. Gibson Acoustic (Studio Mic IR) - Authentic measured acoustic guitar pickup-to-mic
 *      transfer function IR capturing rich dreadnought wood bloom.
 *   3. Modal Resonator Bank - 32-mode IRCAM Modalys parallel biquad physical soundboard.
 *
 * Macro Controls:
 *   - bodyType:     Selects between IR convolution and Modal Resonator Bank.
 *   - bodyCoupling: Blends between direct bridge string (0.0 = DI pickup) and
 *                   100% radiated acoustic soundboard (1.0 = studio mic).
 *   - bodySize:     Scales modal frequencies (in Modal Bank mode).
 *   - bodyDamping:  Scales mode Q factors (in Modal Bank mode).
 */
class BodyResonance
{
public:
    static constexpr int N_MODES = 32;

    enum BodyType
    {
        ClassicalNylonIR = 0,
        GibsonAcousticIR = 1,
        ModalResonatorBank = 2
    };

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

private:
    BiquadFilter filters[N_MODES];
    float sampleRate      = 44100.f;
    int   maxBlockSize    = 512;
    float currentSize     = 1.0f;
    float currentDamping  = 1.0f;
    float currentCoupling = 0.7f;
    int   currentBodyType = 0; // 0 = Classical Nylon IR, 1 = Gibson Acoustic IR, 2 = Modal Bank

    // Cross-plate soundboard acoustic diffusion (Haas delay ~0.3ms) for modal bank
    static constexpr int CROSS_DELAY_LEN = 32;
    float crossDelayL[CROSS_DELAY_LEN] = {};
    float crossDelayR[CROSS_DELAY_LEN] = {};
    int   crossDelayIdx = 0;

    // Wood viscosity & high-frequency air dispersion filter (gentle 10 kHz acoustic roll-off),
    // one state pair per IR body so the two can run side by side during a crossfade.
    static constexpr int NUM_IR_BODIES = 2;
    float airFilterL[NUM_IR_BODIES] = {};
    float airFilterR[NUM_IR_BODIES] = {};
    float airFilterBeta = 0.0f;

    // Zero-latency partitioned stereo convolution, one engine per embedded body IR.
    // Both IRs are loaded in init() (prepareToPlay), so switching body model never loads or
    // allocates on the audio thread. Only the audible engine(s) are processed each block.
    juce::dsp::Convolution convolutions[NUM_IR_BODIES] {
        juce::dsp::Convolution { juce::dsp::Convolution::Latency { 0 } },
        juce::dsp::Convolution { juce::dsp::Convolution::Latency { 0 } }
    };

    // Scratch buffers: dry input copy + one wet buffer pair per IR engine
    std::vector<float> dryBufferL, dryBufferR;
    std::vector<float> convBufferL[NUM_IR_BODIES];
    std::vector<float> convBufferR[NUM_IR_BODIES];

    // Body model crossfade (~20 ms) when the body type changes
    int   previousBodyType = 0;
    int   fadeSamplesTotal = 960;
    int   fadeSamplesLeft  = 0;

    void updateFilters();
    void loadInternalIRs();
    void renderBody(int type, int i, float inL, float inR, float& outL, float& outR) noexcept;
};
