#include "HistoryGraph.h"

namespace onirique
{

using namespace theme;

namespace
{
    // The scale follows the target: 9 LU over it, 27 under.
    constexpr float kOver = 9.0f, kUnder = 27.0f;

    constexpr float kSpreadWidth = 56.0f;

    String clockText (int seconds)
    {
        return String (seconds / 60) + ":" + String (seconds % 60).paddedLeft ('0', 2);
    }
}

HistoryGraph::HistoryGraph (MeterBoxProcessor& p) : processor (p)
{
    setOpaque (true);

    spanBox.addItemList (processor.apvts.getParameter ("span")->getAllValueStrings(), 1);
    spanAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, "span", spanBox);
    addAndMakeVisible (spanBox);
}

void HistoryGraph::resized()
{
    spanBox.setBounds (getWidth() - 14 - 92, 5, 92, 20);
}

void HistoryGraph::paint (Graphics& g)
{
    paintPanel (g, getLocalBounds(), "HISTORY");

    const auto& meter = processor.getMeter();
    const auto target = processor.getTargetLufs();
    const auto ceiling = processor.getCeilingDb();
    const auto top = target + kOver, bottom = target - kUnder;

    // The key, after the name.
    {
        auto key = Rectangle<float> (92.0f, 2.0f, (float) spanBox.getX() - 92.0f - 8.0f, (float) kPanelHeading);

        auto entry = [&] (const String& name, Colour colour, bool filled)
        {
            if (key.getWidth() < GlyphArrangement::getStringWidth (capsFont (9.0f), name) + 32.0f)
            {
                key = {};
                return;
            }

            const auto mark = key.removeFromLeft (14.0f).withSizeKeepingCentre (12.0f, filled ? 8.0f : 2.0f);

            g.setColour (colour);
            g.fillRoundedRectangle (mark, 1.0f);
            g.setColour (haze);
            g.setFont (capsFont (9.0f));
            g.drawText (name, key.removeFromLeft (GlyphArrangement::getStringWidth (capsFont (9.0f), name) + 18.0f).withTrimmedLeft (5.0f),
                        Justification::centredLeft, false);
        };

        entry ("MOMENTARY", ember.withAlpha (0.45f), true);
        entry ("SHORT-TERM", emberLit, false);
        entry ("INTEGRATED", Colours::white, false);
    }

    auto body = getLocalBounds().toFloat().reduced (14.0f, 0.0f).withTrimmedTop ((float) kPanelHeading + 6.0f).withTrimmedBottom (20.0f);
    const auto spread = body.removeFromRight (kSpreadWidth);
    body.removeFromRight (10.0f);
    const auto plot = body;

    auto yOf = [plot, top, bottom] (float lufs) { return plot.getY() + plot.getHeight() * (top - lufs) / (top - bottom); };

    // The grid: every 6 LU from the target.
    g.setColour (line);

    for (int step = 1; step >= -4; --step)
        g.fillRect (plot.getX(), yOf (target + 6.0f * (float) step), plot.getWidth() + 10.0f + kSpreadWidth, 1.0f);

    //==============================================================================
    // What is on show: the last so many seconds, or everything there is.
    const auto end = meter.historyEnd();
    const auto start = meter.historyStart();
    const auto span = processor.getHistorySpan();
    const auto visible = (uint32) (span > 0 ? span * mtx::kStepsPerSecond : jmax (30 * mtx::kStepsPerSecond, (int) (end - start)));
    const auto first = jmax (start, end > visible ? end - visible : 0u);
    const auto count = (int) (end - first);

    auto xOf = [plot, first, visible] (double index) { return plot.getX() + plot.getWidth() * (float) ((index - first) / (double) visible); };

    // Time along the foot, counted from the reset.
    {
        const auto seconds = (int) visible / mtx::kStepsPerSecond;
        int every = 5;

        for (const auto candidate : { 5, 10, 30, 60, 120, 300, 600, 1200 })
        {
            every = candidate;

            if (seconds / candidate <= 7)
                break;
        }

        const auto firstSecond = (int) first / mtx::kStepsPerSecond;

        for (auto t = (firstSecond / every + 1) * every; t < firstSecond + seconds; t += every)
        {
            const auto x = xOf ((double) t * mtx::kStepsPerSecond);

            g.setColour (line);
            g.fillRect (x, plot.getY(), 1.0f, plot.getHeight());
            g.setColour (haze.withAlpha (0.7f));
            g.drawText (clockText (t), Rectangle<float> (x - 30.0f, plot.getBottom() + 4.0f, 60.0f, 10.0f), Justification::centred, false);
        }
    }

    if (count >= 2)
    {
        Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (plot.toNearestInt());

        // No more points than there are pixels to put them on: each stands
        // for the loudest of the stretch it covers.
        const auto width = plot.getWidth() * (float) count / (float) visible;
        const auto steps = jlimit (2, count, roundToInt (width));

        Path ground, shortTerm, integrated;
        std::vector<float> overs;
        bool integratedStarted = false;

        for (int s = 0; s < steps; ++s)
        {
            const auto from = first + (uint32) ((int64) s * count / steps);
            const auto to = jmax (from + 1, first + (uint32) ((int64) (s + 1) * count / steps));
            mtx::HistoryPoint most;

            for (auto i = from; i < to; ++i)
            {
                const auto point = meter.historyAt (i);

                most.momentary = jmax (most.momentary, point.momentary);
                most.shortTerm = jmax (most.shortTerm, point.shortTerm);
                most.truePeak = jmax (most.truePeak, point.truePeak);
                most.integrated = point.integrated;
            }

            const auto x = xOf (0.5 * ((double) from + (double) to));
            const auto lowest = bottom - 2.0f;

            if (s == 0)
            {
                ground.startNewSubPath (x, plot.getBottom() + 2.0f);
                shortTerm.startNewSubPath (x, yOf (jmax (lowest, most.shortTerm)));
            }

            ground.lineTo (x, yOf (jmax (lowest, most.momentary)));
            shortTerm.lineTo (x, yOf (jmax (lowest, most.shortTerm)));

            if (most.integrated > mtx::kFloorDb + 1.0f)
            {
                if (integratedStarted)
                    integrated.lineTo (x, yOf (jmax (lowest, most.integrated)));
                else
                    integrated.startNewSubPath (x, yOf (jmax (lowest, most.integrated)));

                integratedStarted = true;
            }

            if (most.truePeak > ceiling)
                overs.push_back (x);

            if (s == steps - 1)
            {
                ground.lineTo (x, plot.getBottom() + 2.0f);
                ground.closeSubPath();
            }
        }

        g.setGradientFill (ColourGradient (ember.withAlpha (0.5f), 0.0f, plot.getY(), blood.withAlpha (0.25f), 0.0f, plot.getBottom(), false));
        g.fillPath (ground);

        g.setColour (ember.withAlpha (0.2f));
        g.strokePath (shortTerm, PathStrokeType (5.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (emberLit);
        g.strokePath (shortTerm, PathStrokeType (1.7f, PathStrokeType::curved, PathStrokeType::rounded));

        g.setColour (Colours::white.withAlpha (0.9f));
        g.strokePath (integrated, PathStrokeType (1.4f, PathStrokeType::curved, PathStrokeType::rounded));

        // Where the true peak went over its ceiling.
        g.setColour (Colours::white);

        for (const auto x : overs)
            g.fillRect (x - 1.0f, plot.getY(), 2.0f, 7.0f);
    }

    // The target, across the graph and the spread alike.
    {
        const auto y = yOf (target);
        const float dashes[] = { 5.0f, 4.0f };

        g.setColour (mist.withAlpha (0.6f));
        g.drawDashedLine ({ plot.getX(), y, spread.getRight(), y }, dashes, 2, 1.0f);
    }

    // The scale, written over the curves so that it can always be read.
    g.setFont (monoFont (9.0f));

    for (int step = 1; step >= -4; --step)
    {
        const auto lufs = target + 6.0f * (float) step;
        const auto label = Rectangle<float> (plot.getX() + 2.0f, yOf (lufs) - 12.0f, 30.0f, 11.0f);

        g.setColour (deep.withAlpha (0.75f));
        g.fillRoundedRectangle (label, 3.0f);
        g.setColour (step == 0 ? mist : haze);
        g.drawText (String (lufs, std::abs (lufs - std::round (lufs)) < 0.05f ? 0 : 1), label, Justification::centred, false);
    }

    //==============================================================================
    // The spread: how long the short-term loudness has been in each LU.
    {
        uint32 most = 0;

        for (int bin = 0; bin < mtx::kDistributionBins; ++bin)
            most = jmax (most, meter.distribution (bin));

        g.setColour (Colour (0x40000000));
        g.fillRoundedRectangle (spread, 5.0f);

        if (most > 0)
        {
            Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (spread.toNearestInt());

            for (int bin = 0; bin < mtx::kDistributionBins; ++bin)
            {
                const auto held = meter.distribution (bin);

                if (held == 0)
                    continue;

                const auto lufs = mtx::kDistributionFloor + (float) bin;
                const auto y0 = yOf (lufs + 1.0f), y1 = yOf (lufs);
                const auto length = jmax (2.0f, (spread.getWidth() - 6.0f) * (float) held / (float) most);

                g.setColour (lufs >= target ? emberLit.withAlpha (0.9f) : ember.withAlpha (0.75f));
                g.fillRect (spread.getX() + 3.0f, y0 + 0.5f, length, jmax (1.0f, y1 - y0 - 1.0f));
            }
        }
    }
}

} // namespace onirique
