#include "Loudness.h"

#include <algorithm>
#include <cmath>

namespace mtx
{

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    float toDb (float amplitude) noexcept
    {
        return amplitude > 1.0e-6f ? 20.0f * std::log10 (amplitude) : kFloorDb;
    }

    /** The loudness of a mean square already weighted and summed over the channels. */
    float loudnessOf (double meanSquare) noexcept
    {
        return meanSquare > 1.0e-12 ? (float) (-0.691 + 10.0 * std::log10 (meanSquare)) : kFloorDb;
    }

    /** The modified Bessel function a Kaiser window is made of. */
    double besselI0 (double x) noexcept
    {
        double sum = 1.0, term = 1.0;

        for (int k = 1; k < 40; ++k)
        {
            const auto half = x / (2.0 * k);
            term *= half * half;
            sum += term;
        }

        return sum;
    }
}

//==============================================================================
void KWeighting::prepare (double sampleRate) noexcept
{
    // The analogue filters the table of BS.1770 was drawn from, taken to the
    // sample rate in hand.
    {
        const auto k = std::tan (kPi * 1681.974450955533 / sampleRate);
        const auto q = 0.7071752369554196;
        const auto vh = std::pow (10.0, 3.999843853973347 / 20.0);
        const auto vb = std::pow (vh, 0.4996667741545416);
        const auto a0 = 1.0 + k / q + k * k;

        shelf[0] = (vh + vb * k / q + k * k) / a0;
        shelf[1] = 2.0 * (k * k - vh) / a0;
        shelf[2] = (vh - vb * k / q + k * k) / a0;
        shelf[3] = 2.0 * (k * k - 1.0) / a0;
        shelf[4] = (1.0 - k / q + k * k) / a0;
    }

    {
        const auto k = std::tan (kPi * 38.13547087602444 / sampleRate);
        const auto q = 0.5003270373238773;
        const auto a0 = 1.0 + k / q + k * k;

        pass[0] = 2.0 * (k * k - 1.0) / a0;
        pass[1] = (1.0 - k / q + k * k) / a0;
    }

    reset();
}

void KWeighting::reset() noexcept
{
    for (auto& s : state)
        s = 0.0;
}

//==============================================================================
void TruePeak::prepare (double sampleRate) noexcept
{
    phases = sampleRate < 80000.0 ? 4 : sampleRate < 160000.0 ? 2 : 1;

    // A sinc under a Kaiser window, read between the samples: one set of
    // taps for each point that is not a sample itself.
    const auto centre = (double) (kTaps / 2 - 1);
    const auto beta = 8.0;

    for (int p = 1; p < phases; ++p)
    {
        const auto shift = (double) p / (double) phases;
        double taps[kTaps];
        double sum = 0.0;

        for (int k = 0; k < kTaps; ++k)
        {
            const auto t = (double) k - centre - shift;
            const auto sinc = std::abs (t) < 1.0e-9 ? 1.0 : std::sin (kPi * t) / (kPi * t);
            const auto u = t / (kTaps * 0.5);

            taps[k] = sinc * besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - u * u))) / besselI0 (beta);
            sum += taps[k];
        }

        for (int k = 0; k < kTaps; ++k)
            coefficients[p - 1][k] = (float) (taps[k] / sum);
    }

    reset();
}

void TruePeak::reset() noexcept
{
    for (auto& h : history)
        h = 0.0f;

    position = 0;
}

float TruePeak::process (const float* x, int count) noexcept
{
    float peak = 0.0f;

    for (int i = 0; i < count; ++i)
    {
        const auto v = x[i];

        // Written twice, so that the last kTaps samples are always in a row.
        history[position] = history[position + kTaps] = v;
        position = (position + 1) % kTaps;

        peak = std::max (peak, std::abs (v));

        const float* const recent = history + position;

        for (int p = 0; p < phases - 1; ++p)
        {
            float sum = 0.0f;

            for (int k = 0; k < kTaps; ++k)
                sum += coefficients[p][k] * recent[k];

            peak = std::max (peak, std::abs (sum));
        }
    }

    return peak;
}

