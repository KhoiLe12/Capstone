#include "BodyResonance.h"
#include "EmbeddedIRs.h"
#include <cmath>
#include <algorithm>
#include <iterator>

// ---------------------------------------------------------------------------
// 32-Mode Acoustic Guitar Body Dataset (IRCAM Modalys Physical Model)
// ---------------------------------------------------------------------------
// Literature-grounded modal eigenfrequencies, Q factors, and coupling weights
// for a master-built spruce/rosewood acoustic guitar (Rossing 2010; Fletcher 1998).
// ---------------------------------------------------------------------------

struct ModalSpec
{
    float freqHz;
    float Q;
    float amp;
    float pan; // Spatial tilt: -0.35 (bass bout left) to +0.35 (treble bout right), 0 = center
};

static constexpr ModalSpec kModalDataset[BodyResonance::N_MODES] =
{
    // Air cavity & fundamental soundboard modes (deep warm wooden body cavity)
    {  102.f, 10.f, 2.80f,  0.00f }, // A0: Helmholtz air resonance (soundhole pumping - deep acoustic cavity warmth)
    {  185.f, 14.f, 2.50f, -0.35f }, // T(1,1)_L: Main top plate lower dipole (bass warmth - left bout)
    {  205.f, 12.f, 1.40f,  0.00f }, // A1: First internal air cavity longitudinal mode (center)
    {  225.f, 16.f, 2.60f,  0.35f }, // T(1,1)_U: Main top plate upper dipole (core body thump - right bout)
    {  260.f, 14.f, 1.60f, -0.20f }, // B(1,1): Back plate fundamental coupling (bass bout)
    {  340.f, 16.f, 1.40f,  0.30f }, // T(2,1): Cross-dipole top plate mode (treble bout)
    {  390.f, 12.f, 0.60f,  0.00f }, // A2: Second cavity air mode (center)
    {  440.f, 14.f, 0.90f, -0.25f }, // T(3,1): Tripole soundboard mode (left)

    // Soundboard longitudinal & mid-body modes
    {  490.f, 16.f, 0.90f,  0.25f }, // L(1,1): Longitudinal wood grain mode (right)
    {  550.f, 15.f, 0.70f, -0.20f }, // Mid soundboard flexure (left)
    {  620.f, 14.f, 0.65f,  0.20f }, // Ribs & waist coupling (right)
    {  710.f, 14.f, 0.55f, -0.15f }, // Soundboard higher order (left)
    {  810.f, 13.f, 0.50f,  0.15f }, // Soundboard higher order (right)
    {  920.f, 12.f, 0.45f, -0.15f }, // Upper body formant (left)
    { 1050.f, 12.f, 0.45f,  0.15f }, // Upper body formant (right)
    { 1180.f, 11.f, 0.40f, -0.10f }, // Upper wood resonance (left)

    // Presence & bridge radiation formants
    { 1320.f, 11.f, 0.35f,  0.10f }, // Soundboard presence mode (right)
    { 1470.f, 10.f, 0.30f, -0.10f }, // Soundboard presence mode (left)
    { 1640.f, 10.f, 0.30f,  0.10f }, // High presence formant (right)
    { 1820.f,  9.f, 0.25f, -0.10f }, // High presence formant (left)
    { 2020.f,  9.f, 0.25f,  0.10f }, // Treble wood formant (right)
    { 2240.f,  8.f, 0.20f, -0.08f }, // Treble wood formant (left)
    { 2480.f,  8.f, 0.20f,  0.08f }, // Bridge sparkle formant (right)
    { 2740.f,  8.f, 0.18f, -0.08f }, // Bridge sparkle formant (left)

    // Air shimmer & high-frequency wood sheen
    { 3020.f,  7.f, 0.15f,  0.08f }, // Air shimmer mode (right)
    { 3320.f,  7.f, 0.12f, -0.08f }, // Air shimmer mode (left)
    { 3650.f,  6.f, 0.10f,  0.06f }, // High harmonic formant (right)
    { 4010.f,  6.f, 0.08f, -0.06f }, // High harmonic formant (left)
    { 4400.f,  6.f, 0.06f,  0.05f }, // Top sheen formant (right)
    { 4820.f,  5.f, 0.05f, -0.05f }, // Top sheen formant (left)
    { 5270.f,  5.f, 0.04f,  0.04f }, // High frequency wood loss (right)
    { 5750.f,  5.f, 0.03f, -0.04f }, // Extreme air boundary loss (left)
};

// ---------------------------------------------------------------------------
// Constructor & Lifecycle
// ---------------------------------------------------------------------------

BodyResonance::BodyResonance()
{
}

