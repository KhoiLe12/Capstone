#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <array>
#include <iostream>

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
        float rho     = 1140.0f;      ///< Density (kg/m^3) (nylon core with silver-plated wrap)
        float E       = 5.4e9f;       ///< Young's modulus (Pa)
        float T0      = 60.0f;        ///< Tension (N)
        float sigma0  = 1.38f;        ///< Frequency-independent damping (1/s)
        float sigma1  = 1.3e-4f;      ///< Frequency-dependent air/viscoelastic loss (m^2/s)

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

    FdtdString() = default;

    /** Configure spatial grid, stability bounds, and allocate buffers. */
    void init(float sampleRateHz)
    {
        init(sampleRateHz, StringParams());
    }

    void init(float sampleRateHz, const StringParams& params)
    {
        fs = sampleRateHz;
        k  = 1.0f / fs;
        p  = params;

        // Derived physical constants
        const float A = 3.14159265358979323846f * p.r * p.r;
        const float I = 3.14159265358979323846f * p.r * p.r * p.r * p.r * 0.25f;
        rhoA = p.rho * A;
        EI   = p.E * I;
        EA   = p.E * A;

        // Stability condition (Bilbao DAFx24 Eq. 47):
        // h^2 >= (k/2) * ( T0*k / rhoA + 4*sigma1 + sqrt( (T0*k / rhoA + 4*sigma1)^2 + 16*EI / rhoA ) )
        const float term1 = (p.T0 * k) / rhoA + 4.0f * p.sigma1;
        const float hMinSq = (k * 0.5f) * (term1 + std::sqrt(term1 * term1 + (16.0f * EI) / rhoA));
        const float hMin   = std::sqrt(hMinSq);

        // Grid spacing h: Choose integer N = floor(L / h_min)
        N = static_cast<int>(std::floor(p.L / hMin));
        if (N < 10) N = 10;
        h = p.L / static_cast<float>(N);

        // Ensure vector capacity is pre-reserved to eliminate audio thread heap allocations
        constexpr size_t kMaxGridSize = 256;
        if (u_next.capacity() < kMaxGridSize)
        {
            u_next.reserve(kMaxGridSize);
            u_curr.reserve(kMaxGridSize);
            u_prev.reserve(kMaxGridSize);
            Du.reserve(kMaxGridSize);
            D2u.reserve(kMaxGridSize);
            Du_prev.reserve(kMaxGridSize);
            kn.reserve(kMaxGridSize);
            v_lin.reserve(kMaxGridSize);
            Bu_plus_Cu.reserve(kMaxGridSize);
            gradVB.reserve(kMaxGridSize);
            gB.reserve(kMaxGridSize);
            gradVF.reserve(kMaxGridSize);
            gF.reserve(kMaxGridSize);
            gFG.reserve(kMaxGridSize);
            b_string.reserve(kMaxGridSize);
            fretIndices.reserve(32);
            fretAlpha.reserve(32);
        }

        // States
        N_pts = N - 1; // interior points
        u_next.assign(static_cast<size_t>(N_pts), 0.0f);
        u_curr.assign(static_cast<size_t>(N_pts), 0.0f);
        u_prev.assign(static_cast<size_t>(N_pts), 0.0f);

        // Pre-allocate scratch vectors for zero heap allocations in tick()
        Du.assign(static_cast<size_t>(N_pts), 0.0f);
        D2u.assign(static_cast<size_t>(N_pts), 0.0f);
        Du_prev.assign(static_cast<size_t>(N_pts), 0.0f);
        kn.assign(static_cast<size_t>(N_pts), 0.0f);
        v_lin.assign(static_cast<size_t>(N_pts), 0.0f);
        Bu_plus_Cu.assign(static_cast<size_t>(N_pts), 0.0f);
        gradVB.assign(static_cast<size_t>(N_pts), 0.0f);
        gB.assign(static_cast<size_t>(N_pts), 0.0f);
        gradVF.assign(static_cast<size_t>(N_pts), 0.0f);
        gF.assign(static_cast<size_t>(N_pts), 0.0f);
        gFG.assign(static_cast<size_t>(N_pts), 0.0f);
        b_string.assign(static_cast<size_t>(N_pts), 0.0f);

        // Saddle terminating mechanical impedance filter (4.5 kHz lowpass)
        saddleBeta = std::exp(-2.0f * 3.14159265358979323846f * 4500.0f / fs);
        saddleFilterState = 0.0f;

        // Finger states
        w_next = 0.0f;
        w_curr = 0.0f;
        w_prev = 0.0f;

        // Auxiliary variables: [psi_B, psi_F, psi_FG]
        psi = { 0.0f, 0.0f, 0.0f };
        psi_prev = { 0.0f, 0.0f, 0.0f };

        // Fret coordinates: only frets ahead of the fretting position towards the bridge
        const int maxFrets = 20;
        const int activeFrets = std::max(0, maxFrets - p.fretNumber);
        fretsM = activeFrets;
        fretIndices.resize(static_cast<size_t>(fretsM));
        fretAlpha.resize(static_cast<size_t>(fretsM));
        for (int q = 1; q <= fretsM; ++q)
        {
            const float xq = p.L * (1.0f - std::pow(2.0f, -static_cast<float>(q) / 12.0f));
            const float gridPos = xq / h;
            const int idx = static_cast<int>(gridPos) - 1;
            fretIndices[static_cast<size_t>(q - 1)] = std::clamp(idx, 0, N_pts - 2);
            fretAlpha[static_cast<size_t>(q - 1)]   = gridPos - static_cast<float>(idx + 1);
        }

        // Finger interpolator coordinate
        fingerIdx = std::clamp(static_cast<int>(p.xFG * p.L / h) - 1, 0, N_pts - 2);
        fingerAlpha = (p.xFG * p.L / h) - static_cast<float>(fingerIdx + 1);

        // Diagonal scaling Lambda (Eq. 51):
        // Lambda_string = k^2 / (rhoA * h * (1 + sigma0*k))
        lambdaString = (k * k) / (rhoA * h * (1.0f + p.sigma0 * k));
        lambdaFinger = (k * k) / p.MFG;

        reset();
    }

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
        saddleFilterState = 0.0f;
        rampSamplesLeft = 0;
    }

    void setFingerEngaged(bool engaged) noexcept { fingerEngaged = engaged; }
    bool isFingerEngaged() const noexcept { return fingerEngaged; }

    /** Excite string by releasing from static triangular pluck displacement (authentic acoustic pluck). */
    void pluck(float xLocFraction, float forceNewtons)
    {
        const float xLoc = std::clamp(xLocFraction, 0.05f, 0.95f) * p.L;
        // Peak displacement from statics: u_peak = F * x * (L - x) / (T0 * L)
        const float uPeak = (forceNewtons * xLoc * (p.L - xLoc)) / (p.T0 * p.L);
        pluckDisplacement(xLocFraction, uPeak);
    }

    /** Excite string with specified peak displacement in meters. */
    void pluckDisplacement(float xLocFraction, float peakDisplacementMeters)
    {
        const float xLoc = std::clamp(xLocFraction, 0.05f, 0.95f) * p.L;
        for (int i = 0; i < N_pts; ++i)
        {
            const float x = static_cast<float>(i + 1) * h;
            if (x <= xLoc)
                u_curr[static_cast<size_t>(i)] = peakDisplacementMeters * (x / xLoc);
            else
                u_curr[static_cast<size_t>(i)] = peakDisplacementMeters * ((p.L - x) / (p.L - xLoc));
        }
        u_prev = u_curr;
        w_curr = 0.0f;
        w_prev = 0.0f;
        psi = { 0.0f, 0.0f, 0.0f };
        psi_prev = { 0.0f, 0.0f, 0.0f };
        prevXiB = 0.0f;
        prevXiF = 0.0f;
        prevRawForce = u_curr[static_cast<size_t>(N_pts - 1)] / h;
        dcBlockerState = 0.0f;
        saddleFilterState = 0.0f;
        rampSamplesLeft = 32;
        rampTotalSamples = 32;
        pluckDurSamples = 0;
        pluckSampleCount = 0;
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
    float tick() noexcept
    {
        // 1. Current external plucking force f_e^n (Eq. 5)
        float fe = 0.0f;
        if (pluckSampleCount < pluckDurSamples && pluckDurSamples > 0)
        {
            const float phase = 3.14159265358979323846f * static_cast<float>(pluckSampleCount) / static_cast<float>(pluckDurSamples);
            const float s = std::sin(phase);
            fe = pluckForceAmp * s * s;
            pluckSampleCount++;
        }

        // 2. Compute spatial differences on u_curr: D*u and D^2*u
        // D is tridiagonal: Du[i] = (u[i+1] - 2*u[i] + u[i-1]) / h^2
        const float invHSq = 1.0f / (h * h);

        for (int i = 0; i < N_pts; ++i)
        {
            const float left  = (i > 0) ? u_curr[static_cast<size_t>(i - 1)] : 0.0f;
            const float right = (i < N_pts - 1) ? u_curr[static_cast<size_t>(i + 1)] : 0.0f;
            Du[static_cast<size_t>(i)] = (left - 2.0f * u_curr[static_cast<size_t>(i)] + right) * invHSq;
        }

        // Biharmonic D^2*u
        for (int i = 0; i < N_pts; ++i)
        {
            const float left  = (i > 0) ? Du[static_cast<size_t>(i - 1)] : 0.0f;
            const float right = (i < N_pts - 1) ? Du[static_cast<size_t>(i + 1)] : 0.0f;
            D2u[static_cast<size_t>(i)] = (left - 2.0f * Du[static_cast<size_t>(i)] + right) * invHSq;
        }

        // Spatial difference on u_prev for damping term D*u_prev
        for (int i = 0; i < N_pts; ++i)
        {
            const float left  = (i > 0) ? u_prev[static_cast<size_t>(i - 1)] : 0.0f;
            const float right = (i < N_pts - 1) ? u_prev[static_cast<size_t>(i + 1)] : 0.0f;
            Du_prev[static_cast<size_t>(i)] = (left - 2.0f * u_prev[static_cast<size_t>(i)] + right) * invHSq;
        }

        // 3. Kirchhoff-Carrier dynamic tension vector k^n (Eq. 52):
        // k^n = (k / 2) * sqrt(E*h / (rho*L*(1 + sigma0*k))) * [Du^T, 0]^T
        const float kcFactor = (k * 0.5f) * std::sqrt((p.E * h) / (p.rho * p.L * (1.0f + p.sigma0 * k)));
        float knDotZprev = 0.0f;
        for (int i = 0; i < N_pts; ++i)
        {
            kn[static_cast<size_t>(i)] = kcFactor * Du[static_cast<size_t>(i)];
            knDotZprev += kn[static_cast<size_t>(i)] * u_prev[static_cast<size_t>(i)];
        }

        // 4. Compute Non-linear Potentials and Gradients (SAV, Eq. 54-55)
        // Pre-compute unconstrained linear update: v_lin = Bu^n + Cu^{n-1} - u^{n-1}
        const float c0 = 1.0f / (1.0f + p.sigma0 * k);
        const float c1 = (p.T0 * k * k) / rhoA;
        const float c2 = (EI * k * k) / rhoA;
        const float c3 = 2.0f * p.sigma1 * k;
        const float c4 = (p.sigma0 * k - 1.0f);

        for (int i = 0; i < N_pts; ++i)
        {
            const float Bu_i = c0 * (2.0f * u_curr[static_cast<size_t>(i)] + c1 * Du[static_cast<size_t>(i)] - c2 * D2u[static_cast<size_t>(i)] + c3 * Du[static_cast<size_t>(i)]);
            const float Cu_i = c0 * (c4 * u_prev[static_cast<size_t>(i)] - c3 * Du_prev[static_cast<size_t>(i)]);
            Bu_plus_Cu[static_cast<size_t>(i)] = Bu_i + Cu_i;
            v_lin[static_cast<size_t>(i)] = Bu_i + Cu_i - u_prev[static_cast<size_t>(i)];
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
        float kn_dot_kn = 0.0f;

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

            kn_dot_kn   += k_val * k_val;
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

        // 9. Audio output drawn at the bridge boundary x = L (penultimate interior point)
        // Force on the bridge saddle is proportional to spatial slope: u[N-1] / h
        float bridgeForce = (u_next[static_cast<size_t>(N_pts - 1)]) / h;

        // Smooth initial attack ramp (32 samples = 0.7 ms) to eliminate static step click
        if (rampSamplesLeft > 0)
        {
            const int idx = rampTotalSamples - rampSamplesLeft;
            const float ramp = 0.5f * (1.0f - std::cos(3.14159265358979323846f * static_cast<float>(idx) / static_cast<float>(rampTotalSamples)));
            bridgeForce *= ramp;
            rampSamplesLeft--;
        }

        // Bone saddle 4.5 kHz terminating mechanical impedance lowpass filter
        saddleFilterState = (1.0f - saddleBeta) * bridgeForce + saddleBeta * saddleFilterState;

        // 10. Pointer / State rotation
        u_prev = u_curr;
        u_curr = u_next;
        w_prev = w_curr;
        w_curr = w_next;

        return saddleFilterState;
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
        const float invK = 1.0f / k;
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

private:
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

    float saddleBeta = 0.0f;
    float saddleFilterState = 0.0f;
    int rampSamplesLeft = 0;
    int rampTotalSamples = 32;

    float lambdaString = 0.0f;
    float lambdaFinger = 0.0f;

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
};
