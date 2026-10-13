// Onirique MeterBox — the two sides against each other: correlation, balance, width (no dependency)
#pragma once

#include <algorithm>
#include <cmath>

#include "Loudness.h"

namespace mtx
{

/** Floats only: handed from one thread to another one float at a time. */
struct StereoValues
{
    float correlation = 0.0f;   // -1 the sides cancel .. 0 nothing in common .. +1 the same on both
    float balance = 0.0f;       // dB, the right side over the left
    float width = 0.0f;         // 0 mono .. 1 nothing in the middle: the share of the sound that is on the sides
    float monoLoss = 0.0f;      // dB lost when the two sides are added into one: 0 for mono, -3 for two unrelated sides
    float level = kFloorDb;     // dB, of the two sides together: under about -80 there is nothing to compare
};

/** What a correlation meter shows, over the last third of a second. */
class StereoField
{
public:
    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate;
        reset();
    }

    void reset() noexcept
    {
        ll = rr = lr = 0.0;
        current = {};
    }

    /** `right` can be `left`. */
    void process (const float* left, const float* right, int count) noexcept
    {
        if (count <= 0)
            return;

        double sumLL = 0.0, sumRR = 0.0, sumLR = 0.0;

        for (int i = 0; i < count; ++i)
        {
            const double l = left[i], r = right[i];
            sumLL += l * l;
            sumRR += r * r;
            sumLR += l * r;
        }

        const auto k = 1.0 - std::exp (-(double) count / (kSeconds * rate));

        ll += k * (sumLL / count - ll);
        rr += k * (sumRR / count - rr);
        lr += k * (sumLR / count - lr);

        const auto both = std::sqrt (ll * rr);
        const auto middle = 0.25 * (ll + rr + 2.0 * lr);
        const auto sides = 0.25 * (ll + rr - 2.0 * lr);
        const auto mean = 0.5 * (ll + rr);

        current.level = mean > 1.0e-12 ? (float) (10.0 * std::log10 (2.0 * mean)) : kFloorDb;
        current.correlation = both > 1.0e-12 ? (float) std::clamp (lr / both, -1.0, 1.0) : 0.0f;
        current.balance = (float) std::clamp (10.0 * std::log10 ((rr + 1.0e-12) / (ll + 1.0e-12)), -24.0, 24.0);
        current.width = middle + sides > 1.0e-12 ? (float) std::clamp (sides / (middle + sides), 0.0, 1.0) : 0.0f;
        current.monoLoss = mean > 1.0e-12 ? (float) std::max (-60.0, 10.0 * std::log10 (std::max (middle, 1.0e-18) / mean)) : 0.0f;
    }

    const StereoValues& values() const noexcept { return current; }

private:
    static constexpr double kSeconds = 0.3;

    double rate = 48000.0;
    double ll = 0.0, rr = 0.0, lr = 0.0;
    StereoValues current;
};

} // namespace mtx
