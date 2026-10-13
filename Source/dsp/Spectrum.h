// Onirique MeterBox — the spectrum, twelve points to the octave, and the two sides against each other (no dependency)
#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace mtx
{

/** From 20 Hz to 20.48 kHz, a twelfth of an octave apart. */
constexpr int kSpectrumPoints = 121;
constexpr float kSpectrumLowHz = 20.0f;

/** The frequency a point of the spectrum stands for. */
inline float spectrumHz (int point) noexcept
{
    return kSpectrumLowHz * std::pow (2.0f, (float) point / 12.0f);
}

/** Over how long the slow curve is averaged. */
enum Averaging { averageShort = 0, averageLong = 1, averageAll = 2 };   // 3 s, 10 s, since the reset

/** One picture of the spectrum. Every level is that of the third of an octave
    round its point, in dB, with a full-scale sine at 0: pink noise is a flat
    line, and a curve and a target can be laid over one another. */
struct SpectrumFrame
{
    float live[kSpectrumPoints] = {};           // as it stands: up at once, down in a third of a second
    float average[kSpectrumPoints] = {};        // over the averaging time, silences left out
    float correlation[kSpectrumPoints] = {};    // -1..1 between the two sides, round each point
    bool measured = false;                      // false until there has been something to average
};

/** The analyser. Samples are handed over on the audio thread and nothing
    else happens there; the transforms are made by whoever calls analyse().

    Two transforms run side by side: a long one for the points under 400 Hz,
    where a twelfth of an octave is a few hertz wide, and a short one for the
    rest, which follows the sound eight times as closely. */
class Spectrum
{
public:
    Spectrum();
    ~Spectrum();

    /** Not while process() or analyse() may be running elsewhere. */
    void prepare (double sampleRate);

    /** Any thread. */
    void setAveraging (int averaging) noexcept { wanted.store (averaging, std::memory_order_relaxed); }

    /** Any thread: the average starts again from nothing. */
    void requestReset() noexcept { resetPending.store (true, std::memory_order_release); }

    /** Audio thread. `right` can be `left`. What there is no room for is
        dropped: the picture skips, the sound does not wait. */
    void push (const float* left, const float* right, int count) noexcept;

    /** Works through what has been handed over. Never from the audio
        thread, unless the audio is being rendered offline. */
    void analyse();

    /** The latest picture. Never from the audio thread. */
    void read (SpectrumFrame&) const;

private:
    class Fft;
    struct Resolution;

    void transform (Resolution&, int averaging);
    void publish();

    static constexpr uint32_t kFifoFrames = 1u << 17;

    std::vector<float> fifo;                    // left and right, interleaved
    std::atomic<uint32_t> written { 0 }, taken { 0 };
    std::atomic<int> wanted { averageLong };
    std::atomic<bool> resetPending { false };

    // Everything below is touched under analysisLock only.
    std::mutex analysisLock;
    std::unique_ptr<Resolution> resolutions[2];
    int averagingNow = averageLong;             // the one in force

    float live[kSpectrumPoints] = {};           // the power in the twelfth of an octave of each point
    double averageSum[kSpectrumPoints] = {};
    float sideL[kSpectrumPoints] = {}, sideR[kSpectrumPoints] = {}, sideCross[kSpectrumPoints] = {};

    mutable std::mutex frameLock;
    SpectrumFrame frame;
};

} // namespace mtx
