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
}

void WaveformPanel::paint (Graphics& g)
{
    paintPanel (g, getLocalBounds(), "WAVEFORM");

    const auto& meter = processor.getMeter();
    const auto ceiling = processor.getCeilingDb();
    const auto ref = processor.getReference().getTrack();

    auto body = getLocalBounds().toFloat().withTrimmedTop ((float) kPanelHeading + 4.0f).withTrimmedBottom (12.0f);

    // Wide (the simplified view), the figures in a column at the right; narrow, under the waveform.
    const auto wide = getWidth() > 420;
    Rectangle<float> figures;

    if (wide)
    {
        figures = body.removeFromRight (200.0f).withTrimmedTop (14.0f);
        body.removeFromRight (28.0f);
    }

    // Wide, the waveform also lies on its side: time from left to right, the
    // left side above the middle and the right below. It is drawn as when it
    // runs down the panel, in a frame turned a quarter, and only the words
    // are written apart, so that they are not turned with it.
    Rectangle<float> names, marks, plot, frame;

    if (wide)
    {
        auto side = body.removeFromLeft (34.0f);
        side.removeFromBottom (16.0f);
        body.removeFromBottom (16.0f);
        plot = body;
        names = side;
        frame = { 0.0f, 0.0f, plot.getHeight(), plot.getWidth() };
    }
    else
    {
        names = body.removeFromTop (14.0f);
        figures = body.removeFromBottom ((ref != nullptr ? 9.0f : 6.0f) * 17.0f);
        body.removeFromBottom (6.0f);
        marks = body.removeFromBottom (11.0f);
        plot = body;
        frame = plot;
    }

    // In the turned frame, x is the amplitude and y the time, as in the column.
    const auto centre = frame.getCentreX();
    const auto reach = frame.getWidth() * 0.5f - 4.0f;       // full scale
    const auto turn = wide ? AffineTransform (0.0f, 1.0f, plot.getX(), 1.0f, 0.0f, plot.getY()) : AffineTransform();

    // Where a point of the frame is on the screen.
    auto onScreen = [&turn] (float x, float y) { auto px = x, py = y; turn.transformPoint (px, py); return Point<float> (px, py); };

    g.setColour (haze);
    g.setFont (capsFont (9.0f));

    if (wide)
    {
        g.drawText ("L", Rectangle<float> (names.getX(), onScreen (centre - reach * 0.25f, 0.0f).y - 6.0f, 12.0f, 12.0f), Justification::centredLeft, false);
        g.drawText ("R", Rectangle<float> (names.getX(), onScreen (centre + reach * 0.25f, 0.0f).y - 6.0f, 12.0f, 12.0f), Justification::centredLeft, false);
    }
    else
    {
        g.drawText ("L", names.withWidth (names.getWidth() * 0.5f), Justification::centred, false);
        g.drawText ("R", names.withTrimmedLeft (names.getWidth() * 0.5f), Justification::centred, false);
    }

    g.saveState();
    g.addTransform (turn);


    // The grid: the middle, then -12 dB, -6 dB and full scale on each side,
    // the last two named under the waveform.
    g.setFont (monoFont (8.5f));

    for (const auto& [amplitude, name] : std::initializer_list<std::pair<float, const char*>> { { 0.25f, "" }, { 0.5f, "-6" }, { 1.0f, "0" } })
        for (const auto side : { -1.0f, 1.0f })
        {
            const auto x = centre + side * amplitude * reach;

            g.setColour (line);
            g.fillRect (x, frame.getY(), 1.0f, frame.getHeight());

            if (! wide)
            {
                g.setColour (haze.withAlpha (0.7f));
                g.drawText (name, Rectangle<float> (x - 10.0f, marks.getY(), 20.0f, marks.getHeight()), Justification::centred, false);
            }
        }

    //==============================================================================
    // Everything there is, and never less than thirty seconds' worth of room.
    const auto end = meter.historyEnd();
    const auto first = meter.historyStart();
    const auto count = (int) (end - first);
    const auto visible = jmax (30 * mtx::kStepsPerSecond, count);

    auto yOf = [frame, first, visible] (double index) { return frame.getY() + frame.getHeight() * (float) ((index - first) / (double) visible); };

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
        g.fillRect (frame.getX(), yOf ((double) t * mtx::kStepsPerSecond), frame.getWidth(), 1.0f);

    if (count >= 2)
    {
        Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (frame.toNearestInt());

        // No more rows than there are pixels: each stands for the highest
        // of the stretch it covers.
        const auto steps = jlimit (2, count, roundToInt (frame.getHeight() * (float) count / (float) visible));

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
                g.fillRect (frame.getX(), row.y - 1.0f, 6.0f, 2.0f);
                g.fillRect (frame.getRight() - 6.0f, row.y - 1.0f, 6.0f, 2.0f);
            }
    }

    g.setColour (Colour (0x40ffd2d6));
    g.fillRect (centre, frame.getY(), 1.0f, frame.getHeight());

    // Back to the screen for the words.
    g.restoreState();

    // On its side, the levels down the left of the waveform.
    if (wide)
    {
        g.setFont (monoFont (8.5f));
        g.setColour (haze.withAlpha (0.7f));

        for (const auto& [amplitude, name] : std::initializer_list<std::pair<float, const char*>> { { 0.5f, "-6" }, { 1.0f, "0" } })
            for (const auto side : { -1.0f, 1.0f })
            {
                const auto y = onScreen (centre + side * amplitude * reach, 0.0f).y;
                g.drawText (name, Rectangle<float> (names.getRight() - 22.0f, y - 6.0f, 20.0f, 12.0f), Justification::centredRight, false);
            }
    }

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

        // The reference's, over the whole record.
        if (ref != nullptr)
        {
            const auto& s = ref->stats;

            row ("REF PEAK", dbText (jmax (s.peak[0], s.peak[1])), refInk);
            row ("REF RMS", dbText (jmax (s.rms[0], s.rms[1])), refInk);
            row ("REF CREST", String (s.crestDb, 1), refInk);
        }
    }

    // The times, written over the waveform so that they can always be read.
    g.setFont (monoFont (9.0f));

    for (auto t = (firstSecond / every + 1) * every; t < firstSecond + seconds; t += every)
    {
        // Under the waveform on its side, beside it in the column.
        const auto at = yOf ((double) t * mtx::kStepsPerSecond);
        const auto label = wide ? Rectangle<float> (onScreen (frame.getX(), at).x - 20.0f, plot.getBottom() + 3.0f, 40.0f, 11.0f)
                                : Rectangle<float> (plot.getX() + 2.0f, at - 12.0f, 30.0f, 11.0f);

        g.setColour (haze);
        g.drawText (clockText (t), label, Justification::centred, false);
    }
}

} // namespace onirique