void BodyResonance::init(float sr, int blockSize, float bodySize, float bodyDamping)
{
    sampleRate     = sr;
    maxBlockSize   = std::max(blockSize, 4096);
    currentSize    = bodySize;
    currentDamping = bodyDamping;

    convBufferL.assign(static_cast<size_t>(maxBlockSize), 0.f);
    convBufferR.assign(static_cast<size_t>(maxBlockSize), 0.f);

    updateFilters();
    loadInternalIR(currentBodyType);
}

void BodyResonance::loadInternalIR(int type)
{
    if (type == ClassicalNylonIR)
    {
        // Load as mono: JUCE sums both WAV channels into one symmetric IR,
        // eliminating any L/R imbalance present in the source capture.
        convolution.loadImpulseResponse(kClassicalNylonWav_data,
                                        kClassicalNylonWav_size,
                                        juce::dsp::Convolution::Stereo::no,
                                        juce::dsp::Convolution::Trim::no,
                                        0,
                                        juce::dsp::Convolution::Normalise::no);
        loadedIRType = ClassicalNylonIR;
    }
    else if (type == GibsonAcousticIR)
    {
        convolution.loadImpulseResponse(kGibsonAcousticWav_data,
                                        kGibsonAcousticWav_size,
                                        juce::dsp::Convolution::Stereo::yes,
                                        juce::dsp::Convolution::Trim::no,
                                        0,
                                        juce::dsp::Convolution::Normalise::no);
        loadedIRType = GibsonAcousticIR;
    }

    // Immediately call prepare() to force synchronous initialisation on the caller thread.
    // This executes JUCE's background message queue popAll() synchronously, ensuring
    // the convolution engine is fully constructed and active before audio begins.
    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = static_cast<double>(sampleRate);
    spec.maximumBlockSize = static_cast<juce::uint32>(convBufferL.size());
    spec.numChannels      = 2;
    convolution.prepare(spec);
}

void BodyResonance::loadCustomIR(const juce::File& file)
{
    if (file.existsAsFile())
    {
        convolution.loadImpulseResponse(file,
                                        juce::dsp::Convolution::Stereo::yes,
                                        juce::dsp::Convolution::Trim::no,
                                        0,
                                        juce::dsp::Convolution::Normalise::no);
        loadedIRType = 99;

        juce::dsp::ProcessSpec spec;
        spec.sampleRate       = static_cast<double>(sampleRate);
        spec.maximumBlockSize = static_cast<juce::uint32>(convBufferL.size());
        spec.numChannels      = 2;
        convolution.prepare(spec);
    }
}

void BodyResonance::setParameters(float bodySize, float bodyDamping, float bodyCoupling, int bodyType)
{
    currentCoupling = std::clamp(bodyCoupling, 0.0f, 1.0f);

    if (bodyType != currentBodyType)
    {
        currentBodyType = bodyType;
        if (currentBodyType == ClassicalNylonIR || currentBodyType == GibsonAcousticIR)
        {
            loadInternalIR(currentBodyType);
        }
    }

    if (std::abs(bodySize - currentSize) > 0.005f || std::abs(bodyDamping - currentDamping) > 0.005f)
    {
        currentSize    = std::clamp(bodySize, 0.5f, 2.0f);
        currentDamping = std::clamp(bodyDamping, 0.2f, 3.0f);
        updateFilters();
    }
}

void BodyResonance::updateFilters()
{
    // Normalization scale factor for 32 parallel resonators
    constexpr float kNorm = 0.10f;

    for (int i = 0; i < N_MODES; ++i)
    {
        // Larger body size -> lower resonant frequencies
        const float scaledFreq = kModalDataset[i].freqHz / currentSize;
        const float scaledQ    = kModalDataset[i].Q * currentDamping;
        const float gain       = kModalDataset[i].amp * kNorm;

        filters[i].setModalResonator(scaledFreq, scaledQ, gain, sampleRate);
    }
}

// ---------------------------------------------------------------------------
// Block Audio Processing
// ---------------------------------------------------------------------------

