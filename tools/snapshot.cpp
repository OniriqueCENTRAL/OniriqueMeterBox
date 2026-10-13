// Renders the editor to a PNG without a host, with something like a record
// running through it, so a change to the interface can be looked at straight
// from the build.
//
//   meterbox_snapshot out.png [width height]
//   meterbox_snapshot out.png --phase       the same with the lows out of phase, to see what a fault looks like
//   meterbox_snapshot out.png --licence     with the licence badge and its page left on
//   meterbox_snapshot out.png --bench       and how long the window takes to draw
//   meterbox_snapshot --selftest            the plugin as a host drives it, without a window
#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"
#include "TestSignal.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace
{
constexpr double kSr = 48000.0;
constexpr int kBlock = 512;

std::unique_ptr<onirique::MeterBoxProcessor> makeProcessor()
{
    return std::unique_ptr<onirique::MeterBoxProcessor> (static_cast<onirique::MeterBoxProcessor*> (createPluginFilter()));
}

void setParameter (onirique::MeterBoxProcessor& processor, const char* id, float value)
{
    auto* p = processor.apvts.getParameter (id);
    p->setValueNotifyingHost (p->convertTo0to1 (value));
}

/** Plays two sides through the plugin, a block at a time. */
void play (onirique::MeterBoxProcessor& processor, const std::vector<float>& left, const std::vector<float>& right,
           size_t from, size_t to)
{
    juce::AudioBuffer<float> buffer (2, kBlock);
    juce::MidiBuffer midi;

    for (auto at = from; at + kBlock <= to; at += kBlock)
    {
        buffer.copyFrom (0, 0, left.data() + at, kBlock);
        buffer.copyFrom (1, 0, right.data() + at, kBlock);
        processor.processBlock (buffer, midi);
    }
}

/** Sets the plugin up, runs most of a signal through it at once, as a render
    would, then plays the end of it on the message thread one timer tick at a
    time so that what moves on the screen has something to follow; takes the
    picture and ends the program. */
struct Director final : juce::Timer
{
    Director (juce::File destination, bool outOfPhase, bool withLicence, bool timed, int width, int height)
        : file (std::move (destination)), bench (timed)
    {
        constexpr double seconds = 96.0;

        mtxtest::makeMusic (seconds, kSr, left, right);

        if (outOfPhase)
        {
            // What a bass widened carelessly does: under 150 Hz or so, the
            // right side is the left one turned over.
            double low = 0.0;
            const auto k = 1.0 - std::exp (-2.0 * mtxtest::kPi * 150.0 / kSr);

            for (size_t i = 0; i < left.size(); ++i)
            {
                low += k * (0.5 * ((double) left[i] + right[i]) - low);
                right[i] -= (float) (2.0 * low);
            }
        }

        processor = makeProcessor();
        processor->setPlayConfigDetails (2, 2, kSr, kBlock);
        setParameter (*processor, "span", 1.0f);
        processor->prepareToPlay (kSr, kBlock);

        // All but the last three seconds, as fast as it will go.
        position = left.size() - (size_t) (kSr * 3.0);
        position -= position % kBlock;

        processor->setNonRealtime (true);
        play (*processor, left, right, 0, position);
        processor->setNonRealtime (false);

        editor.reset (processor->createEditor());
        editor->setSize (width, height);

        if (! withLicence)
            if (auto* meterBox = dynamic_cast<onirique::MeterBoxEditor*> (editor.get()))
                meterBox->hideLicence();

        startTimer (30);
    }

    void timerCallback() override
    {
        // About a tick's worth of audio per tick.
        const auto to = std::min (left.size(), position + (size_t) (3 * kBlock));
        play (*processor, left, right, position, to);
        position += (to - position) / kBlock * kBlock;

        if (++ticks > 80)
            finish();
    }

    void finish()
    {
        stopTimer();

        if (bench)
        {
            // The whole window, as often as the editor draws it in two seconds.
            const auto start = juce::Time::getMillisecondCounterHiRes();

            for (int i = 0; i < 60; ++i)
                editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);

            const auto each = (juce::Time::getMillisecondCounterHiRes() - start) / 60.0;
            std::printf ("  the whole window drawn in %.1f ms: %.0f %% of one core at 30 pictures a second\n", each, each * 3.0);
        }

        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);

        file.deleteFile();

        if (auto stream = file.createOutputStream())
            juce::PNGImageFormat().writeImageToStream (image, *stream);

        juce::MessageManager::getInstance()->stopDispatchLoop();
    }

    juce::File file;
    bool bench = false;
    std::unique_ptr<onirique::MeterBoxProcessor> processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::vector<float> left, right;
    size_t position = 0;
    int ticks = 0;
};
}

/** The plugin as a host drives it: what goes out is what came in, the
    measurement follows the settings, and the state comes back. Returns the
    number of things found wrong. */
