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

    // High-frequency plectrum edge air sizzle filter (~8 kHz 1-pole HP)
    pickEdgeHPAlpha = 1.0f - std::exp(-2.0f * kPi * 8000.0f / sampleRate);
}

// ---------------------------------------------------------------------------
// Note Events
// ---------------------------------------------------------------------------

void Voice::noteOn(int note, float vel,
                   float brightness, float pickPosition, float decay,
                   float stiffness, KarplusStrong::MuteMode muteMode,
                   int stringIdx, int mat)
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
    stringMaterial = mat;

    currentMuteMode = muteMode;
    currentDecay = decay;

    // 6 acoustic guitar strings: E2 (40), A2 (45), D3 (50), G3 (55), B3 (59), E4 (64)
    static constexpr int openMidi[6] = { 40, 45, 50, 55, 59, 64 };

    // Nylon string geometries (radii and inner flexible bending core radii)
    static constexpr float nylonRadii[6]        = { 0.00055f, 0.00048f, 0.00042f, 0.00038f, 0.00034f, 0.00028f };
    static constexpr float nylonBendingRadii[6] = { 0.00018f, 0.00019f, 0.00020f, 0.00026f, 0.00024f, 0.00020f };

    // Steel string geometries (standard light acoustic set 12-53):
    // Strings 0, 1, 2, 3: phosphor bronze / nickel wound on hex steel core
    // Strings 4, 5: plain steel wire
    static constexpr float steelRadii[6]        = { 0.00067f, 0.00053f, 0.00041f, 0.00030f, 0.00020f, 0.00015f };
    static constexpr float steelBendingRadii[6] = { 0.00018f, 0.00018f, 0.00018f, 0.00016f, 0.00020f, 0.00015f };
    static constexpr float steelDensities[6]    = { 6200.0f,  6400.0f,  6600.0f,  6800.0f,  7850.0f,  7850.0f };

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

    const float clampedVel = std::clamp(velocity, 0.01f, 1.0f);

    // Check for Authentic Full Mute / Dead Notes ("X" notes):
    // Fretting hand rests loosely across the fretboard. No standing wave or pitch f0 can form.
    // Instead, the plectrum strike delivers an unpitched mechanical dipole impulse into
    // the bridge saddle, exciting the BodyResonance soundboard cavity (A0/T1 modes)
    // with an authentic, deep, woody acoustic chug!
    if (muteMode == KarplusStrong::MuteMode::Full)
    {
        isDeadNotePulse = true;
        deadPulseElapsed = 0;
        deadPulseDuration = static_cast<int>(0.028f * sampleRate); // ~28 ms

        // Mass-dependent dipole impulse geometry:
        // Wound bass strings (E2, A2, D3) have substantial mass and tension,
        // delivering a broader mechanical thrust (~1.35 ms) pumping the 105 Hz body cavity (A0 mode).
        // Plain treble strings (G3, B3, E4) deliver a narrower mechanical thrust (~0.75 ms) pumping the 200 Hz plate mode.
        const float woundMassFactor = (s <= (isSteel ? 3 : 2)) ? (1.0f - static_cast<float>(s) * 0.18f) : 0.40f;
        const float pulseSec = 0.00075f + 0.00060f * woundMassFactor;
        deadPulseSigma = std::max(10.0f, pulseSec * sampleRate);
        deadPulseCenter = 2.0f * deadPulseSigma;

        // Force amplitude: physical bridge drive proportional to velocity and string gauge:
        deadPulseAmp = clampedVel * (isSteel ? 0.36f : 0.30f) * (0.75f + 0.70f * woundMassFactor);

        // Reset string models so ZERO periodic pitch f0 can circulate
        fdtdString.reset();
        string.reset();

        // ── Direct Airborne Plectrum Physical Radiation for Dead Notes (v55.f) ───
        const float clampedPickPos = std::clamp(pickPosition, 0.08f, 0.48f);
        const float posStiffness = 1.0f + 0.85f * (0.24f - clampedPickPos);
        const float woundBonus   = 1.0f + 0.55f * woundMassFactor;
        const float effBrightness = std::clamp(brightness * (0.35f + 0.65f * clampedVel), 0.05f, 1.0f);

        // Full acoustic presence: crisp tactile pick snap & wire clack
        pickClickAmp = clampedVel * effBrightness * (isSteel ? 0.125f : 0.095f) * woundBonus * posStiffness;

        const float clickDurSec = isSteel ? (0.011f + 0.005f * effBrightness) : (0.009f + 0.004f * effBrightness);
        pickClickSamplesLeft    = static_cast<int>(clickDurSec * sampleRate);
        pickClickElapsed        = 0;

        pickEdgeDecayRate       = 3.8f / (clickDurSec * sampleRate);
        pickEdgeHPState         = 0.0f;

        const float sigmaSec    = (isSteel ? 0.00014f : 0.00019f) * (1.15f - 0.35f * effBrightness) / std::max(0.6f, posStiffness);
        pickDipoleSigma         = std::max(2.5f, sigmaSec * sampleRate);
        pickDipoleCenter        = 2.2f * pickDipoleSigma;

        // Material flexural eigenmodes (Steel: ~4.2 kHz & ~7.2 kHz; Nylon: ~2.8 kHz & ~4.8 kHz)
        const float f1 = (isSteel ? 4200.0f : 2800.0f) * (0.92f + 0.16f * effBrightness) * (0.95f + 0.10f * posStiffness);
        const float Q1 = isSteel ? 7.5f : 3.8f;
        const float f2 = (isSteel ? 7200.0f : 4800.0f) * (0.90f + 0.20f * effBrightness);
        const float Q2 = isSteel ? 5.5f : 3.0f;

        const float w1 = 2.0f * kPi * std::clamp(f1, 200.0f, sampleRate * 0.45f) / sampleRate;
        const float bw1 = f1 / Q1;
        const float r1 = std::clamp(std::exp(-kPi * bw1 / sampleRate), 0.1f, 0.995f);
        pickRes1_b0 = (1.0f - r1 * r1) * 0.5f;
        pickRes1_a1 = -2.0f * r1 * std::cos(w1);
        pickRes1_a2 = r1 * r1;
        pickRes1_x1 = 0.0f; pickRes1_x2 = 0.0f; pickRes1_y1 = 0.0f; pickRes1_y2 = 0.0f;

        const float w2 = 2.0f * kPi * std::clamp(f2, 400.0f, sampleRate * 0.45f) / sampleRate;
        const float bw2 = f2 / Q2;
        const float r2 = std::clamp(std::exp(-kPi * bw2 / sampleRate), 0.1f, 0.995f);
        pickRes2_b0 = (1.0f - r2 * r2) * 0.5f;
        pickRes2_a1 = -2.0f * r2 * std::cos(w2);
        pickRes2_a2 = r2 * r2;
        pickRes2_x1 = 0.0f; pickRes2_x2 = 0.0f; pickRes2_y1 = 0.0f; pickRes2_y2 = 0.0f;

        pickClickRng = static_cast<uint32_t>(0xDEAD1234u)
                       ^ static_cast<uint32_t>(note * 7919)
                       ^ static_cast<uint32_t>(clampedVel * 65535.0f)
                       ^ static_cast<uint32_t>(s * 31337);

        // Pre-release wound string rasp
        if (woundMassFactor > 0.45f)
        {
            const float ridgeDurSec = 0.0035f + 0.0035f * woundMassFactor;
            pickRidgeDurSamples = static_cast<int>(ridgeDurSec * sampleRate);
            pickRidgeAmp = clampedVel * (0.35f + 0.65f * effBrightness) * woundMassFactor * 0.45f;
            pickRidgePhase = 0.0f;
            pickRidgePhaseInc = 2.0f * kPi * 1800.0f / sampleRate;
            pickRidgeChirpRate = (2.0f * kPi * (3400.0f - 1800.0f) / sampleRate) / static_cast<float>(std::max(1, pickRidgeDurSamples));
        }
        else
        {
            pickRidgeAmp        = 0.0f;
            pickRidgeDurSamples = 0;
        }

        return;
    }

    isDeadNotePulse = false;

    if (engineType == EngineType::BilbaoFdtd)
    {
        const int fret = std::max(0, std::min(22, note - openMidi[s]));
        const float fretLength = 0.65f * std::pow(2.0f, -static_cast<float>(fret) / 12.0f);
        const float targetFreq = midiToFreq(note);

        FdtdString::StringParams p;
        p.fretNumber = fret;
        p.L = fretLength;
        p.r = isSteel ? steelRadii[s] : nylonRadii[s];
        p.r_stiffness = isSteel ? steelBendingRadii[s] : nylonBendingRadii[s];
        p.rho = isSteel ? steelDensities[s] : 1140.0f;
        // Nylon Young's modulus E ~ 1.2 GPa, Steel E ~ 200 GPa:
        p.E = isSteel ? (2.0e11f * (0.35f + 0.85f * stiffness))
                      : (1.2e9f  * (0.15f + 1.20f * stiffness));

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

        // Dynamic velocity-dependent brightness with humanization micro-variation:
        const float humanJitter = 1.0f + 0.03f * humanRand();
        const float pickJitter  = 0.012f * humanRand();
        const float humanAngle  = humanRand();
        float effBrightness = std::clamp(brightness * (0.35f + 0.65f * clampedVel) * humanJitter, 0.05f, 1.0f);

        // Register-dependent acoustic decay and compliance:
        const float fRatio = std::clamp(targetFreq / 82.4f, 1.0f, 16.0f);
        const float registerDecayScale = std::pow(fRatio, isSteel ? 0.65f : 0.80f);

        // Fretboard Positional Damping Profile:
        const float nutVsFretScale = (fret == 0) ? 0.88f : (1.0f + 0.008f * static_cast<float>(fret));
        float effSigma0 = (isSteel ? (0.45f + 0.85f * (1.0f - decay)) : (0.75f + 1.25f * (1.0f - decay)))
                          * registerDecayScale * nutVsFretScale;

        const float trebleFactor = std::clamp((targetFreq - 110.0f) / (1000.0f - 110.0f), 0.0f, 1.0f);
        const float decaySigma1Scale = (1.0f + 0.6f * (1.0f - decay));
        float effSigma1 = (isSteel ? (3.5e-4f + 1.8e-3f * trebleFactor)
                                   : (4.5e-4f + 2.5e-3f * trebleFactor)) * decaySigma1Scale;
        float effDispScale = 1.0f;

        if (muteMode == KarplusStrong::MuteMode::Palm)
        {
            // Palm muting: fleshy palm heel rests on bridge saddles.
            // High sigma1 viscoelastic absorption rapidly dissipates high partials (>350 Hz),
            // while allowing the low fundamental and 2nd harmonic (80-250 Hz) to produce
            // a tight, punchy acoustic "chug" lasting ~75-110 ms (v55.f).
            effSigma0 = (isSteel ? 9.5f : 11.0f) + 8.5f * (1.0f - decay);
            effSigma1 = std::max(2.8e-3f, effSigma1 * 4.0f);
            effDispScale = 0.90f;
            effBrightness *= 0.50f;
        }

        p.sigma0 = effSigma0;
        p.sigma1 = effSigma1;
        p.brightness = effBrightness;

        // Longitudes and tension bloom
        p.longitudinalAttack = isSteel ? 0.35f : 0.15f;
        p.phantomCoupling    = isSteel ? 0.30f : 0.20f;

        // Physical wound bass string vs plain treble factor:
        p.woundFactor = (s <= (isSteel ? 3 : 2)) ? (1.0f - static_cast<float>(s) * 0.22f) : 0.0f;

        // Calibrated guitar fret action clearance (1.3 mm bass down to 1.0 mm treble)
        p.m0 = -0.0010f - 0.0003f * (1.0f - static_cast<float>(s) / 5.0f);
        p.b0 = -0.0028f;

        fdtdString.init(sampleRate, p);

        // String physical compliance scales with vibrating length and velocity
        const float lengthScale = std::clamp(std::sqrt(p.L / 0.65f), 0.60f, 1.0f);
        const float peakDisp = (-0.0004f - 0.00125f * clampedVel) * effDispScale * lengthScale;
        const float clampedPickPos = std::clamp(pickPosition + pickJitter, 0.08f, 0.48f);
        fdtdString.pluckDisplacement(clampedPickPos, peakDisp, effBrightness, humanAngle);

        // ── Direct Airborne Plectrum Physical Radiation (v50.f) ───────────────
        // Physical model of:
        // 1. Acoustic dipole shock wave (finite-width Ricker/bipolar pressure snap)
        // 2. Dual plectrum flexural resonators (Mode 1 body flex + Mode 2 tip clack)
        //    - Steel: Celluloid/Tortex flatpick modes (~4.2 kHz and ~7.2 kHz)
        //    - Nylon: Keratin fingernail + fingerpad modes (~2.8 kHz and ~4.8 kHz)
        // 3. String pick position sensitivity: plucking near the bridge (stiff boundary)
        //    sharpens the dipole release wave and boosts the snap.
        // 4. Wound string pre-release winding ridge micro-texture (accelerating micro-chirp).
        {
            // Pick position stiffness scale:
            // Bridge position (0.08) -> stiff, sharp snap (+20%)
            // Soundhole (0.23) -> nominal
            // Neck / 12th fret (0.45) -> cushioned, softer release (-18%)
            const float posStiffness = 1.0f + 0.85f * (0.24f - clampedPickPos);
            const float woundBonus   = 1.0f + 0.55f * p.woundFactor;

            // Mute mode amplitude & contour scaling:
            const float muteClickScale = (muteMode == KarplusStrong::MuteMode::Palm) ? 0.38f : 1.0f;

            // Amplitude scaling: calibrated to natural -18 dB to -22 dB acoustic ratio
            pickClickAmp = clampedVel * effBrightness * (isSteel ? 0.115f : 0.085f) * woundBonus * posStiffness * muteClickScale;

            const float clickDurSec = isSteel ? (0.009f + 0.006f * effBrightness)
                                              : (0.007f + 0.005f * effBrightness);
            pickClickSamplesLeft    = static_cast<int>(clickDurSec * sampleRate);
            pickClickElapsed        = 0;

            // Ultra-high frequency edge air bite decay (~6-9 ms)
            pickEdgeDecayRate       = 3.8f / (clickDurSec * sampleRate);
            pickEdgeHPState         = 0.0f;

            // Dipole shock wave geometry (acoustic pressure doublet)
            // Bridge pluck -> faster, narrower shock (~0.12 ms); Neck pluck -> wider (~0.22 ms)
            const float sigmaSec    = (isSteel ? 0.00014f : 0.00019f) * (1.15f - 0.35f * effBrightness) / std::max(0.6f, posStiffness);
            pickDipoleSigma         = std::max(2.5f, sigmaSec * sampleRate);
            if (muteMode == KarplusStrong::MuteMode::Palm)
                pickDipoleSigma *= 1.45f;
            else if (muteMode == KarplusStrong::MuteMode::Full)
                pickDipoleSigma *= 1.85f;
            pickDipoleCenter        = 2.2f * pickDipoleSigma; // peak occurs shortly after release starts

            // Material flexural eigenmodes:
            // Steel flatpick: Mode 1 ~ 4200 Hz (Q~7.5), Mode 2 ~ 7200 Hz (Q~5.5)
            // Nylon fingernail: Mode 1 ~ 2800 Hz (Q~3.8), Mode 2 ~ 4800 Hz (Q~3.0)
            const float f1 = (isSteel ? 4200.0f : 2800.0f) * (0.92f + 0.16f * effBrightness) * (0.95f + 0.10f * posStiffness);
            const float Q1 = isSteel ? 7.5f : 3.8f;
            const float f2 = (isSteel ? 7200.0f : 4800.0f) * (0.90f + 0.20f * effBrightness);
            const float Q2 = isSteel ? 5.5f : 3.0f;

            // Configure 2-pole bandpass resonator 1 (unity peak gain)
            const float w1 = 2.0f * kPi * std::clamp(f1, 200.0f, sampleRate * 0.45f) / sampleRate;
            const float bw1 = f1 / Q1;
            const float r1 = std::clamp(std::exp(-kPi * bw1 / sampleRate), 0.1f, 0.995f);
            pickRes1_b0 = (1.0f - r1 * r1) * 0.5f;
            pickRes1_a1 = -2.0f * r1 * std::cos(w1);
            pickRes1_a2 = r1 * r1;
            pickRes1_x1 = 0.0f; pickRes1_x2 = 0.0f;
            pickRes1_y1 = 0.0f; pickRes1_y2 = 0.0f;

            // Configure 2-pole bandpass resonator 2 (unity peak gain)
            const float w2 = 2.0f * kPi * std::clamp(f2, 400.0f, sampleRate * 0.45f) / sampleRate;
            const float bw2 = f2 / Q2;
            const float r2 = std::clamp(std::exp(-kPi * bw2 / sampleRate), 0.1f, 0.995f);
            pickRes2_b0 = (1.0f - r2 * r2) * 0.5f;
            pickRes2_a1 = -2.0f * r2 * std::cos(w2);
            pickRes2_a2 = r2 * r2;
            pickRes2_x1 = 0.0f; pickRes2_x2 = 0.0f;
            pickRes2_y1 = 0.0f; pickRes2_y2 = 0.0f;

            // Wound string pre-release winding ridge micro-texture:
            if (p.woundFactor > 0.05f)
            {
                const float ridgeDurSec = 0.003f + 0.004f * p.woundFactor;
                pickRidgeDurSamples = static_cast<int>(ridgeDurSec * sampleRate);
                pickRidgeAmp = clampedVel * (0.35f + 0.65f * effBrightness) * p.woundFactor * 0.45f;
                pickRidgePhase = 0.0f;
                pickRidgePhaseInc = 2.0f * kPi * 1800.0f / sampleRate;
                // Chirp accelerates from 1800 Hz to ~3400 Hz by release time
                pickRidgeChirpRate = (2.0f * kPi * (3400.0f - 1800.0f) / sampleRate) / static_cast<float>(std::max(1, pickRidgeDurSamples));
            }
            else
            {
                pickRidgeDurSamples = 0;
                pickRidgeAmp = 0.0f;
            }

            pickClickRng = static_cast<uint32_t>(0xDEAD1234u)
                           ^ static_cast<uint32_t>(note * 7919)
                           ^ static_cast<uint32_t>(clampedVel * 65535.0f)
                           ^ static_cast<uint32_t>(s * 31337);
        }

        // Note: Full mute ("X" note / percussive dead note) is simulated physically
        // by the string's calibrated effSigma0/effSigma1 loss, allowing natural acoustic body excitation.
        return;
    }

    // DWG path: silence any residual pick-click burst
    pickClickSamplesLeft = 0;
    pickClickAmp         = 0.0f;

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

