// Onirique MeterBox — the two sides against each other: goniometer, correlation, and correlation by frequency
#pragma once

#include "../PluginProcessor.h"
#include "Controls.h"

namespace onirique
{

/** What the phase of a mix looks like, three ways.

    The goniometer draws each sample as a point: what is the same on both
    sides goes up the middle, what is only on one leans to that side, and
    what is out of phase spreads sideways. It is drawn at the same size
    whatever the level.

    The bar is the correlation as a number, from -1 to +1, with the lowest it
    has lately been. The curve at the foot is the same thing, frequency by
    frequency: where it dips under the middle, that part of the spectrum
    goes missing in mono. */
class PhasePanel final : public Component
{
public:
    explicit PhasePanel (MeterBoxProcessor&);

    void paint (Graphics&) override;

    /** Takes the latest readings: once a tick of the editor. */
    void refresh (const mtx::SpectrumFrame&);

private:
    void renderScope();

    static constexpr int kScopeSize = 240;

    MeterBoxProcessor& processor;
    mtx::StereoValues values;
    float correlation[mtx::kSpectrumPoints] = {};
    bool measured = false;

    // The goniometer: what each point of it has lately been lit by, and the
    // picture made of that.
    std::vector<float> glow;
    std::vector<float> left, right;
    Image scope;
    PixelARGB shades[256];
    float envelope = 0.0f;

    float shown = 0.0f;         // the correlation as the bar draws it
    float lowest = 1.0f;        // and the lowest it has lately been
    int holding = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhasePanel)
};

} // namespace onirique
