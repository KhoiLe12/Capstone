#include "Voice.h"
#include <cmath>
#include <algorithm>

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
                   int stringIdx, int mat, int geometryString)
{
    midiNote = note;
    velocity = vel;
    active   = true;
    samplesSinceNoteOn = 0;
    releasing = false;
    choking  = false;
    chokeGain = 1.0f;
    releaseGain = 1.0f;
    fadeSamplesLeft = -1;
    fadeSamplesTotal = 96;
    stringMaterial = mat;

    currentMuteMode = muteMode;
    currentDecay = decay;

    const bool isSteel = (stringMaterial == 1);

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

    // A note placed past the fretboard (non-strict chord voicing) occupies string slot s but
    // sounds with the gauge of the string that would naturally play it.
    geometryStringIndex = (geometryString >= 0 && geometryString <= 5) ? geometryString : s;
    s = geometryStringIndex;

    {
        static constexpr int openMidi[6] = { 40, 45, 50, 55, 59, 64 };
        const int fret = std::clamp(note - openMidi[std::clamp(s, 0, 5)], 0, 22);
        const float baseScale = (instrumentMode == 1) ? ((guitarModel == 1) ? 0.62865f : 0.64770f) : 0.65f;
        currentStringLengthMeters = baseScale * std::pow(2.0f, -static_cast<float>(fret) / 12.0f);
    }

    const float clampedVel = std::clamp(velocity, 0.01f, 1.0f);

    if (engineType == EngineType::BilbaoFdtd)
    {
        // Dynamic velocity-dependent brightness with humanization micro-variation:
        const float humanJitter = 1.0f + 0.03f * humanRand();
        const float pickJitter  = 0.012f * humanRand();
        const float humanAngle  = humanRand();
        float effBrightness = std::clamp(brightness * (0.35f + 0.65f * clampedVel) * humanJitter, 0.05f, 1.0f);

        FdtdString::StringParams p = makeFdtdParams(note, s, stiffness, decay, effBrightness);

        const FdtdString::MuteMode fdtdMute = (muteMode == KarplusStrong::MuteMode::Palm) ? FdtdString::MuteMode::Palm :
                                              (muteMode == KarplusStrong::MuteMode::Full) ? FdtdString::MuteMode::Full :
                                              FdtdString::MuteMode::Open;

        fdtdString.init(sampleRate, p, fdtdMute, decay);

        // String physical compliance scales with vibrating length and velocity
        const float lengthScale = std::clamp(std::sqrt(p.L / 0.65f), 0.60f, 1.0f);
        const float peakDisp = (-0.0004f - 0.00125f * clampedVel) * lengthScale;
        const float clampedPickPos = std::clamp(pickPosition + pickJitter, 0.08f, 0.48f);
        fdtdString.startPlectrumContact(clampedPickPos, peakDisp, effBrightness, humanAngle);
        resetFdtdSilenceReference();
        return;
    }

    const float freq = midiToFreq(note);
    string.setFrequency(freq, isSteel ? std::min(1.0f, stiffness * 1.5f) : stiffness);
    string.setMuteMode(muteMode);
    string.setDecay(isSteel ? std::min(0.98f, decay * 1.08f) : decay);

    // Exciter length matches one wavelength (delay-line size)
    const int exciterLen = std::min(static_cast<int>(sampleRate / freq),
                                    kMaxExciterLength);

    // Register-dependent string physics:
    const float woundFactor = std::clamp((196.0f - freq) / (196.0f - 82.0f), 0.0f, 1.0f);

    float effBrightness = brightness;
    if (muteMode == KarplusStrong::MuteMode::Full)
    {
        effBrightness = brightness * (0.28f + 0.22f * (1.0f - woundFactor));
    }

    // Fill with exciter model (Plectrum for steel, Nylon Finger for nylon)
    exciter.fill(exciterScratch, exciterLen,
                 velocity,
                 isSteel ? ExciterType::PLECTRUM_MODEL : ExciterType::NYLON_FINGER_MODEL,
                 effBrightness,
                 pickPosition,
                 sampleRate,
                 freq);

    string.trigger(exciterScratch, exciterLen, velocity);
}

