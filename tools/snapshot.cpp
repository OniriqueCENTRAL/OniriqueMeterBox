// Renders the editor to a PNG without a host, with something like a record
// running through it, so a change to the interface can be looked at straight
// from the build.
//
//   meterbox_snapshot out.png [width height]
//   meterbox_snapshot out.png --phase       the same with the lows out of phase, to see what a fault looks like
//   meterbox_snapshot out.png --licence     with the licence badge and its page left on
//   meterbox_snapshot out.png --bench       and how long the window takes to draw
//   meterbox_snapshot out.png --reference   with a reference loaded (louder, brighter, wider), its figures beside the mix's
//   meterbox_snapshot out.png --listen      the same, the reference being listened to (B)
//   meterbox_snapshot out.png --rest        at rest, as the shop shows it: nothing played, the licence badge on
//   meterbox_snapshot out.png --find TEXT   searches as the strip does, and shows what it found (the first is then loaded)
//   meterbox_snapshot out.png --link URL    the reference fetched from a platform by the Downloader of the site
//   meterbox_snapshot --selftest            the plugin as a host drives it, without a window
#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"
#include "TestSignal.h"

#if JUCE_MAC
 #include <objc/message.h>
 #include <objc/runtime.h>
#endif

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

/** Writes a reference to compare the test music with: the same music, louder,
    brighter and wider, as a mastered record of it would be. */
juce::File writeReference (double seconds)
{
    std::vector<float> left, right;
    mtxtest::makeMusic (seconds, kSr, left, right);

    double lowL = 0.0, lowR = 0.0;
    const auto k = 1.0 - std::exp (-2.0 * mtxtest::kPi * 3000.0 / kSr);
    juce::AudioBuffer<float> buffer (2, (int) left.size());

    for (size_t i = 0; i < left.size(); ++i)
    {
        lowL += k * (left[i] - lowL);
        lowR += k * (right[i] - lowR);

        // Highs up, sides up, and everything pushed up into a soft limit.
        auto l = (double) left[i] + 0.8 * (left[i] - lowL);
        auto r = (double) right[i] + 0.8 * (right[i] - lowR);
        const auto mid = 0.5 * (l + r), side = 0.5 * (l - r) * 1.6;

        l = std::tanh (3.0 * (mid + side)) * 0.95;
        r = std::tanh (3.0 * (mid - side)) * 0.95;

        buffer.setSample (0, (int) i, (float) l);
        buffer.setSample (1, (int) i, (float) r);
    }

    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("meterbox-reference.wav");
    file.deleteFile();

    juce::WavAudioFormat wav;

    if (auto writer = wav.createWriterFor (file.createOutputStream().release(), kSr, 2, 24, {}, 0))
    {
        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        delete writer;
    }

    return file;
}