//==============================================================================
Loudness::Loudness()
    : blockEnergy (kBins, 0.0), binEnergy (kBins, 0.0), blockCount (kBins, 0), shortCount (kBins, 0)
{
    // What a short-term loudness in the middle of each bin is as a mean square.
    for (int i = 0; i < kBins; ++i)
        binEnergy[(size_t) i] = std::pow (10.0, ((double) kBinFloor + ((double) i + 0.5) * 0.1 + 0.691) * 0.1);
}

void Loudness::prepare (double sampleRate, int numChannels)
{
    channels = std::clamp (numChannels, 1, kMaxChannels);
    stepLength = std::max (1, (int) std::lround (sampleRate / kStepsPerSecond));

    for (int c = 0; c < kMaxChannels; ++c)
    {
        weighting[c].prepare (sampleRate);
        peaks[c].prepare (sampleRate);
    }

    reset();
}

void Loudness::reset() noexcept
{
    for (int c = 0; c < kMaxChannels; ++c)
    {
        weighting[c].reset();
        peaks[c].reset();
        stepPeak[c] = 0.0f;
    }

    for (int i = 0; i < kShortTermSteps; ++i)
    {
        stepEnergy[i] = 0.0;
        stepPeaks[i] = 0.0f;
    }

    std::fill (blockEnergy.begin(), blockEnergy.end(), 0.0);
    std::fill (blockCount.begin(), blockCount.end(), 0u);
    std::fill (shortCount.begin(), shortCount.end(), 0u);

    for (auto& s : spread)
        s = 0;

    stepSum = 0.0;
    filled = 0;
    stepIndex = 0;
    counted = 0;
    current = {};
    justStepped = false;
}

int Loudness::binOf (float loudness) noexcept
{
    return std::clamp ((int) ((loudness - kBinFloor) * 10.0f), 0, kBins - 1);
}

int Loudness::process (const float* const* in, int numSamples, bool accumulate) noexcept
{
    const auto count = std::min (numSamples, stepLength - filled);

    for (int c = 0; c < channels; ++c)
    {
        auto& filter = weighting[c];
        const auto* x = in[c];
        double sum = 0.0;

        for (int i = 0; i < count; ++i)
        {
            const auto y = filter.process ((double) x[i]);
            sum += y * y;
        }

        stepSum += sum;
        stepPeak[c] = std::max (stepPeak[c], peaks[c].process (x, count));
    }

    filled += count;
    justStepped = filled >= stepLength;

    if (justStepped)
        endStep (accumulate);

    return count;
}