FdtdString::StringParams Voice::makeFdtdParams(int note, int s, float stiffness, float decay, float brightness) noexcept
{
    static constexpr int openMidi[6] = { 40, 45, 50, 55, 59, 64 };

    // Nylon string geometries (radii and inner flexible bending core radii)
    static constexpr float nylonRadii[6]        = { 0.00055f, 0.00048f, 0.00042f, 0.00038f, 0.00034f, 0.00028f };
    static constexpr float nylonBendingRadii[6] = { 0.00018f, 0.00019f, 0.00020f, 0.00026f, 0.00024f, 0.00020f };

    // Steel string geometries (standard light acoustic set 12-53):
    // Strings 0..3: phosphor bronze wound on hex steel core; strings 4, 5: plain steel wire
    static constexpr float steelRadii[6]        = { 0.00067f, 0.00053f, 0.00041f, 0.00030f, 0.00020f, 0.00015f };
    static constexpr float steelBendingRadii[6] = { 0.00018f, 0.00018f, 0.00018f, 0.00016f, 0.00020f, 0.00015f };
    static constexpr float steelDensities[6]    = { 6200.0f,  6400.0f,  6600.0f,  6800.0f,  7850.0f,  7850.0f };

    // Discrete grid numerical dispersion compensation across physical strings (0..5)
    static constexpr float centsOffsets[6] = { 1.5f, 1.5f, 1.0f, 0.8f, 0.5f, 0.0f };

    s = std::clamp(s, 0, 5);
    const bool isElectric = (instrumentMode == 1);
    const bool isSteel = isElectric ? true : (stringMaterial == 1);
    const float targetFreq = midiToFreq(note);
    const int fret = std::clamp(note - openMidi[s], 0, 22);

    FdtdString::StringParams p;
    p.fretNumber  = fret;
    const float baseScale = isElectric ? ((guitarModel == 1) ? 0.62865f : 0.64770f) : 0.65f;
    p.L           = baseScale * std::pow(2.0f, -static_cast<float>(fret) / 12.0f);
    p.f0          = targetFreq;
    p.r           = isSteel ? steelRadii[s] : nylonRadii[s];
    p.r_stiffness = isSteel ? steelBendingRadii[s] : nylonBendingRadii[s];
    p.rho         = isSteel ? steelDensities[s] : 1140.0f;
    // Nylon Young's modulus E ~ 1.2 GPa, Steel E ~ 200 GPa:
    p.E = isSteel ? (2.0e11f * (0.35f + 0.85f * stiffness))
                  : (1.2e9f  * (0.15f + 1.20f * stiffness));

    const float A = 3.14159265358979323846f * p.r * p.r;
    const float I = 3.14159265358979323846f * p.r_stiffness * p.r_stiffness * p.r_stiffness * p.r_stiffness * 0.25f;
    const float rhoA = p.rho * A;
    const float EI = p.E * I;

    // Stiff-string tuning: f = (1 / 2L) * sqrt(T0/rhoA + pi^2 EI / (rhoA L^2))
    //   -> T0 = rhoA * (2*L*f)^2 - pi^2 * EI / L^2
    const float tunedFreq = targetFreq * std::pow(2.0f, -centsOffsets[s] / 1200.0f);
    p.T0 = std::max(5.0f, rhoA * std::pow(2.0f * p.L * tunedFreq, 2.0f)
                          - (3.14159265f * 3.14159265f * EI) / (p.L * p.L));

    // Register-dependent acoustic decay with fretboard positional damping (open nut rings longer).
    // Exponents calibrated against GuitarSet fundamental decay rates (benchmarks/decay_calibration.py):
    // real steel-string decay grows only weakly with pitch (~f^0.21). Anchored at E2, so the Decay
    // knob's bass sustain is unchanged. Nylon keeps its previous +0.15 offset (no nylon reference data).
    const float fRatio = std::clamp(targetFreq / 82.4f, 1.0f, 16.0f);
    const float registerDecayScale = std::pow(fRatio, isSteel ? kFdtdSteelRegisterExponent : kFdtdNylonRegisterExponent);
    const float nutVsFretScale = (fret == 0) ? 0.88f : (1.0f + 0.008f * static_cast<float>(fret));

    if (isElectric)
    {
        // Solid body termination (rigid bridge on solid hardwood slab):
        // Vastly lower boundary loss than an acoustic soundboard, yielding 8–15s of singing sustain.
        const float baseSigma0 = (guitarModel == 1) ? 0.16f : 0.20f;
        fdtdOpenSigma0 = (baseSigma0 + 0.35f * (1.0f - decay)) * registerDecayScale * nutVsFretScale;

        const float trebleFactor = std::clamp((targetFreq - 110.0f) / (1000.0f - 110.0f), 0.0f, 1.0f);
        const float decaySigma1Scale = 1.0f + 0.5f * (1.0f - decay);
        fdtdOpenSigma1 = (2.2e-4f + 1.2e-3f * trebleFactor) * decaySigma1Scale;
    }
    else
    {
        fdtdOpenSigma0 = (isSteel ? (0.45f + 0.85f * (1.0f - decay)) : (0.75f + 1.25f * (1.0f - decay)))
                         * registerDecayScale * nutVsFretScale;

        const float trebleFactor = std::clamp((targetFreq - 110.0f) / (1000.0f - 110.0f), 0.0f, 1.0f);
        const float decaySigma1Scale = 1.0f + 0.6f * (1.0f - decay);
        fdtdOpenSigma1 = (isSteel ? (3.5e-4f + 1.8e-3f * trebleFactor)
                                  : (4.5e-4f + 2.5e-3f * trebleFactor)) * decaySigma1Scale;
    }

    p.sigma0     = fdtdOpenSigma0;
    p.sigma1     = fdtdOpenSigma1;
    p.brightness = brightness;
    fdtdBrightness = brightness;

    // Longitudinal snap and tension bloom
    p.longitudinalAttack = isSteel ? 0.35f : 0.15f;
    p.phantomCoupling    = isSteel ? 0.30f : 0.20f;

    // Wound bass string vs plain treble factor
    // On acoustic, strings 0..3 are wound (E2, A2, D3, G3); on electric sets (10-46), G3 (string 3) is plain steel!
    if (isElectric)
        p.woundFactor = (s <= 2) ? (1.0f - static_cast<float>(s) * 0.33f) : 0.0f;
    else
        p.woundFactor = (s <= (isSteel ? 3 : 2)) ? (1.0f - static_cast<float>(s) * 0.22f) : 0.0f;

    // Calibrated fret action clearance (1.3 mm bass down to 1.0 mm treble)
    p.m0 = -0.0010f - 0.0003f * (1.0f - static_cast<float>(s) / 5.0f);
    p.b0 = -0.0028f;
    return p;
}

