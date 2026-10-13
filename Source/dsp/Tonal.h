// Onirique MeterBox — what the spectrum of a finished record looks like, to hold a mix against (no dependency)
#pragma once

#include "Spectrum.h"

namespace mtx
{

/** A shape the averaged spectrum is expected to have, and how far from it is
    still within it, at each point. It says nothing of how loud: it is laid on
    the spectrum before the two are compared. */
struct TonalTarget
{
    float mean[kSpectrumPoints] = {};
    float tolerance[kSpectrumPoints] = {};
};

/** The targets that are built in. They were drawn by reasoning, from what is
    known of the long-term spectrum of commercial records, not measured on a
    library of them: a reference captured from a record is worth more. */
constexpr int kNumStockTargets = 4;

const char* stockTargetName (int index) noexcept;
TonalTarget stockTarget (int index) noexcept;

/** A target made of a spectrum that was measured: its shape, give or take 3 dB. */
TonalTarget targetFrom (const float* averageDb) noexcept;

/** How far a target has to be raised to lie on a spectrum. */
float fitTarget (const TonalTarget&, const float* averageDb) noexcept;

/** The four wide regions the balance is summed up in. */
constexpr int kNumRegions = 4;

struct Region
{
    const char* name;
    float lowHz, highHz;
};

const Region& region (int index) noexcept;

/** For each region, how far what it holds is from what the target, once
    laid on the spectrum, would have it hold, and how far it may be. */
void compareRegions (const TonalTarget&, const float* averageDb, float offset,
                     float* deviation, float* tolerance) noexcept;

} // namespace mtx
