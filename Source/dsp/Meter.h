// Onirique MeterBox — everything that is measured, and how it gets from the audio thread to the screen (no dependency)
#pragma once

#include <atomic>
#include <cstring>
#include <memory>
#include <type_traits>

#include "Loudness.h"
#include "Spectrum.h"
#include "Stereo.h"
#include "Tonal.h"

namespace mtx
{

/** An hour of loudness, ten points to the second. */
constexpr uint32_t kHistoryCapacity = 36000;

/** The pairs of samples the goniometer is drawn from: the last 85 ms. */
constexpr int kScopeFrames = 4096;

struct HistoryPoint
{
    float momentary = kFloorDb, shortTerm = kFloorDb, integrated = kFloorDb;
    float truePeak = kFloorDb;      // of that tenth of a second, the higher side

    // The waveform of that tenth of a second, as amplitudes: the highest
    // sample of each side, and its RMS.
    float peakLeft = 0.0f, peakRight = 0.0f, rmsLeft = 0.0f, rmsRight = 0.0f;
};

/** How many floats a point of the history is kept as. */
constexpr uint32_t kHistoryFloats = 8;

/** A struct of floats written by one thread and read by others, one float at
    a time: what is read may mix two steps, which a meter can live with. */
template <typename Values>
class Shared
{
public:
    Shared() { store (Values()); }

    void store (const Values& values) noexcept
    {
        float raw[kCount];
        std::memcpy (raw, &values, sizeof (raw));

        for (size_t i = 0; i < kCount; ++i)
            slots[i].store (raw[i], std::memory_order_relaxed);
    }

    Values load() const noexcept
    {
        float raw[kCount];

        for (size_t i = 0; i < kCount; ++i)
            raw[i] = slots[i].load (std::memory_order_relaxed);

        Values values;
        std::memcpy (&values, raw, sizeof (raw));
        return values;
    }

private:
    static_assert (std::is_trivially_copyable<Values>::value && sizeof (Values) % sizeof (float) == 0,
                   "only a struct made of floats can be shared this way");

    static constexpr size_t kCount = sizeof (Values) / sizeof (float);

    std::atomic<float> slots[kCount];
};

/** The meter: the sound goes in on the audio thread and is left as it was.
    What was measured is read from any other thread, at any time. */
class Meter
{
public:
    Meter();

    /** Not while process() may be running. */
    void prepare (double sampleRate, int numChannels);

    /** Audio thread. `running`: whether this counts towards what is measured
        since the reset. The momentary and short-term loudness, the spectrum
        and the stereo picture go on either way. */
    void process (const float* const* in, int numChannels, int numSamples, bool running) noexcept;

    /** Any thread: forgets what was measured. Taken up by the next block. */
    void requestReset() noexcept;

    void setAveraging (int averaging) noexcept { spectrum.setAveraging (averaging); }

    /** Makes the transforms the spectrum is waiting for. Never from the
        audio thread, unless the audio is being rendered offline. */
    void analyse() { spectrum.analyse(); }

    //==============================================================================
    // What was measured. Any thread but the audio thread.
    LoudnessValues loudness() const noexcept { return loudnessShared.load(); }
    StereoValues stereo() const noexcept { return stereoShared.load(); }
    void readSpectrum (SpectrumFrame& frame) const { spectrum.read (frame); }

    /** How many steps the short-term loudness has spent in each LU. */
    uint32_t distribution (int bin) const noexcept { return spread[bin].load (std::memory_order_relaxed); }

    /** The points of the history are numbered from the reset. Those from
        historyStart() to historyEnd() can be read. */
    uint32_t historyEnd() const noexcept { return historyCount.load (std::memory_order_acquire); }
    uint32_t historyStart() const noexcept;
    HistoryPoint historyAt (uint32_t index) const noexcept;

    /** The latest pairs of samples, the oldest first; returns how many. */
    int readScope (float* left, float* right, int maxFrames) const noexcept;

private:
    Loudness meter;
    StereoField field;
    Spectrum spectrum;

    Shared<LoudnessValues> loudnessShared;
    Shared<StereoValues> stereoShared;
    std::atomic<uint32_t> spread[kDistributionBins];

    std::unique_ptr<std::atomic<float>[]> history;      // kHistoryFloats to the point, a ring
    std::atomic<uint32_t> historyCount { 0 };

    std::unique_ptr<std::atomic<float>[]> scope;        // left and right, a ring
    std::atomic<uint32_t> scopeCount { 0 };
    int scopeStride = 1, scopePhase = 0;

    // The waveform of the step in hand.
    float wavePeak[kMaxChannels] = {};
    double waveSquares[kMaxChannels] = {};
    int waveCount = 0;

    int channels = 2;
    std::atomic<bool> resetPending { false };
};

} // namespace mtx
