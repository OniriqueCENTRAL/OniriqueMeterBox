// Onirique MeterBox — the reference: A/B between the mix and a record, at the same loudness
#pragma once

#include "../PluginProcessor.h"
#include "Controls.h"

namespace onirique
{

/** The titles a search found, as a music app lists them: the cover, the title
    over the artist and the album, the length. Opens over the graphs, above
    the strip; a click takes one. */
class SearchResults final : public Component
{
public:
    void show (const Array<ReferencePlayer::Found>&, const String& message);
    void setCover (int index, const Image&);
    void clear();

    std::function<void (const ReferencePlayer::Found&)> onPick;

    /** The height it wants, for the rows it has. */
    int wantedHeight() const;

    void paint (Graphics&) override;
    void mouseMove (const MouseEvent&) override;
    void mouseExit (const MouseEvent&) override;
    void mouseDown (const MouseEvent&) override;

private:
    int rowAt (int y) const;

    static constexpr int kRow = 50, kHeading = 30;

    Array<ReferencePlayer::Found> found;
    std::vector<Image> covers;
    String message;
    int hover = -1;
};

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
    void mouseDoubleClick (const MouseEvent&) override;
    void mouseMove (const MouseEvent&) override;
    void mouseExit (const MouseEvent&) override;

    /** Once a tick of the editor. */
    void refresh();

    /** Opens the file browser. */
    void browse();

    /** While a file or a link is being dragged over the window. */
    void setDragging (bool);

    /** Opens the field a title, an artist or a link is typed into, or, open,
        searches for what is in it. */
    void openSearch();

    TextButton matchButton { "MATCH" }, lockButton { "LOCK" }, searchButton { "SEARCH" }, loadButton { "LOAD" };

    /** What a search found: the editor lays it over the graphs. */
    SearchResults results;

    /** The parts a description is given for. */
    Rectangle<int> getSwitchArea() const noexcept { return switchArea; }
    Rectangle<int> getTrackArea() const noexcept { return trackArea; }

    /** Two parts of the strip that the editor describes as if they were controls. */
    Component switchZone, trackZone;

private:
    void seekTo (int x);
    void runSearch();
    void closeSearch();
    void pick (const ReferencePlayer::Found&);

    TextEditor linkField;
    String linkProblem;
    int searchGeneration = 0;           // a search answered after a newer one began is dropped
    bool searching = false;
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
