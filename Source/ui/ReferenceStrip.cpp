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

//==============================================================================
void SearchResults::show (const Array<ReferencePlayer::Found>& titles, const String& text)
{
    found = titles;
    covers.assign ((size_t) titles.size(), Image());
    message = text;
    hover = -1;
    setVisible (true);
    toFront (false);

    if (auto* parent = getParentComponent())
    {
        const auto bottom = getBottom();
        setBounds (getX(), bottom - wantedHeight(), getWidth(), wantedHeight());
        parent->repaint (getBounds().getUnion (getBounds().withTop (bottom - 600)));
    }

    repaint();
}

void SearchResults::setCover (int index, const Image& cover)
{
    if (isPositiveAndBelow (index, (int) covers.size()))
    {
        covers[(size_t) index] = cover;
        repaint();
    }
}

void SearchResults::clear()
{
    found.clear();
    covers.clear();
    message = {};
    setVisible (false);
}

int SearchResults::wantedHeight() const
{
    return kHeading + jmax (1, found.size()) * kRow + 10;
}

int SearchResults::rowAt (int y) const
{
    const auto row = (y - kHeading) / kRow;
    return y >= kHeading && isPositiveAndBelow (row, found.size()) ? row : -1;
}

void SearchResults::mouseMove (const MouseEvent& e)
{
    const auto row = rowAt (e.y);

    if (row != hover)
    {
        hover = row;
        setMouseCursor (row >= 0 ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
        repaint();
    }
}

void SearchResults::mouseExit (const MouseEvent&)
{
    hover = -1;
    repaint();
}

void SearchResults::mouseDown (const MouseEvent& e)
{
    if (const auto row = rowAt (e.y); row >= 0 && onPick != nullptr)
        onPick (found[row]);
}

void SearchResults::paint (Graphics& g)
{
    using namespace theme;

    const auto box = getLocalBounds().toFloat().reduced (0.5f);

    g.setGradientFill (ColourGradient (onirique::look::tint (0xfa2a0a0f), 0.0f, 0.0f, onirique::look::tint (0xfa140608), 0.0f, box.getBottom(), false));
    g.fillRoundedRectangle (box, 12.0f);
    g.setColour (refInk.withAlpha (0.45f));
    g.drawRoundedRectangle (box, 12.0f, 1.0f);

    auto area = getLocalBounds().reduced (10, 0);
    auto heading = area.removeFromTop (kHeading);

    g.setColour (haze);
    g.setFont (capsFont (9.0f).withExtraKerningFactor (0.25f));
    g.drawText (String::fromUTF8 ("RÉSULTATS  ·  EXTRAITS OFFICIELS DE 30 S"), heading.withTrimmedLeft (6), Justification::centredLeft, false);

    if (found.isEmpty())
    {
        g.setColour (message.startsWith ("RECHERCHE") ? refInk : mist);
        g.setFont (displayFont (13.0f, false));
        g.drawText (message, area.removeFromTop (kRow), Justification::centred, true);
        return;
    }

    for (int i = 0; i < found.size(); ++i)
    {
        auto row = area.removeFromTop (kRow);
        const auto& f = found.getReference (i);

        if (i == hover)
        {
            g.setColour (Colours::white.withAlpha (0.07f));
            g.fillRoundedRectangle (row.toFloat().reduced (0.0f, 2.0f), 8.0f);
        }

        // The cover, or its place while it comes.
        const auto cover = row.removeFromLeft (kRow).reduced (6).toFloat();
        const auto& image = covers[(size_t) i];

        if (image.isValid())
        {
            Graphics::ScopedSaveState state (g);
            Path clip;
            clip.addRoundedRectangle (cover, 5.0f);
            g.reduceClipRegion (clip);
            g.drawImage (image, cover, RectanglePlacement::fillDestination);
        }
        else
        {
            g.setColour (Colours::white.withAlpha (0.06f));
            g.fillRoundedRectangle (cover, 5.0f);
        }

        row.removeFromLeft (8);

        const auto time = f.seconds > 0 ? String (f.seconds / 60) + ":" + String (f.seconds % 60).paddedLeft ('0', 2) : String();
        g.setColour (haze);
        g.setFont (monoFont (11.5f));
        g.drawText (time, row.removeFromRight (56).withTrimmedRight (6), Justification::centredRight, false);

        auto lines = row.reduced (0, 8);
        g.setColour (i == hover ? Colours::white : mist);
        g.setFont (displayFont (13.5f, true));
        g.drawText (f.title, lines.removeFromTop (lines.getHeight() / 2), Justification::bottomLeft, true);

        g.setColour (haze);
        g.setFont (displayFont (12.0f, false));
        g.drawText (f.album.isNotEmpty() ? f.artist + String::fromUTF8 ("  ·  ") + f.album : f.artist, lines, Justification::topLeft, true);
    }
}

//==============================================================================
ReferenceStrip::ReferenceStrip (MeterBoxProcessor& p) : processor (p)
{
    auto& player = processor.getReference();

    matchButton.setClickingTogglesState (true);
    matchButton.setToggleState (player.isMatching(), dontSendNotification);
    matchButton.onClick = [this] { processor.getReference().setMatching (matchButton.getToggleState()); };
    addAndMakeVisible (matchButton);

    lockButton.setClickingTogglesState (true);
    lockButton.setToggleState (player.isLocked(), dontSendNotification);
    lockButton.onClick = [this] { processor.getReference().setLocked (lockButton.getToggleState()); repaint(); };
    addAndMakeVisible (lockButton);

    loadButton.getProperties().set ("plain", true);
    loadButton.onClick = [this] { browse(); };
    addAndMakeVisible (loadButton);

    searchButton.getProperties().set ("plain", true);
    searchButton.onClick = [this] { openSearch(); };
    addAndMakeVisible (searchButton);

    linkField.setColour (TextEditor::backgroundColourId, Colours::transparentBlack);
    linkField.setColour (TextEditor::outlineColourId, Colours::transparentBlack);
    linkField.setColour (TextEditor::focusedOutlineColourId, Colours::transparentBlack);
    linkField.setColour (TextEditor::textColourId, Colours::white);
    linkField.setFont (displayFont (13.0f, false));
    linkField.setTextToShowWhenEmpty (String::fromUTF8 ("Titre, artiste, ou lien Spotify / YouTube / TikTok"), haze);
    linkField.setIndents (12, 0);
    linkField.setJustification (Justification::centredLeft);
    linkField.onReturnKey = [this] { runSearch(); };
    linkField.onEscapeKey = [this] { closeSearch(); };
    linkField.onTextChange = [this] { linkProblem = {}; repaint(); };
    addChildComponent (linkField);

    results.onPick = [this] (const ReferencePlayer::Found& f) { pick (f); };
    results.setVisible (false);

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

    loadButton.setBounds (area.removeFromRight (64).withSizeKeepingCentre (64, 26));
    area.removeFromRight (6);
    searchButton.setBounds (area.removeFromRight (76).withSizeKeepingCentre (76, 26));
    area.removeFromRight (16);
    lockButton.setBounds (area.removeFromRight (84).withSizeKeepingCentre (84, 28));
    area.removeFromRight (10);
    area.removeFromRight (kGainWidth);
    matchButton.setBounds (area.removeFromRight (96).withSizeKeepingCentre (96, 28));
    area.removeFromRight (16);

    trackArea = area.withTrimmedTop (4).withTrimmedBottom (4);

    switchZone.setBounds (switchArea);
    trackZone.setBounds (trackArea);

    // The field takes the place of the reference while a search is typed, its
    // hint at the right.
    linkField.setBounds (trackArea.withTrimmedRight (210).reduced (1, 3));
}

void ReferenceStrip::openSearch()
{
    if (linkField.isVisible())
    {
        runSearch();
        return;
    }

    // A link copied from the browser (Spotify, YouTube, TikTok) is already in it.
    const auto copied = SystemClipboard::getTextFromClipboard().trim();
    const auto spotify = copied.containsIgnoreCase ("open.spotify.com/") && ! copied.containsAnyOf (" \n");

    linkProblem = {};

    if (ReferencePlayer::isPlatformLink (copied) || spotify)
        linkField.setText (copied, dontSendNotification);

    linkField.setVisible (true);
    linkField.grabKeyboardFocus();
    linkField.selectAll();
    searchButton.setButtonText ("GO");
    searchButton.setToggleState (true, dontSendNotification);
    repaint();
}

void ReferenceStrip::runSearch()
{
    const auto query = linkField.getText().trim();

    // A YouTube or TikTok link: the whole track, through the Downloader.
    if (ReferencePlayer::isPlatformLink (query))
    {
        processor.loadReferenceLink (query);
        closeSearch();
        return;
    }

    if (query.length() < 2)
    {
        linkProblem = String::fromUTF8 ("TAPE UN TITRE, UN ARTISTE OU UN LIEN");
        repaint();
        return;
    }

    // A title, an artist or a Spotify link: the search of the site, off the message thread.
    const auto generation = ++searchGeneration;
    results.show ({}, String::fromUTF8 ("RECHERCHE…"));

    Thread::launch ([safe = Component::SafePointer<ReferenceStrip> (this), query, generation]
    {
        String problem;
        const auto titles = ReferencePlayer::search (query, problem);

        MessageManager::callAsync ([safe, titles, problem, generation]
        {
            if (safe != nullptr && safe->searchGeneration == generation)
                safe->results.show (titles, problem);
        });

        // The covers, one by one, as they come.
        for (int i = 0; i < titles.size(); ++i)
        {
            if (titles[i].coverUrl.isEmpty() || ! titles[i].coverUrl.startsWithIgnoreCase ("https://"))
                continue;

            if (auto in = URL (titles[i].coverUrl).createInputStream (URL::InputStreamOptions (URL::ParameterHandling::inAddress)
                                                                          .withConnectionTimeoutMs (5000)))
            {
                // Read whole first: the JPEG reader does not take a stream from the network.
                MemoryBlock bytes;
                in->readIntoMemoryBlock (bytes, 2 * 1024 * 1024);
                const auto cover = ImageFileFormat::loadFrom (bytes.getData(), bytes.getSize());

                MessageManager::callAsync ([safe, cover, i, generation]
                {
                    if (safe != nullptr && safe->searchGeneration == generation)
                        safe->results.setCover (i, cover);
                });
            }
        }
    });
}

void ReferenceStrip::pick (const ReferencePlayer::Found& found)
{
    // Whatever is in place is replaced, once the new one is ready.
    processor.loadReferencePreview (found);
    closeSearch();
}

void ReferenceStrip::closeSearch()
{
    ++searchGeneration;
    results.clear();
    linkField.setVisible (false);
    linkProblem = {};
    searchButton.setButtonText ("SEARCH");
    searchButton.setToggleState (false, dontSendNotification);
    refresh();
}

void ReferenceStrip::refresh()
{
    track = processor.getReference().getTrack();

    const auto& player = processor.getReference();
    matchButton.setToggleState (player.isMatching(), dontSendNotification);
    matchButton.setEnabled (track != nullptr);
    lockButton.setToggleState (player.isLocked(), dontSendNotification);
    lockButton.setEnabled (track != nullptr);

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

void ReferenceStrip::mouseDoubleClick (const MouseEvent& e)
{
    // Locked: back in step with the project, second for second.
    if (track != nullptr && trackArea.contains (e.getPosition()))
        processor.getReference().setOffset (0.0);
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
        g.drawText (linkProblem.isNotEmpty() ? linkProblem : String::fromUTF8 ("ENTRÉE OU GO  ·  ESC POUR FERMER"),
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
            text = "FETCHING THE TRACK FROM THE ONIRIQUE SITE...";
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
            // Locked and moved against the project: by how much.
            const auto shift = player.isLocked() && std::abs (player.getOffset()) >= 0.005
                                 ? "    SYNC " + signedText ((float) player.getOffset(), 2) + " s" : String();
            const auto figures = "I " + String (stats.loudness.integrated, 1) + " LUFS    TP " + String (peak, 1)
                                 + "    LRA " + String (stats.loudness.range, 1) + "    " + clockText (track->seconds()) + shift;
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
        // Locked, the playhead can be before the start or after the end: it waits at the edge.
        const auto played = jlimit (0.0f, 1.0f, (float) (player.getPosition() / jmax (0.001, track->seconds())));
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
