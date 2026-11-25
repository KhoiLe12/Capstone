#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <array>
#include <iostream>

#if defined(_MSC_VER)
    #define FDTD_RESTRICT __restrict
#elif defined(__clang__) || defined(__GNUC__)
    #define FDTD_RESTRICT __restrict__
#else
    #define FDTD_RESTRICT
#endif

/**
 * FdtdString — Real-Time Non-Iterative Guitar String Physical Model
 * Based on:
 *   Stefan Bilbao, Riccardo Russo, Craig J. Webb, and Michele Ducceschi,
 *   "Real-Time Guitar Synthesis",
 *   Proc. 27th Int. Conf. Digital Audio Effects (DAFx24), Guildford, UK, Sept. 2024.
 *
 * Implements:
 *   1. 1D Stiff linear string PDE with spatial grid spacing h >= h_min.
 *   2. Kirchhoff-Carrier geometric non-linearity (pitch glide at large amplitudes).
 *   3. Fretboard barrier collision potential (SAV formulation).
 *   4. 20 discrete fret point-obstacle collisions (SAV formulation).
 *   5. Finger lumped mass dynamics and collision (SAV formulation).
 *   6. Low-rank (size 4) Woodbury linear system solver (zero Newton-Raphson loops).
 */
class FdtdString
{
public:
    struct StringParams
    {
        int fretNumber = 0;           ///< 0 = open string, 1..22 = fretted note
        float L       = 0.65f;        ///< String vibrating length (m)
        float f0      = 146.83f;      ///< Nominal fundamental frequency (Hz) (D string default)
        float r       = 0.00045f;     ///< String radius (m)
        float r_stiffness = 0.0f;     ///< Effective bending radius for stiffness EI (core of wound strings). If <= 0, uses r.
        float rho     = 1140.0f;      ///< Density (kg/m^3) (nylon core with silver-plated wrap)
        float E       = 5.4e9f;       ///< Young's modulus (Pa)
        float T0      = 60.0f;        ///< Tension (N)
        float sigma0  = 1.38f;        ///< Frequency-independent damping (1/s)
        float sigma1  = 1.3e-4f;      ///< Frequency-dependent air/viscoelastic loss (m^2/s)
        float brightness = 0.60f;     ///< Exciter brightness / nail polish (0..1)

        // Dual-polarization shimmer
        // H-polarization (horizontal, parallel to soundboard) vibrates at a slightly different
        // frequency from V-polarization (vertical). This difference creates a slow beat (shimmer)
        // that is the characteristic "living" quality of a sustained acoustic guitar note.
        // Typical real-guitar beat rate: ~0.1 Hz (bass) to ~1.0 Hz (treble).
        // polarizationSplit is the fractional frequency offset: f_H = f_V * (1 + polarizationSplit).
        float polarizationSplit = 0.0012f; ///< H vs V frequency ratio offset (0.001-0.003)

        // Longitudinal string physics (Phantom Harmonics & P-wave attack snap)
        // 1. Phantom Harmonics: Transverse vibration modulates string tension dynamically at 2*f0, 4*f0.
        //    The bone saddle break angle converts this axial tension modulation into vertical soundboard force.
        // 2. P-wave attack snap: Longitudinal standing waves travel at c_L = sqrt(E/rho) (~700-1500 m/s),
        //    creating a metallic bell-like "ping" transient that decays within 20-35 ms.
        float phantomCoupling    = 0.20f; ///< Saddle break angle coupling for 2f0 dynamic tension modulation
        float longitudinalAttack = 0.15f; ///< P-wave axial release resonance strength during initial attack
        float woundFactor        = 0.0f;  ///< 1.0 = heavy wound bass (E2), 0.0 = plain nylon treble (G3+)

        // Collisions
        float m0      = -0.0012f;     ///< Fret wire protrusion height below string (m)
        float b0      = -0.0028f;     ///< Fretboard surface height below string (m)
        float KF      = 1.0e8f;       ///< Fret stiffness (N / m^alpha)
        float alphaF  = 1.3f;         ///< Fret collision exponent
        float KB      = 1.0e7f;       ///< Fretboard stiffness (N / m^alpha)
        float alphaB  = 1.3f;         ///< Fretboard collision exponent

        // Finger
        float MFG     = 0.035f;       ///< Finger lumped mass (kg)
        float KFG     = 5.0e4f;       ///< Finger contact stiffness (N / m^alpha)
        float alphaFG = 1.3f;         ///< Finger contact exponent
        float xFG     = 0.25f;        ///< Finger coordinate along string (fraction of L)
    };

    enum class MuteMode { Open, Palm, Full };

    FdtdString() = default;

    /** Lowest fundamental the grid capacity must hold (Drop-D D2 = 73.4 Hz, with margin). */
    static constexpr float kMinStringFreqHz = 70.0f;

    /**
     * Configure for a sample rate with default parameters and pre-allocate all buffers.
     * Call from prepareToPlay / constructors only (may allocate).
     */
    void init(float sampleRateHz)
    {
        init(sampleRateHz, StringParams(), MuteMode::Open);
    }

    /**
     * Configure grid for new string parameters. Allocates only if the sample rate requires more
     * capacity than previously reserved, so after init(sampleRate) it is audio-thread safe.
     */
    void init(float sampleRateHz, const StringParams& params, MuteMode muteMode = MuteMode::Open, float decay = 0.8f)
    {
        fs = sampleRateHz;
        k  = 1.0f / fs;
        currentMuteMode = muteMode;
        currentDecay = decay;
        reserveCapacity();
        p = params;
        configureGrid();
        reset();
    }

    /** Maximum interior grid points this string can hold without allocating. */
    int getGridCapacity() const noexcept { return static_cast<int>(gridCapacity); }

    void reset()
    {
        std::fill(u_next.begin(), u_next.end(), 0.0f);
        std::fill(u_curr.begin(), u_curr.end(), 0.0f);
        std::fill(u_prev.begin(), u_prev.end(), 0.0f);
        w_next = 0.0f; w_curr = 0.0f; w_prev = 0.0f;
        psi = { 0.0f, 0.0f, 0.0f };
        psi_prev = { 0.0f, 0.0f, 0.0f };
        prevXiB = 0.0f;
        prevXiF = 0.0f;
        prevRawForce = 0.0f;
        dcBlockerState = 0.0f;
        accumulatedLoss = 0.0f;
        pluckDurSamples = 0;
        pluckSampleCount = 0;
        pickContactActive = false;
        pickElapsedSamples = 0;
        pickEtaPrev = 0.0f;
        saddleFilterState = 0.0f;
        saddleFilterState2 = 0.0f;
        rampSamplesLeft = 0;

        // H-polarization reset
        std::fill(u_next_H.begin(), u_next_H.end(), 0.0f);
        std::fill(u_curr_H.begin(), u_curr_H.end(), 0.0f);
        std::fill(u_prev_H.begin(), u_prev_H.end(), 0.0f);
        std::fill(Du.begin(),       Du.end(),       0.0f);
        std::fill(Du_prev.begin(),  Du_prev.end(),  0.0f);
        std::fill(Du_H.begin(),     Du_H.end(),     0.0f);
        std::fill(D2u_H.begin(),    D2u_H.end(),    0.0f);
        std::fill(Du_prev_H.begin(),Du_prev_H.end(),0.0f);
        computeLaplacian(u_prev.data(), Du_prev.data());
        computeLaplacian(u_prev_H.data(), Du_prev_H.data());
        dcBlockerStateH    = 0.0f;
        prevRawForceH      = 0.0f;
        saddleFilterStateH  = 0.0f;
        saddleFilterState2H = 0.0f;

        // Longitudinal and phantom harmonics reset
        longitudinalS1 = 0.0f;
        longitudinalS2 = 0.0f;
        longitudinalImpulse = 0.0f;
        dcBlockerStatePhantom = 0.0f;
        prevRawForcePhantom = 0.0f;
        saddleFilterPhantom = 0.0f;

        // Tactile scrape reset
        scrapeSamplesLeft = 0;
        scrapeTotalSamples = 0;
        scrapeAmp = 0.0f;
        scrapeFilterState = 0.0f;
        lastScrapeForce = 0.0f;
    }

