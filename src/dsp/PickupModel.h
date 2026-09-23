#pragma once
#include <vector>

/**
 * PickupModel — Simulates electric guitar magnetic pickups and passive RLC tone control.
 *
 * Physical modeling:
 *   1. Spatial Pickup Comb Filtering:
 *      A pickup detects string vibration at a specific distance from the bridge:
 *      - Bridge position (p = 0.0): ~2 cm from bridge (biting, aggressive treble)
 *      - Neck position (p = 1.0): ~13 cm from bridge (warm, round, hollow fundamental)
 *   2. Passive Tone Circuit:
 *      Simulates standard 250k/500k pot with 0.047 uF capacitor to ground.
 */
class PickupModel
{
public:
    PickupModel() = default;

    void init(float sampleRate);

    /**
     * Process one sample through the virtual pickup and tone circuitry.
     * @param stringSignal Input raw string velocity from the bridge
     * @param pickupPos    0.0 = Bridge pickup, 1.0 = Neck pickup
     * @param toneControl  0.0 = fully rolled off (dark jazz), 1.0 = wide open
     */
    float process(float stringSignal, float pickupPos, float toneControl) noexcept;

    void reset() noexcept;

private:
    float sampleRate = 44100.f;

    // Spatial pickup delay line (up to ~20 ms / 64 samples at bridge)
    static constexpr int kPickupDelaySize = 64;
    float pickupDelay[kPickupDelaySize] = {};
    int writeIndex = 0;

    // Passive tone capacitor 1-pole filter state
    float toneFilterState = 0.f;
};