void BodyResonance::processBlock(float* channelL, float* channelR, int numSamples) noexcept
{
    if (currentBodyType == ModalResonatorBank)
    {
        // Modal Resonator Bank (IRCAM Modalys parallel biquad filters)
        for (int i = 0; i < numSamples; ++i)
        {
            float outL = 0.f;
            float outR = 0.f;
            processStereo(channelL[i], channelR[i], outL, outR);
            channelL[i] = outL;
            channelR[i] = outR;
        }
    }
    else
    {
        // Physical acoustic radiation blend:
        // currentCoupling = 1.0 -> 100% radiated acoustic soundboard (zero piezo quack)
        // currentCoupling = 0.0 -> 100% direct bridge string (dry DI pickup)
        const float dryFactor = std::clamp(1.0f - currentCoupling, 0.0f, 1.0f);
        const float dryGain   = dryFactor * dryFactor;

        // Both Classical Nylon and Gibson Acoustic IRs are matched in RMS energy and chord peaks.
        // Wet gain of 0.85 provides uniform perceived volume across the entire Body Coupling blend
        // while preserving generous polyphonic headroom for 6-string chords.
        const float wetGain   = 0.85f * std::sqrt(std::clamp(currentCoupling, 0.0f, 1.0f));

        // Process in chunks of convBufferL.size() to handle any arbitrary host block size
        // without heap reallocations on the audio thread.
        int remaining = numSamples;
        int offset = 0;
        const int chunkSize = static_cast<int>(convBufferL.size());

        while (remaining > 0)
        {
            const int currentChunk = std::min(remaining, chunkSize);
            std::copy(channelL + offset, channelL + offset + currentChunk, convBufferL.begin());
            std::copy(channelR + offset, channelR + offset + currentChunk, convBufferR.begin());

            float* channels[2] = { convBufferL.data(), convBufferR.data() };
            juce::dsp::AudioBlock<float> block(channels, 2, static_cast<size_t>(currentChunk));
            juce::dsp::ProcessContextReplacing<float> context(block);
            convolution.process(context);

            for (int i = 0; i < currentChunk; ++i)
            {
                channelL[offset + i] = dryGain * channelL[offset + i] + wetGain * convBufferL[static_cast<size_t>(i)];
                channelR[offset + i] = dryGain * channelR[offset + i] + wetGain * convBufferR[static_cast<size_t>(i)];
            }

            offset += currentChunk;
            remaining -= currentChunk;
        }
    }
}

// ---------------------------------------------------------------------------
// Stereo Soundboard Radiation & Diffusion (Modal Bank Fallback)
// ---------------------------------------------------------------------------

void BodyResonance::processStereo(float bridgeForceL, float bridgeForceR, float& outL, float& outR) noexcept
{
    // 1. Excitation split into common mode (pumping) and differential mode (rocking saddle)
    const float commonBridge = 0.5f * (bridgeForceL + bridgeForceR);
    const float diffBridge   = bridgeForceL - bridgeForceR;

    float modalL = 0.f;
    float modalR = 0.f;

    for (int i = 0; i < N_MODES; ++i)
    {
        const float pan = kModalDataset[i].pan;
        // Asymmetric soundboard modes receive differential rocking excitation
        const float drive = commonBridge + diffBridge * (pan * 0.4f);
        const float modeOut = filters[i].process(drive);

        modalL += modeOut * (1.0f - pan);
        modalR += modeOut * (1.0f + pan);
    }

    // 2. Soundboard cross-plate acoustic diffusion (Haas delay ~0.3ms = 14 samples at 44.1k/48k)
    const int delaySamples = std::max(2, std::min(CROSS_DELAY_LEN - 1, static_cast<int>(sampleRate * 0.00032f)));
    const int readIdx = (crossDelayIdx - delaySamples + CROSS_DELAY_LEN) % CROSS_DELAY_LEN;

    const float crossL = crossDelayL[readIdx];
    const float crossR = crossDelayR[readIdx];

    crossDelayL[crossDelayIdx] = modalL;
    crossDelayR[crossDelayIdx] = modalR;
    crossDelayIdx = (crossDelayIdx + 1) % CROSS_DELAY_LEN;

    // Acoustic cross-bout diffusion
    constexpr float kCrossBleed = 0.22f;
    const float soundboardL = (modalL + kCrossBleed * crossR) / (1.0f + kCrossBleed);
    const float soundboardR = (modalR + kCrossBleed * crossL) / (1.0f + kCrossBleed);

    // 3. Physical acoustic soundboard radiation vs direct mechanical bridge pick
    const float dryGain = 1.0f - currentCoupling * 0.85f;
    const float wetGain = currentCoupling * 4.2f;

    outL = dryGain * bridgeForceL + wetGain * soundboardL;
    outR = dryGain * bridgeForceR + wetGain * soundboardR;
}

void BodyResonance::reset() noexcept
{
    for (auto& f : filters)
        f.reset();
    std::fill_n(crossDelayL, CROSS_DELAY_LEN, 0.f);
    std::fill_n(crossDelayR, CROSS_DELAY_LEN, 0.f);
    crossDelayIdx = 0;
    convolution.reset();
}
