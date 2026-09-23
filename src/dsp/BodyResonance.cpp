#include "BodyResonance.h"
#include <cmath>
#include <algorithm>

// ---------------------------------------------------------------------------
// 32-Mode Acoustic Guitar Body Dataset
// ---------------------------------------------------------------------------
// Literature-grounded modal eigenfrequencies, Q factors, and coupling weights
// for a master-built spruce/rosewood acoustic guitar (Rossing 2010; Fletcher 1998).
// ---------------------------------------------------------------------------

struct ModalSpec
{
    float freqHz;
    float Q;
    float amp;
};

static constexpr ModalSpec kModalDataset[BodyResonance::N_MODES] =
{
    // Air cavity & fundamental soundboard modes
    {  105.f, 15.f, 1.00f }, // A0: Helmholtz air resonance (soundhole pumping)
    {  185.f, 22.f, 1.20f }, // T(1,1)_L: Main top plate lower dipole (bass warmth)
    {  205.f, 14.f, 0.50f }, // A1: First internal air cavity longitudinal mode
    {  225.f, 25.f, 1.40f }, // T(1,1)_U: Main top plate upper dipole (core body thump)
    {  260.f, 18.f, 0.80f }, // B(1,1): Back plate fundamental coupling
    {  340.f, 20.f, 0.90f }, // T(2,1): Cross-dipole top plate mode
    {  390.f, 12.f, 0.40f }, // A2: Second cavity air mode
    {  440.f, 16.f, 0.70f }, // T(3,1): Tripole soundboard mode

    // Soundboard longitudinal & mid-body modes
    {  490.f, 18.f, 0.75f }, // L(1,1): Longitudinal wood grain mode
    {  550.f, 16.f, 0.60f }, // Mid soundboard flexure
    {  620.f, 15.f, 0.55f }, // Ribs & waist coupling
    {  710.f, 14.f, 0.50f }, // Soundboard higher order
    {  810.f, 14.f, 0.45f }, // Soundboard higher order
    {  920.f, 12.f, 0.40f }, // Upper body formant
    { 1050.f, 12.f, 0.40f }, // Upper body formant
    { 1180.f, 11.f, 0.35f }, // Upper wood resonance

    // Presence & bridge radiation formants
    { 1320.f, 11.f, 0.35f }, // Soundboard presence mode
    { 1470.f, 10.f, 0.30f }, // Soundboard presence mode
    { 1640.f, 10.f, 0.30f }, // High presence formant
    { 1820.f,  9.f, 0.25f }, // High presence formant
    { 2020.f,  9.f, 0.25f }, // Treble wood formant
    { 2240.f,  8.f, 0.20f }, // Treble wood formant
    { 2480.f,  8.f, 0.20f }, // Bridge sparkle formant
    { 2740.f,  8.f, 0.18f }, // Bridge sparkle formant

    // Air shimmer & high-frequency wood sheen
    { 3020.f,  7.f, 0.15f }, // Air shimmer mode
    { 3320.f,  7.f, 0.15f }, // Air shimmer mode
    { 3650.f,  6.f, 0.12f }, // High harmonic formant
    { 4010.f,  6.f, 0.10f }, // High harmonic formant
    { 4400.f,  6.f, 0.08f }, // Top sheen formant
    { 4820.f,  5.f, 0.07f }, // Top sheen formant
    { 5270.f,  5.f, 0.05f }, // High frequency wood loss
    { 5750.f,  5.f, 0.04f }  // High frequency wood loss
};

// ---------------------------------------------------------------------------
// Lifecycle & Parameter Updates
// ---------------------------------------------------------------------------

void BodyResonance::init(float sr, float bodySize, float bodyDamping)
{
    sampleRate      = sr;
    currentSize     = std::max(0.5f, std::min(bodySize, 2.0f));
    currentDamping  = std::max(0.2f, std::min(bodyDamping, 3.0f));
    updateFilters();
    reset();
}

void BodyResonance::setParameters(float bodySize, float bodyDamping, float bodyCoupling)
{
    const float clampedSize     = std::max(0.5f, std::min(bodySize, 2.0f));
    const float clampedDamping  = std::max(0.2f, std::min(bodyDamping, 3.0f));
    currentCoupling             = std::max(0.0f, std::min(bodyCoupling, 1.0f));

    // Only redesign filter coefficients if frequency or Q scaling changed
    if (std::abs(clampedSize - currentSize) > 0.005f ||
        std::abs(clampedDamping - currentDamping) > 0.005f)
    {
        currentSize    = clampedSize;
        currentDamping = clampedDamping;
        updateFilters();
    }
}

void BodyResonance::updateFilters()
{
    // Normalization scale factor for 32 parallel resonators
    constexpr float kNorm = 0.08f;

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
// Processing
// ---------------------------------------------------------------------------

float BodyResonance::process(float bridgeForce) noexcept
{
    // Compute parallel summation across all 32 physical eigenmodes
    float modalSum = 0.f;
    for (int i = 0; i < N_MODES; ++i)
    {
        modalSum += filters[i].process(bridgeForce);
    }

    // Direct bridge force plus radiated acoustic soundboard eigenmode resonance
    return bridgeForce + modalSum * 1.6f;
}

void BodyResonance::reset() noexcept
{
    for (auto& f : filters)
        f.reset();
}
