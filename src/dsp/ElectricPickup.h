#pragma once
#include "BiquadFilter.h"
#include <cmath>
#include <algorithm>

/**
 * ElectricPickup — First-principles electromagnetic guitar pickup physics & passive RLC circuitry.
 *
 * Models:
 *  1. Exact factory blueprint geometries:
 *     - 1962 Fender Stratocaster (25.5" / 647.7 mm scale length):
 *         * Bridge pickup: slanted (47.6 mm bass / 41.3 mm treble from bridge)
 *         * Middle pickup: 98.4 mm from bridge
 *         * Neck pickup: 146.0 mm from bridge
 *         * Single-coil Alnico V pole pieces (fine aperture)
 *     - 1959 Gibson Les Paul (24.75" / 628.65 mm scale length):
 *         * Bridge PAF humbucker: center 42.0 mm from bridge
 *         * Neck PAF humbucker: center 155.0 mm from bridge
 *         * Dual-pole spacing: 18.5 mm (produces the classic ~3.6 kHz humbucker comb-filter phase cancellation notch)
 *  2. Passive RLC Guitar Tone Circuit:
 *     - Second-order resonant lowpass filter formed by pickup coil inductance (L), resistance (R),
 *       cable capacitance (C_cable ~500 pF), and tone pot / capacitor sweep.
 *     - Strat: L ~ 2.2 H, R ~ 5.9 kOhm, 250 kOhm pots, 47 nF cap -> fres sweeps 4.2 kHz (Q=2.6) down to 450 Hz
 *     - Les Paul: L ~ 4.4 H, R ~ 7.8 kOhm, 500 kOhm pots, 22 nF cap -> fres sweeps 2.8 kHz (Q=1.9) down to 400 Hz
 */
namespace ElectricGuitar
{
    enum class GuitarModel
    {
        Stratocaster1962 = 0,
        LesPaul1959 = 1
    };

    enum class PickupPosition
    {
        Bridge = 0,
        BridgeMiddle = 1,
        Middle = 2,
        NeckMiddle = 3,
        Neck = 4,
        BridgeNeck = 5
    };

    enum class PickupType
    {
        SingleCoil = 0,
        Humbucker = 1
    };

    struct Blueprint
    {
        float scaleLengthMeters;
        float solidBodySigma0;   ///< Base damping for solid body (~0.18 s^-1, extended sustain)
    };

    inline Blueprint getBlueprint(GuitarModel model) noexcept
    {
        if (model == GuitarModel::LesPaul1959)
        {
            return { 0.62865f, 0.16f }; // 24.75" Gibson scale, heavy mahogany solid body
        }
        return { 0.64770f, 0.20f };     // 25.50" Fender scale, alder/ash solid body
    }

    /**
     * Compute pickup sampling distances from bridge in meters for a given physical string (0=low E, 5=high E).
     * Returns up to 2 active pickup positions (for parallel combinations) and whether each is a humbucker.
     */
    struct PickupSampleConfig
    {
        int   numActivePickups = 1;
        float distBridge1 = 0.042f;
        bool  isHumbucker1 = false;
        float poleSpacing1 = 0.0185f;

        float distBridge2 = 0.0f;
        bool  isHumbucker2 = false;
        float poleSpacing2 = 0.0185f;

        float mixGain = 1.0f; ///< 1.0 for single pickup, ~0.707 for parallel coil loading
    };