    /**
     * Legato transition (Hammer-On / Pull-Off) for FDTD elastodynamic string PDE.
     * Retunes vibrating length L to new fret without zeroing existing wave displacement,
     * transferring existing wave momentum to the new speaking length.
     */
    void legatoTransition(const StringParams& newParams, float velocity, bool isHammerOn) noexcept
    {
        // 1. Save existing wave state (both polarizations) in pre-reserved scratch
        const int oldN_pts = N_pts;
        const float oldH = h;
        const float oldL = p.L;
        saved_curr.resize(static_cast<size_t>(oldN_pts));
        saved_prev.resize(static_cast<size_t>(oldN_pts));
        saved_curr_H.resize(static_cast<size_t>(oldN_pts));
        saved_prev_H.resize(static_cast<size_t>(oldN_pts));
        std::copy_n(u_curr.data(),   oldN_pts, saved_curr.data());
        std::copy_n(u_prev.data(),   oldN_pts, saved_prev.data());
        std::copy_n(u_curr_H.data(), oldN_pts, saved_curr_H.data());
        std::copy_n(u_prev_H.data(), oldN_pts, saved_prev_H.data());

        // 2. Rebuild ALL derived state (grid, CFL, T0_H, frets, finger, lambdas, saddle filter)
        //    for the new speaking length. Vectors are resized within reserved capacity.
        p = newParams;
        configureGrid();

        // Collision auxiliaries refer to the old grid geometry: restart them from rest.
        psi = { 0.0f, 0.0f, 0.0f };
        psi_prev = { 0.0f, 0.0f, 0.0f };
        prevXiB = 0.0f;
        prevXiF = 0.0f;
        pluckDurSamples = 0;
        pluckSampleCount = 0;

        // 3. Bridge-aligned remap: the saddle is fixed, fretting shortens the string from the nut.
        //    New point i sits (N_pts - i) * h from the bridge; old point j sat (oldN_pts - j) * oldH.
        auto sampleOld = [oldN_pts](const std::vector<float>& v, float g) noexcept -> float
        {
            // Fixed (zero) boundaries at old indices -1 (nut/fret) and oldN_pts (bridge)
            if (g <= -1.0f || g >= static_cast<float>(oldN_pts)) return 0.0f;
            const int i0 = static_cast<int>(std::floor(g));
            const float frac = g - static_cast<float>(i0);
            const float v0 = (i0 >= 0)            ? v[static_cast<size_t>(i0)]     : 0.0f;
            const float v1 = (i0 + 1 < oldN_pts)  ? v[static_cast<size_t>(i0 + 1)] : 0.0f;
            return (1.0f - frac) * v0 + frac * v1;
        };

        for (int i = 0; i < N_pts; ++i)
        {
            const float distFromBridge = static_cast<float>(N_pts - i) * h;
            const float g = static_cast<float>(oldN_pts) - distFromBridge / oldH;

            // Taper displacement smoothly to 0 at the new fretted nut (i = 0)
            const float taper = std::min(1.0f, static_cast<float>(i + 1) / 4.0f);
            const auto si = static_cast<size_t>(i);
            u_curr[si]   = sampleOld(saved_curr,   g) * taper;
            u_prev[si]   = sampleOld(saved_prev,   g) * taper;
            u_curr_H[si] = sampleOld(saved_curr_H, g) * taper;
            u_prev_H[si] = sampleOld(saved_prev_H, g) * taper;
            u_next[si]   = 0.0f;
            u_next_H[si] = 0.0f;
        }

        // 4. Acoustic fret excitation on legato note transition:
        if (isHammerOn)
        {
            // Hammer-On: Warm fret wire impact impulse
            if (N_pts > 4)
            {
                const float tapAmp = -velocity * 0.000045f;
                u_curr[1] += tapAmp * 0.5f;
                u_curr[2] += tapAmp;
                u_curr[3] += tapAmp * 0.5f;
                u_prev[2] += tapAmp * 0.2f;
            }
        }
        else
        {
            // Pull-Off: Left-hand pizzicato release at the released fret position.
            // As the finger releases from the higher fret, it snaps the string laterally,
            // imparting an authentic triangular pluck displacement wave over the newly lengthened string.
            const float xRelease = std::clamp(p.L - oldL, 0.04f, 0.35f * p.L);
            // Lower frets (and open string) require higher pull-off compliance to overcome
            // bridge boundary impedance and Fletcher-Munson bass perception curves:
            const float bassBoost = (p.fretNumber <= 2) ? 1.55f : 1.15f;
            const float pullAmp = velocity * 0.00070f * bassBoost;
            for (int i = 0; i < N_pts; ++i)
            {
                const float x = static_cast<float>(i + 1) * h;
                float disp = 0.0f;
                if (x < xRelease)
                    disp = pullAmp * (x / xRelease);
                else
                    disp = pullAmp * (p.L - x) / (p.L - xRelease);

                u_curr[static_cast<size_t>(i)] += disp;
                u_prev[static_cast<size_t>(i)] += disp; // Plucked release from rest for rich fundamental sustain
            }
        }

        // Reset scrape to avoid plectrum click
        scrapeSamplesLeft = 0;
        lastScrapeForce = 0.0f;

        // Recompute previous-step Laplacians once for O(1) Du_prev swap on subsequent ticks
        computeLaplacian(u_prev.data(), Du_prev.data());
        computeLaplacian(u_prev_H.data(), Du_prev_H.data());
    }

    void setFingerEngaged(bool engaged) noexcept { fingerEngaged = engaged; }
    bool isFingerEngaged() const noexcept { return fingerEngaged; }

    /** Dynamically switch playing articulation mode (Open, Palm, or Full mute dead notes). */
    void setMuteMode(MuteMode mode, float decay = -1.0f) noexcept
    {
        currentMuteMode = mode;
        if (decay >= 0.0f)
            currentDecay = decay;
        updateDampingProfile();
    }

    MuteMode getMuteMode() const noexcept { return currentMuteMode; }

    /** Dynamically update loss parameters for palm muting, choking or release mid-note. */
    void setDamping(float sigma0, float sigma1 = -1.0f) noexcept
    {
        p.sigma0 = sigma0;
        // Never allow sigma1 to exceed the CFL stability bound determined at init()
        if (sigma1 > 0.0f)
            p.sigma1 = std::min(sigma1, initialSigma1);
        else
            p.sigma1 = initialSigma1;
        lambdaString = (k * k) / (rhoA * h * (1.0f + p.sigma0 * k));
        c3 = 2.0f * p.sigma1 * k;
        c1_plus_c3 = c1 + c3;
        c1H_plus_c3 = c1_H + c3;
        updateKcFactor();
        updateDampingProfile();
    }

    /** Excite string by releasing from static triangular pluck displacement (authentic acoustic pluck). */
    void pluck(float xLocFraction, float forceNewtons)
    {
        const float xLoc = std::clamp(xLocFraction, 0.05f, 0.95f) * p.L;
        // Peak displacement from statics: u_peak = F * x * (L - x) / (T0 * L)
        const float uPeak = (forceNewtons * xLoc * (p.L - xLoc)) / (p.T0 * p.L);
        startPlectrumContact(xLocFraction, -uPeak);
    }

    /** Return true if plectrum is currently in dynamic contact with the string. */
    bool isPlectrumContactActive() const noexcept { return pickContactActive; }

    /** Excite string via continuous nonlinear elastoplastic plectrum contact (v71.f, Literature Review §6). */
    void startPlectrumContact(float xLocFraction, float peakDisplacementMeters, float brightness = 0.5f, float humanAngle = 0.0f)
    {
        const float xLoc = std::clamp(xLocFraction, 0.05f, 0.95f) * p.L;
        const float b = std::clamp(brightness, 0.05f, 1.0f);

        // Center contact node index (clamped strictly within interior grid points)
        pickContactIdx = std::clamp(static_cast<int>(xLoc / h) - 1, 1, N_pts - 2);
        const float xCenter = static_cast<float>(pickContactIdx + 1) * h;
        const float dx = (xLoc - xCenter) / h; // relative offset in [-0.5, 0.5]

        // 3-point quadratic B-spline spatial distribution: sum(weights) == 1.0
        pickContactWeights[0] = 0.5f * (0.5f - dx) * (0.5f - dx);
        pickContactWeights[1] = 0.75f - dx * dx;
        pickContactWeights[2] = 0.5f * (0.5f + dx) * (0.5f + dx);

        // Precompute discrete grid compliance Sp = sum_j (lambdaString[j] * weights[j]^2)
        pickSp = 0.0f;
        for (int j = 0; j < 3; ++j)
        {
            const size_t idx = static_cast<size_t>(pickContactIdx - 1 + j);
            const float lam = (k * k / (rhoA * h)) * c0_vec[idx];
            pickSp += lam * pickContactWeights[j] * pickContactWeights[j];
        }

        // Target stroke deflection and kinematics
        pickTargetY = peakDisplacementMeters;
        pickCurrentY = 0.0f;
        pickEtaPrev = 0.0f;
        pickElapsedSamples = 0;

        // Contact transit duration:
        // Bright/stiff pick or high velocity -> faster transit (~1.2 - 1.8 ms);
        // Dark/warm flesh -> softer, slower contact (~2.2 - 3.2 ms).
        const float contactDurSec = (0.0012f + 0.0010f * (1.0f - b));
        pickMaxSamples = std::max(12, static_cast<int>(contactDurSec * fs));
        pickVelocity = pickTargetY / static_cast<float>(pickMaxSamples);

        // Material contact stiffness & damping (Stefan Bilbao contact law):
        // Steel Tortex pick: Kp ~ 22000 - 32000 N/m; Nylon fingernail/flesh: Kp ~ 11000 - 18000 N/m
        const bool isSteel = (p.rho > 3000.0f);
        const float baseKp = isSteel ? 24000.0f : 13000.0f;
        pickKp = baseKp * (0.75f + 0.50f * b);
        pickRp = 1.35f * (0.50f + 0.60f * (1.0f - b));

        // Plectrum slip force threshold (Coulomb / bevel slip limit):
        // When restoring string tension exceeds the plectrum grip, the string unzips and slips off the edge.
        const float velNorm = std::abs(peakDisplacementMeters) / 0.0015f;
        pickSlipForce = (0.75f + 1.25f * std::clamp(velNorm, 0.2f, 1.5f)) * (0.75f + 0.50f * b);

        // Pluck angle projection (vertical vs horizontal):
        // Pick angle is ~36 degrees to soundboard normal (+/- organic human stroke angle variation)
        const float angleRad = std::clamp(0.628f + 0.10f * humanAngle, 0.40f, 0.85f);
        pickCosTheta = std::cos(angleRad);
        pickSinTheta = std::sin(angleRad);

        // Wound string winding ridge micro-texture:
        // As plectrum edge slides across round wire winding coils, contact height undergoes micro-corrugation
        if (p.woundFactor > 0.05f)
        {
            const float ridgePitch = 0.00028f; // ~0.28 mm wire winding pitch
            const float vp = std::abs(pickVelocity) * fs;
            pickRidgePhase = 0.0f;
            pickRidgePhaseInc = 2.0f * 3.1415926535f * (vp / ridgePitch) * k;
            pickRidgeAmp = 0.000030f * p.woundFactor * (0.35f + 0.65f * b);
            pickRidgeDurSamples = pickMaxSamples;
        }
        else
        {
            pickRidgeDurSamples = 0;
            pickRidgeAmp = 0.0f;
            pickRidgePhase = 0.0f;
            pickRidgePhaseInc = 0.0f;
        }

        // Plectrum contact is now actively engaged
        pickContactActive = true;

        // Reset DC blocker & transient state cleanly at note start
        dcBlockerState = 0.0f;
        prevRawForce   = 0.0f;
        dcBlockerStateH = 0.0f;
        prevRawForceH  = 0.0f;
        saddleFilterState = 0.0f;
        saddleFilterState2 = 0.0f;
        saddleFilterStateH = 0.0f;
        saddleFilterState2H = 0.0f;
        rampSamplesLeft = 0;
        rampTotalSamples = 0;
        scrapeSamplesLeft = 0;
        scrapeTotalSamples = 0;
        lastScrapeForce = 0.0f;

        // Axial longitudinal P-wave impulse (sharper release imparts crisper P-wave)
        longitudinalImpulse = std::abs(peakDisplacementMeters) * p.longitudinalAttack * (0.8f + 1.2f * b);
        longitudinalS1 = 0.0f;
        longitudinalS2 = 0.0f;
        dcBlockerStatePhantom = 0.0f;
        prevRawForcePhantom = 0.0f;
        saddleFilterPhantom = 0.0f;
    }

