#include "ReferenceStrip.h"

namespace onirique
{

using namespace theme;

namespace
{
    String clockText (double seconds)
    {
        const auto whole = jmax (0, (int) seconds);
        return String (whole / 60) + ":" + String (whole % 60).paddedLeft ('0', 2);
    }

    String signedText (float value, int decimals)
    {
        const auto text = String (value, decimals);
        return (value > 0.0f && text.containsAnyOf ("123456789") ? "+" : "") + text;
    }

    constexpr int kSwitchWidth = 188;
    constexpr int kGainWidth = 64;
}

ReferenceStrip::ReferenceStrip (MeterBoxProcessor& p) : processor (p)
{
    auto& player = processor.getReference();

    matchButton.setClickingTogglesState (true);
    matchButton.setToggleState (player.isMatching(), dontSendNotification);
    matchButton.onClick = [this] { processor.getReference().setMatching (matchButton.getToggleState()); };
    addAndMakeVisible (matchButton);

    loadButton.getProperties().set ("plain", true);
    loadButton.onClick = [this] { browse(); };
    addAndMakeVisible (loadButton);

    linkButton.getProperties().set ("plain", true);
    linkButton.onClick = [this] { openLink(); };
    addAndMakeVisible (linkButton);

    linkField.setColour (TextEditor::backgroundColourId, Colours::transparentBlack);
    linkField.setColour (TextEditor::outlineColourId, Colours::transparentBlack);
    linkField.setColour (TextEditor::focusedOutlineColourId, Colours::transparentBlack);
    linkField.setColour (TextEditor::textColourId, Colours::white);
    linkField.setFont (monoFont (12.5f));
    linkField.setTextToShowWhenEmpty ("https://www.youtube.com/watch?v=...", haze);
    linkField.setIndents (12, 0);
    linkField.setJustification (Justification::centredLeft);
    linkField.onReturnKey = [this] { fetchLink(); };
    linkField.onEscapeKey = [this] { closeLink(); };
    linkField.onTextChange = [this] { linkProblem = {}; repaint(); };
    addChildComponent (linkField);

    ejectButton.getProperties().set ("plain", true);
    ejectButton.onClick = [this] { processor.getReference().eject(); refresh(); };
    addAndMakeVisible (ejectButton);

    for (auto* zone : { &switchZone, &trackZone })
    {
        zone->setInterceptsMouseClicks (false, false);
        addAndMakeVisible (*zone);
    }
}

void ReferenceStrip::resized()
{
    auto area = getLocalBounds();

    switchArea = area.removeFromLeft (kSwitchWidth).withSizeKeepingCentre (kSwitchWidth, 34);
    area.removeFromLeft (16);

    ejectButton.setBounds (area.removeFromRight (64).withSizeKeepingCentre (64, 26));
    area.removeFromRight (6);
    loadButton.setBounds (area.removeFromRight (64).withSizeKeepingCentre (64, 26));
    area.removeFromRight (6);
    linkButton.setBounds (area.removeFromRight (64).withSizeKeepingCentre (64, 26));
    area.removeFromRight (16);
    area.removeFromRight (kGainWidth);
    matchButton.setBounds (area.removeFromRight (96).withSizeKeepingCentre (96, 28));
    area.removeFromRight (16);

    trackArea = area.withTrimmedTop (4).withTrimmedBottom (4);

    switchZone.setBounds (switchArea);
    trackZone.setBounds (trackArea);

    // The field takes the place of the reference while a link is typed, its
    // hint at the right.
    linkField.setBounds (trackArea.withTrimmedRight (250).reduced (1, 3));
}

void ReferenceStrip::openLink()
{
    if (linkField.isVisible())
    {
        fetchLink();
        return;
    }

    // A link copied from the browser is already in it.
    const auto copied = SystemClipboard::getTextFromClipboard().trim();

    linkProblem = {};
    linkField.setText (ReferencePlayer::isPlatformLink (copied) ? copied : String(), dontSendNotification);
    linkField.setVisible (true);
    linkField.grabKeyboardFocus();
    linkField.selectAll();
    linkButton.setButtonText ("FETCH");
    linkButton.setToggleState (true, dontSendNotification);
    repaint();
}

void ReferenceStrip::fetchLink()
{
    const auto link = linkField.getText().trim();

    if (! ReferencePlayer::isPlatformLink (link))
    {
        linkProblem = String::fromUTF8 ("COLLE UN LIEN YOUTUBE OU TIKTOK");
        repaint();
        return;
    }

    processor.loadReferenceLink (link);
    closeLink();
}

void ReferenceStrip::closeLink()
{
    linkField.setVisible (false);
    linkProblem = {};
    linkButton.setButtonText ("LINK");
    linkButton.setToggleState (false, dontSendNotification);
    refresh();
}

void ReferenceStrip::refresh()
{
    track = processor.getReference().getTrack();

    const auto& player = processor.getReference();
    matchButton.setToggleState (player.isMatching(), dontSendNotification);
    matchButton.setEnabled (track != nullptr);
    ejectButton.setEnabled (track != nullptr || player.getStatus() == ReferencePlayer::Status::failed);

    repaint();
}

void ReferenceStrip::setDragging (bool isDragging)
{
    if (dragging != isDragging)
    {
        dragging = isDragging;
        repaint();
    }
}

void ReferenceStrip::browse()
{
    const auto from = track != nullptr ? track->file.getParentDirectory()
                                       : File::getSpecialLocation (File::userMusicDirectory);

    chooser = std::make_unique<FileChooser> ("Choose a reference", from, ReferencePlayer::getWildcard());

    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                          [safe = Component::SafePointer<ReferenceStrip> (this)] (const FileChooser& fc)
                          {
                              if (safe == nullptr)
                                  return;

                              const auto file = fc.getResult();

                              if (file.existsAsFile())
                                  safe->processor.loadReference (file);

                              safe->refresh();
                          });
}

