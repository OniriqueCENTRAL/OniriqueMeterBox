#include "PluginEditor.h"

#include <BinaryData.h>

namespace onirique
{

using namespace theme;

namespace
{
    OniriqueLookAndFeel& sharedLookAndFeel()
    {
        static OniriqueLookAndFeel laf;
        return laf;
    }

    constexpr int kHeaderHeight = 68;
    constexpr int kTitleLeft = 72;
    constexpr int kFootHeight = 46;
    constexpr int kGap = 12;

    Font titleFont (bool heavy)
    {
        return displayFont (23.0f, heavy).withExtraKerningFactor (0.09f);
    }

    /** A four-pointed star, as on the Onirique sigil. */
    Path star (Point<float> centre, float radius)
    {
        Path p;
        const auto waist = radius * 0.16f;

        p.startNewSubPath (centre.x, centre.y - radius);
        p.lineTo (centre.x + waist, centre.y - waist);
        p.lineTo (centre.x + radius, centre.y);
        p.lineTo (centre.x + waist, centre.y + waist);
        p.lineTo (centre.x, centre.y + radius);
        p.lineTo (centre.x - waist, centre.y + waist);
        p.lineTo (centre.x - radius, centre.y);
        p.lineTo (centre.x - waist, centre.y - waist);
        p.closeSubPath();
        return p;
    }

    constexpr int kLogoWidth = 44;
    constexpr int kLogoMargin = 8;   // room for the glow around it

    Image makeLogo()
    {
        const auto sigil = ImageCache::getFromMemory (BinaryData::sigil_png, BinaryData::sigil_pngSize);

        if (! sigil.isValid())
            return {};

        const auto width = 2 * kLogoWidth;
        const auto height = width * sigil.getHeight() / sigil.getWidth();
        const auto margin = 2 * kLogoMargin;

        Image padded (Image::ARGB, width + 2 * margin, height + 2 * margin, true);

        {
            Graphics g (padded);
            g.setImageResamplingQuality (Graphics::highResamplingQuality);
            g.drawImage (sigil, Rectangle<int> (margin, margin, width, height).toFloat());
        }

        Image result (Image::ARGB, padded.getWidth(), padded.getHeight(), true);
        Graphics g (result);

        GlowEffect glow;
        glow.setGlowProperties (7.0f, ember.withAlpha (0.8f));
        glow.applyEffect (padded, g, 1.0f, 1.0f);

        return result;
    }

    String clockText (int seconds)
    {
        return String (seconds / 60).paddedLeft ('0', 2) + ":" + String (seconds % 60).paddedLeft ('0', 2);
    }
}

MeterBoxEditor::MeterBoxEditor (MeterBoxProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      loudness (p),
      history (p),
      spectrum (p),
      phase (p),
      waveform (p)
{
    setLookAndFeel (&sharedLookAndFeel());
    logo = makeLogo();

    auto& state = processor.apvts;

    addAndMakeVisible (loudness);
    addAndMakeVisible (history);
    addAndMakeVisible (spectrum);
    addAndMakeVisible (phase);
    addAndMakeVisible (waveform);

    // When it measures.
    syncButton.setClickingTogglesState (true);
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (state, "sync", syncButton));
    addAndMakeVisible (syncButton);

    pauseButton.setClickingTogglesState (true);
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (state, "pause", pauseButton));
    addAndMakeVisible (pauseButton);

    resetButton.getProperties().set ("plain", true);
    resetButton.onClick = [this] { processor.getMeter().requestReset(); };
    addAndMakeVisible (resetButton);

    // What it measures against: a standard sets both figures, and either
    // can then be moved by hand.
    for (const auto& standard : MeterBoxProcessor::getStandards())
        standardBox.addItem (standard.name, standardBox.getNumItems() + 1);

    standardBox.onChange = [this]
    {
        if (const auto id = standardBox.getSelectedId(); id > 0)
            processor.applyStandard (id - 1);
    };
    addAndMakeVisible (standardBox);

    sliderAttachments.push_back (std::make_unique<SliderAttachment> (state, "target", target));
    addAndMakeVisible (target);