int selfTest()
{
    int failures = 0;

    auto check = [&failures] (bool ok, const char* what)
    {
        std::printf ("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        failures += ok ? 0 : 1;
    };

    std::vector<float> left, right;
    mtxtest::makeMusic (12.0, kSr, left, right);

    // Through the plugin: not a bit of it is changed. (Without a licence
    // the plugin puts noise over a render, and over two seconds in thirty
    // of what is listened to: the first ten seconds are clear of it.)
    {
        auto processor = makeProcessor();
        processor->setPlayConfigDetails (2, 2, kSr, kBlock);
        processor->prepareToPlay (kSr, kBlock);

        juce::AudioBuffer<float> buffer (2, kBlock);
        juce::MidiBuffer midi;
        bool same = true;

        for (size_t at = 0; at + kBlock <= (size_t) (kSr * 10.0); at += kBlock)
        {
            buffer.copyFrom (0, 0, left.data() + at, kBlock);
            buffer.copyFrom (1, 0, right.data() + at, kBlock);
            processor->processBlock (buffer, midi);

            for (int i = 0; i < kBlock; ++i)
                same = same && buffer.getSample (0, i) == left[at + (size_t) i] && buffer.getSample (1, i) == right[at + (size_t) i];
        }

        check (same, "what goes out is what came in, to the bit");
        check (processor->getLatencySamples() == 0, "there is no latency");

        const auto values = processor->getMeter().loudness();
        std::printf ("    ten seconds of it: I %.1f LUFS, true peak %.1f dBTP\n", values.integrated, juce::jmax (values.truePeak[0], values.truePeak[1]));
        check (values.integrated > -30.0f && values.integrated < 0.0f && processor->getMeter().historyEnd() >= 99, "and it has been measured on the way");
        check (processor->isMeasuring(), "a host that says nothing of its transport is taken to be playing");

        // Paused, the measurement holds.
        setParameter (*processor, "pause", 1.0f);
        const auto before = processor->getMeter().historyEnd();
        play (*processor, left, right, 0, (size_t) kSr);
        check (processor->getMeter().historyEnd() == before && ! processor->isMeasuring(), "paused, the measurement holds");
    }

    // One channel in, one out; no folding, no spreading.
    {
        auto processor = makeProcessor();

        juce::AudioProcessor::BusesLayout mono, folded;
        mono.inputBuses.add (juce::AudioChannelSet::mono());
        mono.outputBuses.add (juce::AudioChannelSet::mono());
        folded.inputBuses.add (juce::AudioChannelSet::stereo());
        folded.outputBuses.add (juce::AudioChannelSet::mono());

        check (processor->checkBusesLayoutSupported (mono), "one channel is taken as it is");
        check (! processor->checkBusesLayoutSupported (folded), "two channels are not folded into one");
    }

    // The standards.
    {
        auto processor = makeProcessor();

        check (processor->getStandard() == 0, "it starts on the streaming standard, -14 LUFS and -1 dBTP");

        processor->applyStandard (3);
        check (std::abs (processor->getTargetLufs() + 23.0f) < 0.01f && std::abs (processor->getCeilingDb() + 1.0f) < 0.01f
                   && processor->getStandard() == 3,
               "a standard sets the target and the ceiling");

        setParameter (*processor, "target", -11.5f);
        check (processor->getStandard() == -1, "and a figure moved by hand is no standard");
    }

    // The captured reference, and the state a host saves.
    {
        auto processor = makeProcessor();
        processor->setPlayConfigDetails (2, 2, kSr, kBlock);
        processor->prepareToPlay (kSr, kBlock);

        mtx::TonalTarget target;
        setParameter (*processor, "tonal", (float) mtx::kNumStockTargets);
        check (! processor->getTonalTarget (target) && ! processor->captureReference(), "there is no reference before anything has been played");

        setParameter (*processor, "tonal", 0.0f);
        processor->setNonRealtime (true);
        play (*processor, left, right, 0, left.size());
        processor->setNonRealtime (false);

        check (processor->captureReference() && processor->getTonalTarget (target), "what is playing can be captured as the reference");

        mtx::SpectrumFrame frame;
        processor->getMeter().readSpectrum (frame);

        float deviation[mtx::kNumRegions], tolerance[mtx::kNumRegions];
        mtx::compareRegions (target, frame.average, mtx::fitTarget (target, frame.average), deviation, tolerance);
        check (std::abs (deviation[0]) < 0.1f && std::abs (deviation[3]) < 0.1f, "and what it was captured from lies on it");

        setParameter (*processor, "target", -16.0f);
        setParameter (*processor, "sync", 0.0f);

        juce::MemoryBlock saved;
        processor->getStateInformation (saved);

        auto second = makeProcessor();
        second->setStateInformation (saved.getData(), (int) saved.getSize());

        mtx::TonalTarget restored;
        bool sameShape = second->getTonalTarget (restored);

        for (int p = 0; p < mtx::kSpectrumPoints && sameShape; ++p)
            sameShape = std::abs (restored.mean[p] - target.mean[p]) < 0.02f;

        check (std::abs (second->getTargetLufs() + 16.0f) < 0.01f
                   && second->apvts.getRawParameterValue ("sync")->load() < 0.5f
                   && juce::roundToInt (second->apvts.getRawParameterValue ("tonal")->load()) == mtx::kNumStockTargets,
               "a restored state has the settings it was saved with");
        check (sameShape, "and the reference that was captured");
    }

    std::printf (failures == 0 ? "\nall passed\n" : "\n%d FAILED\n", failures);
    return failures;
}

int main (int argc, char** argv)
{
    if (argc < 2)
        return 2;

    const juce::ScopedJuceInitialiser_GUI gui;

    if (juce::String (argv[1]) == "--selftest")
        return selfTest();

    juce::StringArray arguments (argv + 1, argc - 1);
    const auto outOfPhase = arguments.contains ("--phase");
    const auto withLicence = arguments.contains ("--licence");
    const auto bench = arguments.contains ("--bench");

    arguments.removeString ("--phase");
    arguments.removeString ("--licence");
    arguments.removeString ("--bench");

    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (arguments[0]);
    const auto width = arguments.size() > 2 ? arguments[1].getIntValue() : 1280;
    const auto height = arguments.size() > 2 ? arguments[2].getIntValue() : 760;

    {
        Director director (file, outOfPhase, withLicence, bench, width, height);
        juce::MessageManager::getInstance()->runDispatchLoop();
    }

    return file.existsAsFile() ? 0 : 1;
}
