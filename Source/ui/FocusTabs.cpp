#include "FocusTabs.h"

namespace onirique
{

using namespace theme;

const char* toolName (int tool) noexcept
{
    static const char* const names[kNumTools] = { "LOUDNESS", "HISTORY", "SPECTRUM", "WAVEFORM", "PHASE" };
    return names[jlimit (0, kNumTools - 1, tool)];
}

namespace
{
    /** A small picture of what each tool shows, in a square. */
    void pictogram (Graphics& g, int tool, Rectangle<float> r, Colour ink)
    {
        g.setColour (ink);
        const PathStrokeType stroke (1.5f, PathStrokeType::curved, PathStrokeType::rounded);

        switch (tool)
        {
            case (int) Tool::loudness:
            {
                // Three bars and the target across them.
                const float heights[] = { 0.55f, 0.85f, 0.7f };
                const auto w = r.getWidth() / 5.0f;

                for (int i = 0; i < 3; ++i)
                    g.fillRoundedRectangle (r.getX() + w * (0.5f + 1.6f * (float) i), r.getBottom() - r.getHeight() * heights[i],
                                            w, r.getHeight() * heights[i], 1.5f);

                g.fillRect (r.getX(), r.getY() + r.getHeight() * 0.32f, r.getWidth(), 1.2f);
                break;
            }

            case (int) Tool::history:
            {
                Path p;
                const float ys[] = { 0.7f, 0.45f, 0.6f, 0.25f, 0.5f, 0.35f, 0.55f };

                for (int i = 0; i < 7; ++i)
                {
                    const Point<float> at { r.getX() + r.getWidth() * (float) i / 6.0f, r.getY() + r.getHeight() * ys[i] };
                    i == 0 ? p.startNewSubPath (at) : p.lineTo (at);
                }

                g.strokePath (p, stroke);
                break;
            }

            case (int) Tool::spectrum:
            {
                Path p;
                p.startNewSubPath (r.getX(), r.getY() + r.getHeight() * 0.75f);
                p.cubicTo (r.getX() + r.getWidth() * 0.2f, r.getY() + r.getHeight() * 0.05f,
                           r.getX() + r.getWidth() * 0.45f, r.getY() + r.getHeight() * 0.35f,
                           r.getRight(), r.getY() + r.getHeight() * 0.8f);

                auto filled = p;
                filled.lineTo (r.getRight(), r.getBottom());
                filled.lineTo (r.getX(), r.getBottom());
                filled.closeSubPath();

                g.setColour (ink.withMultipliedAlpha (0.3f));
                g.fillPath (filled);
                g.setColour (ink);
                g.strokePath (p, stroke);
                break;
            }

            case (int) Tool::waveform:
            {
                // A waveform running down, the two sides about the middle.
                const float widths[] = { 0.3f, 0.7f, 0.5f, 0.9f, 0.4f, 0.75f, 0.55f };
                const auto h = r.getHeight() / 7.0f;

                for (int i = 0; i < 7; ++i)
                    g.fillRoundedRectangle (Rectangle<float> (r.getWidth() * widths[i], h * 0.6f)
                                                .withCentre ({ r.getCentreX(), r.getY() + h * ((float) i + 0.5f) }), 1.0f);
                break;
            }

            default:
            {
                // The goniometer: its diamond, and the sound in the middle.
                Path p;
                p.startNewSubPath (r.getCentreX(), r.getY());
                p.lineTo (r.getRight(), r.getCentreY());
                p.lineTo (r.getCentreX(), r.getBottom());
                p.lineTo (r.getX(), r.getCentreY());
                p.closeSubPath();
                g.strokePath (p, PathStrokeType (1.2f));
                g.fillEllipse (Rectangle<float> (r.getWidth() * 0.22f, r.getHeight() * 0.42f).withCentre (r.getCentre().translated (0.0f, -r.getHeight() * 0.06f)));
                break;
            }
        }
    }
}

//==============================================================================
void ViewSwitch::setFocus (bool shouldFocus)
{
    if (focus != shouldFocus)
    {
        focus = shouldFocus;
        repaint();
    }
}

void ViewSwitch::mouseDown (const MouseEvent& e)
{
    const auto wantsFocus = e.x > getWidth() / 2;

    if (wantsFocus != focus)
    {
        focus = wantsFocus;
        repaint();

        if (onChange != nullptr)
            onChange (focus);
    }
}

void ViewSwitch::mouseMove (const MouseEvent& e)
{
    const auto side = e.x > getWidth() / 2 ? 1 : 0;

    if (side != hover)
    {
        hover = side;
        setMouseCursor (MouseCursor::PointingHandCursor);
        repaint();
    }
}

void ViewSwitch::mouseExit (const MouseEvent&)
{
    hover = -1;
    repaint();
}

void ViewSwitch::paint (Graphics& g)
{
    const auto box = getLocalBounds().toFloat().reduced (0.5f);
    const auto radius = box.getHeight() * 0.5f;

    g.setColour (Colours::black.withAlpha (0.3f));
    g.fillRoundedRectangle (box, radius);
    g.setColour (Colour (0x26ffd2d6));
    g.drawRoundedRectangle (box, radius, 1.0f);

    for (int side = 0; side < 2; ++side)
    {
        auto half = box.reduced (2.5f);
        half = side == 0 ? half.removeFromLeft (half.getWidth() * 0.5f) : half.withTrimmedLeft (half.getWidth() * 0.5f);
        const auto on = side == (focus ? 1 : 0);
        const auto r = half.getHeight() * 0.5f;

        if (on)
        {
            g.setGradientFill (ColourGradient (ember.withAlpha (0.75f), 0.0f, half.getY(), ember.withAlpha (0.35f), 0.0f, half.getBottom(), false));
            g.fillRoundedRectangle (half, r);
            g.setColour (emberLit.withAlpha (0.9f));
            g.drawRoundedRectangle (half.reduced (0.5f), r, 1.0f);
        }
        else if (hover == side)
        {
            g.setColour (Colours::white.withAlpha (0.06f));
            g.fillRoundedRectangle (half, r);
        }

        g.setColour (on ? Colours::white : haze);
        g.setFont (capsFont (9.0f));
        g.drawText (side == 0 ? "FULL" : "FOCUS", half, Justification::centred, false);
    }
}

//==============================================================================
void FocusTabs::setSelected (int tool)
{
    tool = jlimit (0, kNumTools - 1, tool);

    if (tool != selected)
    {
        selected = tool;
        repaint();
    }
}

Rectangle<float> FocusTabs::tile (int tool) const
{
    const auto gap = 12.0f;
    const auto width = ((float) getWidth() - gap * (float) (kNumTools - 1)) / (float) kNumTools;
    return { (float) tool * (width + gap), 6.0f, width, (float) getHeight() - 12.0f };
}

Rectangle<int> FocusTabs::tileBounds (int tool) const
{
    return tile (tool).toNearestInt();
}

int FocusTabs::tileAt (Point<int> at) const
{
    for (int t = 0; t < kNumTools; ++t)
        if (tile (t).contains (at.toFloat()))
            return t;

    return -1;
}

void FocusTabs::mouseDown (const MouseEvent& e)
{
    if (const auto t = tileAt (e.getPosition()); t >= 0)
    {
        setSelected (t);

        if (onSelect != nullptr)
            onSelect (t);
    }
}

void FocusTabs::mouseMove (const MouseEvent& e)
{
    const auto t = tileAt (e.getPosition());

    if (t != hover)
    {
        hover = t;
        setMouseCursor (t >= 0 ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
    }
}

void FocusTabs::mouseExit (const MouseEvent&)
{
    hover = -1;
}

void FocusTabs::tick()
{
    bool moved = false;

    for (int t = 0; t < kNumTools; ++t)
    {
        const auto wantLit = t == selected ? 1.0f : 0.0f, wantHover = t == hover ? 1.0f : 0.0f;

        if (std::abs (lit[t] - wantLit) > 0.005f || std::abs (hovered[t] - wantHover) > 0.005f)
        {
            lit[t] += 0.35f * (wantLit - lit[t]);
            hovered[t] += 0.35f * (wantHover - hovered[t]);
            moved = true;
        }

        if (readout != nullptr)
            if (const auto text = readout (t); text != figures[t])
            {
                figures[t] = text;
                moved = true;
            }
    }

    if (moved)
        repaint();
}

void FocusTabs::paint (Graphics& g)
{
    for (int t = 0; t < kNumTools; ++t)
    {
        const auto r = tile (t).translated (0.0f, -3.0f * hovered[t]);
        const auto radius = 14.0f;
        const auto on = lit[t];

        // Chosen: the ember glow round it, as a module of VOCALBOX.
        if (on > 0.01f)
        {
            Path halo;
            halo.addRoundedRectangle (r, radius);
            DropShadow (ember.withAlpha (0.5f * on), 22, {}).drawForPath (g, halo);
        }

        g.setColour (Colour (0xff100b0d).withAlpha (0.85f));
        g.fillRoundedRectangle (r, radius);

        ColourGradient glassFill (Colours::white.withAlpha (0.07f), r.getX() + r.getWidth() * 0.3f, r.getY(),
                                  Colours::white.withAlpha (0.02f), r.getRight() - r.getWidth() * 0.3f, r.getBottom(), false);
        g.setGradientFill (glassFill);
        g.fillRoundedRectangle (r, radius);

        g.setColour (Colours::white.withAlpha (0.1f + 0.05f * hovered[t]));
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);

        if (on > 0.01f)
        {
            g.setColour (emberLit.withAlpha (0.75f * on));
            g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
            g.setColour (emberLit.withAlpha (0.45f * on));
            g.drawRoundedRectangle (r.reduced (1.5f), radius - 1.0f, 1.0f);
        }

        auto inside = r.reduced (14.0f, 10.0f);

        // The picture at the right.
        const auto picture = inside.removeFromRight (inside.getHeight()).reduced (4.0f);
        pictogram (g, t, picture, (on > 0.5f ? emberLit : haze).withAlpha (0.55f + 0.45f * jmax (on, hovered[t])));
        inside.removeFromRight (8.0f);

        // The light, the name, and the figure under it.
        auto top = inside.removeFromTop (inside.getHeight() * 0.5f);
        const auto led = Rectangle<float> (8.0f, 8.0f).withCentre ({ top.getX() + 4.0f, top.getCentreY() + 1.0f });

        if (on > 0.01f)
        {
            g.setColour (ember.withAlpha (0.35f * on));
            g.fillEllipse (led.expanded (3.5f));
        }

        g.setColour (ember.interpolatedWith (Colour (0xff4a3a3e), 1.0f - on));
        g.fillEllipse (led);
        top.removeFromLeft (16.0f);

        g.setColour (on > 0.5f ? Colours::white : mist);
        g.setFont (capsFont (10.0f).withExtraKerningFactor (0.2f));
        g.drawText (toolName (t), top, Justification::centredLeft, true);

        g.setColour (on > 0.5f ? mist : haze);
        g.setFont (monoFont (12.0f).boldened());
        g.drawText (figures[t], inside, Justification::centredLeft, true);
    }
}

} // namespace onirique