    /** Excite string with physical acoustic fingertip flesh contour and brightness. */
    void pluckDisplacement(float xLocFraction, float peakDisplacementMeters, float brightness = 0.5f, float humanAngle = 0.0f)
    {
        const float xLoc = std::clamp(xLocFraction, 0.05f, 0.95f) * p.L;
        const float b = std::clamp(brightness, 0.05f, 1.0f);

        // Physical fingertip contact width:
        // Soft fleshy thumb ~ 12 mm (wider contact = warmer acoustic tone); crisp fingernail ~ 4 mm
        const float fingerWidth = 0.004f + 0.008f * (1.0f - b);
        const float s1 = peakDisplacementMeters / xLoc;
        const float s2 = -peakDisplacementMeters / (p.L - xLoc);
        const float w = fingerWidth;
        const float xLeft  = xLoc - 0.5f * w;
        const float xRight = xLoc + 0.5f * w;

        for (int i = 0; i < N_pts; ++i)
        {
            const float x = static_cast<float>(i + 1) * h;
            if (x < xLeft)
                u_curr[static_cast<size_t>(i)] = x * s1;
            else if (x > xRight)
                u_curr[static_cast<size_t>(i)] = (p.L - x) * (-s2);
            else
            {
                const float dx = x - xLoc;
                u_curr[static_cast<size_t>(i)] = peakDisplacementMeters + 0.5f * (s1 + s2) * dx - (s1 - s2) * (dx * dx) / (2.0f * w);
            }
        }

        // Zero-phase spatial binomial smoothing passes (flesh absorption of ultra-high partials)
        // Only applied on very dark/warm plucks (fleshy thumb pad); authentic nail/plectrum plucks preserve sharp triangular apex
        const int smoothPasses = (b < 0.30f) ? 1 : 0;
        for (int pass = 0; pass < smoothPasses; ++pass)
        {
            for (int i = 1; i < N_pts - 1; ++i)
                u_curr[static_cast<size_t>(i)] = 0.25f * u_curr[static_cast<size_t>(i - 1)]
                                               + 0.50f * u_curr[static_cast<size_t>(i)]
                                               + 0.25f * u_curr[static_cast<size_t>(i + 1)];
        }

        // Plectrum & Fingernail Helmholtz Release Slip:
        // At release, string slips off plectrum edge with finite acoustic slip velocity v_slip = c * du/dx.
        // Discrete Cauchy condition: u_prev = u_curr - k * v_slip, launching authentic traveling Helmholtz corner.
        const float cWave = std::sqrt(p.T0 / rhoA);
        const float nominalSlope = std::abs(peakDisplacementMeters) / xLoc;
        const float slipVel = (peakDisplacementMeters > 0.0f ? -1.0f : 1.0f) * cWave * nominalSlope * (0.85f + 0.65f * b);
        const float slipOffset = k * slipVel;

        for (int i = 0; i < N_pts; ++i)
        {
            const float dist = std::abs(static_cast<float>(i + 1) * h - xLoc);
            const float pulse = (dist < w) ? (0.5f * (1.0f + std::cos(3.1415926535f * dist / w))) : 0.0f;
            u_prev[static_cast<size_t>(i)] = u_curr[static_cast<size_t>(i)] + pulse * slipOffset;
        }

        w_curr = 0.0f;
        w_prev = 0.0f;
        psi = { 0.0f, 0.0f, 0.0f };
        psi_prev = { 0.0f, 0.0f, 0.0f };
        prevXiB = 0.0f;
        prevXiF = 0.0f;

        // Correct DC blocker initialization:
        // Prior to pluck release, string was held in static equilibrium (DC).
        // Steady-state DC blocker output y[-1] is 0; prior static input x[-1] is u_curr[N-1] / h.
        // This ensures the dynamic bridge force is 0 until the transverse traveling wave
        // actually arrives from the soundhole ~2-4 ms later, eliminating the t=0 piano hammer blow.
        dcBlockerState = u_curr[static_cast<size_t>(N_pts - 1)] / h;
        prevRawForce   = 0.0f;
        saddleFilterState = 0.0f;
        saddleFilterState2 = 0.0f;
        rampSamplesLeft = 0;
        rampTotalSamples = 0;
        pluckDurSamples = 0;
        pluckSampleCount = 0;

        // H-polarization initial conditions:
        // The nail sweeps primarily perpendicular (V direction). Lateral deflection (H)
        // is ~45% of peak displacement, with organic human stroke variation (+/- 6%)
        const float hAmp = std::clamp(0.45f + 0.06f * humanAngle, 0.35f, 0.55f);
        for (int i = 0; i < N_pts; ++i)
            u_curr_H[static_cast<size_t>(i)] = hAmp * u_curr[static_cast<size_t>(i)];
        u_prev_H = u_curr_H;
        std::fill(u_next_H.begin(), u_next_H.end(), 0.0f);
        std::fill(Du_H.begin(),     Du_H.end(),     0.0f);
        std::fill(D2u_H.begin(),    D2u_H.end(),    0.0f);
        std::fill(Du_prev_H.begin(),Du_prev_H.end(),0.0f);
        computeLaplacian(u_prev.data(), Du_prev.data());
        computeLaplacian(u_prev_H.data(), Du_prev_H.data());
        dcBlockerStateH    = u_curr_H[static_cast<size_t>(N_pts - 1)] / h;
        prevRawForceH      = 0.0f;
        saddleFilterStateH  = 0.0f;
        saddleFilterState2H = 0.0f;

        // Excite longitudinal P-wave resonator with axial release impulse
        // Proportional to pluck displacement and brightness (sharper release imparts crisper P-wave)
        longitudinalImpulse = peakDisplacementMeters * p.longitudinalAttack * (0.8f + 1.2f * b);
        longitudinalS1 = 0.0f;
        longitudinalS2 = 0.0f;
        dcBlockerStatePhantom = 0.0f;
        prevRawForcePhantom = 0.0f;
        saddleFilterPhantom = 0.0f;

        // Tactile scrape generator initialization
        // Wound strings have wider contact and longer pre-release rasp (~22ms)
        // Plain treble wire has crisp, glassy micro-slip (~9ms)
        const float scrapeDurationSec = 0.009f + 0.015f * p.woundFactor;
        scrapeTotalSamples = static_cast<int>(scrapeDurationSec * fs);
        scrapeSamplesLeft = scrapeTotalSamples;

        // Calibrate scrape amplitude relative to nominal bridge slope (dimensionless slope units)
        // Calibrated to sit at authentic -18 dB to -24 dB acoustic level
        const float nominalBridgeSlope = std::abs(peakDisplacementMeters) / (p.L - xLoc);
        scrapeAmp = nominalBridgeSlope * (0.28f + 0.40f * b) * (0.75f + 0.65f * p.woundFactor);
        scrapeFilterState = 0.0f;
        lastScrapeForce = 0.0f;
    }

    /** Excite string at fractional location with raised-cosine pulse (Eq. 5). */
    void pluck(float xLocFraction, float forceNewtons, float durationSeconds)
    {
        if (durationSeconds <= 0.0f)
        {
            pluck(xLocFraction, forceNewtons);
            return;
        }
        const float xLoc = std::clamp(xLocFraction, 0.05f, 0.95f) * p.L;
        pluckIdx = std::clamp(static_cast<int>(xLoc / h) - 1, 0, N_pts - 1);
        pluckForceAmp = forceNewtons;
        pluckDurSamples = static_cast<int>(durationSeconds * fs);
        pluckSampleCount = 0;
    }