    inline PickupSampleConfig getPickupConfig(GuitarModel model, PickupPosition pos, int stringIdx, int pickupTypeOverride = -1) noexcept
    {
        PickupSampleConfig cfg;
        const int s = std::clamp(stringIdx, 0, 5);

        if (model == GuitarModel::Stratocaster1962)
        {
            // Slanted bridge pickup: 47.6 mm (bass E) down to 41.3 mm (high E)
            const float bridgeDist = 0.0476f - static_cast<float>(s) * ((0.0476f - 0.0413f) / 5.0f);
            const float middleDist = 0.0984f;
            const float neckDist   = 0.1460f;
            const bool isHum = (pickupTypeOverride == 1);

            switch (pos)
            {
                case PickupPosition::Bridge:
                    cfg.numActivePickups = 1;
                    cfg.distBridge1 = bridgeDist;
                    cfg.isHumbucker1 = isHum;
                    cfg.mixGain = 1.0f;
                    break;

                case PickupPosition::BridgeMiddle:
                    cfg.numActivePickups = 2;
                    cfg.distBridge1 = bridgeDist;
                    cfg.distBridge2 = middleDist;
                    cfg.isHumbucker1 = isHum;
                    cfg.isHumbucker2 = isHum;
                    cfg.mixGain = 0.707f; // In-parallel quack
                    break;

                case PickupPosition::Middle:
                    cfg.numActivePickups = 1;
                    cfg.distBridge1 = middleDist;
                    cfg.isHumbucker1 = isHum;
                    cfg.mixGain = 1.0f;
                    break;

                case PickupPosition::NeckMiddle:
                    cfg.numActivePickups = 2;
                    cfg.distBridge1 = neckDist;
                    cfg.distBridge2 = middleDist;
                    cfg.isHumbucker1 = isHum;
                    cfg.isHumbucker2 = isHum;
                    cfg.mixGain = 0.707f; // In-parallel quack
                    break;

                case PickupPosition::Neck:
                    cfg.numActivePickups = 1;
                    cfg.distBridge1 = neckDist;
                    cfg.isHumbucker1 = isHum;
                    cfg.mixGain = 1.0f;
                    break;

                case PickupPosition::BridgeNeck:
                    cfg.numActivePickups = 2;
                    cfg.distBridge1 = bridgeDist;
                    cfg.distBridge2 = neckDist;
                    cfg.isHumbucker1 = isHum;
                    cfg.isHumbucker2 = isHum;
                    cfg.mixGain = 0.707f;
                    break;
            }
        }
        else // Les Paul 1959 (PAF Humbuckers standard)
        {
            const float bridgeDist = 0.0420f;
            const float neckDist   = 0.1550f;
            const bool isHum = (pickupTypeOverride == 0) ? false : true;

            switch (pos)
            {
                case PickupPosition::Bridge:
                    cfg.numActivePickups = 1;
                    cfg.distBridge1 = bridgeDist;
                    cfg.isHumbucker1 = isHum;
                    cfg.mixGain = 1.0f;
                    break;

                case PickupPosition::BridgeMiddle:
                case PickupPosition::BridgeNeck:
                    cfg.numActivePickups = 2;
                    cfg.distBridge1 = bridgeDist;
                    cfg.distBridge2 = neckDist;
                    cfg.isHumbucker1 = isHum;
                    cfg.isHumbucker2 = isHum;
                    cfg.mixGain = 0.707f;
                    break;

                case PickupPosition::Middle:
                case PickupPosition::NeckMiddle:
                case PickupPosition::Neck:
                default:
                    cfg.numActivePickups = 1;
                    cfg.distBridge1 = neckDist;
                    cfg.isHumbucker1 = isHum;
                    cfg.mixGain = 1.0f;
                    break;
            }
        }

        return cfg;
    }

    /**
     * Passive RLC Guitar Tone & Volume Circuit.
     * Accurately shapes pickup output voltage from velocity via 2nd-order resonant lowpass biquad.
     */
    class PassiveElectronics
    {
    public:
        PassiveElectronics() = default;

        void update(GuitarModel model, float toneKnob, float sampleRate) noexcept
        {
            const float tone = std::clamp(toneKnob, 0.0f, 1.0f);
            float fMin = 450.0f, fMax = 4200.0f, qMin = 1.1f, qMax = 2.6f;

            if (model == GuitarModel::LesPaul1959)
            {
                fMin = 400.0f;
                fMax = 2800.0f;
                qMin = 0.95f;
                qMax = 1.95f;
            }

            // Audio-taper logarithmic sweep of resonant cutoff
            const float taperTone = std::pow(tone, 1.4f);
            const float fRes = fMin * std::pow(fMax / fMin, taperTone);
            const float qRes = qMin + taperTone * (qMax - qMin);

            rlcFilter.setLowpass(fRes, qRes, sampleRate);
        }

        float process(float inSample) noexcept
        {
            return rlcFilter.process(inSample);
        }

        void reset() noexcept
        {
            rlcFilter.reset();
        }

    private:
        BiquadFilter rlcFilter;
    };
}