void Voice::legatoTransition(int newNote, float newVelocity, bool isHammerOn, float stiffness) noexcept
{
    midiNote = newNote;
    velocity = newVelocity;
    samplesSinceNoteOn = 0;
    releasing = false;
    releaseGain = 1.0f;
    fadeSamplesLeft = -1; // Reset any decay auto-fade
    active = true;

    const float freq = midiToFreq(midiNote);
    const bool isSteel = (stringMaterial == 1);
    int s = physicalStringIndex;
    if (s < 0 || s > 5)
    {
        if (newNote < 45)      s = 0;
        else if (newNote < 50) s = 1;
        else if (newNote < 55) s = 2;
        else if (newNote < 59) s = 3;
        else if (newNote < 64) s = 4;
        else                   s = 5;
    }

    // Zero any direct airborne plectrum clicks (hammer-ons and pull-offs have NO pick attack)
    pickClickSamplesLeft = 0;
    pickClickAmp = 0.0f;
    pickRidgeDurSamples = 0;
    pickRidgeAmp = 0.0f;

    if (engineType == EngineType::DigitalWaveguide)
    {
        string.legatoTransition(freq, newVelocity, isHammerOn, stiffness);
    }
    else
    {
        // Compute new FDTD parameters for the fretted string length
        static constexpr int openMidi[6] = { 40, 45, 50, 55, 59, 64 };
        static constexpr float nylonRadii[6]        = { 0.00055f, 0.00048f, 0.00042f, 0.00038f, 0.00034f, 0.00028f };
        static constexpr float nylonBendingRadii[6] = { 0.00018f, 0.00019f, 0.00020f, 0.00026f, 0.00024f, 0.00020f };
        static constexpr float steelRadii[6]        = { 0.00067f, 0.00053f, 0.00041f, 0.00030f, 0.00020f, 0.00015f };
        static constexpr float steelBendingRadii[6] = { 0.00018f, 0.00018f, 0.00018f, 0.00016f, 0.00020f, 0.00015f };
        static constexpr float steelDensities[6]    = { 6200.0f,  6400.0f,  6600.0f,  6800.0f,  7850.0f,  7850.0f };

        FdtdString::StringParams p;
        p.fretNumber = std::max(0, newNote - openMidi[s]);
        p.L          = 0.65f * std::pow(2.0f, -static_cast<float>(p.fretNumber) / 12.0f);
        p.f0         = freq;
        p.r          = isSteel ? steelRadii[s] : nylonRadii[s];
        p.r_stiffness = isSteel ? steelBendingRadii[s] : nylonBendingRadii[s];
        p.rho        = isSteel ? steelDensities[s] : 1140.0f;
        p.E          = isSteel ? (2.0e11f * (0.35f + 0.85f * stiffness))
                               : (1.2e9f  * (0.15f + 1.20f * stiffness));

        const float A = 3.14159265358979323846f * p.r * p.r;
        const float I = 3.14159265358979323846f * p.r_stiffness * p.r_stiffness * p.r_stiffness * p.r_stiffness * 0.25f;
        const float rhoA = p.rho * A;
        const float EI = p.E * I;

        // Discrete grid numerical dispersion compensation across physical strings (0..5)
        static constexpr float centsOffsets[6] = { 1.5f, 1.5f, 1.0f, 0.8f, 0.5f, 0.0f };
        const float tunedFreq = freq * std::pow(2.0f, -centsOffsets[s] / 1200.0f);

        // Compute string tension dynamically to ensure EXACT pitch tracking matching noteOn:
        float T0 = rhoA * std::pow(2.0f * p.L * tunedFreq, 2.0f) - (3.14159265f * 3.14159265f * EI) / (p.L * p.L);
        if (T0 < 5.0f) T0 = 5.0f;
        p.T0 = T0;

        // Register-dependent acoustic decay and compliance (matching noteOn):
        const float fRatio = std::clamp(freq / 82.4f, 1.0f, 16.0f);
        const float registerDecayScale = std::pow(fRatio, isSteel ? 0.65f : 0.80f);
        const float nutVsFretScale = (p.fretNumber == 0) ? 0.88f : (1.0f + 0.008f * static_cast<float>(p.fretNumber));
        p.sigma0 = (isSteel ? (0.45f + 0.85f * (1.0f - currentDecay)) : (0.75f + 1.25f * (1.0f - currentDecay)))
                   * registerDecayScale * nutVsFretScale;

        const float trebleFactor = std::clamp((freq - 110.0f) / (1000.0f - 110.0f), 0.0f, 1.0f);
        const float decaySigma1Scale = (1.0f + 0.6f * (1.0f - currentDecay));
        p.sigma1 = (isSteel ? (3.5e-4f + 1.8e-3f * trebleFactor)
                            : (4.5e-4f + 2.5e-3f * trebleFactor)) * decaySigma1Scale;

        p.brightness = 0.60f;
        p.woundFactor = (s <= (isSteel ? 3 : 2)) ? (1.0f - static_cast<float>(s) * 0.22f) : 0.0f;

        fdtdString.legatoTransition(p, newVelocity, isHammerOn);
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
        fdtdString.setDamping(10.5f, 3.2e-3f);
    }
    else if (mode == KarplusStrong::MuteMode::Full)
    {
        if (active && !isDeadNotePulse)
        {
            choke(0.75f);
        }
    }
    else
    {
        fdtdString.setDamping(0.75f + 1.25f * (1.0f - currentDecay));
    }
}