    /** Advance simulation by 1 audio sample and return bridge displacement/force. */
    float tick(bool acousticBridgeOutput = true) noexcept
    {
        float contactForceH = 0.0f;

        // 1. Current external plucking force f_e^n (Eq. 5)
        float fe = 0.0f;
        if (pluckSampleCount < pluckDurSamples && pluckDurSamples > 0)
        {
            const float phase = 3.14159265358979323846f * static_cast<float>(pluckSampleCount) / static_cast<float>(pluckDurSamples);
            const float s = std::sin(phase);
            fe = pluckForceAmp * s * s;
            pluckSampleCount++;
        }

        const int nPts = N_pts;
        const float ih2 = invHSq;
        const float* FDTD_RESTRICT uc = u_curr.data();
        const float* FDTD_RESTRICT up = u_prev.data();
        float* FDTD_RESTRICT un = u_next.data();
        float* FDTD_RESTRICT du = Du.data();
        const float* FDTD_RESTRICT dup = Du_prev.data();
        const float* FDTD_RESTRICT c0p = c0_vec.data();
        const float* FDTD_RESTRICT c4p = c4_vec.data();
        float* FDTD_RESTRICT bStr = b_string.data();

        // 2 & 3 (Pass 1): Compute Du from u_curr, track minU, accumulate dot products for KC & elongation
        float minU = 1e6f;
        float duDotUprev = 0.0f;
        float duDotDu = 0.0f;
        float uDotDu = 0.0f;

        if (nPts >= 2)
        {
            const float d0 = (-2.0f * uc[0] + uc[1]) * ih2;
            du[0] = d0;
            if (uc[0] < minU) minU = uc[0];
            duDotUprev += d0 * up[0];
            duDotDu    += d0 * d0;
            if (acousticBridgeOutput) uDotDu += uc[0] * d0;

            for (int i = 1; i < nPts - 1; ++i)
            {
                const float u_i = uc[i];
                const float d_i = (uc[i - 1] - 2.0f * u_i + uc[i + 1]) * ih2;
                du[i] = d_i;
                if (u_i < minU) minU = u_i;
                duDotUprev += d_i * up[i];
                duDotDu    += d_i * d_i;
                if (acousticBridgeOutput) uDotDu += u_i * d_i;
            }

            const int last = nPts - 1;
            const float dLast = (uc[last - 1] - 2.0f * uc[last]) * ih2;
            du[last] = dLast;
            if (uc[last] < minU) minU = uc[last];
            duDotUprev += dLast * up[last];
            duDotDu    += dLast * dLast;
            if (acousticBridgeOutput) uDotDu += uc[last] * dLast;
        }
        else if (nPts == 1)
        {
            const float d0 = (-2.0f * uc[0]) * ih2;
            du[0] = d0;
            minU = uc[0];
            duDotUprev = d0 * up[0];
            duDotDu    = d0 * d0;
            if (acousticBridgeOutput) uDotDu = uc[0] * d0;
        }

        const float kcFac = kcFactor;
        const float knDotZprev = kcFac * duDotUprev;
        const float kn_dot_kn  = kcFac * kcFac * duDotDu;

        // Fret crown height is p.m0 (-0.0012 m). Fretboard surface is p.b0 (-0.0028 m).
        // If minU > p.m0, string displacement is physically above all frets and fretboard.
        const bool fretCollisionPossible = (minU <= p.m0);
        bool fingerCollisionActive = false;
        if (fingerEngaged)
        {
            const float uFinger = (1.0f - fingerAlpha) * uc[fingerIdx] + fingerAlpha * uc[fingerIdx + 1];
            if ((uFinger - w_curr) > 0.0f)
                fingerCollisionActive = true;
        }

        const bool anyCollision = (fretCollisionPossible || fingerCollisionActive);

        if (!anyCollision)
        {
            // Zero out auxiliary collision states (non-contact free vibration)
            psi[0] = 0.0f;
            psi[1] = 0.0f;
            psi[2] = 0.0f;
            prevXiB = 0.0f;
            prevXiF = 0.0f;

            // Fast-path fused Rank-1 Sherman-Morrison (Kirchhoff-Carrier dynamic tension ONLY):
            // Inline D2u from Du[i-1..i+1], build b_string[i], accumulate Du . b_string
            const float kcTermScale = -kcFac * knDotZprev;
            const float c1pc3 = c1_plus_c3;
            const float c2_loc = c2;
            const float c3_loc = c3;
            float duDotB = 0.0f;

            if (nPts >= 2)
            {
                {
                    const float d2u0 = (-2.0f * du[0] + du[1]) * ih2;
                    const float bi = c0p[0] * (2.0f * uc[0] + c1pc3 * du[0] - c2_loc * d2u0 + c4p[0] * up[0] - c3_loc * dup[0])
                                   + kcTermScale * du[0];
                    bStr[0] = bi;
                    duDotB += du[0] * bi;
                }

                for (int i = 1; i < nPts - 1; ++i)
                {
                    const float d_i = du[i];
                    const float d2u_i = (du[i - 1] - 2.0f * d_i + du[i + 1]) * ih2;
                    const float bi = c0p[i] * (2.0f * uc[i] + c1pc3 * d_i - c2_loc * d2u_i + c4p[i] * up[i] - c3_loc * dup[i])
                                   + kcTermScale * d_i;
                    bStr[i] = bi;
                    duDotB += d_i * bi;
                }

                {
                    const int last = nPts - 1;
                    const float dLast = du[last];
                    const float d2uLast = (du[last - 1] - 2.0f * dLast) * ih2;
                    const float bi = c0p[last] * (2.0f * uc[last] + c1pc3 * dLast - c2_loc * d2uLast + c4p[last] * up[last] - c3_loc * dup[last])
                                   + kcTermScale * dLast;
                    bStr[last] = bi;
                    duDotB += dLast * bi;
                }
            }
            else if (nPts == 1)
            {
                const float d2u0 = (-2.0f * du[0]) * ih2;
                const float bi = c0p[0] * (2.0f * uc[0] + c1pc3 * du[0] - c2_loc * d2u0 + c4p[0] * up[0] - c3_loc * dup[0])
                               + kcTermScale * du[0];
                bStr[0] = bi;
                duDotB = du[0] * bi;
            }

            if (pluckDurSamples > 0)
            {
                const int lo = std::max(0, pluckIdx - 2);
                const int hi = std::min(nPts - 1, pluckIdx + 2);
                for (int i = lo; i <= hi; ++i)
                {
                    const int dist = std::abs(i - pluckIdx);
                    const float spatialWeight = 0.5f * (1.0f + std::cos(3.14159265f * static_cast<float>(dist) / 3.0f));
                    const float Ef_i = fe * lambdaString * spatialWeight * 0.40f;
                    bStr[i] += Ef_i;
                    duDotB += du[i] * Ef_i;
                }
            }

            const float x_kc_scaled = (kcFac * kcFac * duDotB) / (1.0f + kn_dot_kn);

            for (int i = 0; i < nPts; ++i)
                un[i] = bStr[i] - x_kc_scaled * du[i];

            w_next = 2.0f * w_curr - w_prev;
        }
        else
        {
            // Full 4x4 Woodbury collision path (~2% of frames): populate D2u, kn, Bu_plus_Cu, v_lin
            if (nPts >= 2)
            {
                D2u[0] = (-2.0f * du[0] + du[1]) * ih2;
                for (int i = 1; i < nPts - 1; ++i)
                    D2u[static_cast<size_t>(i)] = (du[i - 1] - 2.0f * du[i] + du[i + 1]) * ih2;
                D2u[static_cast<size_t>(nPts - 1)] = (du[nPts - 2] - 2.0f * du[nPts - 1]) * ih2;
            }
            else if (nPts == 1)
            {
                D2u[0] = (-2.0f * du[0]) * ih2;
            }

            for (int i = 0; i < nPts; ++i)
            {
                const auto si = static_cast<size_t>(i);
                kn[si] = kcFac * du[i];
                const float Bu_i = c0p[i] * (2.0f * uc[i] + c1_plus_c3 * du[i] - c2 * D2u[si]);
                const float Cu_i = c0p[i] * (c4p[i] * up[i] - c3 * dup[i]);
                Bu_plus_Cu[si] = Bu_i + Cu_i;
                v_lin[si] = Bu_i + Cu_i - up[i];
            }
            // a) Fretboard: V_B
            float VB = 0.0f;
            std::fill(gradVB.begin(), gradVB.end(), 0.0f);
            for (int i = 0; i < N_pts; ++i)
            {
                const float dist = p.b0 - u_curr[static_cast<size_t>(i)];
                if (dist > 0.0f)
                {
                    const float pB = std::pow(dist, p.alphaB);
                    gradVB[static_cast<size_t>(i)] = -h * p.KB * pB;
                    VB += (p.KB * h / (p.alphaB + 1.0f)) * pB * dist;
                }
            }
            std::fill(gB.begin(), gB.end(), 0.0f);

        if (VB > 1e-12f)
        {
            const float denomB = std::sqrt(2.0f * VB + 1e-12f);
            for (int i = 0; i < N_pts; ++i)
                gB[static_cast<size_t>(i)] = gradVB[static_cast<size_t>(i)] / denomB;

            // Bilbao Section 4.7 Eq. 60-61 non-negativity constraint
            float xiB = 0.0f;
            for (int i = 0; i < N_pts; ++i)
                xiB += gB[static_cast<size_t>(i)] * v_lin[static_cast<size_t>(i)];
            const float threshB = -4.0f * psi[0];
            float gammaB = 1.0f;
            if (std::abs(xiB) > 1e-12f && xiB < threshB)
            {
                gammaB = threshB / xiB;
            }
            else if (std::abs(xiB) <= 1e-12f && std::abs(prevXiB) > 1e-12f)
            {
                const float ratio = threshB / prevXiB;
                gammaB = (ratio < 0.0f ? 0.5f : 1.5f) * ratio;
            }
            prevXiB = xiB;
            for (int i = 0; i < N_pts; ++i)
                gB[static_cast<size_t>(i)] *= gammaB;
        }
        else
        {
            psi[0] = 0.0f;
            prevXiB = 0.0f;
        }

        // b) 20 Frets: V_F
        float VF = 0.0f;
        std::fill(gradVF.begin(), gradVF.end(), 0.0f);
        for (int q = 0; q < fretsM; ++q)
        {
            const int idx = fretIndices[static_cast<size_t>(q)];
            const float a = fretAlpha[static_cast<size_t>(q)];
            const float uFret = (1.0f - a) * u_curr[static_cast<size_t>(idx)] + a * u_curr[static_cast<size_t>(idx + 1)];
            const float dist = p.m0 - uFret;
            if (dist > 0.0f)
            {
                const float pF = std::pow(dist, p.alphaF);
                const float fForce = p.KF * pF;
                gradVF[static_cast<size_t>(idx)]     += -(1.0f - a) * fForce;
                gradVF[static_cast<size_t>(idx + 1)] += -a * fForce;
                VF += (p.KF / (p.alphaF + 1.0f)) * pF * dist;
            }
        }
        std::fill(gF.begin(), gF.end(), 0.0f);
        if (VF > 1e-12f)
        {
            const float denomF = std::sqrt(2.0f * VF + 1e-12f);
            for (int i = 0; i < N_pts; ++i)
                gF[static_cast<size_t>(i)] = gradVF[static_cast<size_t>(i)] / denomF;

            // Bilbao Section 4.7 Eq. 60-61 non-negativity constraint
            float xiF = 0.0f;
            for (int i = 0; i < N_pts; ++i)
                xiF += gF[static_cast<size_t>(i)] * v_lin[static_cast<size_t>(i)];
            const float threshF = -4.0f * psi[1];
            float gammaF = 1.0f;
            if (std::abs(xiF) > 1e-12f && xiF < threshF)
            {
                gammaF = threshF / xiF;
            }
            else if (std::abs(xiF) <= 1e-12f && std::abs(prevXiF) > 1e-12f)
            {
                const float ratio = threshF / prevXiF;
                gammaF = (ratio < 0.0f ? 0.5f : 1.5f) * ratio;
            }
            prevXiF = xiF;
            for (int i = 0; i < N_pts; ++i)
                gF[static_cast<size_t>(i)] *= gammaF;
        }
        else
        {
            psi[1] = 0.0f;
            prevXiF = 0.0f;
        }

        // c) Finger: V_FG (only active when finger is engaged)
        float VFG = 0.0f;
        std::fill(gFG.begin(), gFG.end(), 0.0f);
        float gPrimeFG = 0.0f;

        if (fingerEngaged)
        {
            const float uFinger = (1.0f - fingerAlpha) * u_curr[static_cast<size_t>(fingerIdx)] + fingerAlpha * u_curr[static_cast<size_t>(fingerIdx + 1)];
            const float distFG = uFinger - w_curr;
            if (distFG > 0.0f)
            {
                const float pFG = std::pow(distFG, p.alphaFG);
                const float fFG = p.KFG * pFG;
                VFG = (p.KFG / (p.alphaFG + 1.0f)) * pFG * distFG;
                const float denomFG = std::sqrt(2.0f * VFG + 1e-12f);
                gFG[static_cast<size_t>(fingerIdx)]     = -(1.0f - fingerAlpha) * fFG / denomFG;
                gFG[static_cast<size_t>(fingerIdx + 1)] = -fingerAlpha * fFG / denomFG;
                gPrimeFG = fFG / denomFG;
            }
        }

        // 5. Construct Vector b^n (Eq. 49)
        // Compute (G^n)^T * z^{n-1}
        float gBDotZprev = 0.0f, gFDotZprev = 0.0f, gFGDotZprev = 0.0f;
        for (int i = 0; i < N_pts; ++i)
        {
            gBDotZprev  += gB[static_cast<size_t>(i)]  * u_prev[static_cast<size_t>(i)];
            gFDotZprev  += gF[static_cast<size_t>(i)]  * u_prev[static_cast<size_t>(i)];
            gFGDotZprev += gFG[static_cast<size_t>(i)] * u_prev[static_cast<size_t>(i)];
        }
        gFGDotZprev += gPrimeFG * w_prev;

        for (int i = 0; i < N_pts; ++i)
        {
            // Smooth spatial pluck distribution j(xe) over 5 points (Bilbao Eq. 15):
            float Ef_i = 0.0f;
            if (pluckDurSamples > 0)
            {
                const int dist = std::abs(i - pluckIdx);
                if (dist <= 2)
                {
                    const float spatialWeight = 0.5f * (1.0f + std::cos(3.14159265f * static_cast<float>(dist) / 3.0f));
                    Ef_i = fe * lambdaString * spatialWeight * 0.40f;
                }
            }

            // Collision terms: -Lambda * G * Psi^{n-1/2} + 0.25 * Lambda * G * (G^T * z^{n-1})
            const float collTerm = -lambdaString * (gB[static_cast<size_t>(i)] * psi[0] + gF[static_cast<size_t>(i)] * psi[1] + gFG[static_cast<size_t>(i)] * psi[2])
                                   + 0.25f * lambdaString * (gB[static_cast<size_t>(i)] * gBDotZprev + gF[static_cast<size_t>(i)] * gFDotZprev + gFG[static_cast<size_t>(i)] * gFGDotZprev);

            // Kirchhoff-Carrier term: -k^n * ((k^n)^T * z^{n-1})
            const float kcTerm = -kn[static_cast<size_t>(i)] * knDotZprev;

            b_string[static_cast<size_t>(i)] = Bu_plus_Cu[static_cast<size_t>(i)] + Ef_i + collTerm + kcTerm;
        }

        // Finger component b_finger:
        const float Bw = 2.0f * w_curr;
        const float Cw = -w_prev;
        const float collFinger = -lambdaFinger * (gPrimeFG * psi[2]) + 0.25f * lambdaFinger * (gPrimeFG * gFGDotZprev);
        const float b_finger = Bw + Cw + collFinger;

        // 6. Low-Rank Woodbury Inversion (Size 4 Linear System, Eq. 53):
        // A^n = I_N + U * V^T, where U is N x 4, V is N x 4.
        std::array<std::array<float, 4>, 4> M {};
        for (int r = 0; r < 4; ++r) M[static_cast<size_t>(r)][static_cast<size_t>(r)] = 1.0f;

        // Dot products:
        float gB_dot_gB = 0.0f, gB_dot_gF = 0.0f, gB_dot_gFG = 0.0f, gB_dot_kn = 0.0f;
        float gF_dot_gF = 0.0f, gF_dot_gFG = 0.0f, gF_dot_kn = 0.0f;
        float gFG_dot_gFG = 0.0f, gFG_dot_kn = 0.0f;

        for (int i = 0; i < N_pts; ++i)
        {
            const float gb = gB[static_cast<size_t>(i)];
            const float gf = gF[static_cast<size_t>(i)];
            const float gfg = gFG[static_cast<size_t>(i)];
            const float k_val = kn[static_cast<size_t>(i)];

            gB_dot_gB   += gb * gb;
            gB_dot_gF   += gb * gf;
            gB_dot_gFG  += gb * gfg;
            gB_dot_kn   += gb * k_val;

            gF_dot_gF   += gf * gf;
            gF_dot_gFG  += gf * gfg;
            gF_dot_kn   += gf * k_val;

            gFG_dot_gFG += gfg * gfg;
            gFG_dot_kn  += gfg * k_val;
        }

        // Fill 4x4 matrix M:
        M[0][0] += 0.25f * lambdaString * gB_dot_gB;
        M[0][1] += 0.25f * lambdaString * gB_dot_gF;
        M[0][2] += 0.25f * lambdaString * gB_dot_gFG;
        M[0][3] += 0.50f * gB_dot_kn;

        M[1][0] += 0.25f * lambdaString * gB_dot_gF;
        M[1][1] += 0.25f * lambdaString * gF_dot_gF;
        M[1][2] += 0.25f * lambdaString * gF_dot_gFG;
        M[1][3] += 0.50f * gF_dot_kn;

        M[2][0] += 0.25f * lambdaString * gB_dot_gFG;
        M[2][1] += 0.25f * lambdaString * gF_dot_gFG;
        M[2][2] += 0.25f * (lambdaString * gFG_dot_gFG + lambdaFinger * gPrimeFG * gPrimeFG);
        M[2][3] += 0.50f * gFG_dot_kn;

        M[3][0] += 0.50f * lambdaString * gB_dot_kn;
        M[3][1] += 0.50f * lambdaString * gF_dot_kn;
        M[3][2] += 0.50f * lambdaString * gFG_dot_kn;
        M[3][3] += kn_dot_kn;

        // Compute rhs y = V^T * b:
        std::array<float, 4> y { 0.0f, 0.0f, 0.0f, 0.0f };
        for (int i = 0; i < N_pts; ++i)
        {
            const float bi = b_string[static_cast<size_t>(i)];
            y[0] += 0.5f * gB[static_cast<size_t>(i)]  * bi;
            y[1] += 0.5f * gF[static_cast<size_t>(i)]  * bi;
            y[2] += 0.5f * gFG[static_cast<size_t>(i)] * bi;
            y[3] += kn[static_cast<size_t>(i)]         * bi;
        }
        y[2] += 0.5f * gPrimeFG * b_finger;

        // Solve 4x4 system M * x = y via Gaussian elimination with partial pivoting:
        std::array<float, 4> x = solve4x4(M, y);

        // 7. Complete Update: z^{n+1} = b^n - U * x
        for (int i = 0; i < N_pts; ++i)
        {
            const float u0_i = 0.5f * lambdaString * gB[static_cast<size_t>(i)];
            const float u1_i = 0.5f * lambdaString * gF[static_cast<size_t>(i)];
            const float u2_i = 0.5f * lambdaString * gFG[static_cast<size_t>(i)];
            const float u3_i = kn[static_cast<size_t>(i)];

            u_next[static_cast<size_t>(i)] = b_string[static_cast<size_t>(i)] - (u0_i * x[0] + u1_i * x[1] + u2_i * x[2] + u3_i * x[3]);
        }
        const float u2_w = 0.5f * lambdaFinger * gPrimeFG;
        w_next = b_finger - (u2_w * x[2]);

        // 8. Update Auxiliary Variables (Eq. 48):
        // Psi^{n+1/2} = Psi^{n-1/2} + 0.5 * (G^n)^T * (z^{n+1} - z^{n-1})
        float diffDotGB = 0.0f, diffDotGF = 0.0f, diffDotGFG = 0.0f;
        for (int i = 0; i < N_pts; ++i)
        {
            const float dz = u_next[static_cast<size_t>(i)] - u_prev[static_cast<size_t>(i)];
            diffDotGB  += gB[static_cast<size_t>(i)]  * dz;
            diffDotGF  += gF[static_cast<size_t>(i)]  * dz;
            diffDotGFG += gFG[static_cast<size_t>(i)] * dz;
        }
        diffDotGFG += gPrimeFG * (w_next - w_prev);

        psi_prev = psi;
        if (VB > 1e-12f) psi[0] = std::max(0.0f, psi[0] + 0.5f * diffDotGB);
        else psi[0] = 0.0f;
        if (VF > 1e-12f) psi[1] = std::max(0.0f, psi[1] + 0.5f * diffDotGF);
        else psi[1] = 0.0f;
        if (VFG > 1e-12f) psi[2] = std::max(0.0f, psi[2] + 0.5f * diffDotGFG);
        else psi[2] = 0.0f;
        } // end of else (full 4x4 Woodbury solver)

        // ── Dynamic Nonlinear Plectrum Contact Force (v71.f, Literature Review §6) ──
        if (pickContactActive)
        {
            pickCurrentY += pickVelocity;
            pickElapsedSamples++;

            float yEff = pickCurrentY;
            if (pickRidgeDurSamples > 0 && pickElapsedSamples < pickRidgeDurSamples)
            {
                const float ridgeEnv = std::sin(3.1415926535f * static_cast<float>(pickElapsedSamples) / static_cast<float>(pickRidgeDurSamples));
                yEff += pickRidgeAmp * std::sin(pickRidgePhase) * ridgeEnv;
                pickRidgePhase += pickRidgePhaseInc;
                if (pickRidgePhase >= 6.2831853f) pickRidgePhase -= 6.2831853f;
            }

            // String displacement under the plectrum contact zone
            float u_p = 0.0f;
            for (int j = 0; j < 3; ++j)
                u_p += pickContactWeights[j] * u_next[static_cast<size_t>(pickContactIdx - 1 + j)];

            const float sgn = (pickTargetY < 0.0f) ? -1.0f : 1.0f;
            const float eta = sgn * (yEff - u_p);

            const float denom = 1.0f + pickSp * (pickKp + pickRp * fs);
            const float eta_curr = (eta + pickSp * (pickRp * fs) * pickEtaPrev) / denom;
            float fp = (pickKp + pickRp * fs) * eta_curr - (pickRp * fs) * pickEtaPrev;
            if (fp < 0.0f) fp = 0.0f;

            if (fp >= pickSlipForce || (sgn * pickCurrentY >= sgn * pickTargetY) || pickElapsedSamples >= pickMaxSamples)
            {
                // Plectrum slips off string edge: sharp Helmholtz release transient
                pickContactActive = false;
                pickEtaPrev = 0.0f;
            }
            else if (eta > 0.0f)
            {
                pickEtaPrev = eta_curr;
                const float fV = sgn * fp * pickCosTheta;
                contactForceH  = sgn * fp * pickSinTheta;

                for (int j = 0; j < 3; ++j)
                {
                    const size_t idx = static_cast<size_t>(pickContactIdx - 1 + j);
                    const float lam = lamBase * c0p[idx];
                    un[idx] += lam * pickContactWeights[j] * fV;
                }
            }
        }

        // 9. Audio output drawn at the bridge boundary x = L (only needed for Acoustic bridge radiation)
        constexpr float dcR = 0.9965f;
        float scrapeForce = 0.0f;
        if (acousticBridgeOutput)
        {
            // Force on the bridge saddle is proportional to spatial slope: u[N-1] / h
            float bridgeForce = un[static_cast<size_t>(nPts - 1)] * invH;

            // 25 Hz DC Blocker removes static displacement offset from initial triangular pluck
            const float dcOut = bridgeForce - dcBlockerState + dcR * prevRawForce;
            dcBlockerState = bridgeForce;
            prevRawForce = dcOut;
            bridgeForce = dcOut;

            // Smooth initial attack ramp (32 samples = 0.7 ms) to eliminate static step click
            if (rampSamplesLeft > 0)
            {
                const int idx = rampTotalSamples - rampSamplesLeft;
                const float ramp = 0.5f * (1.0f - std::cos(3.14159265358979323846f * static_cast<float>(idx) / static_cast<float>(rampTotalSamples)));
                bridgeForce *= ramp;
                rampSamplesLeft--;
            }

            // Tactile nail/plectrum scrape friction transient
            if (scrapeSamplesLeft > 0)
            {
                scrapeRng ^= scrapeRng << 13u;
                scrapeRng ^= scrapeRng >> 17u;
                scrapeRng ^= scrapeRng << 5u;
                const float noise = static_cast<float>(static_cast<int32_t>(scrapeRng)) / static_cast<float>(0x7FFFFFFFu);
                const float hfScrape = noise - 0.72f * scrapeFilterState;
                scrapeFilterState = noise;
                const float progress = 1.0f - static_cast<float>(scrapeSamplesLeft) / static_cast<float>(scrapeTotalSamples);
                const float env = (progress < 0.12f) ? (progress / 0.12f) : std::exp(-4.2f * (progress - 0.12f));

                // Wound bass string winding rasp vs plain nylon glassy slip:
                float texture = hfScrape;
                if (p.woundFactor > 0.05f)
                {
                    // Winding ridge corrugation texture (~2400 Hz)
                    const float ridgePhase = static_cast<float>(scrapeTotalSamples - scrapeSamplesLeft) * (2400.0f * 6.2831853f / fs);
                    const float ridgeRasp = std::sin(ridgePhase);
                    texture = (1.0f - 0.45f * p.woundFactor) * hfScrape + (0.45f * p.woundFactor) * ridgeRasp * (0.8f + 0.2f * noise);
                }

                scrapeForce = scrapeAmp * texture * env;
                scrapeSamplesLeft--;
            }

            // Bone saddle dual-pole terminating mechanical impedance lowpass filter (12 dB/octave)
            saddleFilterState  = (1.0f - saddleBeta) * bridgeForce + saddleBeta * saddleFilterState;
            saddleFilterState2 = (1.0f - saddleBeta) * saddleFilterState + saddleBeta * saddleFilterState2;
        }

        // 10. Pointer / State rotation (O(1) buffer swaps, zero memory copies)
        u_prev.swap(u_curr);
        u_curr.swap(u_next);
        Du_prev.swap(Du);
        w_prev = w_curr;
        w_curr = w_next;


        // ── H-Polarization Tick ────────────────────────────────────────────────
        // Horizontal (lateral) string polarization, parallel to the soundboard.
        // Runs free stiff string + Kirchhoff-Carrier nonlinearity only.
        float* FDTD_RESTRICT duh = Du_H.data();
        const float* FDTD_RESTRICT uch = u_curr_H.data();
        const float* FDTD_RESTRICT uph = u_prev_H.data();
        const float* FDTD_RESTRICT duph = Du_prev_H.data();
        float* FDTD_RESTRICT unh = u_next_H.data();

        // Pass 1: H spatial Laplacian (Du_prev_H swapped in O(1) at step end)
        computeLaplacian(uch, duh);

        // Pass 2: Fused inline biharmonic D2u_H + stencil update (eliminates D2u_H array pass)
        {
            const float c1Hpc3 = c1H_plus_c3;
            const float c2_loc = c2;
            const float c3_loc = c3;
            if (nPts >= 2)
            {
                {
                    const float d2u0 = (-2.0f * duh[0] + duh[1]) * ih2;
                    unh[0] = c0p[0] * (2.0f * uch[0] + c1Hpc3 * duh[0] - c2_loc * d2u0 + c4p[0] * uph[0] - c3_loc * duph[0]);
                }
                for (int i = 1; i < nPts - 1; ++i)
                {
                    const float d_i = duh[i];
                    const float d2u_i = (duh[i - 1] - 2.0f * d_i + duh[i + 1]) * ih2;
                    unh[i] = c0p[i] * (2.0f * uch[i] + c1Hpc3 * d_i - c2_loc * d2u_i + c4p[i] * uph[i] - c3_loc * duph[i]);
                }
                {
                    const int last = nPts - 1;
                    const float dLast = duh[last];
                    const float d2uLast = (duh[last - 1] - 2.0f * dLast) * ih2;
                    unh[last] = c0p[last] * (2.0f * uch[last] + c1Hpc3 * dLast - c2_loc * d2uLast + c4p[last] * uph[last] - c3_loc * duph[last]);
                }
            }
            else if (nPts == 1)
            {
                const float d2u0 = (-2.0f * duh[0]) * ih2;
                unh[0] = c0p[0] * (2.0f * uch[0] + c1Hpc3 * duh[0] - c2_loc * d2u0 + c4p[0] * uph[0] - c3_loc * duph[0]);
            }
        }

        if (contactForceH != 0.0f)
        {
            for (int j = 0; j < 3; ++j)
            {
                const size_t idx = static_cast<size_t>(pickContactIdx - 1 + j);
                const float lam = lamBase * c0p[idx];
                unh[idx] += lam * pickContactWeights[j] * contactForceH;
            }
        }

        if (acousticBridgeOutput)
        {
            // H bridge force + DC blocker
            constexpr float dcR_H = 0.9965f;
            float bridgeForce_H = unh[static_cast<size_t>(nPts - 1)] * invH;
            const float dcOut_H = bridgeForce_H - dcBlockerStateH + dcR_H * prevRawForceH;
            dcBlockerStateH  = bridgeForce_H;
            prevRawForceH    = dcOut_H;
            bridgeForce_H    = dcOut_H;

            // H saddle filter (same saddleBeta as V)
            saddleFilterStateH  = (1.0f - saddleBeta) * bridgeForce_H + saddleBeta * saddleFilterStateH;
            saddleFilterState2H = (1.0f - saddleBeta) * saddleFilterStateH + saddleBeta * saddleFilterState2H;
        }

        // H state rotation (O(1) buffer swaps, zero memory copies)
        u_prev_H.swap(u_curr_H);
        u_curr_H.swap(u_next_H);
        Du_prev_H.swap(Du_H);

        if (!acousticBridgeOutput)
            return 0.0f;

        // Mix: V drives the saddle directly (100%); H couples laterally at ~35%.
        // The lateral rocking force on the saddle is less efficient than vertical drive.
        const float transverseMix = saddleFilterState2 + 0.35f * saddleFilterState2H;

        // ── Longitudinal String Physics: Phantom Harmonics (2f0, 4f0) & P-wave Snap ──
        // 1. Dynamic string elongation (uDotDu computed on u^n . D u^n prior to state rotation):
        const float elongation = -0.5f * h * uDotDu;
        const float deltaT = (EA / p.L) * elongation;

        const float potScale = std::clamp(std::abs(elongation) / (0.00015f + 1e-9f), 0.05f, 2.50f);
        const float phantomRaw = (deltaT / p.T0) * (p.phantomCoupling * 0.35f * potScale);
        const float dcOutPhantom = phantomRaw - dcBlockerStatePhantom + dcR * prevRawForcePhantom;
        dcBlockerStatePhantom = phantomRaw;
        prevRawForcePhantom   = dcOutPhantom;

        // Saddle filter for phantom harmonics
        saddleFilterPhantom = (1.0f - saddleBeta) * dcOutPhantom + saddleBeta * saddleFilterPhantom;

        // 2. Longitudinal P-wave attack resonator (eigenfrequency f_L = c_L / 2L):
        const float longSample = longitudinalImpulse - longitudinalB1 * longitudinalS1 - longitudinalB2 * longitudinalS2;
        longitudinalImpulse = 0.0f; // one-shot release impulse
        longitudinalS2 = longitudinalS1;
        longitudinalS1 = longSample;

        const float longAcoustic = longSample * (p.woundFactor > 0.1f ? 0.22f : 0.14f);
        const float phantomAcoustic = saddleFilterPhantom * 0.35f;
        lastScrapeForce = scrapeForce;
        const float scrapeAcoustic = scrapeForce * 0.85f;
        return transverseMix + phantomAcoustic + longAcoustic + scrapeAcoustic;
    }

