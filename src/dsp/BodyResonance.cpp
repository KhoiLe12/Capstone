#include "BodyResonance.h"
#include "EmbeddedIRs.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <algorithm>
#include <iterator>

// ---------------------------------------------------------------------------
// 32-Mode Acoustic Guitar Body Modes (Modalys-style, hand-authored table)
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

namespace
{
    // Reference IR energy (sum of squares, white-input power) for all bodies: the energy of the
    // legacy IR, so "Legacy Body (IR)" sounds exactly as the former "Gibson Acoustic (IR)" did.
    constexpr double kReferenceIREnergy = 0.18036872893571854;
    // Wet gain applied to every (energy-normalised) IR body; former Gibson IR scale.
    constexpr float kIRWetScale = 0.82f;

    juce::AudioBuffer<float> decodeEmbeddedWav(const void* data, size_t size, double& sampleRateOut)
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> reader(
            wav.createReaderFor(new juce::MemoryInputStream(data, size, false), true));
        juce::AudioBuffer<float> buffer;
        sampleRateOut = 48000.0;
        if (reader != nullptr)
        {
            sampleRateOut = reader->sampleRate;
            buffer.setSize(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
            reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true);
        }
        return buffer;
    }
}

namespace
{
    // Peak of the magnitude response |H(f)| of an IR (channels averaged in power). Message thread only.
    float peakMagnitudeResponse(const juce::AudioBuffer<float>& ir)
    {
        const int len = ir.getNumSamples();
        if (len == 0 || ir.getNumChannels() == 0)
            return 0.0f;
        int order = 1;
        while ((1 << order) < len) ++order;
        juce::dsp::FFT fft(order);
        const int n = 1 << order;
        std::vector<float> power(static_cast<size_t>(n / 2 + 1), 0.0f);
        std::vector<float> work(static_cast<size_t>(2 * n));
        for (int ch = 0; ch < ir.getNumChannels(); ++ch)
        {
            std::fill(work.begin(), work.end(), 0.0f);
            std::copy_n(ir.getReadPointer(ch), len, work.begin());
            fft.performFrequencyOnlyForwardTransform(work.data(), true);
            for (size_t k = 0; k < power.size(); ++k)
                power[k] += work[k] * work[k] / static_cast<float>(ir.getNumChannels());
        }
        return std::sqrt(*std::max_element(power.begin(), power.end()));
    }

    // Loudest peak response among the built-in IR bodies after normalisation and trim (computed once)
    float builtInPeakResponse();
}

void BodyResonance::normaliseIREnergy(juce::AudioBuffer<float>& ir)
{
    // Average per-channel energy so stereo and mono IRs of the same body match in level
    double energy = 0.0;
    for (int ch = 0; ch < ir.getNumChannels(); ++ch)
    {
        const float* d = ir.getReadPointer(ch);
        for (int i = 0; i < ir.getNumSamples(); ++i)
            energy += static_cast<double>(d[i]) * d[i];
    }
    energy /= std::max(1, ir.getNumChannels());
    if (energy > 1e-20)
        ir.applyGain(static_cast<float>(std::sqrt(kReferenceIREnergy / energy)));
}

void BodyResonance::init(float sr, int blockSize, float bodySize, float bodyDamping)
{
    sampleRate     = sr;
    maxBlockSize   = std::max(blockSize, 4096);
    currentSize    = bodySize;
    currentDamping = bodyDamping;

    const auto n = static_cast<size_t>(maxBlockSize);
    dryBufferL.assign(n, 0.f);
    dryBufferR.assign(n, 0.f);
    for (int t = 0; t < NUM_BODY_TYPES; ++t)
    {
        convBufferL[t].assign(n, 0.f);
        convBufferR[t].assign(n, 0.f);
    }

    // Wood viscosity & air dispersion filter (~10 kHz gentle cutoff)
    const float cutoff = std::min(10500.0f, 0.45f * sampleRate);
    airFilterBeta = std::exp(-2.0f * 3.14159265358979323846f * cutoff / sampleRate);

    fadeSamplesTotal = std::max(1, static_cast<int>(0.020f * sampleRate));
    fadeSamplesLeft  = 0;
    previousBodyType = currentBodyType;

    updateFilters();
    loadInternalIRs();
    reset();
}