void Voice::legatoTransition(int newNote, float newVelocity, bool isHammerOn, float stiffness) noexcept
{
    midiNote = newNote;
    velocity = newVelocity;
    samplesSinceNoteOn = 0;
    releasing = false;
    releaseGain = 1.0f;
    fadeSamplesLeft = -1; // Reset any decay auto-fade
    fadeSamplesTotal = 96;
    active = true;

    const float freq = midiToFreq(midiNote);
    int s = geometryStringIndex;
    if (s < 0 || s > 5)
    {
        if (newNote < 45)      s = 0;
        else if (newNote < 50) s = 1;
        else if (newNote < 55) s = 2;
        else if (newNote < 59) s = 3;
        else if (newNote < 64) s = 4;
        else                   s = 5;
    }

    {
        static constexpr int openMidi[6] = { 40, 45, 50, 55, 59, 64 };
        const int fret = std::clamp(newNote - openMidi[std::clamp(s, 0, 5)], 0, 22);
        const float baseScale = (instrumentMode == 1) ? ((guitarModel == 1) ? 0.62865f : 0.64770f) : 0.65f;
        currentStringLengthMeters = baseScale * std::pow(2.0f, -static_cast<float>(fret) / 12.0f);
    }

    if (engineType == EngineType::DigitalWaveguide)
    {
        string.legatoTransition(freq, newVelocity, isHammerOn, stiffness);
    }
    else
    {
        // Same physical parameter derivation as a picked note on this string, keeping the
        // note's saddle brightness so the timbre does not jump on the fret change.
        FdtdString::StringParams p = makeFdtdParams(newNote, s, stiffness, currentDecay, fdtdBrightness);
        fdtdString.legatoTransition(p, newVelocity, isHammerOn);
        resetFdtdSilenceReference(); // new reference for the retuned string
    }
}

