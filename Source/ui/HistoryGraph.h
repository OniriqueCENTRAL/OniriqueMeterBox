// Onirique MeterBox — the loudness over time, and how it is spread
#pragma once

#include "../PluginProcessor.h"
#include "Controls.h"

namespace onirique
{

/** The loudness since the reset, drawn against time: the momentary loudness
    as a ground, the short-term one as a line over it, the integrated one as
    the line that settles, and the target across. A mark at the top wherever
    the true peak went over its ceiling.

    At the right, the same scale turned into a count: how long the
    short-term loudness has spent at each level. */
class HistoryGraph final : public Component
{
public:
    explicit HistoryGraph (MeterBoxProcessor&);

    void paint (Graphics&) override;
    void resized() override;

    /** How far back it looks. */
    ComboBox spanBox;

private:
    MeterBoxProcessor& processor;
    std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> spanAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HistoryGraph)
};

} // namespace onirique