void BodyResonance::prepareConvolution(int type)
{
    // prepare() synchronously installs any pending IR. Never call while the audio thread runs.
    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = static_cast<double>(sampleRate);
    spec.maximumBlockSize = static_cast<juce::uint32>(maxBlockSize);
    spec.numChannels      = 2;
    convolutions[type].prepare(spec);
}

namespace
{
    // trimDb: level match to Legacy measured on rendered phrases (FDTD + waveguide, nylon + steel),
    // on top of the energy normalisation. Classical = Contreras 1971, Flamenco = Conde 1964
    // (Mores, Zenodo 10.5281/zenodo.4604577, CC BY 4.0; see resources/ir/ATTRIBUTION.md).
    struct EmbeddedIR { int type; const uint8_t* data; size_t size; float trimDb; };
    const EmbeddedIR kBuiltInIRs[] = {
        { BodyResonance::ClassicalIR, kClassicalBodyWav_data, kClassicalBodyWav_size, 0.24f },
        { BodyResonance::LegacyIR,    kLegacyBodyWav_data,    kLegacyBodyWav_size,    0.0f  },
        { BodyResonance::FlamencoIR,  kFlamencoBodyWav_data,  kFlamencoBodyWav_size,  2.36f },
    };

    juce::AudioBuffer<float> prepareBuiltIn(const EmbeddedIR& b, double& irRate)
    {
        auto ir = decodeEmbeddedWav(b.data, b.size, irRate);
        BodyResonance::normaliseIREnergy(ir);
        ir.applyGain(juce::Decibels::decibelsToGain(b.trimDb));
        return ir;
    }

    float builtInPeakResponse()
    {
        static const float peak = []
        {
            float p = 0.0f;
            for (const auto& b : kBuiltInIRs)
            {
                double rate = 48000.0;
                p = std::max(p, peakMagnitudeResponse(prepareBuiltIn(b, rate)));
            }
            return p;
        }();
        return peak;
    }
}

juce::String BodyResonance::describeIRProblems(const juce::AudioBuffer<float>& ir, double irSampleRate)
{
    const int len = ir.getNumSamples();
    if (len == 0 || ir.getNumChannels() == 0 || irSampleRate <= 0.0)
        return "The file contains no audio.";

    // 10 ms RMS envelope of the channel-averaged signal
    const int frame = std::max(1, static_cast<int>(0.010 * irSampleRate));
    std::vector<float> env;
    for (int start = 0; start + frame <= len; start += frame)
    {
        double sum = 0.0;
        for (int ch = 0; ch < ir.getNumChannels(); ++ch)
        {
            const float* d = ir.getReadPointer(ch) + start;
            for (int i = 0; i < frame; ++i)
                sum += static_cast<double>(d[i]) * d[i];
        }
        env.push_back(static_cast<float>(std::sqrt(sum / (frame * ir.getNumChannels())) + 1e-12));
    }
    if (env.size() < 10)
        return {}; // under 100 ms: too short to judge, accept

    const auto peakIt = std::max_element(env.begin(), env.end());
    const auto peakIndex = static_cast<size_t>(std::distance(env.begin(), peakIt));
    const size_t tailStart = env.size() - std::max<size_t>(1, env.size() / 5);
    double tail = 0.0;
    for (size_t i = tailStart; i < env.size(); ++i)
        tail += static_cast<double>(env[i]) * env[i];
    tail = std::sqrt(tail / static_cast<double>(env.size() - tailStart));
    const double decayDb = 20.0 * std::log10(*peakIt / (tail + 1e-12));

    if (peakIndex > env.size() / 4)
        return "Its loudest point is late in the file, not at the start. An impulse response begins with "
               "a sharp peak and then dies away; this looks more like a recording.";
    if (decayDb < 20.0)
        return "Its level barely decays (only " + juce::String(decayDb, 1) + " dB from start to end). An "
               "impulse response dies away; this looks more like a recording or a full mix.";
    return {};
}