void Voice::noteOff() noexcept
{
    // Begin smooth acoustic release envelope
    releasing = true;

    // Frequency-dependent release time constant:
    // Treble strings (plain nylon) damp rapidly (~35-45 ms) under fingertip flesh.
    // Wound bass strings have larger mass and inertia; their fundamental thumps for ~75-105 ms.
    const float freq = midiToFreq(midiNote > 0 ? midiNote : 40);
    const float woundFactor = std::clamp((196.0f - freq) / (196.0f - 82.0f), 0.0f, 1.0f);

    // Organic micro-release jitter: avoids identical mathematical gate shut-down across polyphonic chords
    const float humanJitter = 1.0f + 0.08f * humanRand();
    const float releaseTau = (0.038f + 0.052f * woundFactor) * humanJitter;
    releaseCoeff = std::exp(-1.0f / (releaseTau * sampleRate));

    if (engineType == EngineType::DigitalWaveguide)
    {
        string.damp();
    }
    else
    {
        // Frequency-scaled flesh damping on fret release (wound bass strings damp slightly slower than thin trebles)
        const float releaseDamping = 45.0f - 18.0f * woundFactor;
        fdtdString.setDamping(releaseDamping);
    }
}

void Voice::setMuteMode(KarplusStrong::MuteMode mode) noexcept
{
    currentMuteMode = mode;
    string.setMuteMode(mode);

    if (mode == KarplusStrong::MuteMode::Palm)
    {
        fdtdString.setMuteMode(FdtdString::MuteMode::Palm, currentDecay);
    }
    else if (mode == KarplusStrong::MuteMode::Full)
    {
        if (active)
        {
            fdtdString.setMuteMode(FdtdString::MuteMode::Full);
        }
    }
    else
    {
        fdtdString.setMuteMode(FdtdString::MuteMode::Open);
    }
}

void Voice::choke(float chokeVelocity) noexcept
{
    if (!active)
    {
        // Slap on muted strings: trigger low-register dead note physical response
        noteOn(40, chokeVelocity, 0.35f, 0.22f, 0.5f, 0.25f, KarplusStrong::MuteMode::Full, 0, stringMaterial);
        return;
    }

    choking = true;
    chokeGain = 1.0f;

    // Physical contact with palm/fret hand absorbs string kinetic energy over ~10-15 ms:
    chokeCoeff = std::exp(-1.0f / (0.010f * sampleRate));
    chokeSamplesLeft = static_cast<int>(0.035f * sampleRate);
    fdtdString.setMuteMode(FdtdString::MuteMode::Full);

    // Mechanical hand slap impulse into bridge saddle (excites BodyResonance soundboard)
    const float freq = midiToFreq(midiNote > 0 ? midiNote : 40);
    const float woundFactor = std::clamp((196.0f - freq) / (196.0f - 82.0f), 0.0f, 1.0f);

    slapActive = true;
    slapPulseAmp = chokeVelocity * 0.30f * (0.8f + 0.4f * woundFactor);
    slapPulseSigma = 48.0f;
    slapPulseCenter = 40.0f;
    slapPulseElapsed = 0;
    slapPulseDuration = static_cast<int>(0.025f * sampleRate);
}

