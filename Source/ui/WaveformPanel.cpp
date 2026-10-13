#include "WaveformPanel.h"

namespace onirique
{

using namespace theme;

namespace
{
    String clockText (int seconds)
    {
        return String (seconds / 60) + ":" + String (seconds % 60).paddedLeft ('0', 2);
    }
}

WaveformPanel::WaveformPanel (MeterBoxProcessor& p) : processor (p)
{
    setOpaque (true);
}

void WaveformPanel::paint (Graphics& g)
{
    paintPanel (g, getLocalBounds(), "WAVEFORM");

    const auto& meter = processor.getMeter();
    const auto ceiling = processor.getCeilingDb();

    auto body = getLocalBounds().toFloat().reduced (10.0f, 0.0f).withTrimmedTop ((float) kPanelHeading + 4.0f).withTrimmedBottom (12.0f);
    const auto names = body.removeFromTop (14.0f);
    const auto plot = body;
    const auto centre = plot.getCentreX();
    const auto reach = plot.getWidth() * 0.5f - 4.0f;       // full scale

    g.setColour (haze);
    g.setFont (capsFont (9.0f));
    g.drawText ("L", names.withWidth (names.getWidth() * 0.5f), Justification::centred, false);
    g.drawText ("R", names.withTrimmedLeft (names.getWidth() * 0.5f), Justification::centred, false);

    g.setColour (Colour (0x40000000));
    g.fillRoundedRectangle (plot, 6.0f);

    // The grid: the middle, -6 dB and full scale on each side.
    g.setColour (line);

    for (const auto amplitude : { -1.0f, -0.5f, 0.5f, 1.0f })
        g.fillRect (centre + amplitude * reach, plot.getY(), 1.0f, plot.getHeight());

    //==============================================================================
    // Everything there is, and never less than thirty seconds' worth of room.
    const auto end = meter.historyEnd();
    const auto first = meter.historyStart();
    const auto count = (int) (end - first);
    const auto visible = jmax (30 * mtx::kStepsPerSecond, count);

    auto yOf = [plot, first, visible] (double index) { return plot.getY() + plot.getHeight() * (float) ((index - first) / (double) visible); };

    // Time down the side, counted from the reset.
    const auto seconds = visible / mtx::kStepsPerSecond;
    const auto firstSecond = (int) first / mtx::kStepsPerSecond;
    int every = 5;

    for (const auto candidate : { 5, 10, 30, 60, 120, 300, 600, 1200 })
    {
        every = candidate;

        if (seconds / candidate <= 7)
            break;
    }

    for (auto t = (firstSecond / every + 1) * every; t < firstSecond + seconds; t += every)
        g.fillRect (plot.getX(), yOf ((double) t * mtx::kStepsPerSecond), plot.getWidth(), 1.0f);

    if (count >= 2)
    {
        Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (plot.toNearestInt());

        // No more rows than there are pixels: each stands for the highest
        // of the stretch it covers.
        const auto steps = jlimit (2, count, roundToInt (plot.getHeight() * (float) count / (float) visible));

        struct Row { float y, peakLeft, peakRight, rmsLeft, rmsRight; bool over; };
        std::vector<Row> rows;
        rows.reserve ((size_t) steps);

        for (int s = 0; s < steps; ++s)
        {
            const auto from = first + (uint32) ((int64) s * count / steps);
            const auto to = jmax (from + 1, first + (uint32) ((int64) (s + 1) * count / steps));
            Row row { yOf (0.5 * ((double) from + (double) to)), 0.0f, 0.0f, 0.0f, 0.0f, false };

            for (auto i = from; i < to; ++i)
            {
                const auto point = meter.historyAt (i);

                row.peakLeft = jmax (row.peakLeft, point.peakLeft);
                row.peakRight = jmax (row.peakRight, point.peakRight);
                row.rmsLeft = jmax (row.rmsLeft, point.rmsLeft);
                row.rmsRight = jmax (row.rmsRight, point.rmsRight);
                row.over = row.over || point.truePeak > ceiling;
            }

            rows.push_back (row);
        }

        // Down the left side and back up the right.
        auto outline = [&] (float Row::* left, float Row::* right)
        {
            Path path;
            path.startNewSubPath (centre, rows.front().y);

            for (const auto& row : rows)
                path.lineTo (centre - jmin (1.05f, row.*left) * reach, row.y);

            path.lineTo (centre, rows.back().y);

            for (auto row = rows.rbegin(); row != rows.rend(); ++row)
                path.lineTo (centre + jmin (1.05f, (*row).*right) * reach, row->y);

            path.closeSubPath();
            return path;
        };

        ColourGradient crests (blood, centre, 0.0f, ember, centre + reach, 0.0f, true);
        g.setGradientFill (crests);
        g.fillPath (outline (&Row::peakLeft, &Row::peakRight));

        g.setColour (bone.withAlpha (0.75f));
        g.fillPath (outline (&Row::rmsLeft, &Row::rmsRight));

        // Where the true peak went over its ceiling.
        g.setColour (Colours::white);

        for (const auto& row : rows)
            if (row.over)
            {
                g.fillRect (plot.getX(), row.y - 1.0f, 6.0f, 2.0f);
                g.fillRect (plot.getRight() - 6.0f, row.y - 1.0f, 6.0f, 2.0f);
            }
    }

    g.setColour (Colour (0x40ffd2d6));
    g.fillRect (centre, plot.getY(), 1.0f, plot.getHeight());

    // The times, written over the waveform so that they can always be read.
    g.setFont (monoFont (9.0f));

    for (auto t = (firstSecond / every + 1) * every; t < firstSecond + seconds; t += every)
    {
        const auto label = Rectangle<float> (plot.getX() + 2.0f, yOf ((double) t * mtx::kStepsPerSecond) - 12.0f, 30.0f, 11.0f);

        g.setColour (deep.withAlpha (0.75f));
        g.fillRoundedRectangle (label, 3.0f);
        g.setColour (haze);
        g.drawText (clockText (t), label, Justification::centred, false);
    }
}

} // namespace onirique