    sliderAttachments.push_back (std::make_unique<SliderAttachment> (state, "ceiling", ceiling));
    addAndMakeVisible (ceiling);

    // What each part does, for a pointer that rests on it. A part comes
    // before the controls that lie on it.
    describe ({ &loudness }, "Le loudness. En grand, le niveau intégré (LUFS) depuis la remise à zéro, et son écart à la cible. Les barres : le true peak de chaque côté, avec sa valeur en direct au-dessus, puis le loudness momentané (0,4 s), court terme (3 s) et intégré, sur la même échelle. Les traits blancs sont le plafond et la cible ; les petits traits, les maxima. En dessous : la plage de loudness (LRA), le true peak le plus haut, le PLR (crête sur loudness intégré) et le PSR (crête sur loudness court terme).");
    describe ({ &history }, "L'historique : le loudness momentané en fond, le court terme en rouge, l'intégré en blanc, la cible en pointillés. Une marque blanche en haut là où le true peak a dépassé le plafond. À droite, la répartition : le temps passé par le court terme à chaque niveau.");
    describe ({ &history.spanBox }, "La durée affichée : les 30 dernières secondes, 2 minutes, 10 minutes, ou tout depuis la remise à zéro (une heure au plus).");
    describe ({ &spectrum }, "Le spectre, par tiers d'octave : un bruit rose y est une ligne plate. En rouge, le spectre à l'instant ; en blanc, sa moyenne ; la bande claire, la forme attendue pour le style choisi, posée sur la moyenne quel que soit le niveau. La moyenne passe au rouge là où elle sort de la bande. En bas, le même écart résumé en quatre zones.");
    describe ({ &spectrum.targetBox }, "La cible de balance tonale : la forme du spectre d'un titre fini de ce style. Reference est celle que tu as capturée.");
    describe ({ &spectrum.averageBox }, "La durée de la moyenne : 3 s, 10 s, ou tout depuis la remise à zéro. Les silences ne comptent pas.");
    describe ({ &spectrum.captureButton }, "Capture : fait de la moyenne affichée la cible Reference. Joue un titre de référence, laisse la moyenne se poser, puis capture. Elle est enregistrée avec le projet.");
    describe ({ &waveform }, "La forme d'onde de tout ce qui a été mesuré depuis la remise à zéro, du haut (le début) vers le bas (maintenant) : la gauche à gauche, la droite à droite. En rouge, les crêtes ; en clair au milieu, le niveau RMS. Les lignes sont -12 dB, -6 dB et la pleine échelle. Une marque blanche au bord là où le true peak a dépassé le plafond. En dessous : la crête la plus haute de chaque côté depuis la remise à zéro, puis le niveau RMS de chaque côté et le facteur de crête sur la dernière seconde, et la durée mesurée.");
    describe ({ &phase }, "La phase. Le goniomètre : ce qui est identique des deux côtés monte au milieu, ce qui est en opposition s'étale à l'horizontale. La barre : la corrélation, de -1 (les côtés s'annulent en mono) à +1 (mono), avec son minimum récent. Balance : le côté le plus fort. Width : la part du son qui est sur les côtés. Mono : ce que le mix perd une fois sommé en mono. En bas, la corrélation fréquence par fréquence : sous le milieu, cette zone disparaît en mono.");
    describe ({ &syncButton }, "Sync : la mesure suit le transport. Elle repart de zéro quand la lecture démarre et se fige à l'arrêt. Éteint, elle tourne en continu.");
    describe ({ &pauseButton }, "Pause : fige l'intégré, l'historique et les maxima. Les niveaux instantanés, le spectre et la phase continuent.");
    describe ({ &resetButton }, "Reset : remet à zéro l'intégré, la plage, les maxima, l'historique et la moyenne du spectre.");
    describe ({ &standardBox }, "La norme visée : elle règle la cible de loudness et le plafond de true peak. Streaming -14 vaut pour Spotify, YouTube, Tidal et Amazon Music.");
    describe ({ &target }, "Target : le loudness intégré visé, en LUFS. Glisse de haut en bas pour le changer.");
    describe ({ &ceiling }, "TP Max : le plafond de true peak, en dBTP. Au-dessus, le true peak est signalé.");