Rectangle<float> ReferenceStrip::outlineArea() const
{
    return trackArea.toFloat().withTrimmedTop (20.0f);
}

void ReferenceStrip::seekTo (int x)
{
    if (track == nullptr)
        return;

    const auto outline = outlineArea();
    const auto share = jlimit (0.0f, 1.0f, ((float) x - outline.getX()) / outline.getWidth());
    processor.getReference().seek (share * track->seconds());
}

void ReferenceStrip::mouseDown (const MouseEvent& e)
{
    auto& player = processor.getReference();

    if (switchArea.contains (e.getPosition()))
    {
        const auto wantsReference = e.x > switchArea.getCentreX();

        if (wantsReference && track == nullptr)
            browse();
        else
            player.setListening (wantsReference);

        repaint();
        return;
    }

    if (trackArea.contains (e.getPosition()) && ! linkField.isVisible())
    {
        if (track == nullptr)
            browse();
        else
            seekTo (e.x);
    }
}

void ReferenceStrip::mouseDrag (const MouseEvent& e)
{
    if (track != nullptr && trackArea.contains (e.getMouseDownPosition()))
        seekTo (e.x);
}

void ReferenceStrip::mouseMove (const MouseEvent& e)
{
    const auto x = trackArea.contains (e.getPosition()) && track != nullptr ? e.x : -1;
    const auto side = switchArea.contains (e.getPosition()) ? (e.x > switchArea.getCentreX() ? 1 : 0) : -1;

    if (x != hoverX || side != hoverSide)
    {
        hoverX = x;
        hoverSide = side;
        setMouseCursor (x >= 0 || side >= 0 || (track == nullptr && trackArea.contains (e.getPosition()))
                            ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
        repaint();
    }
}

void ReferenceStrip::mouseExit (const MouseEvent&)
{
    hoverX = -1;
    hoverSide = -1;
    repaint();
}

void ReferenceStrip::paint (Graphics& g)
{
    const auto& player = processor.getReference();
    const auto listening = player.isListening() && track != nullptr;
    const auto status = player.getStatus();

    //==============================================================================
    // The switch: A the mix, B the reference, the one heard lit.
    {
        const auto box = switchArea.toFloat();
        const auto radius = box.getHeight() * 0.5f;

        g.setColour (Colours::black.withAlpha (0.3f));
        g.fillRoundedRectangle (box, radius);
        g.setColour (Colour (0x26ffd2d6));
        g.drawRoundedRectangle (box.reduced (0.5f), radius, 1.0f);

        for (int side = 0; side < 2; ++side)
        {
            auto half = box.reduced (3.0f);
            half = side == 0 ? half.removeFromLeft (half.getWidth() * 0.5f) : half.withTrimmedLeft (half.getWidth() * 0.5f);

            const auto lit = side == (listening ? 1 : 0);
            const auto ink = side == 0 ? ember : refInk;
            const auto available = side == 0 || track != nullptr;
            const auto r = half.getHeight() * 0.5f;

            if (lit)
            {
                g.setColour (ink.withAlpha (0.16f));
                g.fillRoundedRectangle (half.expanded (2.0f), r + 2.0f);
                g.setGradientFill (ColourGradient (ink.withAlpha (side == 0 ? 0.75f : 0.45f), 0.0f, half.getY(),
                                                   ink.withAlpha (side == 0 ? 0.35f : 0.18f), 0.0f, half.getBottom(), false));
                g.fillRoundedRectangle (half, r);
                g.setColour (ink.withAlpha (0.9f));
                g.drawRoundedRectangle (half.reduced (0.5f), r, 1.2f);
            }
            else if (hoverSide == side && available)
            {
                g.setColour (Colours::white.withAlpha (0.06f));
                g.fillRoundedRectangle (half, r);
            }

            auto text = half.reduced (12.0f, 0.0f);
            const auto letterFont = displayFont (17.0f);
            const auto alpha = available ? 1.0f : 0.35f;

            g.setColour ((lit ? Colours::white : haze).withMultipliedAlpha (alpha));
            g.setFont (letterFont);
            g.drawText (side == 0 ? "A" : "B", text.removeFromLeft (16.0f), Justification::centred, false);

            g.setColour ((lit ? Colours::white : haze).withMultipliedAlpha (alpha));
            g.setFont (capsFont (10.0f));
            g.drawText (side == 0 ? "MIX" : "REF", text.withTrimmedLeft (6.0f), Justification::centredLeft, false);
        }
    }

    //==============================================================================
    // The reference: what it is, where it is, and its outline.
    const auto area = trackArea.toFloat();

    if (linkField.isVisible())
    {
        g.setColour (Colours::black.withAlpha (0.25f));
        g.fillRoundedRectangle (area, 8.0f);
        g.setColour (refInk.withAlpha (0.8f));
        g.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.2f);

        g.setColour (linkProblem.isNotEmpty() ? emberLit : haze);
        g.setFont (capsFont (9.0f));
        g.drawText (linkProblem.isNotEmpty() ? linkProblem : String::fromUTF8 ("YOUTUBE / TIKTOK  ·  ENTER OU FETCH  ·  ESC"),
                    area.withTrimmedLeft (area.getWidth() - 246.0f).withTrimmedRight (12.0f), Justification::centredRight, false);
    }
    else if (dragging)
    {
        g.setColour (refInk.withAlpha (0.08f));
        g.fillRoundedRectangle (area, 8.0f);
        g.setColour (refInk.withAlpha (0.8f));
        g.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.2f);
        g.setColour (Colours::white);
        g.setFont (capsFont (10.5f));
        g.drawText ("RELEASE TO LOAD THE REFERENCE", area, Justification::centred, false);
    }
    else if (track == nullptr)
    {
        Path outline;
        outline.addRoundedRectangle (area.reduced (0.5f), 8.0f);

        const float dashes[] = { 5.0f, 5.0f };
        Path dashed;
        PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);

        g.setColour (Colour (0x30ffd2d6));
        g.fillPath (dashed);

        String text = "DROP A REFERENCE HERE, OR CLICK TO BROWSE";
        auto colour = haze;

        if (status == ReferencePlayer::Status::downloading)
        {
            text = "FETCHING THE TRACK WITH THE ONIRIQUE DOWNLOADER (UP TO A MINUTE)...";
            colour = refInk;
        }
        else if (status == ReferencePlayer::Status::loading)
        {
            text = "READING AND MEASURING THE REFERENCE...";
            colour = refInk;
        }
        else if (status == ReferencePlayer::Status::failed)
        {
            text = player.getError().toUpperCase();
            colour = emberLit;
        }

        g.setColour (colour);
        g.setFont (capsFont (10.0f));
        g.drawText (text, area.reduced (12.0f, 0.0f), Justification::centred, true);
    }
    else
    {
        const auto& stats = track->stats;
        auto top = area.withHeight (16.0f);

        // The figures it is mastered to, at the right of the line.
        {
            const auto peak = jmax (stats.loudness.truePeak[0], stats.loudness.truePeak[1]);
            const auto figures = "I " + String (stats.loudness.integrated, 1) + " LUFS    TP " + String (peak, 1)
                                 + "    LRA " + String (stats.loudness.range, 1) + "    " + clockText (track->seconds());
            const auto width = GlyphArrangement::getStringWidth (monoFont (10.5f), figures) + 4.0f;

            if (width < top.getWidth() * 0.62f)
            {
                g.setColour (refInk.withAlpha (0.85f));
                g.setFont (monoFont (10.5f));
                g.drawText (figures, top.removeFromRight (width), Justification::centredRight, false);
            }
        }

        g.setColour (haze);
        g.setFont (capsFont (9.0f).withExtraKerningFactor (0.25f));
        const auto heading = status == ReferencePlayer::Status::downloading ? String ("DOWNLOADING")
                           : status == ReferencePlayer::Status::loading ? String ("LOADING")
                           : status == ReferencePlayer::Status::failed ? String ("FAILED") : String ("REFERENCE");
        const auto headingWidth = GlyphArrangement::getStringWidth (capsFont (9.0f).withExtraKerningFactor (0.25f), heading) + 12.0f;
        g.drawText (heading, top.removeFromLeft (headingWidth), Justification::centredLeft, false);

        // A link that could not be fetched is said in place of the name, until the next one.
        const auto problem = status == ReferencePlayer::Status::ready ? player.getError() : String();

        g.setColour (problem.isNotEmpty() ? emberLit : bone);
        g.setFont (displayFont (12.5f, false));
        g.drawText (problem.isNotEmpty() ? problem : track->name, top.withTrimmedRight (12.0f), Justification::centredLeft, true);

        // The outline: the part already played brighter, the playhead white.
        const auto outline = outlineArea();
        const auto middle = outline.getCentreY();
        const auto reach = outline.getHeight() * 0.5f;
        const auto played = (float) (player.getPosition() / jmax (0.001, track->seconds()));
        const auto headX = outline.getX() + played * outline.getWidth();
        const auto columns = ReferenceTrack::kColumns;
        const auto columnWidth = outline.getWidth() / (float) columns;

        float loudest = 1.0e-4f;

        for (int c = 0; c < columns; ++c)
            loudest = jmax (loudest, track->columnPeak[c]);

        for (int c = 0; c < columns; ++c)
        {
            const auto x = outline.getX() + (float) c * columnWidth;
            const auto peak = reach * track->columnPeak[c] / loudest;
            const auto rms = reach * track->columnRms[c] / loudest;
            const auto before = x < headX;

            g.setColour (refInk.withAlpha (before ? 0.42f : 0.2f));
            g.fillRect (x, middle - peak, jmax (1.0f, columnWidth - 0.4f), 2.0f * peak);
            g.setColour (refInk.withAlpha (before ? 0.9f : 0.5f));
            g.fillRect (x, middle - rms, jmax (1.0f, columnWidth - 0.4f), 2.0f * rms);
        }

        if (hoverX >= 0)
        {
            g.setColour (Colours::white.withAlpha (0.35f));
            g.fillRect ((float) hoverX, outline.getY(), 1.0f, outline.getHeight());
        }

        g.setColour (listening ? Colours::white : mist.withAlpha (0.7f));
        g.fillRect (headX - 0.75f, outline.getY() - 1.0f, 1.5f, outline.getHeight() + 2.0f);
    }

    //==============================================================================
    // How far the reference is brought to meet the mix.
    {
        const auto gainArea = Rectangle<float> ((float) matchButton.getRight() + 8.0f, 0.0f, (float) kGainWidth, (float) getHeight());
        const auto matched = track != nullptr && player.isMatching();

        g.setColour (matched ? refInk : haze.withAlpha (0.6f));
        g.setFont (monoFont (12.0f).boldened());
        g.drawText (matched ? signedText (player.getGainDb(), 1) + " dB" : String ("0.0 dB"), gainArea, Justification::centredLeft, false);
    }
}

} // namespace onirique