    /** Dual-axis saddle output: returns distinct vertical (pumping) and horizontal (rocking) forces. */
    void tickPolar(float& outV, float& outH, bool acousticBridgeOutput = true) noexcept
    {
        static_cast<void>(tick(acousticBridgeOutput));
        if (!acousticBridgeOutput)
        {
            outV = 0.0f;
            outH = 0.0f;
            return;
        }
        // outV is vertical plate pumping: V transverse + axial tension phantom + longitudinal snap + tactile scrape
        outV = saddleFilterState2 + saddleFilterPhantom * 0.35f + (longitudinalS1 * (p.woundFactor > 0.1f ? 0.22f : 0.14f)) + (lastScrapeForce * 0.85f);
        // outH is lateral rocking along saddle line
        outH = saddleFilterState2H;
    }

    /** Numerical sanity check on grid displacement (used in Electric mode when bridge force outputs are bypassed). */
    bool isStateValid() const noexcept
    {
        if (N_pts <= 0)
            return true;
        const float uMid = u_curr[static_cast<size_t>(N_pts / 2)];
        const float uEnd = u_curr[static_cast<size_t>(N_pts - 1)];
        return !(std::isnan(uMid) || std::isinf(uMid) || std::abs(uMid) > 1.0f ||
                 std::isnan(uEnd) || std::isinf(uEnd) || std::abs(uEnd) > 1.0f);
    }

