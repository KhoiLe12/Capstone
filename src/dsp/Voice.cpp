#include "Voice.h"
#include <cmath>
#include <algorithm>

static constexpr float kPi = 3.14159265358979323846f;

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------

void Voice::init(float sr)
{
    sampleRate = sr;
    string.init(sr);
    fdtdString.init(sr);
    // Classical finger/palm damping time constant ~35ms: smooth, natural decay
    releaseCoeff = std::exp(-1.0f / (0.035f * sampleRate));
}

// ---------------------------------------------------------------------------
// Note Events
// ---------------------------------------------------------------------------

void Voice::noteOn(int note, float vel,
                   float brightness, float pickPosition, float decay,
                   float stiffness, KarplusStrong::MuteMode muteMode,
                   int stringIdx)
{
    midiNote = note;
    velocity = vel;
    active   = true;
    releasing = false;
    choking  = false;
    chokeGain = 1.0f;
    releaseGain = 1.0f;
    fadeSamplesLeft = -1;

    currentMuteMode = muteMode;
    currentDecay = decay;

    // 6 acoustic guitar strings: E2 (40), A2 (45), D3 (50), G3 (55), B3 (59), E4 (64)
    static constexpr int openMidi[6]   = { 40, 45, 50, 55, 59, 64 };
    static constexpr float radii[6]    = { 0.00055f, 0.00048f, 0.00042f, 0.00038f, 0.00034f, 0.00028f };

    // Effective bending core radius for stiffness EI:
    // On acoustic/classical guitars, wound bass strings (strings 0, 1, 2) have an outer wire wrap
    // that flexes freely with negligible beam bending stiffness; bending rigidity comes solely
    // from the inner multifilament nylon core (r_core ~ 0.18-0.20 mm).
    // Plain treble strings (strings 3, 4, 5) are flexible monofilament nylon.
    static constexpr float bendingRadii[6] = { 0.00018f, 0.00019f, 0.00020f, 0.00026f, 0.00024f, 0.00020f };

    int s = stringIdx;
    if (s < 0 || s > 5)
    {
        if (note < 45)      s = 0;
        else if (note < 50) s = 1;
        else if (note < 55) s = 2;
        else if (note < 59) s = 3;
        else if (note < 64) s = 4;
        else                s = 5;
    }

    physicalStringIndex = s;

    if (engineType == EngineType::BilbaoFdtd)
    {
        const int fret = std::max(0, std::min(22, note - openMidi[s]));
        const float fretLength = 0.65f * std::pow(2.0f, -static_cast<float>(fret) / 12.0f);
        const float targetFreq = midiToFreq(note);

        FdtdString::StringParams p;
        p.fretNumber = fret;
        p.L = fretLength;
        p.r = radii[s];
        p.r_stiffness = bendingRadii[s];
        p.rho = 1140.0f;
        p.E = 1.2e9f * (0.15f + 1.2f * stiffness);

        const float A = 3.14159265358979323846f * p.r * p.r;
        const float I = 3.14159265358979323846f * p.r_stiffness * p.r_stiffness * p.r_stiffness * p.r_stiffness * 0.25f;
        const float rhoA = p.rho * A;
        const float EI = p.E * I;

        // Discrete grid numerical dispersion compensation across physical strings (0..5)
        static constexpr float centsOffsets[6] = { 1.5f, 1.5f, 1.0f, 0.8f, 0.5f, 0.0f };
        const float tunedFreq = targetFreq * std::pow(2.0f, -centsOffsets[s] / 1200.0f);

        // f = (1 / 2L) * sqrt(T0/rhoA + pi^2 EI / rhoA L^2)
        // -> T0 = rhoA * (2*L*f)^2 - pi^2 * EI / L^2
        float T0 = rhoA * std::pow(2.0f * p.L * tunedFreq, 2.0f) - (3.14159265f * 3.14159265f * EI) / (p.L * p.L);
        if (T0 < 5.0f) T0 = 5.0f;
        p.T0 = T0;

        // Dynamic velocity-dependent brightness (harder plucks produce crisper fingernail release bite):
        const float clampedVel = std::clamp(velocity, 0.01f, 1.0f);
        const float effBrightness = std::clamp(brightness * (0.35f + 0.65f * clampedVel), 0.05f, 1.0f);

        // Register-dependent acoustic decay and compliance:
        // On a real guitar, higher pitched / shorter fretted strings complete more vibration
        // cycles per second, draining energy into the bridge soundboard proportionally faster.
        // Bass strings (E2..D3) sustain for 7-9 seconds; high frets (C6..D6) decay in 0.35-0.5 seconds.
        const float fRatio = std::clamp(targetFreq / 82.4f, 1.0f, 16.0f);
        const float registerDecayScale = std::pow(fRatio, 1.15f);
        float effSigma0 = (0.5f + 1.0f * (1.0f - decay)) * registerDecayScale;

        // Plain nylon treble damping across the entire register up to 1200 Hz:
        // High fretted notes have rapid loss on high partials, completely taming harsh synthetic screeching.
        const float trebleFactor = std::clamp((targetFreq - 146.0f) / (1175.0f - 146.0f), 0.0f, 1.0f);
        float effSigma1 = 1.5e-4f + 2.5e-3f * trebleFactor;
        float effDispScale = 1.0f;

        if (muteMode == KarplusStrong::MuteMode::Palm)
        {
            // Palm muting: fleshy palm heel on bridge absorbs high partials and drops sustain to ~80-100ms
            effSigma0 = std::max(28.0f, effSigma0 * 2.2f);
            effSigma1 = std::max(1.5e-3f, effSigma1 * 2.0f);
            effDispScale = 0.65f;
        }
        else if (muteMode == KarplusStrong::MuteMode::Full)
        {
            // Full mute / dead notes ("X" notes): fretting hand rests flat across strings
            effSigma0 = std::max(70.0f, effSigma0 * 4.0f);
            effSigma1 = std::max(3.5e-3f, effSigma1 * 2.5f);
            effDispScale = 0.45f;
        }

        p.sigma0 = effSigma0;
        p.sigma1 = effSigma1;
        p.brightness = effBrightness;
        p.m0 = -0.0014f; // authentic classical action clearance (1.4 mm)
        p.b0 = -0.0030f; // fretboard clearance (3.0 mm)

        fdtdString.init(sampleRate, p);

        // String physical compliance scales with vibrating length:
        // C_string = L / (4 * T0). Short fretted strings displace less under equal fingertip force.
        // This also balances bridge force (d_u/d_x ~ u_peak / L) perfectly across all 22 frets.
        const float lengthScale = std::clamp(p.L / 0.65f, 0.35f, 1.0f);
        const float peakDisp = (-0.0003f - 0.0009f * velocity) * effDispScale * lengthScale;
        const float clampedPickPos = std::clamp(pickPosition, 0.10f, 0.50f);
        fdtdString.pluckDisplacement(clampedPickPos, peakDisp, effBrightness);

        if (muteMode == KarplusStrong::MuteMode::Full)
        {
            // Trigger physical mechanical hand slap / fret impact transient
            choke(velocity);
        }
        return;
    }

    const float freq = midiToFreq(note);
    string.setFrequency(freq, stiffness);
    string.setMuteMode(muteMode);
    string.setDecay(decay);

    // Exciter length matches one wavelength (delay-line size)
    const int exciterLen = std::min(static_cast<int>(sampleRate / freq),
                                    kMaxExciterLength);

    // Register-dependent nylon string physics:
    // Wound strings have higher mass and tactile winding scrape
    const float woundFactor = std::clamp((196.0f - freq) / (196.0f - 82.0f), 0.0f, 1.0f);

    float effBrightness = brightness;
    if (muteMode == KarplusStrong::MuteMode::Full)
    {
        // Dead notes:
        // Wound bass strings have warm, wooden fundamental thumps (darker, fleshy)
        // Plain treble strings have crisp, papery clicks (higher frequency snap)
        effBrightness = brightness * (0.28f + 0.22f * (1.0f - woundFactor));
    }

    // Fill with velocity-dependent physical classical nylon finger pulse (flesh + nail)
    exciter.fill(exciterScratch, exciterLen,
                 velocity,
                 ExciterType::NYLON_FINGER_MODEL,
                 effBrightness,
                 pickPosition,
                 sampleRate,
                 freq);

    string.trigger(exciterScratch, exciterLen, velocity);
}

