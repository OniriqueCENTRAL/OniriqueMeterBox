// Onirique MeterBox — the spectrum, and the shape it is expected to have
#pragma once

#include "../PluginProcessor.h"
#include "Controls.h"

namespace onirique
{

/** Three things on one scale. The spectrum as it stands, filled, moving
    with the sound. Its average, a white line that settles. And the target:
    a band laid on the average, the shape a finished record of that kind
    has. Where the line leaves the band, it turns red.

    Under it, the same comparison summed up in four regions. */
class SpectrumDisplay final : public Component
{
public:
    explicit SpectrumDisplay (MeterBoxProcessor&);

    void paint (Graphics&) override;
    void resized() override;

    /** Takes the latest picture: once a tick of the editor. */
    void refresh (const mtx::SpectrumFrame&);

    /** The shape held against, how long the average is, and the button
        that makes a shape of what is playing. */
    ComboBox targetBox, averageBox;
    TextButton captureButton { "CAPTURE" };

private:
    MeterBoxProcessor& processor;
    mtx::SpectrumFrame frame;

    std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> targetAttachment, averageAttachment;
    int captured = 0;       // ticks left for the button to say so

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumDisplay)
};

} // namespace onirique
