#include "Tonal.h"

#include <algorithm>

namespace mtx
{

namespace
{
    struct Anchor { float hz, mean, tolerance; };

    struct Stock
    {
        const char* name;
        Anchor anchors[14];
    };

    // The level of each third of an octave against the one at 1 kHz, in dB.
    const Stock kStock[kNumStockTargets] =
    {
        // Pop, electronic: a full low end, a slow fall through the mids,
        // and the air rolled off.
        { "Modern", { { 20.0f, -6.0f, 6.0f },   { 30.0f, 2.0f, 5.0f },    { 45.0f, 7.0f, 4.0f },    { 60.0f, 8.0f, 3.5f },
                      { 100.0f, 6.5f, 3.0f },   { 200.0f, 3.5f, 3.0f },   { 400.0f, 1.5f, 2.5f },   { 1000.0f, 0.0f, 2.5f },
                      { 2000.0f, -1.5f, 2.5f }, { 4000.0f, -3.5f, 3.0f }, { 8000.0f, -7.0f, 3.0f }, { 12000.0f, -10.0f, 3.5f },
                      { 16000.0f, -14.0f, 4.0f }, { 20000.0f, -20.0f, 5.0f } } },

        // Rap, trap, bass music: the sub carries the record.
        { "Urban", { { 20.0f, -2.0f, 6.0f },    { 30.0f, 6.0f, 5.0f },    { 45.0f, 10.0f, 4.0f },   { 60.0f, 10.0f, 3.5f },
                     { 100.0f, 7.0f, 3.0f },    { 200.0f, 3.0f, 3.0f },   { 400.0f, 1.0f, 2.5f },   { 1000.0f, 0.0f, 2.5f },
                     { 2000.0f, -1.5f, 2.5f },  { 4000.0f, -3.5f, 3.0f }, { 8000.0f, -7.0f, 3.0f }, { 12000.0f, -10.0f, 3.5f },
                     { 16000.0f, -14.5f, 4.0f }, { 20000.0f, -21.0f, 5.0f } } },

        // Rock, metal: little under the kick and the bass, and dense mids.
        { "Rock", { { 20.0f, -12.0f, 6.0f },    { 30.0f, -4.0f, 5.0f },   { 45.0f, 2.0f, 4.0f },    { 60.0f, 5.0f, 3.5f },
                    { 100.0f, 6.0f, 3.0f },     { 200.0f, 4.5f, 3.0f },   { 400.0f, 2.5f, 2.5f },   { 1000.0f, 0.0f, 2.5f },
                    { 2000.0f, -0.5f, 2.5f },   { 4000.0f, -2.5f, 3.0f }, { 8000.0f, -7.0f, 3.0f }, { 12000.0f, -11.0f, 3.5f },
                    { 16000.0f, -16.0f, 4.0f }, { 20000.0f, -22.0f, 5.0f } } },

        // Jazz, classical, folk: no pushed low end, a top that falls away
        // early, and more room all round.
        { "Acoustic", { { 20.0f, -16.0f, 7.0f },  { 30.0f, -8.0f, 6.0f },   { 45.0f, -2.0f, 5.0f },    { 60.0f, 2.0f, 4.5f },
                        { 100.0f, 4.0f, 4.0f },   { 200.0f, 4.0f, 3.5f },   { 400.0f, 2.5f, 3.0f },    { 1000.0f, 0.0f, 3.0f },
                        { 2000.0f, -3.0f, 3.0f }, { 4000.0f, -7.0f, 3.5f }, { 8000.0f, -12.0f, 4.0f }, { 12000.0f, -16.0f, 4.5f },
                        { 16000.0f, -21.0f, 5.0f }, { 20000.0f, -27.0f, 6.0f } } },
    };

    // The points a target is laid on a spectrum by: 40 Hz to 12 kHz. The
    // two ends vary too much from one record to the next to weigh in.
    constexpr int kFitFirst = 12, kFitLast = 112;

    const Region kRegions[kNumRegions] = { { "LOW", 20.0f, 250.0f },
                                           { "LOW-MID", 250.0f, 2000.0f },
                                           { "HIGH-MID", 2000.0f, 8000.0f },
                                           { "HIGH", 8000.0f, 21000.0f } };
}

const char* stockTargetName (int index) noexcept
{
    return kStock[std::clamp (index, 0, kNumStockTargets - 1)].name;
}

TonalTarget stockTarget (int index) noexcept
{
    const auto& anchors = kStock[std::clamp (index, 0, kNumStockTargets - 1)].anchors;
    const auto count = (int) (sizeof (anchors) / sizeof (anchors[0]));

    TonalTarget target;

    for (int p = 0; p < kSpectrumPoints; ++p)
    {
        const auto hz = spectrumHz (p);
        int next = 1;

        while (next < count - 1 && anchors[next].hz < hz)
            ++next;

        // From one anchor to the next in a straight line, on a scale of octaves.
        const auto& a = anchors[next - 1];
        const auto& b = anchors[next];
        const auto t = std::clamp (std::log (hz / a.hz) / std::log (b.hz / a.hz), 0.0f, 1.0f);

        target.mean[p] = a.mean + t * (b.mean - a.mean);
        target.tolerance[p] = a.tolerance + t * (b.tolerance - a.tolerance);
    }

    return target;
}

TonalTarget targetFrom (const float* averageDb) noexcept
{
    TonalTarget target;
    float sum = 0.0f;

    for (int p = kFitFirst; p < kFitLast; ++p)
        sum += averageDb[p];

    // Kept round zero, as the stock ones are: only the shape is the target.
    const auto middle = sum / (float) (kFitLast - kFitFirst);

    for (int p = 0; p < kSpectrumPoints; ++p)
    {
        target.mean[p] = std::max (averageDb[p] - middle, -90.0f);
        target.tolerance[p] = 3.0f;
    }

    return target;
}

float fitTarget (const TonalTarget& target, const float* averageDb) noexcept
{
    float sum = 0.0f;

    for (int p = kFitFirst; p < kFitLast; ++p)
        sum += averageDb[p] - target.mean[p];

    return sum / (float) (kFitLast - kFitFirst);
}

const Region& region (int index) noexcept
{
    return kRegions[std::clamp (index, 0, kNumRegions - 1)];
}

void compareRegions (const TonalTarget& target, const float* averageDb, float offset,
                     float* deviation, float* tolerance) noexcept
{
    for (int i = 0; i < kNumRegions; ++i)
    {
        double heard = 0.0, wanted = 0.0, allowed = 0.0;
        int count = 0;

        for (int p = 0; p < kSpectrumPoints; ++p)
        {
            const auto hz = spectrumHz (p);

            if (hz < kRegions[i].lowHz || hz >= kRegions[i].highHz)
                continue;

            heard += std::pow (10.0, averageDb[p] * 0.1);
            wanted += std::pow (10.0, (target.mean[p] + offset) * 0.1);
            allowed += target.tolerance[p];
            ++count;
        }

        deviation[i] = heard > 0.0 && wanted > 0.0 ? (float) (10.0 * std::log10 (heard / wanted)) : 0.0f;

        // A whole region moves less than any one of its thirds of an octave.
        tolerance[i] = count > 0 ? (float) (0.6 * allowed / count) : 3.0f;
    }
}

} // namespace mtx
