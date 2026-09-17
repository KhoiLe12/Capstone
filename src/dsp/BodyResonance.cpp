#include "BodyResonance.h"

// ---------------------------------------------------------------------------
// Guitar body resonance mode table
// ---------------------------------------------------------------------------
// Source: literature-grounded estimates for a typical spruce-top steel-string
// acoustic guitar (Rossing, 2010; Taylor & Gill, 1993).
//
// Columns: centre frequency (Hz), Q, gain (dB)
//
// Gains are kept modest (1–4 dB) so that a series chain of 8 filters
// produces natural tonal colouring rather than harsh resonance peaks.
// ---------------------------------------------------------------------------

struct ModeSpec { float freqHz; float Q; float gainDB; };

static constexpr ModeSpec kGuitarModes[BodyResonance::N_MODES] =
{
    //  freq    Q    gain   description
    {  105.f,  8.f,  4.f },  // A0   — Helmholtz / soundhole air resonance
    {  185.f,  6.f,  4.f },  // T(1,1) lower — main top-plate resonance, low peak
    {  220.f,  6.f,  3.f },  // T(1,1) upper — main top-plate, upper peak
    {  340.f,  5.f,  2.f },  // T(2,1) — cross-dipole top plate
    {  490.f,  4.f,  2.f },  // L(1,1) — longitudinal plate mode
    {  620.f,  4.f,  1.5f},  // T(3,1) — higher-order top mode
    {  900.f,  3.f,  1.f },  // upper body formant
    { 1250.f,  3.f,  1.f },  // brightness / presence region
};

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------

void BodyResonance::init(float sampleRate)
{
    for (int i = 0; i < N_MODES; ++i)
        filters[i].setResonator(kGuitarModes[i].freqHz,
                                kGuitarModes[i].Q,
                                kGuitarModes[i].gainDB,
                                sampleRate);
}

// ---------------------------------------------------------------------------
// Per-sample processing
// ---------------------------------------------------------------------------

float BodyResonance::process(float input) noexcept
{
    // Run the signal through all biquads in series
    float wet = input;
    for (int i = 0; i < N_MODES; ++i)
        wet = filters[i].process(wet);

    return dryMix * input + wetMix * wet;
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

void BodyResonance::reset() noexcept
{
    for (auto& f : filters)
        f.reset();
}

