// Onirique MeterBox — editor
#pragma once
#include "ui/OniriqueBar.h"   // la barre « Nuit » de VOCALBOX

#include <juce_audio_processors/juce_audio_processors.h>
#include "../License/OniriqueThemeButton.h"
#include "ui/OniriqueDA.h"

#include "License/LicenseOverlay.h"
#include "PluginProcessor.h"
#include "ui/Controls.h"
#include "ui/FocusTabs.h"
#include "ui/HistoryGraph.h"
#include "ui/LoudnessPanel.h"
#include "ui/PhasePanel.h"
#include "ui/ReferenceStrip.h"
#include "ui/SpectrumDisplay.h"
#include "ui/Theme.h"
#include "ui/WaveformPanel.h"

namespace onirique
{

/** One window, read from left to right: how loud (the figures and the
    bars), how that has moved and what the spectrum looks like (the two
    graphs in the middle), the whole of it as a waveform, and how the two
    sides sit against each other.

    The header is when it measures; the foot, what it measures against. */
class MeterBoxEditor final : public AudioProcessorEditor,
                             public FileDragAndDropTarget,
                             public TextDragAndDropTarget,
                             private Timer
{
public:
    explicit MeterBoxEditor (MeterBoxProcessor&);
    ~MeterBoxEditor() override;

    void paint (Graphics&) override;
    void resized() override;

    /** Takes the licence badge and its page away, for a picture of the
        interface alone. */
    void hideLicence();

    // A reference dropped anywhere on the window.
    bool isInterestedInFileDrag (const StringArray&) override;
    void fileDragEnter (const StringArray&, int, int) override;
    void fileDragExit (const StringArray&) override;
    void filesDropped (const StringArray&, int, int) override;

    // A link to a track on a platform, dragged from the browser.
    bool isInterestedInTextDrag (const String&) override;
    void textDragEnter (const String&, int, int) override;
    void textDragExit (const String&) override;
    void textDropped (const String&, int, int) override;

private:
    using ButtonAttachment = AudioProcessorValueTreeState::ButtonAttachment;
    using SliderAttachment = AudioProcessorValueTreeState::SliderAttachment;

    void timerCallback() override;
    void paintHeader (Graphics&, Rectangle<int> header);
    int titleRight() const;

    MeterBoxProcessor& processor;


    // la barre « Nuit » (réglages gardés, A/B, menu ⋯ avec la couleur de l'interface), à la place de l'en-tête

    static constexpr int kBarUnits = 1029;   // 50 points de haut = 68 pixels à la taille d'ouverture (1400)

    static Image barSigil();

    onirique::nuit::Bar bar { processor.presets, processor.apvts, "METERBOX", barSigil(), kBarUnits, 1400 };

    int headerHeight() const { return roundToInt (50.0f * (float) getWidth() / (float) kBarUnits); }

    Rectangle<int> statusArea;   // l'état de la mesure, au pied à gauche

    void paintStatus (Graphics&);

    LoudnessPanel loudness;
    HistoryGraph history;
    SpectrumDisplay spectrum;
    PhasePanel phase;
    WaveformPanel waveform;
    ReferenceStrip referenceStrip;

    // The simplified view: FULL / FOCUS, and the row of tools to focus on.
    ViewSwitch viewSwitch;
    FocusTabs focusTabs;

    /** The parts of the window, in the order of the tools. */
    Component* tool (int index) noexcept;
    String readoutOf (int tool) const;
    void applyView();
    mtx::SpectrumFrame frame;

    /** The hairlines that part the columns: everything else stands on the ground itself. */
    std::vector<Rectangle<float>> rules;
    int ticks = 0;

    // The header: when it measures.
    TextButton syncButton { "SYNC" }, pauseButton { "PAUSE" }, resetButton { "RESET" };

    // The foot: what it measures against.
    ComboBox standardBox;
    ValueField target { "TARGET" }, ceiling { "TP MAX" };

    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;

    //==============================================================================
    /** The description of whatever the pointer is resting on.

        Shown only once the pointer has been still on a control for two full
        seconds. From then on it stays for as long as the pointer is on that
        control, moving or not, and goes when it leaves. */
    struct HoverTip final : Component
    {
        void paint (Graphics& g) override
        {
            getLookAndFeel().drawTooltip (g, text, getWidth(), getHeight());
        }

        String text;
    };

    struct Described
    {
        Component* component;
        String text;
    };

    void describe (std::initializer_list<Component*>, const char* text);
    void updateHoverTip();

    std::vector<Described> described;
    HoverTip hoverTip;
    int resting = -1;           // which of the described, or none
    Point<int> restingAt;
    uint32 restingSince = 0;

    /** The sigil with its ember glow already baked in, at twice the size it is
        drawn so it stays sharp on a scaled display. */
    Image logo;

    /** When the editor opened: the light that runs along the foot of the
        header, and the stars beside the name, keep time from here. */
    double openedAt = Time::getMillisecondCounterHiRes();

    /** The licence badge and its activation page. Declared last and added
        last: it lies over everything else. */
    std::unique_ptr<onirique::license::Overlay> licenseOverlay;

    onirique::look::ThemeButton themeButton;   // ⋯ : la couleur de l'interface

    int themeSeen = -1;

    juce::Image daBackdrop;   // le fond de VOCALBOX, calculé une fois par taille

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterBoxEditor)
};

} // namespace onirique