    hoverTip.setInterceptsMouseClicks (false, false);
    hoverTip.setAlwaysOnTop (true);
    addChildComponent (hoverTip);

    licenseOverlay = std::make_unique<onirique::license::Overlay> ("METERBOX", processor.license);
    addAndMakeVisible (*licenseOverlay);

    setResizable (true, true);
    setResizeLimits (1220, 660, 2400, 1400);
    setSize (1400, 780);

    timerCallback();
    startTimerHz (30);
}

MeterBoxEditor::~MeterBoxEditor()
{
    setLookAndFeel (nullptr);
}

void MeterBoxEditor::hideLicence()
{
    if (licenseOverlay != nullptr)
        licenseOverlay->setVisible (false);
}

//==============================================================================
void MeterBoxEditor::timerCallback()
{
    processor.getMeter().readSpectrum (frame);

    loudness.refresh();
    spectrum.refresh (frame);
    phase.refresh (frame);

    // The history gains a point ten times a second: it is drawn as often.
    if (++ticks % 3 == 0)
    {
        history.repaint();
        waveform.repaint();
    }

    // The standard the two figures amount to, if they amount to one.
    const auto standard = processor.getStandard();

    if (standard >= 0 && standardBox.getSelectedId() != standard + 1)
        standardBox.setSelectedId (standard + 1, dontSendNotification);
    else if (standard < 0 && standardBox.getText() != "Custom")
        standardBox.setText ("Custom", dontSendNotification);

    updateHoverTip();

    // The header keeps time, and says what the meter is doing.
    repaint (0, 0, getWidth(), kHeaderHeight);
}

void MeterBoxEditor::describe (std::initializer_list<Component*> components, const char* text)
{
    // The descriptions are written in French, accents and all.
    for (auto* c : components)
        described.push_back ({ c, String::fromUTF8 (text) });
}

void MeterBoxEditor::updateHoverTip()
{
    const auto now = Time::getMillisecondCounter();
    const auto at = getMouseXYRelative();

    // Only while the pointer is over this window with no button held.
    auto* under = Desktop::getInstance().getMainMouseSource().getComponentUnderMouse();
    const auto here = under != nullptr && (under == this || isParentOf (under))
                      && ! ModifierKeys::currentModifiers.isAnyMouseButtonDown();

    int over = -1;

    if (here)
        for (int i = 0; i < (int) described.size(); ++i)
            if (auto* c = described[(size_t) i].component; c->isShowing() && getLocalArea (c, c->getLocalBounds()).contains (at))
                over = i;

    // Leaving the control takes the description away, and starts the wait
    // for the next one.
    if (over != resting)
    {
        resting = over;
        restingAt = at;
        restingSince = now;
        hoverTip.setVisible (false);
        return;
    }

    // Once it is there it stays for as long as the pointer is on the
    // control, moving or not.
    if (resting < 0 || hoverTip.isVisible())
        return;

    // Until then, any movement starts the two seconds again.
    if (at.getDistanceFrom (restingAt) > 3)
    {
        restingAt = at;
        restingSince = now;
        return;
    }

    if (now - restingSince < 2000)
        return;

    const auto& text = described[(size_t) resting].text;

    hoverTip.text = text;
    hoverTip.setBounds (getLookAndFeel().getTooltipBounds (text, at, getLocalBounds().reduced (6)));
    hoverTip.setVisible (true);
    hoverTip.toFront (false);
    hoverTip.repaint();
}

int MeterBoxEditor::titleRight() const
{
    return kTitleLeft
           + roundToInt (GlyphArrangement::getStringWidth (titleFont (true), "ONIRIQUE")
                         + GlyphArrangement::getStringWidth (titleFont (false), "METERBOX"))
           + 12;
}

