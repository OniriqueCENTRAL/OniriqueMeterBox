#include "SpectrumDisplay.h"

namespace onirique
{

using namespace theme;

namespace
{
    constexpr float kTopDb = 0.0f, kBottomDb = -78.0f;

    // How far either way the gauge of a region reaches.
    constexpr float kGaugeDb = 9.0f;

    // Where the target is drawn before there is a spectrum to lay it on.
    constexpr float kRestingOffset = -30.0f;

    String signedText (float value)
    {
        const auto text = String (value, 1);
        return (value > 0.0f && text.containsAnyOf ("123456789") ? "+" : "") + text;
    }
}

SpectrumDisplay::SpectrumDisplay (MeterBoxProcessor& p) : processor (p)
{
    setOpaque (true);

    auto& state = processor.apvts;

    targetBox.addItemList (state.getParameter ("tonal")->getAllValueStrings(), 1);
    targetAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (state, "tonal", targetBox);
    addAndMakeVisible (targetBox);

    averageBox.addItemList (state.getParameter ("average")->getAllValueStrings(), 1);
    averageAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (state, "average", averageBox);
    addAndMakeVisible (averageBox);

    captureButton.getProperties().set ("plain", true);
    captureButton.onClick = [this]
    {
        if (processor.captureReference())
            captured = 45;
    };
    addAndMakeVisible (captureButton);
}

void SpectrumDisplay::resized()
{
    auto row = getLocalBounds().withHeight (kPanelHeading).withTrimmedRight (14).withTrimmedTop (5).withHeight (20);

    captureButton.setBounds (row.removeFromRight (82));
    row.removeFromRight (6);
    averageBox.setBounds (row.removeFromRight (74));
    row.removeFromRight (6);
    targetBox.setBounds (row.removeFromRight (118));
}

void SpectrumDisplay::refresh (const mtx::SpectrumFrame& latest)
{
    frame = latest;

    if (captured > 0)
        --captured;

    captureButton.setToggleState (captured > 0, dontSendNotification);
    captureButton.setButtonText (captured > 0 ? "CAPTURED" : "CAPTURE");
    captureButton.setEnabled (frame.measured);

    repaint();
}

void SpectrumDisplay::paint (Graphics& g)
{
    paintPanel (g, getLocalBounds(), "SPECTRUM");

    // The key, after the name: as much of it as there is room for before
    // the controls.
    {
        auto key = Rectangle<float> (104.0f, 2.0f, (float) targetBox.getX() - 104.0f - 8.0f, (float) kPanelHeading);

        auto entry = [&] (const String& name, Colour colour, float height)
        {
            if (key.getWidth() < GlyphArrangement::getStringWidth (capsFont (9.0f), name) + 32.0f)
            {
                key = {};
                return;
            }

            g.setColour (colour);
            g.fillRoundedRectangle (key.removeFromLeft (14.0f).withSizeKeepingCentre (12.0f, height), 1.0f);
            g.setColour (haze);
            g.setFont (capsFont (9.0f));
            g.drawText (name, key.removeFromLeft (GlyphArrangement::getStringWidth (capsFont (9.0f), name) + 18.0f).withTrimmedLeft (5.0f),
                        Justification::centredLeft, false);
        };

        entry ("LIVE", ember.withAlpha (0.6f), 8.0f);
        entry ("AVERAGE", Colours::white, 2.0f);
        entry ("TARGET", bone.withAlpha (0.3f), 8.0f);
    }

    auto body = getLocalBounds().toFloat().reduced (14.0f, 0.0f).withTrimmedTop ((float) kPanelHeading + 6.0f).withTrimmedBottom (12.0f);
    auto regions = body.removeFromBottom (40.0f);
    body.removeFromBottom (18.0f);
    const auto plot = body;

    auto yOf = [plot] (float db) { return plot.getY() + plot.getHeight() * (kTopDb - db) / (kTopDb - kBottomDb); };
    auto xOf = [plot] (float hz) { return xOfHz (plot, hz); };

    //==============================================================================
    // The grid: the decades, and every twelve dB.
    g.setFont (monoFont (9.0f));

    for (const auto& [hz, name] : std::initializer_list<std::pair<float, const char*>> { { 50.0f, "50" }, { 100.0f, "100" }, { 200.0f, "200" },
                                                                                          { 500.0f, "500" }, { 1000.0f, "1k" }, { 2000.0f, "2k" },
                                                                                          { 5000.0f, "5k" }, { 10000.0f, "10k" } })
    {
        const auto x = xOf (hz);

        g.setColour (line);
        g.fillRect (x, plot.getY(), 1.0f, plot.getHeight());
        g.setColour (haze.withAlpha (0.7f));
        g.drawText (name, Rectangle<float> (x - 20.0f, plot.getBottom() + 4.0f, 40.0f, 10.0f), Justification::centred, false);
    }

    for (int db = -12; db > (int) kBottomDb; db -= 12)
    {
        const auto y = yOf ((float) db);

        g.setColour (line);
        g.fillRect (plot.getX(), y, plot.getWidth(), 1.0f);
        g.setColour (haze.withAlpha (0.7f));
        g.drawText (String (db), Rectangle<float> (plot.getX() + 2.0f, y - 11.0f, 30.0f, 10.0f), Justification::centredLeft, false);
    }

    //==============================================================================
    mtx::TonalTarget target;
    const auto hasTarget = processor.getTonalTarget (target);
    const auto offset = hasTarget && frame.measured ? mtx::fitTarget (target, frame.average) : kRestingOffset;

    std::vector<Point<float>> live, average, upper, lower;

    for (int p = 0; p < mtx::kSpectrumPoints; ++p)
    {
        const auto x = xOf (mtx::spectrumHz (p));

        live.push_back ({ x, yOf (jmax (kBottomDb - 4.0f, frame.live[p])) });
        average.push_back ({ x, yOf (jmax (kBottomDb - 4.0f, frame.average[p])) });
        upper.push_back ({ x, yOf (target.mean[p] + offset + target.tolerance[p]) });
        lower.push_back ({ x, yOf (target.mean[p] + offset - target.tolerance[p]) });
    }

    {
        Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (plot.toNearestInt());

        // The target: a band, fainter while it has nothing to lie on.
        if (hasTarget)
        {
            auto band = curveThrough (upper);

            for (auto point = lower.rbegin(); point != lower.rend(); ++point)
                band.lineTo (*point);

            band.closeSubPath();

            const auto strength = frame.measured ? 1.0f : 0.45f;

            g.setColour (bone.withAlpha (0.11f * strength));
            g.fillPath (band);
            g.setColour (bone.withAlpha (0.28f * strength));
            g.strokePath (curveThrough (upper), PathStrokeType (1.0f));
            g.strokePath (curveThrough (lower), PathStrokeType (1.0f));
        }

        // The spectrum as it stands.
        {
            const auto curve = curveThrough (live);
            auto filled = curve;

            filled.lineTo (plot.getRight(), plot.getBottom() + 4.0f);
            filled.lineTo (plot.getX(), plot.getBottom() + 4.0f);
            filled.closeSubPath();

            g.setGradientFill (ColourGradient (ember.withAlpha (0.55f), 0.0f, plot.getY() + plot.getHeight() * 0.15f,
                                               blood.withAlpha (0.12f), 0.0f, plot.getBottom(), false));
            g.fillPath (filled);
            g.setColour (emberLit.withAlpha (0.85f));
            g.strokePath (curve, PathStrokeType (1.2f, PathStrokeType::curved, PathStrokeType::rounded));
        }

        // Its average, and in red the stretches of it that are out of the band.
        if (frame.measured)
        {
            const auto curve = curveThrough (average);

            g.setColour (Colours::black.withAlpha (0.35f));
            g.strokePath (curve, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour (Colours::white);
            g.strokePath (curve, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));

            if (hasTarget)
            {
                Path out;
                bool drawing = false;

                for (int p = 0; p < mtx::kSpectrumPoints; ++p)
                {
                    const auto away = std::abs (frame.average[p] - target.mean[p] - offset) > target.tolerance[p];

                    if (away && drawing)
                        out.lineTo (average[(size_t) p]);
                    else if (away)
                        out.startNewSubPath (average[(size_t) p]);

                    drawing = away;
                }

                g.setColour (ember.withAlpha (0.35f));
                g.strokePath (out, PathStrokeType (6.0f, PathStrokeType::curved, PathStrokeType::rounded));
                g.setColour (emberLit);
                g.strokePath (out, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));
            }
        }
    }

