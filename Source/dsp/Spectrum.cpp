#include "Spectrum.h"

#include <algorithm>

#include "Loudness.h"

namespace mtx
{

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    // Under this a transform is taken for silence, and leaves the average
    // and the correlation as they were.
    constexpr double kAudiblePower = 1.0e-8;

    constexpr double kAttackSeconds = 0.03, kReleaseSeconds = 0.3, kSideSeconds = 0.6;

    /** A full-scale sine is 0 dB: twice its mean square. */
    float levelOf (double power) noexcept
    {
        return power > 0.5e-12 ? (float) (10.0 * std::log10 (2.0 * power)) : kFloorDb;
    }

    /** What lies under a spectrum up to a point between two of its bins: the
        spectrum is taken as a straight line from each bin to the next. */
    double integral (const std::vector<double>& bins, const std::vector<double>& running, double at) noexcept
    {
        const auto k = (size_t) at;

        if (k + 1 >= bins.size())
            return running.back();

        const auto t = at - (double) k;
        return running[k] + bins[k] * t + (bins[k + 1] - bins[k]) * t * t * 0.5;
    }
}

//==============================================================================
/** A plain radix-2 transform, in place. */
class Spectrum::Fft
{
public:
    explicit Fft (int order) : size (1 << order), reversed ((size_t) size), cosines ((size_t) size / 2), sines ((size_t) size / 2)
    {
        for (int i = 0; i < size; ++i)
        {
            int r = 0;

            for (int bit = 0; bit < order; ++bit)
                r |= ((i >> bit) & 1) << (order - 1 - bit);

            reversed[(size_t) i] = r;
        }

        for (int i = 0; i < size / 2; ++i)
        {
            cosines[(size_t) i] = (float) std::cos (2.0 * kPi * i / size);
            sines[(size_t) i] = (float) std::sin (2.0 * kPi * i / size);
        }
    }

    void perform (float* re, float* im) const noexcept
    {
        for (int i = 0; i < size; ++i)
        {
            const auto j = reversed[(size_t) i];

            if (j > i)
            {
                std::swap (re[i], re[j]);
                std::swap (im[i], im[j]);
            }
        }

        for (int span = 2; span <= size; span <<= 1)
        {
            const auto half = span >> 1;
            const auto stride = size / span;

            for (int start = 0; start < size; start += span)
            {
                for (int k = 0; k < half; ++k)
                {
                    const auto wr = cosines[(size_t) (k * stride)];
                    const auto wi = -sines[(size_t) (k * stride)];
                    const auto a = start + k, b = a + half;
                    const auto tr = re[b] * wr - im[b] * wi;
                    const auto ti = re[b] * wi + im[b] * wr;

                    re[b] = re[a] - tr;
                    im[b] = im[a] - ti;
                    re[a] += tr;
                    im[a] += ti;
                }
            }
        }
    }

private:
    int size;
    std::vector<int> reversed;
    std::vector<float> cosines, sines;
};

/** One of the two transforms, and the points it answers for. */
struct Spectrum::Resolution
{
    Resolution (int order, int hopLength, int firstPoint, int lastPoint, double sampleRate)
        : size (1 << order), hop (hopLength), first (firstPoint), last (lastPoint),
          hopSeconds ((double) hopLength / sampleRate),
          fft (order),
          window ((size_t) size), left ((size_t) size, 0.0f), right ((size_t) size, 0.0f),
          re ((size_t) size), im ((size_t) size),
          powerL ((size_t) size / 2 + 1), powerR ((size_t) size / 2 + 1), cross ((size_t) size / 2 + 1),
          runningL ((size_t) size / 2 + 1), runningR ((size_t) size / 2 + 1), runningCross ((size_t) size / 2 + 1)
    {
        double squares = 0.0;

        for (int i = 0; i < size; ++i)
        {
            const auto w = 0.5 - 0.5 * std::cos (2.0 * kPi * i / size);
            window[(size_t) i] = (float) w;
            squares += w * w;
        }

        // What turns a squared bin into its share of the signal's mean square.
        scale = 2.0 / ((double) size * squares);

        // Where the twelfth of an octave of each point starts and ends, in bins.
        const auto binHz = sampleRate / size;
        const auto halfBand = std::pow (2.0, 1.0 / 24.0);

        for (int p = first; p < last; ++p)
        {
            const auto hz = (double) spectrumHz (p);
            low.push_back (std::min (hz / halfBand / binHz, size * 0.5));
            high.push_back (std::min (hz * halfBand / binHz, size * 0.5));
        }
    }

    const int size, hop, first, last;
    const double hopSeconds;
    double scale = 0.0;