    /** Fast string displacement energy estimate for voice stealing. */
    float getEnergy() const noexcept
    {
        float sumSq = 0.0f;
        for (int i = 0; i < N_pts; ++i)
            sumSq += u_curr[static_cast<size_t>(i)] * u_curr[static_cast<size_t>(i)];
        return sumSq * 1e6f; // Scale to ~1.0 range
    }

    /** Compute total discrete energy E^(d),n+1/2 for validation (Eq. 43-46, 62). */
    float computeDiscreteEnergy() const noexcept
    {
        // Kinetic string energy: (rhoA*h / 2) * ||D- u^{n+1}||^2
        float kineticString = 0.0f;
        for (int i = 0; i < N_pts; ++i)
        {
            const float v = (u_curr[static_cast<size_t>(i)] - u_prev[static_cast<size_t>(i)]) * invK;
            kineticString += v * v;
        }
        kineticString *= (rhoA * h * 0.5f);

        // Potential string energy (T0 and EI)
        float potT0 = 0.0f;
        for (int i = 0; i <= N_pts; ++i)
        {
            const float left  = (i == 0) ? 0.0f : u_curr[static_cast<size_t>(i - 1)];
            const float right = (i == N_pts) ? 0.0f : u_curr[static_cast<size_t>(i)];
            const float slope = (right - left) / h;
            potT0 += slope * slope;
        }
        potT0 *= (p.T0 * h * 0.5f);

        // Nonlinear potentials from quadratised SAV: 0.5 * psi^2
        const float potCollisions = 0.5f * (psi[0] * psi[0] + psi[1] * psi[1] + psi[2] * psi[2]);

        return kineticString + potT0 + potCollisions;
    }

