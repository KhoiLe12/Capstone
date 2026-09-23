#include "PickupModel.h"
#include <cmath>
#include <algorithm>

static constexpr float kPi = 3.14159265358979323846f;

void PickupModel::init(float sr)
{
    sampleRate = sr;
    reset();
}

float PickupModel::process(float stringSignal, float pickupPos, float toneControl) noexcept
{
    // 1. Write incoming bridge signal to pickup delay buffer
    pickupDelay[writeIndex] = stringSignal;

    // 2. Spatial comb delay (Bridge = 2.0 samples ~ 2cm, Neck = 20.0 samples ~ 14cm)
    const float p = std::max(0.0f, std::min(pickupPos, 1.0f));
    const float delaySamples = 2.0f + 18.0f * p;

    const int tapInt = static_cast<int>(delaySamples);
    const float tapFrac = delaySamples - static_cast<float>(tapInt);

    const int r0 = (writeIndex - tapInt + kPickupDelaySize) % kPickupDelaySize;
    const int r1 = (r0 - 1 + kPickupDelaySize) % kPickupDelaySize;
    const float delayed = (1.0f - tapFrac) * pickupDelay[r0] + tapFrac * pickupDelay[r1];

    writeIndex = (writeIndex + 1) % kPickupDelaySize;

    // Spatial pickup output: combines direct and reflected string velocity
    const float pickupOut = 0.55f * stringSignal + 0.45f * delayed;

    // 3. Passive guitar tone capacitor (700 Hz rolled off to 18 kHz wide open)
    const float t = std::max(0.0f, std::min(toneControl, 1.0f));
    const float fc = 700.0f + 17300.0f * (t * t);
    const float omega = 2.f * kPi * fc / sampleRate;
    const float alpha = 1.0f - std::exp(-omega);

    toneFilterState += alpha * (pickupOut - toneFilterState);

    return toneFilterState;
}

void PickupModel::reset() noexcept
{
    std::fill(std::begin(pickupDelay), std::end(pickupDelay), 0.f);
    writeIndex = 0;
    toneFilterState = 0.f;
}