/** A host that says where it is: the project's time, as a DAW gives it. */
struct FakeHead final : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (true);
        info.setTimeInSamples ((juce::int64) samples);
        info.setTimeInSeconds ((double) samples / kSr);
        return info;
    }

    size_t samples = 0;
};

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
    Director (juce::File destination, bool outOfPhase, bool withLicence, bool timed, int width, int height,
              bool withReference, bool listen, juce::String link, bool atRest, juce::String find = {})
        : file (std::move (destination)), bench (timed), listening (listen), rest (atRest)
    {
        if (find.isNotEmpty())
        {
            juce::String problem;
            found = onirique::ReferencePlayer::search (find, problem);
            std::printf ("  search \"%s\": %d found %s\n", find.toRawUTF8(), found.size(), problem.toRawUTF8());
        }

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

        if (! found.isEmpty())
            processor->loadReferencePreview (found.getFirst());
        else if (link.isNotEmpty())
            processor->loadReferenceLink (link);
        else if (withReference || listen)
            processor->loadReference (writeReference (60.0));

        // All but the last three seconds, as fast as it will go.
        position = left.size() - (size_t) (kSr * 3.0);
        position -= position % kBlock;

        if (! rest)
        {
            processor->setNonRealtime (true);
            play (*processor, left, right, 0, position);
            processor->setNonRealtime (false);
        }

        editor.reset (processor->createEditor());
        editor->setSize (width, height);

        if (! withLicence && ! rest)
            if (auto* meterBox = dynamic_cast<onirique::MeterBoxEditor*> (editor.get()))
                meterBox->hideLicence();

        startTimer (30);
    }

    void timerCallback() override
    {
        // About a tick's worth of audio per tick.
        if (! rest)
        {
            const auto to = std::min (left.size(), position + (size_t) (3 * kBlock));
            play (*processor, left, right, position, to);
            position += (to - position) / kBlock * kBlock;
        }

        // Not before the reference is in place, and seen to move.
        const auto status = processor->getReference().getStatus();

        if (status == onirique::ReferencePlayer::Status::loading || status == onirique::ReferencePlayer::Status::downloading)
            return;

        if (listening && ! processor->getReference().isListening())
        {
            processor->getReference().seek (41.0);
            processor->getReference().setListening (true);
        }

        // What the search found, laid over the graphs as the strip lays it, with its covers.
        if (ticks == 60 && ! found.isEmpty())
            for (auto* child : editor->getChildren())
                if (auto* list = dynamic_cast<onirique::SearchResults*> (child))
                {
                    list->show (found, {});

                    for (int i = 0; i < found.size(); ++i)
                        if (auto in = juce::URL (found[i].coverUrl).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)))
                        {
                            juce::MemoryBlock bytes;
                            in->readIntoMemoryBlock (bytes);
                            list->setCover (i, juce::ImageFileFormat::loadFrom (bytes.getData(), bytes.getSize()));
                        }
                }

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
    bool bench = false, listening = false, rest = false;
    juce::Array<onirique::ReferencePlayer::Found> found;
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

    // The reference: heard in place of the mix, at its loudness, and never in a render.
    {
        const auto file = writeReference (12.0);

        auto processor = makeProcessor();
        processor->setPlayConfigDetails (2, 2, kSr, kBlock);
        processor->prepareToPlay (kSr, kBlock);

        auto& player = processor->getReference();
        player.load (file);

        for (int wait = 0; wait < 400 && player.getStatus() == onirique::ReferencePlayer::Status::loading; ++wait)
            juce::Thread::sleep (25);

        const auto track = player.getTrack();
        check (track != nullptr && player.getStatus() == onirique::ReferencePlayer::Status::ready, "a reference is read and measured");

        if (track != nullptr)
        {
            const auto& stats = track->stats;
            std::printf ("    the reference: I %.1f LUFS, LRA %.1f, TP %.1f dBTP, width %.0f %%, crest %.1f dB\n",
                         stats.loudness.integrated, stats.loudness.range,
                         juce::jmax (stats.loudness.truePeak[0], stats.loudness.truePeak[1]), stats.stereo.width * 100.0f, stats.crestDb);
            check (stats.spectrumMeasured && stats.loudness.integrated > -20.0f && stats.distributionMost > 0, "and its figures are those of a loud record");

            juce::AudioBuffer<float> buffer (2, kBlock);
            juce::MidiBuffer midi;
            auto through = [&] (size_t at)
            {
                buffer.copyFrom (0, 0, left.data() + at, kBlock);
                buffer.copyFrom (1, 0, right.data() + at, kBlock);
                processor->processBlock (buffer, midi);
            };

            // A: the mix, to the bit, the reference loaded or not.
            bool same = true;
            size_t at = 0;

            for (; at + kBlock <= (size_t) (kSr * 4.0); at += kBlock)
            {
                through (at);

                for (int i = 0; i < kBlock; ++i)
                    same = same && buffer.getSample (0, i) == left[at + (size_t) i];
            }

            check (same, "on A, what goes out is still the mix, to the bit");

            // B: the reference, brought to the loudness of the mix.
            player.setListening (true);
            double mixSquares = 0.0, outSquares = 0.0;
            bool differs = false;

            for (int b = 0; at + kBlock <= (size_t) (kSr * 8.0); at += kBlock, ++b)
            {
                through (at);

                if (b < 20)
                    continue;

                for (int i = 0; i < kBlock; ++i)
                {
                    mixSquares += (double) left[at + (size_t) i] * left[at + (size_t) i];
                    outSquares += (double) buffer.getSample (0, i) * buffer.getSample (0, i);
                    differs = differs || buffer.getSample (0, i) != left[at + (size_t) i];
                }
            }

            const auto mixLufs = processor->getMeter().loudness().integrated;
            std::printf ("    on B: gain %.1f dB (mix %.1f LUFS), level against the mix %.1f dB\n",
                         player.getGainDb(), mixLufs, 10.0 * std::log10 (outSquares / mixSquares));
            check (differs && player.isHeard(), "on B, the reference is heard in its place");
            check (std::abs (player.getGainDb() - (mixLufs - stats.loudness.integrated)) < 0.05f, "brought to the loudness of the mix");

            // The meter still measures the mix.
            check (mixLufs < stats.loudness.integrated - 2.0f, "and the meter still measures the mix, not the reference");

            // A render is the mix, B or not.
            processor->setNonRealtime (true);
            same = true;

            for (int b = 0; b < 40 && at + kBlock <= left.size(); ++b, at += kBlock)
            {
                through (at);

                for (int i = 0; i < kBlock; ++i)
                    same = same && buffer.getSample (0, i) == left[at + (size_t) i];
            }

            processor->setNonRealtime (false);
            check (same, "a render is always the mix, even on B");

            // The reference goes with the project.
            juce::MemoryBlock saved;
            processor->getStateInformation (saved);

            auto second = makeProcessor();
            second->setStateInformation (saved.getData(), (int) saved.getSize());

            for (int wait = 0; wait < 400 && second->getReference().getStatus() == onirique::ReferencePlayer::Status::loading; ++wait)
                juce::Thread::sleep (25);

            const auto restored = second->getReference().getTrack();
            check (restored != nullptr && restored->file == file && ! second->getReference().isListening(),
                   "a restored project has its reference back, on A");
        }

        file.deleteFile();
    }

    // Locked to the project: two versions of the same song stay in step,
    // wherever the host starts.
    {
        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("meterbox-same-song.wav");
        file.deleteFile();

        {
            juce::AudioBuffer<float> copy (2, (int) left.size());
            copy.copyFrom (0, 0, left.data(), (int) left.size());
            copy.copyFrom (1, 0, right.data(), (int) right.size());

            juce::WavAudioFormat wav;

            if (auto* writer = wav.createWriterFor (file.createOutputStream().release(), kSr, 2, 24, {}, 0))
            {
                writer->writeFromAudioSampleBuffer (copy, 0, copy.getNumSamples());
                delete writer;
            }
        }

        auto processor = makeProcessor();
        processor->setPlayConfigDetails (2, 2, kSr, kBlock);
        processor->prepareToPlay (kSr, kBlock);

        FakeHead head;
        processor->setPlayHead (&head);

        auto& player = processor->getReference();
        player.load (file);

        for (int wait = 0; wait < 400 && player.getStatus() == onirique::ReferencePlayer::Status::loading; ++wait)
            juce::Thread::sleep (25);

        // Played from second 5 of the project, B from the start. (Without the
        // matching, whose gain moves while the mix's loudness settles.)
        player.setMatching (false);
        player.setListening (true);
        juce::AudioBuffer<float> buffer (2, kBlock);
        juce::MidiBuffer midi;
        double cross = 0.0, mine = 0.0, theirs = 0.0;

        for (size_t at = (size_t) (5.0 * kSr), b = 0; at + kBlock <= (size_t) (10.0 * kSr); at += kBlock, ++b)
        {
            head.samples = at;
            buffer.copyFrom (0, 0, left.data() + at, kBlock);
            buffer.copyFrom (1, 0, right.data() + at, kBlock);
            processor->processBlock (buffer, midi);

            if (b < 20)
                continue;

            for (int i = 0; i < kBlock; ++i)
            {
                const double x = left[at + (size_t) i], y = buffer.getSample (0, i);
                cross += x * y;
                mine += x * x;
                theirs += y * y;
            }
        }

        const auto alignment = cross / std::sqrt (mine * theirs + 1.0e-30);
        std::printf ("    locked, from second 5: the reference against the mix, correlation %.4f\n", alignment);
        check (player.isLocked() && alignment > 0.999, "locked, the reference plays at the time of the project");

        // Moved against the project by a click on its outline: a second later.
        player.seek (6.0 + 1.0);   // the project is at about 10 s: the reference jumps to 7 s, three seconds back
        head.samples = (size_t) (10.0 * kSr);
        buffer.clear();
        processor->processBlock (buffer, midi);
        check (std::abs (player.getOffset() + 3.0) < 0.05, "a click on its outline moves it against the project");

        processor->setPlayHead (nullptr);
        file.deleteFile();
    }

    std::printf (failures == 0 ? "\nall passed\n" : "\n%d FAILED\n", failures);
    return failures;
}