void Loudness::endStep (bool accumulate) noexcept
{
    const auto higher = std::max (stepPeak[0], channels > 1 ? stepPeak[1] : 0.0f);

    stepEnergy[stepIndex] = stepSum / (double) stepLength;
    stepPeaks[stepIndex] = higher;
    stepIndex = (stepIndex + 1) % kShortTermSteps;

    double momentary = 0.0, shortTerm = 0.0;
    float recentPeak = 0.0f;

    for (int i = 0; i < kShortTermSteps; ++i)
    {
        shortTerm += stepEnergy[i];
        recentPeak = std::max (recentPeak, stepPeaks[i]);
    }

    for (int i = 1; i <= kMomentarySteps; ++i)
        momentary += stepEnergy[(stepIndex - i + kShortTermSteps) % kShortTermSteps];

    momentary /= kMomentarySteps;
    shortTerm /= kShortTermSteps;

    current.momentary = loudnessOf (momentary);
    current.shortTerm = loudnessOf (shortTerm);

    for (int c = 0; c < kMaxChannels; ++c)
        current.peakNow[c] = toDb (stepPeak[c < channels ? c : 0]);

    current.peakToShort = current.shortTerm > kBinFloor ? toDb (recentPeak) - current.shortTerm : 0.0f;

    if (accumulate)
    {
        ++counted;

        for (int c = 0; c < kMaxChannels; ++c)
            current.truePeak[c] = std::max (current.truePeak[c], current.peakNow[c]);

        // A block of the integrated loudness is 0.4 s, one every step: they
        // overlap by three quarters. Nothing is entered before a whole one
        // has been heard.
        if (counted >= kMomentarySteps)
        {
            current.momentaryMax = std::max (current.momentaryMax, current.momentary);

            if (current.momentary >= kBinFloor)
            {
                const auto bin = (size_t) binOf (current.momentary);
                blockEnergy[bin] += momentary;
                ++blockCount[bin];
            }

            measureIntegrated();
        }

        if (counted >= kShortTermSteps)
        {
            current.shortTermMax = std::max (current.shortTermMax, current.shortTerm);

            if (current.shortTerm >= kBinFloor)
                ++shortCount[(size_t) binOf (current.shortTerm)];

            const auto bin = (int) std::floor (current.shortTerm - kDistributionFloor);

            if (bin >= 0)
                ++spread[std::min (bin, kDistributionBins - 1)];

            measureRange();
        }

        const auto highest = std::max (current.truePeak[0], current.truePeak[1]);
        current.peakToLoudness = current.integrated > kBinFloor ? highest - current.integrated : 0.0f;
    }
    else
    {
        counted = 0;
    }

    stepSum = 0.0;
    filled = 0;

    for (auto& p : stepPeak)
        p = 0.0f;
}

void Loudness::measureIntegrated() noexcept
{
    // Everything over the absolute gate is in the histogram. The relative
    // gate is 10 LU under the loudness of all of it.
    double sum = 0.0;
    uint64_t count = 0;

    for (size_t i = 0; i < (size_t) kBins; ++i)
    {
        sum += blockEnergy[i];
        count += blockCount[i];
    }

    if (count == 0)
    {
        current.integrated = kFloorDb;
        return;
    }

    const auto gate = loudnessOf (sum / (double) count) - 10.0f;
    const auto first = (size_t) std::clamp ((int) std::lround ((gate - kBinFloor) * 10.0f), 0, kBins - 1);

    sum = 0.0;
    count = 0;

    for (auto i = first; i < (size_t) kBins; ++i)
    {
        sum += blockEnergy[i];
        count += blockCount[i];
    }

    current.integrated = count > 0 ? loudnessOf (sum / (double) count) : kFloorDb;
}

void Loudness::measureRange() noexcept
{
    // EBU Tech 3342: of the short-term loudness over a gate 20 LU under its
    // own mean, the distance from the 10th to the 95th percentile.
    double sum = 0.0;
    uint64_t count = 0;

    for (size_t i = 0; i < (size_t) kBins; ++i)
    {
        sum += binEnergy[i] * shortCount[i];
        count += shortCount[i];
    }

    current.range = 0.0f;

    if (count == 0)
        return;

    const auto gate = loudnessOf (sum / (double) count) - 20.0f;
    const auto first = std::clamp ((int) std::lround ((gate - kBinFloor) * 10.0f), 0, kBins - 1);

    count = 0;

    for (int i = first; i < kBins; ++i)
        count += shortCount[(size_t) i];

    if (count < 2)
        return;

    const auto lowRank = (uint64_t) std::llround ((double) (count - 1) * 0.10);
    const auto highRank = (uint64_t) std::llround ((double) (count - 1) * 0.95);
    int low = -1, high = first;
    uint64_t seen = 0;

    for (int i = first; i < kBins; ++i)
    {
        seen += shortCount[(size_t) i];

        if (low < 0 && seen > lowRank)
            low = i;

        if (seen > highRank)
        {
            high = i;
            break;
        }
    }

    current.range = low >= 0 ? (float) (high - low) * 0.1f : 0.0f;
}

} // namespace mtx