void MeterBoxEditor::paintHeader (Graphics& g, Rectangle<int> header)
{
    const auto area = header.toFloat();
    const auto seconds = (Time::getMillisecondCounterHiRes() - openedAt) * 0.001;

    // The crimson of the range: darkest at the sigil, brightest behind the
    // name, and falling away again to the right and towards the foot.
    ColourGradient ground (Colour (0xff2c060b), 0.0f, 0.0f, Colour (0xff3b080e), area.getRight(), 0.0f, false);
    ground.addColour (0.32, Colour (0xff7a0e1c));
    ground.addColour (0.7, Colour (0xff5a0a14));
    g.setGradientFill (ground);
    g.fillRect (area);

    g.setGradientFill (ColourGradient (Colours::transparentBlack, 0.0f, area.getY(),
                                       Colour (0x73000000), 0.0f, area.getBottom(), false));
    g.fillRect (area);

    if (logo.isValid())
    {
        const auto logoWidth = (float) logo.getWidth() * 0.5f;
        const auto logoHeight = (float) logo.getHeight() * 0.5f;

        g.drawImage (logo, { 16.0f - (float) kLogoMargin, area.getCentreY() - logoHeight * 0.5f, logoWidth, logoHeight });
    }

    // The name: the house in white, the product in a paler weight beside it.
    const auto nameWidth = GlyphArrangement::getStringWidth (titleFont (true), "ONIRIQUE");

    g.setColour (Colours::white);
    g.setFont (titleFont (true));
    g.drawText ("ONIRIQUE", area.withTrimmedLeft ((float) kTitleLeft).withWidth (nameWidth + 4.0f),
                Justification::centredLeft, false);

    g.setColour (Colour (0xffe3b5ba));
    g.setFont (titleFont (false));
    g.drawText ("METERBOX", area.withTrimmedLeft ((float) kTitleLeft + nameWidth + 10.0f).withWidth (280.0f),
                Justification::centredLeft, false);

    // Stars beside it, each breathing at its own pace.
    struct Spark { float x, y, radius, rate, offset; };
    static const Spark sparks[] = { { 10.0f, 17.0f, 5.5f, 0.9f, 0.0f },  { 27.0f, 11.0f, 3.0f, 1.3f, 1.7f },
                                    { 38.0f, 24.0f, 4.0f, 0.7f, 3.1f },  { 18.0f, 42.0f, 4.5f, 1.1f, 4.4f },
                                    { 34.0f, 48.0f, 2.5f, 1.6f, 2.3f },  { 46.0f, 38.0f, 2.0f, 1.0f, 5.2f } };

    const auto sparkLeft = (float) titleRight();

    for (const auto& s : sparks)
    {
        const auto glow = 0.5f + 0.5f * (float) std::sin (seconds * s.rate * 2.0 + s.offset);

        g.setColour (Colours::white.withAlpha (0.25f + 0.65f * glow));
        g.fillPath (star ({ sparkLeft + s.x, area.getY() + s.y }, s.radius * (0.8f + 0.2f * glow)));
    }

    // What the meter is doing, and for how long it has been measuring.
    {
        const auto measuring = processor.isMeasuring();
        const auto elapsed = (int) (processor.getMeter().historyEnd() / (uint32) mtx::kStepsPerSecond);
        const auto text = measuring ? "MEASURING" : processor.isWaitingForHost() ? "WAITING FOR PLAYBACK" : "PAUSED";
        const auto room = Rectangle<float> (sparkLeft + 70.0f, area.getY(), (float) syncButton.getX() - 20.0f - sparkLeft - 70.0f, area.getHeight() - 6.0f);

        if (room.getWidth() > 150.0f)
        {
            const auto clock = clockText (elapsed);
            const auto clockWidth = GlyphArrangement::getStringWidth (monoFont (13.0f).boldened(), clock) + 4.0f;
            const auto textWidth = GlyphArrangement::getStringWidth (capsFont (10.0f), text) + 4.0f;
            auto row = room;

            g.setColour (Colours::white);
            g.setFont (monoFont (13.0f).boldened());
            g.drawText (clock, row.removeFromRight (clockWidth), Justification::centredRight, false);
            row.removeFromRight (10.0f);

            g.setColour (measuring ? mist : haze);
            g.setFont (capsFont (10.0f));
            g.drawText (text, row.removeFromRight (textWidth), Justification::centredRight, false);

            // Lit, and breathing, while it measures.
            const auto led = Rectangle<float> (7.0f, 7.0f).withCentre ({ row.getRight() - 10.0f, room.getCentreY() });
            const auto pulse = 0.5f + 0.5f * (float) std::sin (seconds * 4.0);

            if (measuring)
            {
                g.setColour (ember.withAlpha (0.2f + 0.25f * pulse));
                g.fillEllipse (led.expanded (3.5f));
            }

            g.setColour (measuring ? emberLit : Colour (0x55ffd2d6));
            g.fillEllipse (led);
        }
    }

    // The foot of the header: a faint rule, and a streak of light that runs
    // along it from left to right and comes round again.
    const auto footY = area.getBottom() - 2.0f;

    g.setColour (Colour (0x30ffd2d6));
    g.fillRect (0.0f, footY, area.getWidth(), 1.0f);

    const auto length = jmax (160.0f, area.getWidth() * 0.22f);
    const auto cycle = 3.6;
    const auto travelled = (float) (std::fmod (seconds, cycle) / cycle);
    const auto head = -40.0f + travelled * (area.getWidth() + length + 80.0f);
    const auto tail = head - length;

    // Bright at the head, trailing off behind it, with a halo around the lot.
    ColourGradient halo (Colours::transparentBlack, tail, 0.0f, Colours::transparentBlack, head + 14.0f, 0.0f, false);
    halo.addColour (0.82, ember.withAlpha (0.55f));
    g.setGradientFill (halo);
    g.fillRect (tail, footY - 3.0f, length + 14.0f, 7.0f);

    ColourGradient streak (Colours::transparentWhite, tail, 0.0f, Colours::transparentWhite, head + 6.0f, 0.0f, false);
    streak.addColour (0.55, emberLit.withAlpha (0.9f));
    streak.addColour (0.93, Colours::white);
    g.setGradientFill (streak);
    g.fillRoundedRectangle (tail, footY - 0.5f, length + 6.0f, 2.0f, 1.0f);
}

