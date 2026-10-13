// Onirique MeterBox — the waveform of everything measured, top to bottom
#pragma once

#include "../PluginProcessor.h"
#include "Controls.h"

namespace onirique
{

/** The whole of what has been measured since the reset, as a waveform that
    runs down the panel: the start at the top, now at the bottom, the left
    side to the left of the middle and the right side to the right. The
    crests are the outline, the RMS level the paler core, and a white mark at
    the edge is a true peak over its ceiling.

    It always shows everything: it is the view of the whole. */
class WaveformPanel final : public Component
{
public:
    explicit WaveformPanel (MeterBoxProcessor&);

    void paint (Graphics&) override;

private:
    MeterBoxProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformPanel)
};

} // namespace onirique