    Fft fft;
    std::vector<float> window, left, right, re, im;     // left and right: the last `size` samples, a ring
    std::vector<double> powerL, powerR, cross;          // of each bin: the two sides, and one against the other
    std::vector<double> runningL, runningR, runningCross;
    std::vector<double> low, high;

    int position = 0, pending = 0;
    double weight = 0.0;                                // of what the average of its points holds
};

//==============================================================================
Spectrum::Spectrum() : fifo ((size_t) kFifoFrames * 2, 0.0f)
{
    prepare (48000.0);
}

Spectrum::~Spectrum() = default;

void Spectrum::prepare (double sampleRate)
{
    const std::lock_guard<std::mutex> lock (analysisLock);

    // The transforms keep their length in time, whatever the rate.
    const auto extra = sampleRate > 100000.0 ? 2 : sampleRate > 50000.0 ? 1 : 0;
    const auto longOrder = 14 + extra, shortOrder = 11 + extra;

    // 400 Hz is between the 51st point and the 52nd.
    constexpr int kSplit = 52;

    resolutions[0] = std::make_unique<Resolution> (longOrder, (1 << longOrder) / 4, 0, kSplit, sampleRate);
    resolutions[1] = std::make_unique<Resolution> (shortOrder, (1 << shortOrder) / 2, kSplit, kSpectrumPoints, sampleRate);

    for (int p = 0; p < kSpectrumPoints; ++p)
    {
        live[p] = 0.0f;
        averageSum[p] = 0.0;
        sideL[p] = sideR[p] = sideCross[p] = 0.0f;
    }

    written.store (0, std::memory_order_release);
    taken.store (0, std::memory_order_release);

    publish();
}

void Spectrum::push (const float* left, const float* right, int count) noexcept
{
    const auto at = written.load (std::memory_order_relaxed);
    const auto room = kFifoFrames - (at - taken.load (std::memory_order_acquire));
    const auto frames = std::min ((uint32_t) std::max (0, count), room);

    for (uint32_t i = 0; i < frames; ++i)
    {
        const auto slot = (size_t) ((at + i) & (kFifoFrames - 1)) * 2;
        fifo[slot] = left[i];
        fifo[slot + 1] = right[i];
    }

    written.store (at + frames, std::memory_order_release);
}

void Spectrum::analyse()
{
    const std::lock_guard<std::mutex> lock (analysisLock);

    bool changed = false;

    if (resetPending.exchange (false, std::memory_order_acq_rel))
    {
        for (auto& sum : averageSum)
            sum = 0.0;

        for (auto& resolution : resolutions)
            resolution->weight = 0.0;

        changed = true;
    }

    // An average that changes its span keeps what it has, as one reading.
    if (const auto mode = wanted.load (std::memory_order_relaxed); mode != averagingNow)
    {
        averagingNow = mode;

        for (auto& resolution : resolutions)
        {
            if (resolution->weight <= 0.0)
                continue;

            for (int p = resolution->first; p < resolution->last; ++p)
                averageSum[p] /= resolution->weight;

            resolution->weight = 1.0;
        }
    }

    auto at = taken.load (std::memory_order_relaxed);
    const auto end = written.load (std::memory_order_acquire);

    while (at != end)
    {
        const auto slot = (size_t) (at & (kFifoFrames - 1)) * 2;
        const auto l = fifo[slot], r = fifo[slot + 1];
        ++at;

        for (auto& resolution : resolutions)
        {
            auto& res = *resolution;

            res.left[(size_t) res.position] = l;
            res.right[(size_t) res.position] = r;
            res.position = (res.position + 1) & (res.size - 1);

            if (++res.pending >= res.hop)
            {
                res.pending = 0;
                transform (res, averagingNow);
                changed = true;
            }
        }
    }

    taken.store (at, std::memory_order_release);

    if (changed)
        publish();
}

