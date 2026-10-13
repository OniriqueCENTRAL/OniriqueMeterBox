// Onirique MeterBox — loudness as ITU-R BS.1770-4 and EBU R 128 define it (no dependency)
#pragma once

#include <cstdint>
#include <vector>

namespace mtx
{

constexpr int kMaxChannels = 2;

/** What a level reads as when there is nothing to measure: lower than
    anything that has been. */
constexpr float kFloorDb = -120.0f;

/** Everything is counted in steps of a tenth of a second: a momentary
    loudness is four of them, a short-term one thirty. */
constexpr int kStepsPerSecond = 10;
constexpr int kMomentarySteps = 4;
constexpr int kShortTermSteps = 30;

/** How often the short-term loudness has been at each level, for the picture
    of it: one bin to the LU, from kDistributionFloor up. */
constexpr int kDistributionBins = 60;
constexpr float kDistributionFloor = -60.0f;

/** The two filters of BS.1770 in series: the shelf that stands for the head,
    and the high-pass that takes the lows out. Their coefficients are worked
    out for the sample rate, not taken from the table given for 48 kHz. */
class KWeighting
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    double process (double x) noexcept
    {
        // Each a transposed direct form II.
        const auto y = shelf[0] * x + state[0];
        state[0] = shelf[1] * x - shelf[3] * y + state[1];
        state[1] = shelf[2] * x - shelf[4] * y;

        const auto z = y + state[2];
        state[2] = -2.0 * y - pass[0] * z + state[3];
        state[3] = y - pass[1] * z;

        return z;
    }

private:
    double shelf[5] = { 1.0, 0.0, 0.0, 0.0, 0.0 };     // b0 b1 b2 a1 a2
    double pass[2] = {};                               // a1 a2; its b are 1 -2 1
    double state[4] = {};
};

/** The peak of the signal between its samples as well as on them. The signal
    is rebuilt at four times its rate (twice from 88.2 kHz up, not at all from
    176.4 kHz), as BS.1770 asks, and the highest point of that is kept. */
class TruePeak
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    /** The highest point of a run of samples, as an amplitude. */
    float process (const float* x, int count) noexcept;

private:
    static constexpr int kTaps = 16;            // to each phase

    int phases = 4;                             // how many points to the sample
    float coefficients[3][kTaps] = {};          // the first phase is the samples themselves
    float history[2 * kTaps] = {};
    int position = 0;
};

/** Everything the loudness meter knows, as it stands at the end of a step.
    Floats only: it is handed from one thread to another one float at a time. */
struct LoudnessValues
{
    float momentary = kFloorDb;         // LUFS, over 0.4 s
    float shortTerm = kFloorDb;         // LUFS, over 3 s
    float integrated = kFloorDb;        // LUFS, gated, since the reset
    float range = 0.0f;                 // LU: the loudness range, LRA
    float momentaryMax = kFloorDb;
    float shortTermMax = kFloorDb;
    float truePeak[kMaxChannels] = { kFloorDb, kFloorDb };      // dBTP, the highest since the reset
    float peakNow[kMaxChannels] = { kFloorDb, kFloorDb };       // dBTP, over the step just ended
    float peakToShort = 0.0f;           // PSR: the true peak of the last 3 s over the short-term loudness
    float peakToLoudness = 0.0f;        // PLR: the highest true peak over the integrated loudness
};

class Loudness
{
public:
    Loudness();

    void prepare (double sampleRate, int numChannels);

    /** Forgets everything measured so far. */
    void reset() noexcept;

    /** Takes samples up to the end of the step in hand and returns how many
        that was. `accumulate`: whether the step counts towards what is
        measured since the reset, or only towards the momentary and
        short-term loudness, which never stop. */
    int process (const float* const* in, int numSamples, bool accumulate) noexcept;

    /** True when the last call to process() ended a step: the values have
        just changed. */
    bool stepped() const noexcept { return justStepped; }

    const LoudnessValues& values() const noexcept { return current; }

    /** How many steps the short-term loudness has spent in each LU. */
    uint32_t distribution (int bin) const noexcept { return spread[bin]; }

private:
    void endStep (bool accumulate) noexcept;
    void measureIntegrated() noexcept;
    void measureRange() noexcept;

    // The gates are worked out on histograms: a tenth of a LU to the bin,
    // from the absolute gate at -70 LUFS up.
    static constexpr int kBins = 800;
    static constexpr float kBinFloor = -70.0f;

    static int binOf (float loudness) noexcept;

    int channels = 2, stepLength = 4800, filled = 0;

    KWeighting weighting[kMaxChannels];
    TruePeak peaks[kMaxChannels];

    double stepSum = 0.0;                       // the weighted squares of the step in hand, every channel
    float stepPeak[kMaxChannels] = {};

    double stepEnergy[kShortTermSteps] = {};    // the mean square of each of the last steps, a ring
    float stepPeaks[kShortTermSteps] = {};      // and its true peak, the higher side
    int stepIndex = 0;
    int counted = 0;                            // steps in a row that have counted

    std::vector<double> blockEnergy, binEnergy;
    std::vector<uint32_t> blockCount, shortCount;
    uint32_t spread[kDistributionBins] = {};

    LoudnessValues current;
    bool justStepped = false;
};

} // namespace mtx