void Voice::noteOff() noexcept
{
    // Begin smooth acoustic release envelope
    releasing = true;
    if (engineType == EngineType::DigitalWaveguide)
        string.damp();
}

void Voice::setMuteMode(KarplusStrong::MuteMode mode) noexcept
{
    currentMuteMode = mode;
    string.setMuteMode(mode);

    if (mode == KarplusStrong::MuteMode::Palm)
    {
        fdtdString.setDamping(24.0f, 8.0e-4f);
    }
    else if (mode == KarplusStrong::MuteMode::Full)
    {
        fdtdString.setDamping(65.0f, 2.0e-3f);
    }
    else
    {
        fdtdString.setDamping(0.5f + 1.0f * (1.0f - currentDecay), 1.0e-4f);
    }
}

void Voice::choke(float chokeVelocity) noexcept
{
    if (!active) return;

    choking = true;
    chokeGain = 1.0f;

    // Physical contact with palm/fret hand instantly absorbs string kinetic energy:
    // Damping time constant 1.8ms (chokeCoeff): string ringing drops to <6% in ~5ms
    chokeCoeff = std::exp(-1.0f / (0.0018f * sampleRate));
    // Transient impulse of ~45ms excites the acoustic body IR without synthetic bass boom
    chokeSamplesLeft = static_cast<int>(0.045f * sampleRate);

    // Instant damping for FDTD physical string
    fdtdString.setDamping(75.0f, 3.0e-3f);

    const float freq = midiToFreq(midiNote);
    const float woundFactor = std::clamp((196.0f - freq) / (196.0f - 82.0f), 0.0f, 1.0f);

    // 1. Soundboard cavity air thud (A0 Helmholtz / lower bout mode):
    // Wound strings have higher mass impact -> deep, warm 105 Hz thud; plain strings ~145 Hz
    const float thudFreq = 105.0f + 40.0f * (1.0f - woundFactor);
    slapThudPhase = 0.f;
    slapThudPhaseInc = 2.0f * kPi * thudFreq / sampleRate;
    slapThudAmp = chokeVelocity * (0.07f + 0.07f * woundFactor);
    const float thudTime = 0.010f + 0.010f * woundFactor; // 10ms (plain) to 20ms (wound)
    slapThudDecay = std::exp(-1.0f / (thudTime * sampleRate));

    // 2. Fret-wire contact snap (sharp high-frequency mechanical transient):
    const float clickFreq = 1800.0f + 800.0f * (1.0f - woundFactor);
    slapClickPhase = 0.f;
    slapClickPhaseInc = 2.0f * kPi * clickFreq / sampleRate;
    slapClickAmp = chokeVelocity * (0.04f + 0.04f * (1.0f - woundFactor));
    const float clickTime = 0.003f + 0.003f * woundFactor; // 3ms to 6ms
    slapClickDecay = std::exp(-1.0f / (clickTime * sampleRate));
}

