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

    /** Load a custom external WAV impulse response file. */
    void loadCustomIR(const juce::File& file);

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

    // Zero-latency partitioned stereo convolution engine
    juce::dsp::Convolution convolution { juce::dsp::Convolution::Latency { 0 } };
    int loadedIRType = -1;

    // Scratch buffers for block convolution
    std::vector<float> convBufferL;
    std::vector<float> convBufferR;

    void updateFilters();
    void loadInternalIR(int type);
};
