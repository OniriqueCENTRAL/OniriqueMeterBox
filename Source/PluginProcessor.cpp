#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace onirique
{

namespace
{
    using Attributes = AudioParameterFloatAttributes;

    const Identifier kReferenceProperty ("reference");

    float parseNumber (const String& text)
    {
        return text.retainCharacters ("0123456789.,-").replaceCharacter (',', '.').getFloatValue();
    }

    Attributes level (const String& unit)
    {
        return Attributes()
            .withStringFromValueFunction ([unit] (float v, int) { return String (v, 1) + " " + unit; })
            .withValueFromStringFunction ([] (const String& t) { return parseNumber (t); });
    }

    // How far back the history looks, for each choice of "span".
    constexpr int kSpanSeconds[] = { 30, 120, 600, 0 };

    AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        AudioProcessorValueTreeState::ParameterLayout layout;

        // What the measurement is held against.
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "target", 1 }, "Target Loudness",
                                                           NormalisableRange<float> { -36.0f, -6.0f, 0.1f }, -14.0f, level ("LUFS")));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "ceiling", 1 }, "True Peak Ceiling",
                                                           NormalisableRange<float> { -9.0f, 0.0f, 0.1f }, -1.0f, level ("dBTP")));

        // When it measures.
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "sync", 1 }, "Follow Transport", true));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "pause", 1 }, "Pause", false));

        // How it is shown.
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "span", 1 }, "History Span",
                                                            StringArray { "30 s", "2 min", "10 min", "All" }, 1));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "tonal", 1 }, "Tonal Target",
                                                            MeterBoxProcessor::getTonalTargetNames(), 0));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "average", 1 }, "Spectrum Average",
                                                            StringArray { "3 s", "10 s", "All" }, mtx::averageLong));

        return layout;
    }

    void moveParameter (AudioProcessorValueTreeState& state, const String& id, float value)
    {
        if (auto* parameter = state.getParameter (id))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
            parameter->endChangeGesture();
        }
    }
}

/** The parameters' values, looked up once. */
struct MeterBoxProcessor::Raw
{
    explicit Raw (AudioProcessorValueTreeState& state)
    {
        auto get = [&state] (const String& id) { return state.getRawParameterValue (id); };

        target = get ("target");     ceiling = get ("ceiling");
        sync = get ("sync");         pause = get ("pause");
        span = get ("span");         tonal = get ("tonal");       average = get ("average");
    }

    std::atomic<float> *target, *ceiling, *sync, *pause, *span, *tonal, *average;
};

/** The spectrum's transforms are made here, off the audio thread, whether
    the editor is open or not: an average since the reset has to go on being
    taken while nobody is looking. */
class MeterBoxProcessor::Analysis final : public Thread
{
public:
    explicit Analysis (mtx::Meter& m) : Thread ("MeterBox analysis"), meter (m) {}

    void run() override
    {
        while (! threadShouldExit())
        {
            meter.analyse();
            wait (12);
        }
    }

private:
    mtx::Meter& meter;
};

//==============================================================================
MeterBoxProcessor::MeterBoxProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MeterBox", createLayout()),
      raw (std::make_unique<Raw> (apvts)),
      analysis (std::make_unique<Analysis> (meter))
{
    analysis->startThread();
}

MeterBoxProcessor::~MeterBoxProcessor()
{
    analysis->stopThread (2000);
}

bool MeterBoxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    // Nothing is changed: what goes out has the channels of what came in.
    return in == out && (in == AudioChannelSet::mono() || in == AudioChannelSet::stereo());
}

void MeterBoxProcessor::prepareToPlay (double sampleRate, int)
{
    license.refresh();
    meter.prepare (sampleRate, jlimit (1, (int) mtx::kMaxChannels, getMainBusNumInputChannels()));
    meter.setAveraging (roundToInt (raw->average->load()));

    // A render starts a measurement of its own.
    wasPlaying = false;
}

void MeterBoxProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;

    const auto numIn = jmin (getMainBusNumInputChannels(), (int) mtx::kMaxChannels, buffer.getNumChannels());
    const auto numSamples = buffer.getNumSamples();

    if (numIn >= 1 && numSamples > 0)
    {
        // Following the host: a measurement starts when playback does, and
        // holds while it is stopped. A host that says nothing is taken to be
        // playing.
        const auto sync = raw->sync->load() > 0.5f;
        bool playing = true;

        if (sync)
            if (auto* head = getPlayHead())
                if (const auto position = head->getPosition())
                    playing = position->getIsPlaying();

        if (sync && playing && ! wasPlaying)
            meter.requestReset();

        wasPlaying = playing;

        const auto paused = raw->pause->load() > 0.5f;

        measuring.store (playing && ! paused, std::memory_order_relaxed);
        waiting.store (! playing && ! paused, std::memory_order_relaxed);

        const float* in[mtx::kMaxChannels] = { buffer.getReadPointer (0), buffer.getReadPointer (numIn > 1 ? 1 : 0) };

        meter.setAveraging (roundToInt (raw->average->load()));
        meter.process (in, numIn, numSamples, playing && ! paused);

        // A render goes faster than the thread that makes the transforms
        // can follow, and can afford to wait for them.
        if (isNonRealtime())
            meter.analyse();
    }

    // Last, and over every output: what an unlicensed plugin lets through.
    license.process (buffer, getSampleRate(), isNonRealtime());
}

