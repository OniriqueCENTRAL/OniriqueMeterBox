// Onirique MeterBox — plugin entry point
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "License/LicenseGuard.h"
#include "dsp/Meter.h"

namespace onirique
{

using namespace juce;

/** The plugin instance. The sound goes through as it came; on its way it is
    handed to the meter, which the editor reads. What is kept here is what a
    measurement is held against: a loudness, a ceiling, and the shape the
    spectrum is expected to have. */
class MeterBoxProcessor final : public AudioProcessor
{
public:
    MeterBoxProcessor();
    ~MeterBoxProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (AudioBuffer<float>&, MidiBuffer&) override;

    AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override     { return false; }
    bool producesMidi() const override    { return false; }
    bool isMidiEffect() const override    { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override            { return 1; }
    int getCurrentProgram() override         { return 0; }
    void setCurrentProgram (int) override    {}
    const String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const String&) override {}

    void getStateInformation (MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==============================================================================
    mtx::Meter& getMeter() noexcept { return meter; }

    float getTargetLufs() const noexcept;
    float getCeilingDb() const noexcept;

    /** How far back the history looks, in seconds; 0 for all of it. */
    int getHistorySpan() const noexcept;

    /** Whether what is going through just now counts towards the measurement,
        and if not, whether it is the host being stopped that holds it. */
    bool isMeasuring() const noexcept { return measuring.load (std::memory_order_relaxed); }
    bool isWaitingForHost() const noexcept { return waiting.load (std::memory_order_relaxed); }

    //==============================================================================
    /** A loudness and a ceiling that go together: what a platform or a
        broadcaster asks for. */
    struct Standard
    {
        const char* name;
        float target, ceiling;
    };

    static const std::vector<Standard>& getStandards();

    /** The standard the target and the ceiling are set to, or -1. */
    int getStandard() const noexcept;
    void applyStandard (int index);

    //==============================================================================
    /** The names of the shapes the spectrum can be held against: the stock
        ones, then the one captured from a record. */
    static StringArray getTonalTargetNames();

    /** The shape chosen. False when it is the captured one and nothing has
        been captured. */
    bool getTonalTarget (mtx::TonalTarget&) const;

    /** Makes a target of the spectrum as it is averaged just now, and
        chooses it. False if nothing has been measured. Message thread. */
    bool captureReference();
    bool hasReference() const;

    AudioProcessorValueTreeState apvts;

    /** The licence ONIRIQUE CENTRAL wrote on this machine for this plugin, under
        the identifier the plugin has in its catalogue. */
    onirique::license::Guard license { "onirique-meterbox" };

private:
    class Analysis;
    struct Raw;

    mtx::Meter meter;
    std::unique_ptr<Raw> raw;
    std::unique_ptr<Analysis> analysis;

    bool wasPlaying = false;
    std::atomic<bool> measuring { false }, waiting { false };

    CriticalSection referenceLock;
    mtx::TonalTarget reference;
    bool referenceValid = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterBoxProcessor)
};

} // namespace onirique