void Spectrum::transform (Resolution& res, int mode)
{
    const auto mask = res.size - 1;

    // Both sides in one transform: the left as the real part, the right as
    // the imaginary one, and the two spectra told apart afterwards.
    for (int i = 0; i < res.size; ++i)
    {
        const auto from = (size_t) ((res.position + i) & mask);
        res.re[(size_t) i] = res.left[from] * res.window[(size_t) i];
        res.im[(size_t) i] = res.right[from] * res.window[(size_t) i];
    }

    res.fft.perform (res.re.data(), res.im.data());

    const auto half = res.size / 2;

    for (int k = 0; k <= half; ++k)
    {
        const auto mirror = (size_t) ((res.size - k) & mask);
        const double a = res.re[(size_t) k], b = res.im[(size_t) k];
        const double c = res.re[mirror], d = -res.im[mirror];

        res.powerL[(size_t) k] = res.scale * 0.25 * ((a + c) * (a + c) + (b + d) * (b + d));
        res.powerR[(size_t) k] = res.scale * 0.25 * ((a - c) * (a - c) + (b - d) * (b - d));
        res.cross[(size_t) k] = res.scale * 0.5 * (b * c - a * d);
    }

    res.runningL[0] = res.runningR[0] = res.runningCross[0] = 0.0;

    for (size_t k = 1; k <= (size_t) half; ++k)
    {
        res.runningL[k] = res.runningL[k - 1] + 0.5 * (res.powerL[k - 1] + res.powerL[k]);
        res.runningR[k] = res.runningR[k - 1] + 0.5 * (res.powerR[k - 1] + res.powerR[k]);
        res.runningCross[k] = res.runningCross[k - 1] + 0.5 * (res.cross[k - 1] + res.cross[k]);
    }

    const auto audible = 0.5 * (res.runningL.back() + res.runningR.back()) > kAudiblePower;

    const auto attack = (float) (1.0 - std::exp (-res.hopSeconds / kAttackSeconds));
    const auto release = (float) (1.0 - std::exp (-res.hopSeconds / kReleaseSeconds));
    const auto side = (float) (1.0 - std::exp (-res.hopSeconds / kSideSeconds));

    // What the average keeps of itself at each transform, and what it takes
    // of the new one. Since the reset: everything, each counting the same.
    double keep = 1.0, gain = 1.0;

    if (mode != averageAll)
    {
        keep = std::exp (-res.hopSeconds / (mode == averageShort ? 3.0 : 10.0));
        gain = 1.0 - keep;
    }

    for (int p = res.first; p < res.last; ++p)
    {
        const auto i = (size_t) (p - res.first);
        const auto l = std::max (0.0, integral (res.powerL, res.runningL, res.high[i]) - integral (res.powerL, res.runningL, res.low[i]));
        const auto r = std::max (0.0, integral (res.powerR, res.runningR, res.high[i]) - integral (res.powerR, res.runningR, res.low[i]));
        const auto x = integral (res.cross, res.runningCross, res.high[i]) - integral (res.cross, res.runningCross, res.low[i]);
        const auto power = 0.5 * (l + r);

        live[p] += ((float) power > live[p] ? attack : release) * ((float) power - live[p]);

        if (live[p] < 1.0e-20f)
            live[p] = 0.0f;

        if (audible)
        {
            averageSum[p] = averageSum[p] * keep + power * gain;

            sideL[p] += side * ((float) l - sideL[p]);
            sideR[p] += side * ((float) r - sideR[p]);
            sideCross[p] += side * ((float) x - sideCross[p]);
        }
    }

    if (audible)
        res.weight = res.weight * keep + gain;
}

void Spectrum::publish()
{
    SpectrumFrame fresh;
    double averages[kSpectrumPoints];

    fresh.measured = true;

    for (const auto& resolution : resolutions)
    {
        fresh.measured = fresh.measured && resolution->weight > 0.0;

        for (int p = resolution->first; p < resolution->last; ++p)
            averages[p] = resolution->weight > 0.0 ? averageSum[p] / resolution->weight : 0.0;
    }

    // A third of an octave is four twelfths: the point, its neighbours, and
    // half of the two beyond. The quick curve looks at half as much, to stay
    // sharp, and is raised to what a whole third would hold.
    static const double wide[] = { 0.5, 1.0, 1.0, 1.0, 0.5 };
    static const double narrow[] = { 0.0, 0.5, 1.0, 0.5, 0.0 };

    for (int p = 0; p < kSpectrumPoints; ++p)
    {
        double quick = 0.0, quickWeight = 0.0, slow = 0.0, slowWeight = 0.0;
        double l = 0.0, r = 0.0, x = 0.0;

        for (int offset = -2; offset <= 2; ++offset)
        {
            const auto q = p + offset;

            if (q < 0 || q >= kSpectrumPoints)
                continue;

            quick += narrow[offset + 2] * live[q];
            quickWeight += narrow[offset + 2];
            slow += wide[offset + 2] * averages[q];
            slowWeight += wide[offset + 2];

            l += wide[offset + 2] * sideL[q];
            r += wide[offset + 2] * sideR[q];
            x += wide[offset + 2] * sideCross[q];
        }

        fresh.live[p] = levelOf (quick * 4.0 / quickWeight);
        fresh.average[p] = levelOf (slow * 4.0 / slowWeight);

        const auto both = std::sqrt (l * r);
        fresh.correlation[p] = both > 1.0e-14 ? (float) std::clamp (x / both, -1.0, 1.0) : 0.0f;
    }

    const std::lock_guard<std::mutex> lock (frameLock);
    frame = fresh;
}

void Spectrum::read (SpectrumFrame& destination) const
{
    const std::lock_guard<std::mutex> lock (frameLock);
    destination = frame;
}

} // namespace mtx