AudioProcessorEditor* MeterBoxProcessor::createEditor()
{
    return new MeterBoxEditor (*this);
}

//==============================================================================
float MeterBoxProcessor::getTargetLufs() const noexcept   { return raw->target->load(); }
float MeterBoxProcessor::getCeilingDb() const noexcept    { return raw->ceiling->load(); }

int MeterBoxProcessor::getHistorySpan() const noexcept
{
    return kSpanSeconds[jlimit (0, 3, roundToInt (raw->span->load()))];
}

const std::vector<MeterBoxProcessor::Standard>& MeterBoxProcessor::getStandards()
{
    static const std::vector<Standard> standards
    {
        { "Streaming -14", -14.0f, -1.0f },         // Spotify, YouTube, Tidal, Amazon Music
        { "Apple Music -16", -16.0f, -1.0f },
        { "Loud master -9", -9.0f, -1.0f },
        { "EBU R 128 -23", -23.0f, -1.0f },
        { "ATSC A/85 -24", -24.0f, -2.0f },
    };

    return standards;
}

int MeterBoxProcessor::getStandard() const noexcept
{
    const auto& standards = getStandards();

    for (size_t i = 0; i < standards.size(); ++i)
        if (std::abs (standards[i].target - getTargetLufs()) < 0.05f && std::abs (standards[i].ceiling - getCeilingDb()) < 0.05f)
            return (int) i;

    return -1;
}

void MeterBoxProcessor::applyStandard (int index)
{
    const auto& standards = getStandards();

    if (! isPositiveAndBelow (index, (int) standards.size()))
        return;

    moveParameter (apvts, "target", standards[(size_t) index].target);
    moveParameter (apvts, "ceiling", standards[(size_t) index].ceiling);
}

//==============================================================================
StringArray MeterBoxProcessor::getTonalTargetNames()
{
    StringArray names;

    for (int i = 0; i < mtx::kNumStockTargets; ++i)
        names.add (mtx::stockTargetName (i));

    names.add ("Reference");
    return names;
}

bool MeterBoxProcessor::getTonalTarget (mtx::TonalTarget& target) const
{
    const auto choice = roundToInt (raw->tonal->load());

    if (choice < mtx::kNumStockTargets)
    {
        target = mtx::stockTarget (choice);
        return true;
    }

    const ScopedLock lock (referenceLock);

    if (referenceValid)
        target = reference;

    return referenceValid;
}

bool MeterBoxProcessor::hasReference() const
{
    const ScopedLock lock (referenceLock);
    return referenceValid;
}

bool MeterBoxProcessor::captureReference()
{
    mtx::SpectrumFrame frame;
    meter.readSpectrum (frame);

    if (! frame.measured)
        return false;

    {
        const ScopedLock lock (referenceLock);
        reference = mtx::targetFrom (frame.average);
        referenceValid = true;
    }

    moveParameter (apvts, "tonal", (float) mtx::kNumStockTargets);
    return true;
}

//==============================================================================
void MeterBoxProcessor::getStateInformation (MemoryBlock& destination)
{
    auto state = apvts.copyState();

    {
        // The captured shape goes with the project: a point to the number.
        const ScopedLock lock (referenceLock);

        if (referenceValid)
        {
            StringArray points;

            for (const auto value : reference.mean)
                points.add (String (value, 2));

            state.setProperty (kReferenceProperty, points.joinIntoString (" "), nullptr);
        }
        else
        {
            state.removeProperty (kReferenceProperty, nullptr);
        }
    }

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destination);
}

void MeterBoxProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    const auto state = ValueTree::fromXml (*xml);
    const auto points = StringArray::fromTokens (state.getProperty (kReferenceProperty).toString(), " ", "");

    {
        const ScopedLock lock (referenceLock);

        referenceValid = points.size() == mtx::kSpectrumPoints;

        if (referenceValid)
        {
            float shape[mtx::kSpectrumPoints];

            for (int p = 0; p < mtx::kSpectrumPoints; ++p)
                shape[p] = points[p].getFloatValue();

            reference = mtx::targetFrom (shape);
        }
    }

    apvts.replaceState (state);
}

} // namespace onirique

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new onirique::MeterBoxProcessor();
}
