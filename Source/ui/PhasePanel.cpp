#include "PhasePanel.h"

namespace onirique
{

using namespace theme;

namespace
{
    // Under this there is nothing to compare: the readings are drawn in retreat.
    constexpr float kAudibleDb = -80.0f;

    // How many of the latest samples a picture of the goniometer is made of.
    constexpr int kDrawnFrames = 1600;

    constexpr int kHoldTicks = 90;      // three seconds at the editor's pace

    // The goniometer: full scale is this many times the body of the sound,
    // and each sample lights its point by this much.
    constexpr float kBodyShare = 2.6f, kLight = 0.45f;

    String signedText (float value, int decimals)
    {
        const auto text = String (value, decimals);
        return (value > 0.0f && text.containsAnyOf ("123456789") ? "+" : "") + text;
    }
}

PhasePanel::PhasePanel (MeterBoxProcessor& p)
    : processor (p),
      glow ((size_t) (kScopeSize * kScopeSize), 0.0f),
      left ((size_t) kDrawnFrames), right ((size_t) kDrawnFrames),
      scope (Image::ARGB, kScopeSize, kScopeSize, true)
{
    setOpaque (true);

    // Ember where the sound is thin, white only where it piles up.
    for (int i = 0; i < 256; ++i)
    {
        const auto v = (float) i / 255.0f;
        const auto colour = v < 0.6f ? ember.interpolatedWith (emberLit, v / 0.6f) : emberLit.interpolatedWith (Colours::white, (v - 0.6f) / 0.4f);

        shades[i] = colour.withAlpha (jmin (1.0f, 1.7f * std::sqrt (v))).getPixelARGB();
    }
}

void PhasePanel::refresh (const mtx::SpectrumFrame& frame)
{
    values = processor.getMeter().stereo();
    measured = frame.measured;

    for (int p = 0; p < mtx::kSpectrumPoints; ++p)
        correlation[p] = frame.correlation[p];

    const auto audible = values.level > kAudibleDb;

    shown += 0.35f * ((audible ? values.correlation : 0.0f) - shown);

    if (audible && values.correlation <= lowest)
    {
        lowest = values.correlation;
        holding = kHoldTicks;
    }
    else if (--holding < 0)
    {
        lowest = jmin (1.0f, lowest + 0.02f);

        if (audible)
            lowest = jmin (lowest, values.correlation);
    }

    renderScope();
    repaint();
}

void PhasePanel::renderScope()
{
    // What was lit fades; what is playing lights it again.
    for (auto& v : glow)
        v *= 0.84f;

    const auto count = processor.getMeter().readScope (left.data(), right.data(), kDrawnFrames);
    float squares = 0.0f;

    for (int i = 0; i < count; ++i)
        squares += left[(size_t) i] * left[(size_t) i] + right[(size_t) i] * right[(size_t) i];

    // The picture is the same size whatever the level. It is sized by the
    // body of the sound, not by its peaks, which would leave the rest of it
    // a dot in the middle: up quickly, down slowly.
    const auto body = count > 0 ? std::sqrt (squares / (float) (2 * count)) : 0.0f;

    envelope = body > envelope ? envelope + 0.3f * (body - envelope) : envelope * 0.97f;

    if (envelope > 3.0e-5f)
    {
        const auto half = (float) kScopeSize * 0.5f;
        const auto reach = 0.92f * half / (kBodyShare * jmax (envelope, 1.0e-3f));

        for (int i = 0; i < count; ++i)
        {
            const auto l = left[(size_t) i], r = right[(size_t) i];

            // The middle goes up, the right side to the right.
            const auto x = half + 0.5f * (r - l) * reach - 0.5f;
            const auto y = half - 0.5f * (l + r) * reach - 0.5f;
            const auto ix = (int) std::floor (x), iy = (int) std::floor (y);

            if (ix < 0 || iy < 0 || ix >= kScopeSize - 1 || iy >= kScopeSize - 1)
                continue;

            const auto fx = x - (float) ix, fy = y - (float) iy;
            auto* cell = glow.data() + (size_t) (iy * kScopeSize + ix);

            cell[0] += kLight * (1.0f - fx) * (1.0f - fy);
            cell[1] += kLight * fx * (1.0f - fy);
            cell[kScopeSize] += kLight * (1.0f - fx) * fy;
            cell[kScopeSize + 1] += kLight * fx * fy;
        }
    }

    Image::BitmapData pixels (scope, Image::BitmapData::writeOnly);

    for (int y = 0; y < kScopeSize; ++y)
    {
        auto* row = (PixelARGB*) pixels.getLinePointer (y);
        const auto* from = glow.data() + (size_t) (y * kScopeSize);

        for (int x = 0; x < kScopeSize; ++x)
            row[x] = shades[jlimit (0, 255, (int) (from[x] * 255.0f))];
    }
}

void PhasePanel::paint (Graphics& g)
{
    paintPanel (g, getLocalBounds(), "PHASE");

    const auto audible = values.level > kAudibleDb;
    auto area = getLocalBounds().toFloat().reduced (14.0f, 0.0f).withTrimmedTop ((float) kPanelHeading + 4.0f).withTrimmedBottom (12.0f);

    //==============================================================================
    // The goniometer: a square, as large as the panel is wide, as long as
    // it leaves room for the rest.
    {
        const auto side = jmin (area.getWidth(), jmax (120.0f, area.getHeight() - 250.0f));
        const auto square = area.removeFromTop (side).withSizeKeepingCentre (side, side);
        const auto centre = square.getCentre();
        const auto half = side * 0.5f;

        g.setColour (Colour (0x40000000));
        g.fillRoundedRectangle (square, 8.0f);

        // The diamond is full scale; the cross, the middle and the sides.
        Path diamond;
        diamond.startNewSubPath (centre.x, centre.y - half * 0.92f);
        diamond.lineTo (centre.x + half * 0.92f, centre.y);
        diamond.lineTo (centre.x, centre.y + half * 0.92f);
        diamond.lineTo (centre.x - half * 0.92f, centre.y);
        diamond.closeSubPath();

        g.setColour (line);
        g.strokePath (diamond, PathStrokeType (1.0f));
        g.drawLine (centre.x, centre.y - half * 0.92f, centre.x, centre.y + half * 0.92f, 1.0f);
        g.drawLine (centre.x - half * 0.92f, centre.y, centre.x + half * 0.92f, centre.y, 1.0f);

        const float dashes[] = { 3.0f, 4.0f };
        g.drawDashedLine ({ centre.x - half * 0.46f, centre.y - half * 0.46f, centre.x + half * 0.46f, centre.y + half * 0.46f }, dashes, 2, 1.0f);
        g.drawDashedLine ({ centre.x + half * 0.46f, centre.y - half * 0.46f, centre.x - half * 0.46f, centre.y + half * 0.46f }, dashes, 2, 1.0f);

        g.setImageResamplingQuality (Graphics::mediumResamplingQuality);
        g.drawImage (scope, square);

        g.setColour (haze);
        g.setFont (capsFont (9.0f));
        g.drawText ("M", Rectangle<float> (20.0f, 12.0f).withCentre ({ centre.x + 10.0f, square.getY() + 12.0f }), Justification::centred, false);
        g.drawText ("L", Rectangle<float> (20.0f, 12.0f).withCentre ({ centre.x - half * 0.56f, centre.y - half * 0.56f }), Justification::centred, false);
        g.drawText ("R", Rectangle<float> (20.0f, 12.0f).withCentre ({ centre.x + half * 0.56f, centre.y - half * 0.56f }), Justification::centred, false);
        g.drawText ("+S", Rectangle<float> (24.0f, 12.0f).withCentre ({ square.getRight() - 16.0f, centre.y - 9.0f }), Justification::centred, false);
        g.drawText ("-S", Rectangle<float> (24.0f, 12.0f).withCentre ({ square.getX() + 16.0f, centre.y - 9.0f }), Justification::centred, false);
    }

    area.removeFromTop (12.0f);

    //==============================================================================
    // The correlation: a bar from -1 to +1, filled from the middle.
    {
        auto block = area.removeFromTop (44.0f);
        auto heading = block.removeFromTop (16.0f);

        g.setColour (haze);
        g.setFont (capsFont (9.5f));
        g.drawText ("CORRELATION", heading, Justification::centredLeft, false);

        g.setColour (! audible ? haze : values.correlation < 0.0f ? emberLit : mist);
        g.setFont (monoFont (12.5f).boldened());
        g.drawText (audible ? signedText (values.correlation, 2) : String ("--"), heading, Justification::centredRight, false);

        const auto track = block.removeFromTop (10.0f).translated (0.0f, 3.0f);
        const auto centre = track.getCentreX();
        const auto halfWidth = track.getWidth() * 0.5f;

        g.setColour (Colour (0x59000000));
        g.fillRoundedRectangle (track, 5.0f);

        // The left half is where it should not stay.
        g.setColour (blood.withAlpha (0.35f));
        g.fillRoundedRectangle (track.withWidth (halfWidth), 5.0f);

        const auto x = centre + shown * halfWidth;

        g.setColour (shown < 0.0f ? emberLit : Colours::white.withAlpha (0.85f));
        g.fillRoundedRectangle (Rectangle<float> (jmin (x, centre), track.getY() + 2.0f, std::abs (x - centre), track.getHeight() - 4.0f), 3.0f);

        g.setColour (line);
        g.drawRoundedRectangle (track, 5.0f, 1.0f);
        g.setColour (mist.withAlpha (0.5f));
        g.fillRect (centre - 0.5f, track.getY() - 2.0f, 1.0f, track.getHeight() + 4.0f);

        if (lowest < 0.999f)
        {
            g.setColour (lowest < 0.0f ? emberLit : mist);
            g.fillRect (centre + lowest * halfWidth - 1.0f, track.getY() - 2.0f, 2.0f, track.getHeight() + 4.0f);
        }

        g.setColour (haze.withAlpha (0.7f));
        g.setFont (monoFont (9.0f));

        const auto ends = Rectangle<float> (track.getX(), track.getBottom() + 3.0f, track.getWidth(), 10.0f);
        g.drawText ("-1", ends, Justification::centredLeft, false);
        g.drawText ("0", ends, Justification::centred, false);
        g.drawText ("+1", ends, Justification::centredRight, false);
    }

    area.removeFromTop (8.0f);

    //==============================================================================
    // The numbers.
    {
        auto rows = area.removeFromTop (3.0f * 21.0f);

        auto row = [&] (const String& name, const String& value, Colour colour)
        {
            auto r = rows.removeFromTop (21.0f);

            g.setColour (haze);
            g.setFont (capsFont (9.5f));
            g.drawText (name, r, Justification::centredLeft, false);

            g.setColour (audible ? colour : haze);
            g.setFont (monoFont (12.5f).boldened());
            g.drawText (audible ? value : String ("--"), r, Justification::centredRight, false);
        };

        const auto lean = std::abs (values.balance) < 0.05f ? String ("C")
                                                            : String (values.balance < 0.0f ? "L " : "R ") + String (std::abs (values.balance), 1) + " dB";

        row ("BALANCE", lean, std::abs (values.balance) > 1.5f ? emberLit : mist);
        row ("WIDTH", String (roundToInt (values.width * 100.0f)) + " %", mist);
        row ("MONO", String (values.monoLoss, 1) + " dB", values.monoLoss < -3.0f ? emberLit : mist);
    }

    area.removeFromTop (8.0f);

    //==============================================================================
    // The correlation, frequency by frequency.
    if (area.getHeight() > 50.0f)
    {
        g.setColour (haze);
        g.setFont (capsFont (9.5f));
        g.drawText ("CORRELATION BY FREQUENCY", area.removeFromTop (16.0f), Justification::centredLeft, false);

        auto plot = area.withTrimmedBottom (12.0f);

        g.setColour (Colour (0x40000000));
        g.fillRoundedRectangle (plot, 6.0f);

        const auto middle = plot.getCentreY();
        const auto reach = plot.getHeight() * 0.5f - 4.0f;

        g.setFont (monoFont (9.0f));

        for (const auto& [hz, name] : std::initializer_list<std::pair<float, const char*>> { { 100.0f, "100" }, { 1000.0f, "1k" }, { 10000.0f, "10k" } })
        {
            const auto x = xOfHz (plot, hz);

            g.setColour (line);
            g.fillRect (x, plot.getY(), 1.0f, plot.getHeight());
            g.setColour (haze.withAlpha (0.7f));
            g.drawText (name, Rectangle<float> (x - 20.0f, plot.getBottom() + 2.0f, 40.0f, 10.0f), Justification::centred, false);
        }

        g.setColour (Colour (0x40ffd2d6));
        g.fillRect (plot.getX(), middle, plot.getWidth(), 1.0f);

        if (measured)
        {
            Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (plot.toNearestInt());

            std::vector<Point<float>> points;

            for (int p = 0; p < mtx::kSpectrumPoints; ++p)
                points.push_back ({ xOfHz (plot, mtx::spectrumHz (p)), middle - correlation[p] * reach });

            const auto curve = curveThrough (points);
            auto filled = curve;

            filled.lineTo (points.back().x, middle);
            filled.lineTo (points.front().x, middle);
            filled.closeSubPath();

            // The same shape filled twice: pale over the middle, red under it.
            {
                Graphics::ScopedSaveState upper (g);
                g.reduceClipRegion (plot.withBottom (middle).toNearestInt());
                g.setColour (bone.withAlpha (0.2f));
                g.fillPath (filled);
            }

            {
                Graphics::ScopedSaveState lower (g);
                g.reduceClipRegion (plot.withTop (middle).toNearestInt());
                g.setColour (ember.withAlpha (0.65f));
                g.fillPath (filled);
            }

            g.setColour (Colours::white.withAlpha (0.9f));
            g.strokePath (curve, PathStrokeType (1.4f, PathStrokeType::curved, PathStrokeType::rounded));
        }

        g.setColour (haze.withAlpha (0.7f));
        g.drawText ("+1", Rectangle<float> (plot.getX() + 3.0f, plot.getY() + 2.0f, 20.0f, 10.0f), Justification::centredLeft, false);
        g.drawText ("-1", Rectangle<float> (plot.getX() + 3.0f, plot.getBottom() - 12.0f, 20.0f, 10.0f), Justification::centredLeft, false);
    }
}

} // namespace onirique
