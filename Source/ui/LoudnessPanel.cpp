#include "LoudnessPanel.h"

namespace onirique
{

using namespace theme;

namespace
{
    // The scale the five bars share: LUFS and dBTP are both dB under full scale.
    constexpr float kTopDb = 3.0f, kBottomDb = -54.0f;

    constexpr int kHoldTicks = 45;      // a second and a half at the editor's pace

    String signedText (float value, int decimals)
    {
        const auto text = String (value, decimals);
        return (value > 0.0f && text.containsAnyOf ("123456789") ? "+" : "") + text;
    }
}

LoudnessPanel::LoudnessPanel (MeterBoxProcessor& p) : processor (p)
{
    setOpaque (true);

    for (auto& s : shown)
        s = mtx::kFloorDb;

    for (auto& h : held)
        h = mtx::kFloorDb;
}

void LoudnessPanel::refresh()
{
    values = processor.getMeter().loudness();

    const float readings[numBars] = { values.peakNow[0], values.peakNow[1], values.momentary, values.shortTerm, values.integrated };

    for (int i = 0; i < numBars; ++i)
    {
        const auto reading = jmax (readings[i], kBottomDb - 6.0f);

        // A peak is up at once and falls back; a loudness moves both ways alike.
        if (i <= peakRight)
            shown[i] = reading > shown[i] ? reading : shown[i] + 0.3f * (reading - shown[i]);
        else
            shown[i] += 0.5f * (reading - shown[i]);
    }

    for (int c = 0; c < 2; ++c)
    {
        if (values.peakNow[c] >= held[c])
        {
            held[c] = values.peakNow[c];
            holding[c] = kHoldTicks;
        }
        else if (--holding[c] < 0)
        {
            held[c] = jmax (held[c] - 0.8f, values.peakNow[c]);
        }
    }

    repaint();
}

void LoudnessPanel::paint (Graphics& g)
{
    paintPanel (g, getLocalBounds(), "LOUDNESS");

    const auto target = processor.getTargetLufs();
    const auto ceiling = processor.getCeilingDb();
    const auto measured = values.integrated > mtx::kFloorDb + 1.0f;

    auto area = getLocalBounds().toFloat().reduced (14.0f, 0.0f).withTrimmedTop ((float) kPanelHeading + 4.0f).withTrimmedBottom (12.0f);

    //==============================================================================
    // The integrated loudness, large, and how far it is from the target.
    {
        auto block = area.removeFromTop (86.0f);

        g.setColour (haze);
        g.setFont (capsFont (9.5f));
        g.drawText ("INTEGRATED", block.removeFromTop (13.0f), Justification::centredLeft, false);

        auto number = block.removeFromTop (48.0f);
        const auto font = displayFont (44.0f);
        const auto text = levelText (values.integrated);
        const auto width = GlyphArrangement::getStringWidth (font, text);

        g.setColour (Colours::white);
        g.setFont (font);
        g.drawText (text, number.withWidth (width + 4.0f), Justification::centredLeft, false);

        g.setColour (haze);
        g.setFont (capsFont (10.0f));
        g.drawText ("LUFS", number.withTrimmedLeft (width + 10.0f).withTrimmedBottom (8.0f), Justification::bottomLeft, false);

        auto status = block.removeFromTop (18.0f);

        g.setColour (haze);
        g.setFont (monoFont (11.0f));
        g.drawText ("TARGET " + String (target, 1), status, Justification::centredLeft, false);

        if (measured)
        {
            // Over the target is what a platform turns down; under it costs nothing.
            const auto away = values.integrated - target;

            g.setColour (away > 0.5f ? emberLit : away > -0.5f ? Colours::white : haze);
            g.setFont (monoFont (11.0f).boldened());
            g.drawText (signedText (away, 1) + " LU", status, Justification::centredRight, false);
        }
    }

    area.removeFromTop (6.0f);

    //==============================================================================
    // The numbers, at the foot.
    {
        auto rows = area.removeFromBottom (5.0f * 21.0f);
        const auto highest = jmax (values.truePeak[0], values.truePeak[1]);

        auto row = [&] (const String& name, const String& value, const String& extra, Colour valueColour)
        {
            auto r = rows.removeFromTop (21.0f);

            g.setColour (haze);
            g.setFont (capsFont (9.5f));
            g.drawText (name, r, Justification::centredLeft, false);

            g.setFont (monoFont (10.5f));
            g.drawText (extra, r.removeFromRight (74.0f), Justification::centredRight, false);

            g.setColour (valueColour);
            g.setFont (monoFont (12.5f).boldened());
            g.drawText (value, r, Justification::centredRight, false);
        };

        row ("SHORT-TERM", levelText (values.shortTerm), "MAX " + levelText (values.shortTermMax), mist);
        row ("MOMENTARY", levelText (values.momentary), "MAX " + levelText (values.momentaryMax), mist);
        row ("RANGE", measured ? String (values.range, 1) : String ("--"), "LU", mist);
        row ("TRUE PEAK", levelText (highest), highest > ceiling ? "OVER" : "dBTP", highest > ceiling ? emberLit : mist);
        row ("PLR", measured ? String (values.peakToLoudness, 1) : String ("--"),
             "PSR " + (values.shortTerm > -70.0f ? String (values.peakToShort, 1) : String ("--")), mist);
    }

    area.removeFromBottom (10.0f);

    //==============================================================================
    // The bars.
    auto names = area.removeFromBottom (16.0f);
    auto heads = area.removeFromTop (14.0f);
    const auto scale = area.removeFromLeft (24.0f);
    area.removeFromLeft (6.0f);
    names.removeFromLeft (30.0f);
    heads.removeFromLeft (30.0f);

    const auto track = area;
    auto yOf = [track] (float db) { return track.getY() + track.getHeight() * (kTopDb - jlimit (kBottomDb, kTopDb, db)) / (kTopDb - kBottomDb); };

    g.setFont (monoFont (9.0f));

    for (int db = 0; db >= (int) kBottomDb; db -= 6)
    {
        const auto y = yOf ((float) db);

        g.setColour (db == 0 ? Colour (0x40ffd2d6) : line);
        g.fillRect (track.getX(), y, track.getWidth(), 1.0f);

        if (db % 12 == 0)
        {
            g.setColour (haze.withAlpha (0.7f));
            g.drawText (String (db), Rectangle<float> (scale.getX(), y - 5.0f, scale.getWidth(), 10.0f), Justification::centredRight, false);
        }
    }

    // Two narrow ones for the true peak, three wide ones for the loudness,
    // and room after each group for the arrow of its limit.
    const auto narrow = 12.0f, gap = 4.0f, between = 14.0f, arrowRoom = 10.0f;
    const auto wide = (track.getWidth() - 2.0f * narrow - gap - between - 2.0f * gap - arrowRoom) / 3.0f;

    Rectangle<float> bars[numBars];
    auto x = track.getX();

    for (int i = 0; i < numBars; ++i)
    {
        const auto width = i <= peakRight ? narrow : wide;
        bars[i] = { x, track.getY(), width, track.getHeight() };
        x += width + (i == peakRight ? between : gap);
    }

    static const char* const barNames[numBars] = { "L", "R", "M", "S", "I" };

    for (int i = 0; i < numBars; ++i)
    {
        const auto bar = bars[i];
        const auto limit = i <= peakRight ? ceiling : target;

        g.setColour (Colour (0x59000000));
        g.fillRoundedRectangle (bar, 3.0f);

        const auto inner = bar.reduced (1.0f);
        const auto top = yOf (shown[i]);
        const auto limitY = yOf (limit);

        if (shown[i] > kBottomDb)
        {
            // Dark at the foot and ember towards the limit; past it, white hot.
            g.setGradientFill (ColourGradient (ember, 0.0f, limitY, blood, 0.0f, inner.getBottom(), false));
            g.fillRect (inner.withTop (jmax (top, limitY)));

            if (top < limitY)
            {
                g.setGradientFill (ColourGradient (Colours::white, 0.0f, inner.getY(), emberLit, 0.0f, limitY, false));
                g.fillRect (inner.withTop (top).withBottom (limitY));
            }
        }

        g.setColour (line);
        g.drawRoundedRectangle (bar, 3.0f, 1.0f);

        // What each has lately reached: the peak a moment ago, the loudest so far.
        const auto mark = i == peakLeft ? held[0] : i == peakRight ? held[1]
                        : i == momentary ? values.momentaryMax : i == shortTerm ? values.shortTermMax : mtx::kFloorDb;

        if (mark > kBottomDb)
        {
            g.setColour (mark > limit ? Colours::white : mist.withAlpha (0.75f));
            g.fillRect (inner.getX(), yOf (mark) - 1.0f, inner.getWidth(), 2.0f);
        }

        g.setColour (haze);
        g.setFont (capsFont (9.0f));
        g.drawText (barNames[i], Rectangle<float> (bar.getX() - 4.0f, names.getY(), bar.getWidth() + 8.0f, names.getHeight()),
                    Justification::centred, false);
    }

    // The limits: the ceiling across the peaks, the target across the loudness.
    auto limitLine = [&] (float db, Rectangle<float> across)
    {
        const auto y = yOf (db);

        g.setColour (Colours::white.withAlpha (0.85f));
        g.fillRect (across.getX() - 3.0f, y - 0.5f, across.getWidth() + 6.0f, 1.0f);

        Path arrow;
        arrow.addTriangle (across.getRight() + 3.0f, y, across.getRight() + 8.0f, y - 3.5f, across.getRight() + 8.0f, y + 3.5f);
        g.fillPath (arrow);
    };

    limitLine (ceiling, bars[peakLeft].getUnion (bars[peakRight]));
    limitLine (target, bars[momentary].getUnion (bars[integrated]));

    g.setColour (haze);
    g.setFont (capsFont (8.5f));
    g.drawText ("TP", bars[peakLeft].getUnion (bars[peakRight]).withY (heads.getY()).withHeight (heads.getHeight()).expanded (6.0f, 0.0f),
                Justification::centred, false);
    g.drawText ("LUFS", bars[momentary].getUnion (bars[integrated]).withY (heads.getY()).withHeight (heads.getHeight()),
                Justification::centred, false);
}

} // namespace onirique
