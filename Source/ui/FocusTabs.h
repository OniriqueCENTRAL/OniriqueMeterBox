// Onirique MeterBox — the simplified view: one tool at a time, chosen from a row of tiles, as in VOCALBOX
#pragma once

#include "Controls.h"

namespace onirique
{

/** The five tools of the window, in the order they are shown. */
enum class Tool { loudness = 0, history, spectrum, waveform, phase };
constexpr int kNumTools = 5;

const char* toolName (int tool) noexcept;

/** FULL / FOCUS: every tool at once, or one at a time. */
class ViewSwitch final : public Component
{
public:
    std::function<void (bool focus)> onChange;

    void setFocus (bool);
    bool isFocus() const noexcept { return focus; }

    void paint (Graphics&) override;
    void mouseDown (const MouseEvent&) override;
    void mouseMove (const MouseEvent&) override;
    void mouseExit (const MouseEvent&) override;

private:
    bool focus = false;
    int hover = -1;
};

/** The row of tiles of the simplified view, one to a tool, as the modules
    of VOCALBOX are: its light, its name, a picture of what it shows, and its
    main figure as it stands. A click shows that tool alone. */
class FocusTabs final : public Component
{
public:
    /** The figure under each name, asked once a tick. */
    std::function<String (int tool)> readout;
    std::function<void (int tool)> onSelect;

    void setSelected (int tool);
    int getSelected() const noexcept { return selected; }

    /** Once a tick of the editor: the figures, and the lights' ease. */
    void tick();

    void paint (Graphics&) override;
    void mouseDown (const MouseEvent&) override;
    void mouseMove (const MouseEvent&) override;
    void mouseExit (const MouseEvent&) override;

    /** Where a tile is: the editor describes them one by one. */
    Rectangle<int> tileBounds (int tool) const;

private:
    Rectangle<float> tile (int tool) const;
    int tileAt (Point<int>) const;

    int selected = 0, hover = -1;
    float lit[kNumTools] = {}, hovered[kNumTools] = {};
    String figures[kNumTools];
};

} // namespace onirique