void BodyResonance::loadInternalIRs()
{
    // Called from init() only (prepareToPlay / message thread). Never on the audio thread.
    for (const auto& b : kBuiltInIRs)
    {
        double irRate = 48000.0;
        auto ir = prepareBuiltIn(b, irRate);
        const auto stereo = ir.getNumChannels() >= 2 ? juce::dsp::Convolution::Stereo::yes
                                                     : juce::dsp::Convolution::Stereo::no;
        convolutions[b.type].loadImpulseResponse(std::move(ir), irRate, stereo,
                                                 juce::dsp::Convolution::Trim::no,
                                                 juce::dsp::Convolution::Normalise::no);
        prepareConvolution(b.type);
    }
    prepareConvolution(CustomIR); // keeps a previously loaded custom IR; the processor re-applies it
}

void BodyResonance::setCustomIR(const juce::AudioBuffer<float>& source, double irSampleRate, bool prepareNow)
{
    if (source.getNumSamples() == 0 || source.getNumChannels() == 0 || irSampleRate <= 0.0)
    {
        customIRLoaded.store(false);
        return;
    }

    // Truncate to the maximum length with a 10 ms raised-cosine fade, keep at most two channels
    const int maxLen = static_cast<int>(kMaxCustomIRSeconds * irSampleRate);
    const int len = std::min(source.getNumSamples(), maxLen);
    const int channels = std::min(source.getNumChannels(), 2);
    juce::AudioBuffer<float> ir(channels, len);
    for (int ch = 0; ch < channels; ++ch)
        ir.copyFrom(ch, 0, source, ch, 0, len);
    if (source.getNumSamples() > maxLen)
    {
        const int fade = std::min(len, static_cast<int>(0.010 * irSampleRate));
        for (int ch = 0; ch < channels; ++ch)
        {
            float* d = ir.getWritePointer(ch);
            for (int i = 0; i < fade; ++i)
                d[len - fade + i] *= 0.5f * (1.0f + std::cos(juce::MathConstants<float>::pi * static_cast<float>(i) / static_cast<float>(fade)));
        }
    }
    normaliseIREnergy(ir);

    // Safety cap: energy matching equalises average gain only. A peaky "IR" (e.g. a recording of
    // sustained notes, whose spectrum is ~20 dB peakier than a real body) would otherwise ring and
    // overload at those pitches. Limit its peak response to the loudest built-in's peak + headroom.
    const float peak = peakMagnitudeResponse(ir);
    const float cap = builtInPeakResponse() * juce::Decibels::decibelsToGain(kCustomIRPeakHeadroomDb);
    if (peak > cap && peak > 0.0f)
        ir.applyGain(cap / peak);

    const auto stereo = channels >= 2 ? juce::dsp::Convolution::Stereo::yes : juce::dsp::Convolution::Stereo::no;
    convolutions[CustomIR].loadImpulseResponse(std::move(ir), irSampleRate, stereo,
                                               juce::dsp::Convolution::Trim::no,
                                               juce::dsp::Convolution::Normalise::no);
    if (prepareNow)
        prepareConvolution(CustomIR);
    customIRLoaded.store(true);
}