    int getNumPoints() const noexcept { return N_pts; }
    float getGridSpacing() const noexcept { return h; }

    /**
     * Physical transverse string velocity extraction at distance from bridge (in meters).
     * Senses vertical velocity with subtle lateral coupling for electromagnetic induction.
     */
    float getPointVelocity(float distFromBridgeMeters) const noexcept
    {
        if (N_pts < 2 || distFromBridgeMeters <= 0.0f)
            return 0.0f;

        const float x = p.L - distFromBridgeMeters;
        if (x <= 0.0f || x >= p.L)
            return 0.0f;

        const float pos = (x * invH) - 1.0f;
        const int idx = std::clamp(static_cast<int>(pos), 0, N_pts - 2);
        const float alpha = std::clamp(pos - static_cast<float>(idx), 0.0f, 1.0f);

        const float u_c_V = (1.0f - alpha) * u_curr[static_cast<size_t>(idx)] + alpha * u_curr[static_cast<size_t>(idx + 1)];
        const float u_p_V = (1.0f - alpha) * u_prev[static_cast<size_t>(idx)] + alpha * u_prev[static_cast<size_t>(idx + 1)];
        const float vV = (u_c_V - u_p_V) * invK;

        float vH = 0.0f;
        if (!u_curr_H.empty() && static_cast<size_t>(idx + 1) < u_curr_H.size())
        {
            const float u_c_H = (1.0f - alpha) * u_curr_H[static_cast<size_t>(idx)] + alpha * u_curr_H[static_cast<size_t>(idx + 1)];
            const float u_p_H = (1.0f - alpha) * u_prev_H[static_cast<size_t>(idx)] + alpha * u_prev_H[static_cast<size_t>(idx + 1)];
            vH = (u_c_H - u_p_H) * invK;
        }

        return vV + 0.10f * vH;
    }

    /**
     * Electromagnetic pickup induction velocity.
     * For single-coil: point velocity.
     * For humbucker: dual pole piece velocity sum (creates the authentic ~3.6 kHz comb-filter notch).
     */
    float getPickupVelocity(float distFromBridgeMeters, bool isHumbucker, float poleSpacingMeters = 0.0185f) const noexcept
    {
        if (!isHumbucker)
        {
            return getPointVelocity(distFromBridgeMeters);
        }
        else
        {
            const float d1 = distFromBridgeMeters - 0.5f * poleSpacingMeters;
            const float d2 = distFromBridgeMeters + 0.5f * poleSpacingMeters;
            return 0.5f * (getPointVelocity(d1) + getPointVelocity(d2));
        }
    }

private:
    /** Reserve every grid-sized buffer for the worst case at this sample rate (may allocate). */
    void reserveCapacity()
    {
        // Upper bound on interior points: N <= L / (c k) = fs / (2 f0) for the lowest fundamental
        // (stiffness and sigma1 only enlarge h_min). Never below 512 so 44.1/48 kHz keep headroom.
        const size_t needed = std::max<size_t>(512, static_cast<size_t>(std::ceil(fs / (2.0f * kMinStringFreqHz))) + 8);
        if (gridCapacity >= needed)
            return;
        gridCapacity = needed;

        for (auto* v : { &u_next, &u_curr, &u_prev, &Du, &D2u, &Du_prev, &kn, &v_lin, &Bu_plus_Cu,
                         &gradVB, &gB, &gradVF, &gF, &gFG, &b_string,
                         &u_next_H, &u_curr_H, &u_prev_H, &Du_H, &D2u_H, &Du_prev_H,
                         &saved_curr, &saved_prev, &saved_curr_H, &saved_prev_H,
                         &sigma0_vec, &c0_vec, &c4_vec })
            v->reserve(gridCapacity);
        fretIndices.reserve(32);
        fretAlpha.reserve(32);
    }

    /**
     * Derive every quantity that depends on StringParams and the grid: material constants,
     * H-polarization tension, CFL grid, buffer sizes, fret/finger geometry, Woodbury scalings,
     * saddle filter and longitudinal resonator. Does not touch displacement state.
     * Audio-thread safe after reserveCapacity() (all resizes stay within reserved capacity).
     */
    void configureGrid() noexcept
    {
        initialSigma1 = p.sigma1;

        // Derived physical constants
        const float A = 3.14159265358979323846f * p.r * p.r;
        const float rBending = (p.r_stiffness > 0.0f) ? p.r_stiffness : p.r;
        const float I = 3.14159265358979323846f * rBending * rBending * rBending * rBending * 0.25f;
        rhoA = p.rho * A;
        EI   = p.E * I;
        EA   = p.E * A;

        // H-polarization tension: T0_H = T0_V * (1 + delta)^2 => f_H = f_V * (1 + delta),
        // producing the natural shimmer/beating (0.1..1.0 Hz) heard on real acoustic guitar.
        const float delta = std::clamp(p.polarizationSplit, 0.0f, 0.010f);
        T0_H = p.T0 * (1.0f + delta) * (1.0f + delta);

        // Stability condition (Bilbao DAFx24 Eq. 47), must hold for BOTH polarizations:
        const float maxT0 = std::max(p.T0, T0_H);
        const float term1 = (maxT0 * k) / rhoA + 4.0f * p.sigma1;
        const float hMinSq = (k * 0.5f) * (term1 + std::sqrt(term1 * term1 + (16.0f * EI) / rhoA));
        const float hMin   = std::sqrt(hMinSq);

        // N = floor(L / h_min). Capping N only enlarges h, which keeps the scheme stable.
        N = static_cast<int>(std::floor(p.L / hMin));
        N = std::clamp(N, 10, static_cast<int>(gridCapacity) + 1);
        h = p.L / static_cast<float>(N);
        N_pts = N - 1;

        const auto n = static_cast<size_t>(N_pts);
        for (auto* v : { &u_next, &u_curr, &u_prev, &Du, &D2u, &Du_prev, &kn, &v_lin, &Bu_plus_Cu,
                         &gradVB, &gB, &gradVF, &gF, &gFG, &b_string,
                         &u_next_H, &u_curr_H, &u_prev_H, &Du_H, &D2u_H, &Du_prev_H,
                         &sigma0_vec, &c0_vec, &c4_vec })
            v->resize(n, 0.0f);

        updateDampingProfile();

        // Bone saddle terminating mechanical impedance filter (brightness-dependent cutoff)
        const float saddleCutoff = std::min(3500.0f + 3500.0f * std::clamp(p.brightness, 0.05f, 1.0f), 0.45f * fs);
        saddleBeta = std::exp(-2.0f * 3.14159265358979323846f * saddleCutoff / fs);

        // Longitudinal string wave (P-wave compressional vibration), decays in ~20-30 ms
        const float cL = std::sqrt(p.E / p.rho);
        const float fL = std::clamp(cL / (2.0f * p.L), 300.0f, 4000.0f);
        const float omegaL = 2.0f * 3.14159265358979323846f * fL;
        const float decayTauL = 0.024f;
        longitudinalR  = std::exp(-1.0f / (decayTauL * fs));
        longitudinalB1 = -2.0f * longitudinalR * std::cos(omegaL / fs);
        longitudinalB2 = longitudinalR * longitudinalR;

        // Fret coordinates: only frets ahead of the fretting position towards the bridge
        const int maxFrets = 20;
        fretsM = std::max(0, maxFrets - p.fretNumber);
        fretIndices.resize(static_cast<size_t>(fretsM));
        fretAlpha.resize(static_cast<size_t>(fretsM));
        for (int q = 1; q <= fretsM; ++q)
        {
            const float xq = p.L * (1.0f - std::pow(2.0f, -static_cast<float>(q) / 12.0f));
            const float gridPos = xq / h;
            const int idx = std::clamp(static_cast<int>(gridPos) - 1, 0, N_pts - 2);
            fretIndices[static_cast<size_t>(q - 1)] = idx;
            fretAlpha[static_cast<size_t>(q - 1)]   = std::clamp(gridPos - static_cast<float>(idx + 1), 0.0f, 1.0f);
        }

        // Finger interpolator coordinate
        fingerIdx = std::clamp(static_cast<int>(p.xFG * p.L / h) - 1, 0, N_pts - 2);
        fingerAlpha = std::clamp((p.xFG * p.L / h) - static_cast<float>(fingerIdx + 1), 0.0f, 1.0f);

        // Diagonal scaling Lambda (Eq. 51) & cached tick() loop-invariant scalars
        lambdaString = (k * k) / (rhoA * h * (1.0f + p.sigma0 * k));
        lambdaFinger = (k * k) / p.MFG;

        invH        = 1.0f / h;
        invHSq      = invH * invH;
        invK        = 1.0f / k;
        lamBase     = (k * k) / (rhoA * h);
        c1          = (p.T0 * k * k) / rhoA;
        c1_H        = (T0_H * k * k) / rhoA;
        c2          = (EI * k * k) / rhoA;
        c3          = 2.0f * p.sigma1 * k;
        c1_plus_c3  = c1 + c3;
        c1H_plus_c3 = c1_H + c3;
        updateKcFactor();
    }

    void updateKcFactor() noexcept
    {
        kcFactor = (k * 0.5f) * std::sqrt((p.E * h) / (p.rho * p.L * (1.0f + p.sigma0 * k)));
    }

    void computeLaplacian(const float* FDTD_RESTRICT u, float* FDTD_RESTRICT outDu) const noexcept
    {
        const int nPts = N_pts;
        const float ih2 = invHSq;
        if (nPts >= 2)
        {
            outDu[0] = (-2.0f * u[0] + u[1]) * ih2;
            for (int i = 1; i < nPts - 1; ++i)
                outDu[i] = (u[i - 1] - 2.0f * u[i] + u[i + 1]) * ih2;
            outDu[nPts - 1] = (u[nPts - 2] - 2.0f * u[nPts - 1]) * ih2;
        }
        else if (nPts == 1)
        {
            outDu[0] = (-2.0f * u[0]) * ih2;
        }
    }