void MeterBoxEditor::paint (Graphics& g)
{
    g.fillAll (night);

    paintHeader (g, getLocalBounds().removeFromTop (kHeaderHeight));
}

void MeterBoxEditor::resized()
{
    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop (kHeaderHeight);

    // Controls sit in the upper part of the header, clear of the light that
    // runs along its foot.
    auto row = header.withTrimmedBottom (6).withSizeKeepingCentre (header.getWidth(), 30).withTrimmedRight (14);

    resetButton.setBounds (row.removeFromRight (76));
    row.removeFromRight (10);
    pauseButton.setBounds (row.removeFromRight (96));
    row.removeFromRight (6);
    syncButton.setBounds (row.removeFromRight (90));

    auto foot = bounds.removeFromBottom (kFootHeight).reduced (14, 0).withTrimmedBottom (12).withTrimmedTop (4);

    ceiling.setBounds (foot.removeFromRight (150));
    foot.removeFromRight (12);
    target.setBounds (foot.removeFromRight (164));
    foot.removeFromRight (14);
    standardBox.setBounds (foot.removeFromRight (176).withSizeKeepingCentre (176, 26));

    // The licence badge takes the empty left of the foot. The window grows by
    // giving its contents more room, not by zooming them: the scale is 1.
    if (licenseOverlay != nullptr)
        licenseOverlay->fit (getLocalBounds(), 1.0f, foot.withWidth (248).withSizeKeepingCentre (248, 26));

    // Four columns: the loudness, the two graphs, the waveform, the phase.
    auto body = bounds.reduced (14, kGap);

    loudness.setBounds (body.removeFromLeft (jlimit (250, 300, body.getWidth() * 21 / 100)));
    body.removeFromLeft (kGap);
    phase.setBounds (body.removeFromRight (jlimit (270, 340, body.getWidth() * 29 / 100)));
    body.removeFromRight (kGap);
    waveform.setBounds (body.removeFromRight (jlimit (128, 160, body.getWidth() * 17 / 100)));
    body.removeFromRight (kGap);

    history.setBounds (body.removeFromTop (body.getHeight() * 42 / 100));
    body.removeFromTop (kGap);
    spectrum.setBounds (body);
}

} // namespace onirique