    if (! hasTarget)
    {
        g.setColour (haze);
        g.setFont (capsFont (9.5f));
        g.drawText ("NO REFERENCE YET: PLAY A RECORD, THEN CAPTURE", plot.withTrimmedTop (8.0f).withHeight (14.0f), Justification::centred, false);
    }

    //==============================================================================
    // The four regions, each under its own stretch of the scale: how far
    // from the target, in dB.
    float deviation[mtx::kNumRegions] = {}, tolerance[mtx::kNumRegions] = {};
    const auto compared = hasTarget && frame.measured;

    if (compared)
        mtx::compareRegions (target, frame.average, offset, deviation, tolerance);

    for (int i = 0; i < mtx::kNumRegions; ++i)
    {
        const auto& r = mtx::region (i);
        const auto left = i == 0 ? plot.getX() : xOf (r.lowHz);
        const auto right = i == mtx::kNumRegions - 1 ? plot.getRight() : xOf (r.highHz);
        auto cell = Rectangle<float> (left, regions.getY(), right - left, regions.getHeight()).reduced (3.0f, 0.0f);

        if (i > 0)
        {
            g.setColour (Colour (0x30ffd2d6));
            g.fillRect (left, plot.getY(), 1.0f, plot.getHeight());
        }

        g.setColour (Colour (0x40000000));
        g.fillRoundedRectangle (cell, 6.0f);

        const auto out = compared && std::abs (deviation[i]) > tolerance[i];
        auto text = cell.reduced (8.0f, 0.0f).removeFromTop (22.0f);

        // A narrow region has its name cut short, to leave the figure room.
        static const char* const shortNames[mtx::kNumRegions] = { "LO", "LO-MID", "HI-MID", "HI" };
        const auto roomy = text.getWidth() > GlyphArrangement::getStringWidth (capsFont (9.0f), r.name) + 38.0f;

        g.setColour (haze);
        g.setFont (capsFont (9.0f));
        g.drawText (roomy ? r.name : shortNames[i], text, Justification::centredLeft, false);

        g.setColour (out ? emberLit : mist);
        g.setFont (monoFont (11.0f).boldened());
        g.drawText (compared ? signedText (deviation[i]) : String ("--"), text, Justification::centredRight, false);

        // The gauge: the middle stretch is what the target allows.
        const auto track = cell.reduced (9.0f, 0.0f).withTrimmedTop (24.0f).withHeight (6.0f);
        const auto centre = track.getCentreX();
        const auto perDb = track.getWidth() * 0.5f / kGaugeDb;

        g.setColour (Colour (0x26ffd2d6));
        g.fillRoundedRectangle (track, 3.0f);

        if (compared)
        {
            g.setColour (bone.withAlpha (0.35f));
            g.fillRoundedRectangle (Rectangle<float> (2.0f * tolerance[i] * perDb, track.getHeight()).withCentre (track.getCentre()), 3.0f);

            const auto x = centre + jlimit (-kGaugeDb, kGaugeDb, deviation[i]) * perDb;
            const auto dot = Rectangle<float> (10.0f, 10.0f).withCentre ({ x, track.getCentreY() });

            g.setColour ((out ? ember : Colours::white).withAlpha (0.3f));
            g.fillEllipse (dot.expanded (3.0f));
            g.setColour (out ? emberLit : Colours::white);
            g.fillEllipse (dot);
        }
    }
}

} // namespace onirique