    /**
     * Compute spatial damping profile sigma0(x) and per-node coefficients c0[i], c4[i].
     * First-principles modeling of palm muting (localized bridge damping) and
     * dead notes (broad fretting-hand neck damping).
     * Audio-thread safe: zero heap allocations (operates on pre-resized vectors).
     */
    void updateDampingProfile() noexcept
    {
        if (N_pts <= 0 || sigma0_vec.size() < static_cast<size_t>(N_pts))
            return;

        if (currentMuteMode == MuteMode::Palm)
        {
            // Palm Muting: localized fleshy palm contact across the last 5.5-7.5 cm near bridge
            const float palmLength = std::min(0.065f, 0.22f * p.L);
            const float xPalmStart = p.L - palmLength;
            const float palmDampingRate = 480.0f + 360.0f * (1.0f - currentDecay);

            for (int i = 0; i < N_pts; ++i)
            {
                const float x = static_cast<float>(i + 1) * h;
                if (x <= xPalmStart)
                {
                    sigma0_vec[static_cast<size_t>(i)] = p.sigma0;
                }
                else
                {
                    const float frac = (x - xPalmStart) / (p.L - xPalmStart);
                    const float w = 0.5f * (1.0f - std::cos(3.14159265358979323846f * frac));
                    sigma0_vec[static_cast<size_t>(i)] = p.sigma0 + palmDampingRate * w;
                }
            }
        }
        else if (currentMuteMode == MuteMode::Full)
        {
            // Dead Notes ("X" notes): broad spatial damping patch across the entire neck (frets 1 to 14)
            // Starts right at the nut boundary (x1 = 0.02L) to eliminate any short undamped high-frequency ringing
            const float x1 = 0.02f * p.L;
            const float x2 = 0.68f * p.L;
            constexpr float handDampingRate = 850.0f;

            for (int i = 0; i < N_pts; ++i)
            {
                const float x = static_cast<float>(i + 1) * h;
                if (x >= x1 && x <= x2)
                {
                    const float frac = (x - x1) / (x2 - x1);
                    const float w = std::sin(3.14159265358979323846f * frac);
                    sigma0_vec[static_cast<size_t>(i)] = p.sigma0 + handDampingRate * w;
                }
                else
                {
                    sigma0_vec[static_cast<size_t>(i)] = p.sigma0;
                }
            }
        }
        else
        {
            // Open String: uniform intrinsic material loss
            for (int i = 0; i < N_pts; ++i)
            {
                sigma0_vec[static_cast<size_t>(i)] = p.sigma0;
            }
        }

        // Precompute coefficients c0[i] and c4[i]
        for (int i = 0; i < N_pts; ++i)
        {
            const float s0 = sigma0_vec[static_cast<size_t>(i)];
            c0_vec[static_cast<size_t>(i)] = 1.0f / (1.0f + s0 * k);
            c4_vec[static_cast<size_t>(i)] = s0 * k - 1.0f;
        }
    }

    /** Fast 4x4 linear equation solver using Gaussian elimination with partial pivoting. */
    static std::array<float, 4> solve4x4(std::array<std::array<float, 4>, 4> a, std::array<float, 4> b) noexcept
    {
        for (int i = 0; i < 4; ++i)
        {
            // Find pivot
            int maxRow = i;
            for (int k = i + 1; k < 4; ++k)
                if (std::abs(a[static_cast<size_t>(k)][static_cast<size_t>(i)]) > std::abs(a[static_cast<size_t>(maxRow)][static_cast<size_t>(i)]))
                    maxRow = k;

            std::swap(a[static_cast<size_t>(i)], a[static_cast<size_t>(maxRow)]);
            std::swap(b[static_cast<size_t>(i)], b[static_cast<size_t>(maxRow)]);

            const float pivot = a[static_cast<size_t>(i)][static_cast<size_t>(i)];
            if (std::abs(pivot) < 1e-12f) continue;

            for (int k = i + 1; k < 4; ++k)
            {
                const float factor = a[static_cast<size_t>(k)][static_cast<size_t>(i)] / pivot;
                b[static_cast<size_t>(k)] -= factor * b[static_cast<size_t>(i)];
                for (int j = i; j < 4; ++j)
                    a[static_cast<size_t>(k)][static_cast<size_t>(j)] -= factor * a[static_cast<size_t>(i)][static_cast<size_t>(j)];
            }
        }

        // Back-substitution
        std::array<float, 4> x { 0.0f, 0.0f, 0.0f, 0.0f };
        for (int i = 3; i >= 0; --i)
        {
            float sum = 0.0f;
            for (int j = i + 1; j < 4; ++j)
                sum += a[static_cast<size_t>(i)][static_cast<size_t>(j)] * x[static_cast<size_t>(j)];
            const float denom = a[static_cast<size_t>(i)][static_cast<size_t>(i)];
            x[static_cast<size_t>(i)] = (std::abs(denom) > 1e-12f) ? ((b[static_cast<size_t>(i)] - sum) / denom) : 0.0f;
        }
        return x;
    }

    float fs = 44100.0f;
    float k  = 1.0f / 44100.0f;
    StringParams p;

    float rhoA = 0.0f;
    float EI   = 0.0f;
    float EA   = 0.0f;

    int N      = 0;
    int N_pts  = 0;
    float h    = 0.0f;

    // Cached loop-invariant coefficients (updated in configureGrid / setDamping)
    float invH        = 0.0f;
    float invHSq      = 0.0f;
    float invK        = 0.0f;
    float lamBase     = 0.0f;
    float c1          = 0.0f;
    float c1_H        = 0.0f;
    float c2          = 0.0f;
    float c3          = 0.0f;
    float c1_plus_c3  = 0.0f;
    float c1H_plus_c3 = 0.0f;
    float kcFactor    = 0.0f;

    // Displacement states
    std::vector<float> u_next;
    std::vector<float> u_curr;
    std::vector<float> u_prev;

    float w_next = 0.0f;
    float w_curr = 0.0f;
    float w_prev = 0.0f;

    // Auxiliary variables
    std::array<float, 3> psi { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> psi_prev { 0.0f, 0.0f, 0.0f };
    float prevXiB = 0.0f;
    float prevXiF = 0.0f;
    float prevRawForce = 0.0f;
    float dcBlockerState = 0.0f;
    float initialSigma1 = 1.3e-4f;

    // Pre-allocated scratch buffers (zero heap allocations in tick)
    std::vector<float> Du;
    std::vector<float> D2u;
    std::vector<float> Du_prev;
    std::vector<float> kn;
    std::vector<float> v_lin;
    std::vector<float> Bu_plus_Cu;
    std::vector<float> gradVB;
    std::vector<float> gB;
    std::vector<float> gradVF;
    std::vector<float> gF;
    std::vector<float> gFG;
    std::vector<float> b_string;
    std::vector<float> saved_curr;
    std::vector<float> saved_prev;
    std::vector<float> saved_curr_H;
    std::vector<float> saved_prev_H;
    std::vector<float> sigma0_vec;
    std::vector<float> c0_vec;
    std::vector<float> c4_vec;
    MuteMode currentMuteMode = MuteMode::Open;
    float currentDecay = 0.80f;
    size_t gridCapacity = 0;

    float saddleBeta = 0.0f;
    float saddleFilterState = 0.0f;
    float saddleFilterState2 = 0.0f;
    int rampSamplesLeft = 0;
    int rampTotalSamples = 32;

    float lambdaString = 0.0f;
    float lambdaFinger = 0.0f;

    // Dynamic Nonlinear Plectrum Contact Model (v71.f)
    bool  pickContactActive   = false;
    int   pickContactIdx      = 0;
    float pickContactWeights[3] { 0.25f, 0.50f, 0.25f };
    float pickTargetY         = 0.0f;
    float pickCurrentY        = 0.0f;
    float pickVelocity        = 0.0f;
    float pickKp              = 24000.0f;
    float pickRp              = 1.35f;
    float pickSp              = 0.0f;
    float pickSlipForce       = 1.2f;
    float pickEtaPrev         = 0.0f;
    float pickCosTheta        = 0.866f;
    float pickSinTheta        = 0.500f;
    float pickRidgePhase      = 0.0f;
    float pickRidgePhaseInc   = 0.0f;
    float pickRidgeAmp        = 0.0f;
    int   pickRidgeDurSamples = 0;
    int   pickMaxSamples      = 0;
    int   pickElapsedSamples  = 0;

    // Pluck parameters
    int pluckIdx = 0;
    float pluckForceAmp = 0.0f;
    int pluckDurSamples = 0;
    int pluckSampleCount = 0;

    // Fret geometry
    int fretsM = 20;
    std::vector<int> fretIndices;
    std::vector<float> fretAlpha;

    // Finger geometry
    int fingerIdx = 0;
    float fingerAlpha = 0.0f;
    bool fingerEngaged = false;

    float accumulatedLoss = 0.0f;

    // H-polarization state (horizontal, parallel to soundboard)
    // Runs a simplified free stiff string + KC (no obstacle collisions) at a slightly
    // offset tension T0_H, producing natural shimmer/beating against the V polarization.
    float T0_H = 0.0f;
    std::vector<float> u_next_H;
    std::vector<float> u_curr_H;
    std::vector<float> u_prev_H;
    std::vector<float> Du_H;
    std::vector<float> D2u_H;
    std::vector<float> Du_prev_H;
    float dcBlockerStateH   = 0.0f;
    float prevRawForceH     = 0.0f;
    float saddleFilterStateH  = 0.0f;
    float saddleFilterState2H = 0.0f;

    // Longitudinal string physics state (Phantom Harmonics & P-wave attack snap)
    float longitudinalB1 = 0.0f;
    float longitudinalB2 = 0.0f;
    float longitudinalR  = 0.0f;
    float longitudinalS1 = 0.0f;
    float longitudinalS2 = 0.0f;
    float longitudinalImpulse = 0.0f;
    float dcBlockerStatePhantom = 0.0f;
    float prevRawForcePhantom = 0.0f;
    float saddleFilterPhantom = 0.0f;

    // Tactile fingernail contact scrape & flesh thump transient
    int scrapeSamplesLeft = 0;
    int scrapeTotalSamples = 0;
    float scrapeAmp = 0.0f;
    float scrapeFilterState = 0.0f;
    uint32_t scrapeRng = 0x12345678u;
    float lastScrapeForce = 0.0f;
};