// ---------------------------------------------------------------------------
// Per-Sample Processing
// ---------------------------------------------------------------------------

float Voice::tick() noexcept
{
    if (!active) return 0.f;

    // Scale FDTD spatial slope to match DWG digital line level cleanly
    float out = (engineType == EngineType::BilbaoFdtd) ? (fdtdString.tick() * 95.0f) : string.tick();

    // Physical acoustic choke handling (hand slapped on strings)
    if (choking)
    {
        out *= chokeGain;
        chokeGain *= chokeCoeff;

        // Once the string vibration has decayed into silence (< 0.01 = -40dB), reset the delay line
        // so no residual harmonic pitch can circulate or leak through.
        if (chokeGain < 0.01f && chokeGain > 0.0f)
        {
            string.reset();
            fdtdString.reset();
            chokeGain = 0.0f;
        }

        float slapOut = 0.f;
        if (slapThudAmp > 1e-5f)
        {
            slapOut += slapThudAmp * std::sin(slapThudPhase);
            slapThudPhase += slapThudPhaseInc;
            if (slapThudPhase >= 2.0f * kPi) slapThudPhase -= 2.0f * kPi;
            slapThudAmp *= slapThudDecay;
        }

        if (slapClickAmp > 1e-5f)
        {
            slapOut += slapClickAmp * std::sin(slapClickPhase);
            slapClickPhase += slapClickPhaseInc;
            if (slapClickPhase >= 2.0f * kPi) slapClickPhase -= 2.0f * kPi;
            slapClickAmp *= slapClickDecay;
        }

        out += slapOut;

        --chokeSamplesLeft;
        if (chokeSamplesLeft <= 0 || (chokeGain <= 0.0f && slapThudAmp <= 1e-5f && slapClickAmp <= 1e-5f))
        {
            active = false;
            choking = false;
            releasing = false;
            midiNote = -1;
            string.reset();
            fdtdString.reset();
            return 0.f;
        }

        return out;
    }

    // Apply smooth exponential release envelope on note-off
    if (releasing)
    {
        out *= releaseGain;
        releaseGain *= releaseCoeff;

        // When release gain has decayed into near-inaudibility (-46 dB),
        // trigger an anti-click linear fade to zero
        if (releaseGain < 0.005f && fadeSamplesLeft < 0)
        {
            fadeSamplesLeft = 64;
        }
    }
    else
    {
        const float currentEnergy = (engineType == EngineType::BilbaoFdtd) ? fdtdString.getEnergy() : string.getEnergy();
        if (currentEnergy < 1e-7f && fadeSamplesLeft < 0)
        {
            // Natural ring-out also fades out cleanly instead of hard-cutting
            fadeSamplesLeft = 64;
        }
    }

    // 64-sample linear fade-out to guarantee zero DC or step click
    if (fadeSamplesLeft > 0)
    {
        const float fadeFactor = static_cast<float>(fadeSamplesLeft) / 64.0f;
        out *= fadeFactor;
        --fadeSamplesLeft;

        if (fadeSamplesLeft == 0)
        {
            active = false;
            releasing = false;
            midiNote = -1;
            string.reset();
            fdtdString.reset();
            return 0.f;
        }
    }

    return out;
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------

void Voice::reset() noexcept
{
    string.reset();
    fdtdString.reset();
    active = false;
    releasing = false;
    choking = false;
    chokeGain = 1.0f;
    chokeSamplesLeft = 0;
    slapThudAmp = 0.f;
    slapClickAmp = 0.f;
    releaseGain = 1.0f;
    fadeSamplesLeft = -1;
    midiNote = -1;
}

float Voice::midiToFreq(int note) noexcept
{
    // Equal temperament tuning: A4 = 440 Hz = MIDI 69
    return 440.f * std::pow(2.f, (note - 69) / 12.f);
}