int main (int argc, char** argv)
{
    if (argc < 2)
        return 2;

   #if JUCE_MAC
    // Built against the plugin's code, JUCE does not make the application object
    // a program has: without it the message loop returns at once.
    using Send = void* (*) (void*, SEL);
    ((Send) objc_msgSend) ((void*) objc_getClass ("NSApplication"), sel_registerName ("sharedApplication"));
   #endif

    const juce::ScopedJuceInitialiser_GUI gui;

    if (juce::String (argv[1]) == "--selftest")
        return selfTest();

    juce::StringArray arguments (argv + 1, argc - 1);
    const auto outOfPhase = arguments.contains ("--phase");
    const auto withLicence = arguments.contains ("--licence");
    const auto bench = arguments.contains ("--bench");
    const auto withReference = arguments.contains ("--reference");
    const auto listen = arguments.contains ("--listen");
    const auto atRest = arguments.contains ("--rest");
    arguments.removeString ("--rest");
    juce::String link, find;

    if (const auto at = arguments.indexOf ("--find"); at >= 0)
    {
        find = arguments[at + 1];
        arguments.removeRange (at, 2);
    }

    if (const auto at = arguments.indexOf ("--link"); at >= 0)
    {
        link = arguments[at + 1];
        arguments.removeRange (at, 2);
    }

    arguments.removeString ("--phase");
    arguments.removeString ("--licence");
    arguments.removeString ("--bench");
    arguments.removeString ("--reference");
    arguments.removeString ("--listen");

    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (arguments[0]);
    const auto width = arguments.size() > 2 ? arguments[1].getIntValue() : 1400;
    const auto height = arguments.size() > 2 ? arguments[2].getIntValue() : 780;

    {
        Director director (file, outOfPhase, withLicence, bench, width, height, withReference, listen, link, atRest, find);
        juce::MessageManager::getInstance()->runDispatchLoop();
    }

    return file.existsAsFile() ? 0 : 1;
}