void BodyResonance::setParameters(float bodySize, float bodyDamping, float bodyCoupling, int bodyType)
{
    // Audio-thread safe: only scalar state changes and zeroing of fixed-size modal state.
    currentCoupling = std::clamp(bodyCoupling, 0.0f, 1.0f);

    bodyType = std::clamp(bodyType, 0, NUM_BODY_TYPES - 1);
    if (bodyType == CustomIR && ! customIRLoaded.load())
        bodyType = ClassicalIR; // nothing loaded yet: sound like the default body
    if (bodyType != currentBodyType && freshStart)
    {
        // Nothing has been processed since reset(): take the requested body directly. Otherwise
        // every session would open with a 20 ms crossfade from the default (Classical) body.
        currentBodyType  = bodyType;
        previousBodyType = bodyType;
        fadeSamplesLeft  = 0;
    }
    else if (bodyType != currentBodyType)
    {
        // An engine that is still audible (the outgoing body of an unfinished crossfade) keeps its
        // state; any other body was idle and is cleared before fading in, so no stale tail leaks.
        const bool stillAudible = (fadeSamplesLeft > 0 && bodyType == previousBodyType);
        if (!stillAudible)
        {
            if (bodyType == ModalResonatorBank)
            {
                for (auto& f : filters)
                    f.reset();
                std::fill_n(crossDelayL, CROSS_DELAY_LEN, 0.f);
                std::fill_n(crossDelayR, CROSS_DELAY_LEN, 0.f);
            }
            else
            {
                // juce::dsp::Convolution::reset() only clears buffers (any retired engine is
                // handed to JUCE's background queue), so it is audio-thread safe.
                convolutions[bodyType].reset();
                airFilterL[bodyType] = 0.0f;
                airFilterR[bodyType] = 0.0f;
            }
        }
        previousBodyType = currentBodyType;
        currentBodyType  = bodyType;
        fadeSamplesLeft  = fadeSamplesTotal;
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
    // Process in chunks of the pre-allocated scratch size so any host block size works
    // without heap reallocations on the audio thread.
    const int chunkSize = maxBlockSize;
    int offset = 0;

    while (offset < numSamples)
    {
        const int n = std::min(numSamples - offset, chunkSize);

        std::copy(channelL + offset, channelL + offset + n, dryBufferL.begin());
        std::copy(channelR + offset, channelR + offset + n, dryBufferR.begin());

        // Only the audible IR engines run: the current body, plus the outgoing one during a crossfade.
        for (int t = 0; t < NUM_BODY_TYPES; ++t)
        {
            if (! isIRBody(t) || (t != currentBodyType && !(fadeSamplesLeft > 0 && t == previousBodyType)))
                continue;

            std::copy(dryBufferL.begin(), dryBufferL.begin() + n, convBufferL[t].begin());
            std::copy(dryBufferR.begin(), dryBufferR.begin() + n, convBufferR[t].begin());

            float* channels[2] = { convBufferL[t].data(), convBufferR[t].data() };
            juce::dsp::AudioBlock<float> block(channels, 2, static_cast<size_t>(n));
            juce::dsp::ProcessContextReplacing<float> context(block);
            convolutions[t].process(context);
        }

        for (int i = 0; i < n; ++i)
        {
            const float inL = dryBufferL[static_cast<size_t>(i)];
            const float inR = dryBufferR[static_cast<size_t>(i)];

            float outL = 0.f, outR = 0.f;
            renderBody(currentBodyType, i, inL, inR, outL, outR);

            if (fadeSamplesLeft > 0)
            {
                float prevL = 0.f, prevR = 0.f;
                renderBody(previousBodyType, i, inL, inR, prevL, prevR);
                const float g = static_cast<float>(fadeSamplesLeft) / static_cast<float>(fadeSamplesTotal);
                outL = (1.0f - g) * outL + g * prevL;
                outR = (1.0f - g) * outR + g * prevR;
                --fadeSamplesLeft;
            }

            channelL[offset + i] = outL;
            channelR[offset + i] = outR;
        }

        offset += n;
    }
    freshStart = false;
}

void BodyResonance::renderBody(int type, int i, float inL, float inR, float& outL, float& outR) noexcept
{
    if (type == ModalResonatorBank)
    {
        processStereo(inL, inR, outL, outR);
        return;
    }

    // Physical acoustic radiation blend:
    // currentCoupling = 1.0 -> 100% radiated acoustic soundboard (zero piezo quack)
    // currentCoupling = 0.0 -> 100% direct bridge string (dry DI pickup)
    const float dryFactor = 1.0f - currentCoupling;
    const float dryGain   = dryFactor * dryFactor;

    // All IRs are energy-normalised to the same reference at load, so one wet gain fits all
    const float wetGain = kIRWetScale * std::sqrt(currentCoupling);

    const auto idx = static_cast<size_t>(i);
    const float mixL = dryGain * inL + wetGain * convBufferL[type][idx];
    const float mixR = dryGain * inR + wetGain * convBufferR[type][idx];

    // Wood viscosity & air dispersion (~10 kHz single-pole roll-off)
    const float alpha = 1.0f - airFilterBeta;
    airFilterL[type] = alpha * mixL + airFilterBeta * airFilterL[type];
    airFilterR[type] = alpha * mixR + airFilterBeta * airFilterR[type];
    outL = airFilterL[type];
    outR = airFilterR[type];
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
    std::fill_n(airFilterL, NUM_BODY_TYPES, 0.0f);
    std::fill_n(airFilterR, NUM_BODY_TYPES, 0.0f);
    fadeSamplesLeft = 0;
    previousBodyType = currentBodyType;
    freshStart = true;
    for (auto& conv : convolutions)
        conv.reset();
}