void Voice::choke(float chokeVelocity) noexcept
{
    if (!active)
    {
        active = true;
        releasing = false;
        midiNote = 40; // Default to low E string for acoustic soundboard thud
        isDeadNotePulse = true;
        deadPulseElapsed = 0;
        deadPulseDuration = static_cast<int>(0.028f * sampleRate);
        deadPulseSigma = 55.0f;
        deadPulseCenter = 2.0f * deadPulseSigma;
        deadPulseAmp = chokeVelocity * 0.35f;
        fdtdString.reset();
        string.reset();

        // Direct airborne tactile fretboard slap click (v55.f)
        const bool isSteel = (stringMaterial == 1);
        pickClickAmp = chokeVelocity * (isSteel ? 0.12f : 0.090f);
        const float clickDurSec = 0.012f;
        pickClickSamplesLeft = static_cast<int>(clickDurSec * sampleRate);
        pickClickElapsed = 0;
        pickEdgeDecayRate = 3.8f / (clickDurSec * sampleRate);
        pickEdgeHPState = 0.0f;
        pickDipoleSigma = std::max(2.5f, 0.00015f * sampleRate);
        pickDipoleCenter = 2.2f * pickDipoleSigma;

        const float f1 = isSteel ? 4200.0f : 2800.0f;
        const float Q1 = isSteel ? 7.5f : 3.8f;
        const float f2 = isSteel ? 7200.0f : 4800.0f;
        const float Q2 = isSteel ? 5.5f : 3.0f;
        const float w1 = 2.0f * kPi * f1 / sampleRate;
        const float bw1 = f1 / Q1;
        const float r1 = std::clamp(std::exp(-kPi * bw1 / sampleRate), 0.1f, 0.995f);
        pickRes1_b0 = (1.0f - r1 * r1) * 0.5f;
        pickRes1_a1 = -2.0f * r1 * std::cos(w1);
        pickRes1_a2 = r1 * r1;
        pickRes1_x1 = 0.0f; pickRes1_x2 = 0.0f; pickRes1_y1 = 0.0f; pickRes1_y2 = 0.0f;

        const float w2 = 2.0f * kPi * f2 / sampleRate;
        const float bw2 = f2 / Q2;
        const float r2 = std::clamp(std::exp(-kPi * bw2 / sampleRate), 0.1f, 0.995f);
        pickRes2_b0 = (1.0f - r2 * r2) * 0.5f;
        pickRes2_a1 = -2.0f * r2 * std::cos(w2);
        pickRes2_a2 = r2 * r2;
        pickRes2_x1 = 0.0f; pickRes2_x2 = 0.0f; pickRes2_y1 = 0.0f; pickRes2_y2 = 0.0f;

        pickRidgeDurSamples = 0;
        pickRidgeAmp = 0.0f;
        return;
    }

    choking = true;
    chokeGain = 1.0f;

    // Physical contact with palm/fret hand absorbs string kinetic energy over ~10-15 ms:
    chokeCoeff = std::exp(-1.0f / (0.010f * sampleRate));
    chokeSamplesLeft = static_cast<int>(0.035f * sampleRate);

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

    if (isDeadNotePulse)
    {
        float out = 0.0f;
        if (deadPulseElapsed < deadPulseDuration)
        {
            const float t = static_cast<float>(deadPulseElapsed) - deadPulseCenter;
            const float u = t / deadPulseSigma;
            if (std::abs(u) < 3.8f)
            {
                out = -u * std::exp(-0.5f * u * u) * deadPulseAmp;
            }
            ++deadPulseElapsed;
        }
        else
        {
            active = false;
            isDeadNotePulse = false;
            midiNote = -1;
            out = 0.0f;
        }
        return out;
    }

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
        }
    }
    else
    {
        const float currentEnergy = (engineType == EngineType::BilbaoFdtd) ? fdtdString.getEnergy() : string.getEnergy();
        const float silenceThresh = (engineType == EngineType::BilbaoFdtd) ? 2e-4f : 1e-7f;
        if (currentEnergy < silenceThresh && fadeSamplesLeft < 0)
        {
            // Natural ring-out also fades out cleanly instead of hard-cutting
            fadeSamplesLeft = 64;
        }
    }

    // 96-sample linear fade-out to guarantee zero DC or step click
    if (fadeSamplesLeft > 0)
    {
        const float fadeFactor = static_cast<float>(fadeSamplesLeft) / 96.0f;
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
    ++samplesSinceNoteOn;

    if (isDeadNotePulse)
    {
        float fV = 0.0f;
        if (deadPulseElapsed < deadPulseDuration)
        {
            const float t = static_cast<float>(deadPulseElapsed) - deadPulseCenter;
            const float u = t / deadPulseSigma;
            if (std::abs(u) < 3.8f)
            {
                fV = -u * std::exp(-0.5f * u * u) * deadPulseAmp;
            }
            ++deadPulseElapsed;
        }
        else
        {
            active = false;
            isDeadNotePulse = false;
            midiNote = -1;
            fV = 0.0f;
        }

        outV = fV;
        outH = 0.0f;
        return;
    }

    if (engineType == EngineType::BilbaoFdtd)
    {
        float fV = 0.0f, fH = 0.0f;
        fdtdString.tickPolar(fV, fH);
        fV *= 95.0f;
        fH *= 95.0f;

        // Safety guard against NaN/Inf or runaway numerical explosion
        if (std::isnan(fV) || std::isinf(fV) || std::abs(fV) > 50.0f ||
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
                fadeSamplesLeft = 96;
        }
        else
        {
            if (fdtdString.getEnergy() < 2e-4f && fadeSamplesLeft < 0)
                fadeSamplesLeft = 64;
        }

        if (fadeSamplesLeft > 0)
        {
            gainScale *= (static_cast<float>(fadeSamplesLeft) / 96.0f);
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

        outV = fV * gainScale;
        outH = fH * gainScale;

        // Choke mechanical hand slap is added to vertical plate pumping
        if (choking && slapActive)
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
        // Digital Waveguide fallback: single axis vertical drive
        const float sOut = tick();
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
    isDeadNotePulse = false;
    deadPulseElapsed = 0;
    slapActive = false;
    slapPulseElapsed = 0;
    chokeGain = 1.0f;
    chokeSamplesLeft = 0;
    releaseGain = 1.0f;
    fadeSamplesLeft = -1;
    midiNote = -1;

    // Reset direct airborne pick-click physical model (v51.f)
    pickClickSamplesLeft = 0;
    pickClickElapsed     = 0;
    pickClickAmp         = 0.0f;
    pickRes1_x1 = 0.0f; pickRes1_x2 = 0.0f;
    pickRes1_y1 = 0.0f; pickRes1_y2 = 0.0f;
    pickRes2_x1 = 0.0f; pickRes2_x2 = 0.0f;
    pickRes2_y1 = 0.0f; pickRes2_y2 = 0.0f;
    pickRidgeDurSamples  = 0;
    pickRidgeAmp         = 0.0f;
    pickEdgeHPState      = 0.0f;
}

float Voice::tickPickClick() noexcept
{
    if (pickClickSamplesLeft <= 0)
        return 0.0f;

    // 1. Acoustic Dipole Shock Wave (Ricker wavelet / pressure doublet):
    // Pressure profile: x_dipole = (1 - u^2) * exp(-0.5 * u^2)
    // where u = (elapsed - center) / sigma.
    const float u = (static_cast<float>(pickClickElapsed) - pickDipoleCenter) / pickDipoleSigma;
    float shock = 0.0f;
    if (std::abs(u) < 4.0f)
    {
        shock = (1.0f - u * u) * std::exp(-0.5f * u * u);
    }

    // 2. Add subtle organic micro-roughness from PRNG
    pickClickRng ^= pickClickRng << 13u;
    pickClickRng ^= pickClickRng >> 17u;
    pickClickRng ^= pickClickRng << 5u;
    const float noise = static_cast<float>(static_cast<int32_t>(pickClickRng)) / static_cast<float>(0x7FFFFFFFu);
    const float excitation = shock + 0.15f * noise * std::exp(-u * u * 0.25f);

    // 3. Drive flexural resonator 1 (plectrum body / nail plate flex mode)
    const float y1 = pickRes1_b0 * (excitation - pickRes1_x2) - pickRes1_a1 * pickRes1_y1 - pickRes1_a2 * pickRes1_y2;
    pickRes1_x2 = pickRes1_x1;
    pickRes1_x1 = excitation;
    pickRes1_y2 = pickRes1_y1;
    pickRes1_y1 = y1;

    // 4. Drive flexural resonator 2 (plectrum tip snap / nail edge clack mode)
    const float y2 = pickRes2_b0 * (excitation - pickRes2_x2) - pickRes2_a1 * pickRes2_y1 - pickRes2_a2 * pickRes2_y2;
    pickRes2_x2 = pickRes2_x1;
    pickRes2_x1 = excitation;
    pickRes2_y2 = pickRes2_y1;
    pickRes2_y1 = y2;

    // 5. Pre-release wound-wire winding ridge micro-texture
    float ridgeOut = 0.0f;
    if (pickClickElapsed < pickRidgeDurSamples && pickRidgeDurSamples > 0)
    {
        const float ridgeEnv = std::sin(kPi * static_cast<float>(pickClickElapsed) / static_cast<float>(pickRidgeDurSamples));
        ridgeOut = pickRidgeAmp * std::sin(pickRidgePhase) * ridgeEnv * (0.8f + 0.2f * noise);
        pickRidgePhase += pickRidgePhaseInc;
        pickRidgePhaseInc += pickRidgeChirpRate;
        if (pickRidgePhase >= 2.0f * kPi) pickRidgePhase -= 2.0f * kPi;
    }

    // 6. Airborne edge air sizzle (8–16 kHz cavitation & edge slip bite)
    const float edgeHP = noise - pickEdgeHPState;
    pickEdgeHPState += pickEdgeHPAlpha * edgeHP;
    const float edgeEnv = std::exp(-static_cast<float>(pickClickElapsed) * pickEdgeDecayRate);
    const float edgeOut = edgeHP * edgeEnv;

    // Composite airborne acoustic sound:
    // - Direct dipole shock wave (immediate punch & snap): 0.85f * shock
    // - Resonator Mode 1 (plectrum flex / fingernail plate): 2.4f * y1
    // - Resonator Mode 2 (plectrum tip clack / nail edge): 2.0f * y2
    // - Airborne edge air sizzle (8-16 kHz crisp bite): 0.40f * edgeOut
    // - Pre-release wound wire ridge rasp: 0.70f * ridgeOut
    const float click = pickClickAmp * (0.85f * shock + 2.4f * y1 + 2.0f * y2 + 0.40f * edgeOut + 0.70f * ridgeOut);

    ++pickClickElapsed;
    --pickClickSamplesLeft;

    return click;
}

float Voice::midiToFreq(int note) noexcept
{
    // Equal temperament tuning: A4 = 440 Hz = MIDI 69
    return 440.f * std::pow(2.f, (note - 69) / 12.f);
}
