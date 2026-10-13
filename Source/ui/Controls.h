// Onirique MeterBox — the one control of the range a meter has a use for, and what its panels are drawn with
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../dsp/Loudness.h"
#include "Theme.h"

namespace onirique
{

using namespace juce;

/** A value in a capsule with its name beside it, dragged up and down: the
    controls of the foot of the window, which are set once and left. */
class ValueField final : public Slider
{
public:
    explicit ValueField (const String& name) : title (name)
    {
        setSliderStyle (Slider::RotaryVerticalDrag);
        setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        setMouseDragSensitivity (220);
    }

    void paint (Graphics& g) override
    {
        using namespace theme;

        auto bounds = getLocalBounds().toFloat();
        const auto font = capsFont (10.0f);
        const auto titleWidth = GlyphArrangement::getStringWidth (font, title) + 10.0f;

        g.setColour (haze);
        g.setFont (font);
        g.drawText (title, bounds.removeFromLeft (titleWidth), Justification::centredLeft, false);

        const auto pill = bounds.reduced (0.5f, 3.5f);
        const auto radius = pill.getHeight() * 0.5f;

        g.setColour (Colour (0x73000000));
        g.fillRoundedRectangle (pill, radius);
        g.setColour (isMouseOverOrDragging() ? ember.withAlpha (0.7f) : Colour (0x33ffd2d6));
        g.drawRoundedRectangle (pill, radius, 1.0f);

        g.setColour (mist);
        g.setFont (monoFont (11.0f).boldened());
        g.drawText (getTextFromValue (getValue()), pill, Justification::centred, false);
    }

    void mouseEnter (const MouseEvent& e) override { Slider::mouseEnter (e); repaint(); }
    void mouseExit (const MouseEvent& e) override  { Slider::mouseExit (e); repaint(); }

private:
    String title;
};

//==============================================================================
constexpr int kPanelHeading = 26;

/** The ground each part of the window stands on, a shade redder than the
    rest, under its name. */
inline void paintPanel (Graphics& g, Rectangle<int> bounds, const String& name)
{
    using namespace theme;

    const auto panel = bounds.toFloat().reduced (0.5f);

    g.fillAll (night);
    g.setColour (deep);
    g.fillRoundedRectangle (panel, 10.0f);
    g.setColour (line);
    g.drawRoundedRectangle (panel, 10.0f, 1.0f);

    g.setColour (haze);
    g.setFont (capsFont (9.5f).withExtraKerningFactor (0.3f));
    g.drawText (name, bounds.withHeight (kPanelHeading).withTrimmedLeft (14).translated (0, 2), Justification::centredLeft, false);
}

/** A level as it is written: a dash while there is nothing to write. */
inline String levelText (float db, int decimals = 1)
{
    return db <= mtx::kFloorDb + 1.0f ? String ("--") : String (db, decimals);
}

/** A curve through points, with the corners taken off. */
inline Path curveThrough (const std::vector<Point<float>>& points)
{
    Path path;

    if (points.empty())
        return path;

    path.startNewSubPath (points.front());

    for (size_t i = 1; i + 1 < points.size(); ++i)
        path.quadraticTo (points[i], (points[i] + points[i + 1]) * 0.5f);

    path.lineTo (points.back());
    return path;
}

/** Where a frequency is on a scale of octaves from 20 Hz to 20 kHz. */
inline float xOfHz (Rectangle<float> plot, float hz)
{
    return plot.getX() + plot.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f);
}

} // namespace onirique
