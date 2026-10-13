// Onirique MeterBox — the loudness as it stands: the integrated figure, five bars, and the numbers under them
#pragma once

#include "../PluginProcessor.h"
#include "Controls.h"

namespace onirique
{

/** Reads from top to bottom, from what matters most: the integrated loudness
    against the target, then the levels as they move (the true peak of each
    side, the momentary, short-term and integrated loudness on one scale),
    then everything else as numbers.

    It only shows; nothing in it is dragged. */
class LoudnessPanel final : public Component
{
public:
    explicit LoudnessPanel (MeterBoxProcessor&);

    void paint (Graphics&) override;

    /** Takes the latest readings: once a tick of the editor. */
    void refresh();

private:
    enum Bar { peakLeft, peakRight, momentary, shortTerm, integrated, numBars };

    MeterBoxProcessor& processor;
    mtx::LoudnessValues values;

    float shown[numBars];       // where each bar is drawn: it eases towards its reading
    float held[2];              // the highest each true peak has lately been, and for how long it stays
    int holding[2] = {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoudnessPanel)
};

} // namespace onirique
