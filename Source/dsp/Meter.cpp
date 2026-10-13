#include "Meter.h"

#include <algorithm>

namespace mtx
{

namespace
{
    // The oldest points of a full history are the next to be written over:
    // they are not handed out.
    constexpr uint32_t kHistoryMargin = 16;
}

Meter::Meter()
    : history (new std::atomic<float>[(size_t) kHistoryCapacity * 4]),
      scope (new std::atomic<float>[(size_t) kScopeFrames * 2])
{
    for (size_t i = 0; i < (size_t) kHistoryCapacity * 4; ++i)
        history[i].store (kFloorDb, std::memory_order_relaxed);

    for (size_t i = 0; i < (size_t) kScopeFrames * 2; ++i)
        scope[i].store (0.0f, std::memory_order_relaxed);

    for (auto& s : spread)
        s.store (0, std::memory_order_relaxed);

    prepare (48000.0, 2);
}

void Meter::prepare (double sampleRate, int numChannels)
{
    channels = std::clamp (numChannels, 1, kMaxChannels);

    meter.prepare (sampleRate, channels);
    field.prepare (sampleRate);
    spectrum.prepare (sampleRate);

    // The goniometer is drawn from the same stretch of time whatever the rate.
    scopeStride = sampleRate > 150000.0 ? 4 : sampleRate > 75000.0 ? 2 : 1;
    scopePhase = 0;

    loudnessShared.store (meter.values());
    stereoShared.store (field.values());
    historyCount.store (0, std::memory_order_release);

    for (auto& s : spread)
        s.store (0, std::memory_order_relaxed);
}

void Meter::requestReset() noexcept
{
    resetPending.store (true, std::memory_order_release);
    spectrum.requestReset();
}

void Meter::process (const float* const* in, int numChannels, int numSamples, bool running) noexcept
{
    if (numChannels < 1 || numSamples <= 0)
        return;

    if (resetPending.exchange (false, std::memory_order_acq_rel))
    {
        meter.reset();
        historyCount.store (0, std::memory_order_release);
        loudnessShared.store (meter.values());

        for (auto& s : spread)
            s.store (0, std::memory_order_relaxed);
    }

    const auto* left = in[0];
    const auto* right = numChannels > 1 && channels > 1 ? in[1] : in[0];

    // The loudness, a step at a time: each one that ends is a point of the history.
    const float* at[kMaxChannels] = { left, right };

    for (int remaining = numSamples; remaining > 0;)
    {
        const auto used = meter.process (at, remaining, running);

        if (meter.stepped())
        {
            const auto& values = meter.values();
            loudnessShared.store (values);

            if (running)
            {
                const auto index = historyCount.load (std::memory_order_relaxed);
                auto* point = history.get() + (size_t) (index % kHistoryCapacity) * 4;

                point[0].store (values.momentary, std::memory_order_relaxed);
                point[1].store (values.shortTerm, std::memory_order_relaxed);
                point[2].store (values.integrated, std::memory_order_relaxed);
                point[3].store (std::max (values.peakNow[0], values.peakNow[1]), std::memory_order_relaxed);

                historyCount.store (index + 1, std::memory_order_release);

                for (int bin = 0; bin < kDistributionBins; ++bin)
                    spread[bin].store (meter.distribution (bin), std::memory_order_relaxed);
            }
        }

        at[0] += used;
        at[1] += used;
        remaining -= used;
    }

    field.process (left, right, numSamples);
    stereoShared.store (field.values());

    spectrum.push (left, right, numSamples);

    auto written = scopeCount.load (std::memory_order_relaxed);

    for (int i = 0; i < numSamples; ++i)
    {
        if (scopePhase == 0)
        {
            const auto slot = (size_t) (written % (uint32_t) kScopeFrames) * 2;
            scope[slot].store (left[i], std::memory_order_relaxed);
            scope[slot + 1].store (right[i], std::memory_order_relaxed);
            ++written;
        }

        scopePhase = (scopePhase + 1) % scopeStride;
    }

    scopeCount.store (written, std::memory_order_release);
}

uint32_t Meter::historyStart() const noexcept
{
    const auto end = historyEnd();
    return end > kHistoryCapacity - kHistoryMargin ? end - (kHistoryCapacity - kHistoryMargin) : 0;
}

HistoryPoint Meter::historyAt (uint32_t index) const noexcept
{
    const auto* point = history.get() + (size_t) (index % kHistoryCapacity) * 4;

    return { point[0].load (std::memory_order_relaxed), point[1].load (std::memory_order_relaxed),
             point[2].load (std::memory_order_relaxed), point[3].load (std::memory_order_relaxed) };
}

int Meter::readScope (float* left, float* right, int maxFrames) const noexcept
{
    const auto end = scopeCount.load (std::memory_order_acquire);
    const auto count = (uint32_t) std::clamp (maxFrames, 0, kScopeFrames - 64);
    const auto frames = std::min (count, end);

    for (uint32_t i = 0; i < frames; ++i)
    {
        const auto slot = (size_t) ((end - frames + i) % (uint32_t) kScopeFrames) * 2;
        left[i] = scope[slot].load (std::memory_order_relaxed);
        right[i] = scope[slot + 1].load (std::memory_order_relaxed);
    }

    return (int) frames;
}

} // namespace mtx