// ---------------------------------------------------------------------------
// Per-Sample Processing
// ---------------------------------------------------------------------------

float Voice::tick() noexcept
{
    if (!active) return 0.f;
    ++samplesSinceNoteOn;

    // Scale FDTD spatial slope to match DWG digital line level cleanly
    float out = (engineType == EngineType::BilbaoFdtd) ? (fdtdString.tick() * 95.0f) : string.tick();

    // Safety guard against NaN/Inf or runaway numerical explosion
    if (std::isnan(out) || std::isinf(out) || std::abs(out) > 50.0f)
    {
        fdtdString.reset();
        string.reset();
        active = false;
        return 0.0f;
    }

    // Physical acoustic choke handling (hand slapped on strings)
    if (choking)
    {
        out *= chokeGain;
        chokeGain *= chokeCoeff;

        // Once the string vibration has decayed into silence (< 0.002 = -54dB), reset the delay line
        // so no residual harmonic pitch can circulate or leak through.
        if (chokeGain < 0.002f && chokeGain > 0.0f)
        {
            string.reset();
            fdtdString.reset();
            chokeGain = 0.0f;
        }

        if (slapActive)
        {
            if (slapPulseElapsed < slapPulseDuration)
            {
                const float t = static_cast<float>(slapPulseElapsed) - slapPulseCenter;
                const float u = t / slapPulseSigma;
                if (std::abs(u) < 3.8f)
                {
                    out += -u * std::exp(-0.5f * u * u) * slapPulseAmp;
                }
                ++slapPulseElapsed;
            }
            else
            {
                slapActive = false;
            }
        }

        --chokeSamplesLeft;
        if (chokeSamplesLeft <= 0 || (chokeGain <= 0.0f && !slapActive))
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

        // When release gain has decayed into near-inaudibility (-52 dB),
        // trigger an anti-click linear fade to zero
        if (releaseGain < 0.0025f && fadeSamplesLeft < 0)
        {
            fadeSamplesLeft = 96;
            fadeSamplesTotal = 96;
        }
    }
    else
    {
        const bool silent = (engineType == EngineType::BilbaoFdtd) ? fdtdIsSilent()
                                                                   : (string.getEnergy() < 1e-7f);
        if (silent && fadeSamplesLeft < 0)
        {
            // Natural ring-out also fades out cleanly instead of hard-cutting
            fadeSamplesLeft = 64;
            fadeSamplesTotal = 64;
        }
    }

    // Linear fade-out to guarantee zero DC or step click
    if (fadeSamplesLeft > 0)
    {
        const float fadeFactor = static_cast<float>(fadeSamplesLeft) / static_cast<float>(fadeSamplesTotal);
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

void Voice::tickPolar(float& outV, float& outH) noexcept
{
    if (!active)
    {
        outV = 0.0f;
        outH = 0.0f;
        return;
    }

    if (engineType == EngineType::BilbaoFdtd)
    {
        ++samplesSinceNoteOn;
        const bool isElectric = (instrumentMode == 1);
        float fV = 0.0f, fH = 0.0f;
        fdtdString.tickPolar(fV, fH, !isElectric);
        fV *= 95.0f;
        fH *= 95.0f;

        // Safety guard against NaN/Inf or runaway numerical explosion
        if (!fdtdString.isStateValid() ||
            std::isnan(fV) || std::isinf(fV) || std::abs(fV) > 50.0f ||
            std::isnan(fH) || std::isinf(fH) || std::abs(fH) > 50.0f)
        {
            fdtdString.reset();
            string.reset();
            active = false;
            outV = 0.0f;
            outH = 0.0f;
            return;
        }

        // Apply choking, envelope and fade
        float gainScale = 1.0f;
        if (choking)
        {
            gainScale = chokeGain;
            chokeGain *= chokeCoeff;
            if (chokeGain < 0.002f && chokeGain > 0.0f)
            {
                string.reset();
                fdtdString.reset();
                chokeGain = 0.0f;
            }
        }
        else if (releasing)
        {
            gainScale = releaseGain;
            releaseGain *= releaseCoeff;
            if (releaseGain < 0.0025f && fadeSamplesLeft < 0)
            {
                fadeSamplesLeft = 96;
                fadeSamplesTotal = 96;
            }
        }
        else
        {
            if (fdtdIsSilent() && fadeSamplesLeft < 0)
            {
                fadeSamplesLeft = 64;
                fadeSamplesTotal = 64;
            }
        }

        if (fadeSamplesLeft > 0)
        {
            gainScale *= (static_cast<float>(fadeSamplesLeft) / static_cast<float>(fadeSamplesTotal));
            --fadeSamplesLeft;
            if (fadeSamplesLeft == 0)
            {
                active = false;
                releasing = false;
                midiNote = -1;
                string.reset();
                fdtdString.reset();
            }
        }

        lastGainScale = gainScale;
        outV = fV * gainScale;
        outH = fH * gainScale;

        // Choke mechanical hand slap & full voice termination (independent of slapActive!)
        if (choking)
        {
            if (slapActive)
            {
                if (slapPulseElapsed < slapPulseDuration)
                {
                    const float t = static_cast<float>(slapPulseElapsed) - slapPulseCenter;
                    const float u = t / slapPulseSigma;
                    if (std::abs(u) < 3.8f)
                    {
                        outV += -u * std::exp(-0.5f * u * u) * slapPulseAmp;
                    }
                    ++slapPulseElapsed;
                }
                else
                {
                    slapActive = false;
                }
            }

            --chokeSamplesLeft;
            if (chokeSamplesLeft <= 0 || (chokeGain <= 0.0f && !slapActive))
            {
                active = false;
                choking = false;
                releasing = false;
                midiNote = -1;
                string.reset();
                fdtdString.reset();
            }
        }
    }
    else
    {
        // Digital Waveguide fallback: single axis vertical drive (tick() increments samplesSinceNoteOn)
        const float sOut = tick();
        lastGainScale = releasing ? releaseGain : (choking ? chokeGain : 1.0f);
        outV = sOut;
        outH = 0.0f;
    }
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
    slapActive = false;
    slapPulseElapsed = 0;
    chokeGain = 1.0f;
    chokeSamplesLeft = 0;
    releaseGain = 1.0f;
    fadeSamplesLeft = -1;
    fadeSamplesTotal = 96;
    lastGainScale = 1.0f;
    midiNote = -1;
}

float Voice::midiToFreq(int note) noexcept
{
    // Equal temperament tuning: A4 = 440 Hz = MIDI 69
    return 440.f * std::pow(2.f, static_cast<float>(note - 69) / 12.f);
}

float Voice::getPickupVelocity(float distFromBridgeMeters, bool isHumbucker, float poleSpacingMeters) const noexcept
{
    if (!active)
        return 0.0f;

    float rawVel = 0.0f;
    if (engineType == EngineType::BilbaoFdtd)
    {
        rawVel = fdtdString.getPickupVelocity(distFromBridgeMeters, isHumbucker, poleSpacingMeters);
    }
    else
    {
        rawVel = string.getPickupVelocity(distFromBridgeMeters, currentStringLengthMeters, isHumbucker, poleSpacingMeters);
    }

    return rawVel * lastGainScale;
}
