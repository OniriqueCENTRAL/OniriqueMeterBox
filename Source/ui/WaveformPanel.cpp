#include "WaveformPanel.h"

namespace onirique
{

using namespace theme;

namespace
{
    String dbText (float amplitude)
    {
        return amplitude > 1.0e-5f ? String (20.0f * std::log10 (amplitude), 1) : String ("--");
    }

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
    auto figures = body.removeFromBottom (6.0f * 17.0f);
    body.removeFromBottom (6.0f);
    const auto marks = body.removeFromBottom (11.0f);
    const auto plot = body;
    const auto centre = plot.getCentreX();
    const auto reach = plot.getWidth() * 0.5f - 4.0f;       // full scale

    g.setColour (haze);
    g.setFont (capsFont (9.0f));
    g.drawText ("L", names.withWidth (names.getWidth() * 0.5f), Justification::centred, false);
    g.drawText ("R", names.withTrimmedLeft (names.getWidth() * 0.5f), Justification::centred, false);

    g.setColour (Colour (0x40000000));
    g.fillRoundedRectangle (plot, 6.0f);

    // The grid: the middle, then -12 dB, -6 dB and full scale on each side,
    // the last two named under the waveform.
    g.setFont (monoFont (8.5f));

    for (const auto& [amplitude, name] : std::initializer_list<std::pair<float, const char*>> { { 0.25f, "" }, { 0.5f, "-6" }, { 1.0f, "0" } })
        for (const auto side : { -1.0f, 1.0f })
        {
            const auto x = centre + side * amplitude * reach;

            g.setColour (line);
            g.fillRect (x, plot.getY(), 1.0f, plot.getHeight());
            g.setColour (haze.withAlpha (0.7f));
            g.drawText (name, Rectangle<float> (x - 10.0f, marks.getY(), 20.0f, marks.getHeight()), Justification::centred, false);
        }

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

    g.setColour (line);

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

    //==============================================================================
    // The figures: the highest sample of each side since the reset, then
    // the RMS level of each side and the crest factor over the last second.
    {
        float highest[2] = {}, crest[2] = {};
        double squares[2] = {};
        const auto recent = jmin (count, mtx::kStepsPerSecond);

        for (auto i = first; i < end; ++i)
        {
            const auto point = meter.historyAt (i);

            highest[0] = jmax (highest[0], point.peakLeft);
            highest[1] = jmax (highest[1], point.peakRight);

            if (end - i <= (uint32) recent)
            {
                crest[0] = jmax (crest[0], point.peakLeft);
                crest[1] = jmax (crest[1], point.peakRight);
                squares[0] += (double) point.rmsLeft * point.rmsLeft;
                squares[1] += (double) point.rmsRight * point.rmsRight;
            }
        }

        const float rms[2] = { recent > 0 ? (float) std::sqrt (squares[0] / recent) : 0.0f,
                               recent > 0 ? (float) std::sqrt (squares[1] / recent) : 0.0f };
        const auto loudest = jmax (rms[0], rms[1]);
        const auto over = Decibels::decibelsToGain (ceiling);

        auto row = [&] (const String& name, const String& value, Colour colour)
        {
            auto r = figures.removeFromTop (17.0f);

            g.setColour (haze);
            g.setFont (capsFont (9.0f));
            g.drawText (name, r, Justification::centredLeft, false);

            g.setColour (colour);
            g.setFont (monoFont (11.5f).boldened());
            g.drawText (value, r, Justification::centredRight, false);
        };

        row ("PEAK L", dbText (highest[0]), highest[0] > over ? emberLit : mist);
        row ("PEAK R", dbText (highest[1]), highest[1] > over ? emberLit : mist);
        row ("RMS L", dbText (rms[0]), mist);
        row ("RMS R", dbText (rms[1]), mist);
        row ("CREST", loudest > 1.0e-5f ? String (20.0f * std::log10 (jmax (crest[0], crest[1]) / loudest), 1) : String ("--"), mist);
        row ("TIME", clockText (count / mtx::kStepsPerSecond), mist);
    }

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
