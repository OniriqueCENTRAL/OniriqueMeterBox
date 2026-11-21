// Onirique MeterBox — the reference: A/B between the mix and a record, at the same loudness
#pragma once

#include "../PluginProcessor.h"
#include "Controls.h"

namespace onirique
{

/** A strip across the window, under the graphs.

    At the left, the switch: A the mix, B the reference. In the middle, the
    reference itself: dropped there from the Finder or the Explorer, or picked
    by clicking; once in place, its outline, where it is playing (click to
    move), and its figures. At the right, whether it is brought to the
    loudness of the mix, by how much, and the buttons to change it or take
    it out. */
class ReferenceStrip final : public Component
{
public:
    explicit ReferenceStrip (MeterBoxProcessor&);

    void paint (Graphics&) override;
    void resized() override;

    void mouseDown (const MouseEvent&) override;
    void mouseDrag (const MouseEvent&) override;
    void mouseMove (const MouseEvent&) override;
    void mouseExit (const MouseEvent&) override;

    /** Once a tick of the editor. */
    void refresh();

    /** Opens the file browser. */
    void browse();

    /** While a file or a link is being dragged over the window. */
    void setDragging (bool);

    /** Opens the field a link to a track on a platform is pasted into, or,
        open, fetches what is in it. */
    void openLink();

    TextButton matchButton { "MATCH" }, linkButton { "LINK" }, loadButton { "LOAD" }, ejectButton { "EJECT" };

    /** The parts a description is given for. */
    Rectangle<int> getSwitchArea() const noexcept { return switchArea; }
    Rectangle<int> getTrackArea() const noexcept { return trackArea; }

    /** Two parts of the strip that the editor describes as if they were controls. */
    Component switchZone, trackZone;

private:
    void seekTo (int x);
    void fetchLink();
    void closeLink();

    TextEditor linkField;
    String linkProblem;
    Rectangle<float> outlineArea() const;

    MeterBoxProcessor& processor;
    std::unique_ptr<FileChooser> chooser;
    std::shared_ptr<const ReferenceTrack> track;

    Rectangle<int> switchArea, trackArea;
    bool dragging = false;
    int hoverX = -1;
    int hoverSide = -1;         // which half of the switch the pointer is over

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReferenceStrip)
};

} // namespace onirique
