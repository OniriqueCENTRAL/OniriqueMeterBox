// Everything the plugin measures, offline, held to the signals the standards
// give for it: EBU Tech 3341 for the loudness and the true peak, Tech 3342
// for the range. The numbers a meter shows are decided in Source/dsp.
#include "dsp/Meter.h"
#include "TestSignal.h"

#include <chrono>
#include <cstdio>

namespace
{
int failures = 0;

void check (bool ok, const char* what)
{
    std::printf ("  %s  %s\n", ok ? "ok  " : "FAIL", what);

    if (! ok)
        ++failures;
}

bool within (double value, double wanted, double tolerance)
{
    return std::abs (value - wanted) <= tolerance;
}

/** The loudness of a signal, the same on both sides unless a second is given. */
mtx::LoudnessValues measure (const std::vector<float>& left, double sampleRate, int channels = 2,
                             const std::vector<float>* right = nullptr, int block = 512)
{
    mtx::Loudness loudness;
    loudness.prepare (sampleRate, channels);

    const auto& other = right != nullptr ? *right : left;

    for (size_t at = 0; at < left.size();)
    {
        const float* in[2] = { left.data() + at, other.data() + at };
        const auto count = (int) std::min ((size_t) block, left.size() - at);

        for (int done = 0; done < count;)
        {
            const float* here[2] = { in[0] + done, in[1] + done };
            done += loudness.process (here, count - done, true);
        }

        at += (size_t) count;
    }

    return loudness.values();
}

/** Stretches of a 1 kHz sine, one after the other: seconds, then dB under full scale. */
std::vector<float> stretches (double sampleRate, std::initializer_list<std::pair<double, double>> parts)
{
    std::vector<float> out;

    for (const auto& [seconds, level] : parts)
        mtxtest::append (out, mtxtest::makeTone (seconds, sampleRate, 1000.0, level));

    return out;
}

void testLoudness()
{
    std::printf ("\nLoudness (EBU Tech 3341)\n");

    for (const auto rate : { 44100.0, 48000.0, 96000.0 })
    {
        const auto v = measure (mtxtest::makeTone (20.0, rate, 1000.0, -23.0), rate);
        std::printf ("    1 kHz at -23 dBFS, %.0f Hz: M %.2f  S %.2f  I %.2f LUFS\n", rate, v.momentary, v.shortTerm, v.integrated);
        check (within (v.momentary, -23.0, 0.1) && within (v.shortTerm, -23.0, 0.1) && within (v.integrated, -23.0, 0.1),
               "a stereo sine at -23 dBFS reads -23.0 LUFS on all three");
    }

    {
        const auto v = measure (mtxtest::makeTone (20.0, 48000.0, 1000.0, -33.0), 48000.0);
        check (within (v.integrated, -33.0, 0.1) && within (v.shortTerm, -33.0, 0.1), "and at -33 dBFS, -33.0 LUFS");
    }

    {
        const auto v = measure (mtxtest::makeTone (20.0, 48000.0, 1000.0, -23.0), 48000.0, 1);
        std::printf ("    one channel alone: I %.2f LUFS\n", v.integrated);
        check (within (v.integrated, -26.01, 0.1), "one channel alone is 3 dB under the same on two");
    }

    {
        const auto v = measure (stretches (48000.0, { { 10.0, -36.0 }, { 60.0, -23.0 }, { 10.0, -36.0 } }), 48000.0);
        std::printf ("    -36 / -23 / -36: I %.2f LUFS\n", v.integrated);
        check (within (v.integrated, -23.0, 0.1), "the relative gate leaves the quiet ends out");
    }

    {
        const auto v = measure (stretches (48000.0, { { 10.0, -72.0 }, { 10.0, -36.0 }, { 60.0, -23.0 }, { 10.0, -36.0 }, { 10.0, -72.0 } }), 48000.0);
        std::printf ("    -72 / -36 / -23 / -36 / -72: I %.2f LUFS\n", v.integrated);
        check (within (v.integrated, -23.0, 0.1), "and the absolute gate what is under -70 LUFS");
    }

    {
        const auto v = measure (stretches (48000.0, { { 20.0, -26.0 }, { 20.1, -20.0 }, { 20.0, -26.0 } }), 48000.0);
        std::printf ("    -26 / -20 / -26: I %.2f LUFS\n", v.integrated);
        check (within (v.integrated, -23.0, 0.1), "two levels 6 dB apart come out between them, at -23.0");
    }

    {
        const auto v = measure (stretches (48000.0, { { 10.0, -30.0 }, { 3.0, -20.0 }, { 10.0, -30.0 } }), 48000.0);
        check (within (v.shortTermMax, -20.0, 0.1) && within (v.momentaryMax, -20.0, 0.1), "the maxima keep the loudest three seconds");
    }

    {
        const std::vector<float> silence ((size_t) (48000 * 5), 0.0f);
        const auto v = measure (silence, 48000.0);
        check (v.integrated <= mtx::kFloorDb && v.range == 0.0f && v.truePeak[0] <= mtx::kFloorDb, "silence measures as nothing");
    }
}

void testRange()
{
    std::printf ("\nLoudness range (EBU Tech 3342)\n");

    const std::pair<std::pair<double, double>, double> cases[] = { { { -20.0, -30.0 }, 10.0 },
                                                                    { { -20.0, -15.0 }, 5.0 },
                                                                    { { -40.0, -20.0 }, 20.0 } };

    for (const auto& [levels, wanted] : cases)
    {
        const auto v = measure (stretches (48000.0, { { 20.0, levels.first }, { 20.0, levels.second } }), 48000.0);
        std::printf ("    %.0f then %.0f dBFS: LRA %.1f LU\n", levels.first, levels.second, v.range);
        check (within (v.range, wanted, 1.0), "the range is the distance between the two levels");
    }
}

void testTruePeak()
{
    std::printf ("\nTrue peak (EBU Tech 3341)\n");

    // Sines whose samples all miss the crest: the peak is between them.
    const std::pair<double, double> cases[] = { { 4.0, 0.0 }, { 4.0, 45.0 }, { 6.0, 60.0 }, { 8.0, 67.5 } };

    for (const auto rate : { 44100.0, 48000.0 })
        for (const auto& [division, phase] : cases)
        {
            auto tone = mtxtest::makeTone (2.0, rate, rate / division, -6.0, phase);
            float sample = 0.0f;

            // Brought in over 20 ms: a sine that starts part of the way up
            // its crest is a step, and a step does overshoot once rebuilt.
            for (size_t i = 0; i < (size_t) (rate * 0.02); ++i)
                tone[i] *= (float) (0.5 - 0.5 * std::cos (mtxtest::kPi * (double) i / (rate * 0.02)));

            for (const auto x : tone)
                sample = std::max (sample, std::abs (x));

            const auto v = measure (tone, rate);
            std::printf ("    fs/%.0f at %.1f deg, %.0f Hz: samples %.2f dBFS, true peak %.2f dBTP\n",
                         division, phase, rate, 20.0 * std::log10 (sample), v.truePeak[0]);
            check (v.truePeak[0] <= -6.0 + 0.2 && v.truePeak[0] >= -6.0 - 0.4, "within +0.2 / -0.4 dB of -6.0 dBTP");
        }

    {
        const auto v = measure (mtxtest::makeTone (2.0, 96000.0, 11025.0, -6.0, 45.0), 96000.0);
        check (within (v.truePeak[0], -6.0, 0.3), "and at 96 kHz, rebuilt at twice the rate");
    }

    {
        const auto v = measure (mtxtest::makeTone (20.0, 48000.0, 1000.0, -23.0), 48000.0);
        std::printf ("    1 kHz at -23 dBFS: PLR %.2f  PSR %.2f\n", v.peakToLoudness, v.peakToShort);
        check (within (v.peakToLoudness, 0.0, 0.2) && within (v.peakToShort, 0.0, 0.2), "a 1 kHz sine has its peak at its loudness");
    }
}

void testStereo()
{
    std::printf ("\nStereo\n");

    const auto a = mtxtest::makeNoise (2.0, 48000.0, 0.5f, 3);
    const auto b = mtxtest::makeNoise (2.0, 48000.0, 0.5f, 7);
    std::vector<float> inverted (a.size()), nothing (a.size(), 0.0f);

    for (size_t i = 0; i < a.size(); ++i)
        inverted[i] = -a[i];

    auto run = [] (const std::vector<float>& left, const std::vector<float>& right)
    {
        mtx::StereoField field;
        field.prepare (48000.0);

        for (size_t at = 0; at + 512 <= left.size(); at += 512)
            field.process (left.data() + at, right.data() + at, 512);

        return field.values();
    };

    {
        const auto v = run (a, a);
        check (within (v.correlation, 1.0, 0.001) && within (v.width, 0.0, 0.001) && within (v.monoLoss, 0.0, 0.01)
                   && within (v.balance, 0.0, 0.01),
               "the same on both sides: correlation +1, no width, nothing lost in mono");
    }

    {
        const auto v = run (a, inverted);
        check (within (v.correlation, -1.0, 0.001) && within (v.width, 1.0, 0.001) && v.monoLoss < -50.0f,
               "one side inverted: correlation -1, all width, everything lost in mono");
    }

    {
        const auto v = run (a, b);
        std::printf ("    two unrelated noises: correlation %.3f, width %.2f, mono %.2f dB\n", v.correlation, v.width, v.monoLoss);
        check (within (v.correlation, 0.0, 0.1) && within (v.width, 0.5, 0.06) && within (v.monoLoss, -3.0, 0.5),
               "two unrelated sides: correlation 0, half width, 3 dB lost in mono");
    }

    {
        const auto v = run (a, nothing);
        check (v.balance < -20.0f && within (v.correlation, 0.0, 0.001), "the left side alone: the balance is hard left");
    }

    {
        const auto v = run (nothing, nothing);
        check (v.level <= mtx::kFloorDb && v.correlation == 0.0f, "silence: nothing to compare");
    }
}

/** Runs two sides through the analyser and returns its picture. */
mtx::SpectrumFrame analyse (const std::vector<float>& left, const std::vector<float>& right, double sampleRate, int averaging)
{
    mtx::Spectrum spectrum;
    spectrum.prepare (sampleRate);
    spectrum.setAveraging (averaging);

    for (size_t at = 0; at + 512 <= left.size(); at += 512)
    {
        spectrum.push (left.data() + at, right.data() + at, 512);
        spectrum.analyse();
    }

    mtx::SpectrumFrame frame;
    spectrum.read (frame);
    return frame;
}

int highestPoint (const float* curve)
{
    return (int) (std::max_element (curve, curve + mtx::kSpectrumPoints) - curve);
}

void testSpectrum()
{
    std::printf ("\nSpectrum\n");

    for (const auto rate : { 44100.0, 48000.0, 96000.0 })
    {
        const auto pink = mtxtest::makePinkNoise (30.0, rate, 0.5f);
        const auto frame = analyse (pink, pink, rate, mtx::averageAll);

        // 40 Hz to 16 kHz.
        const auto [lowest, highest] = std::minmax_element (frame.average + 12, frame.average + 117);
        std::printf ("    pink noise at %.0f Hz: from %.2f to %.2f dB\n", rate, *lowest, *highest);
        check (frame.measured && *highest - *lowest < 3.0f, "pink noise is a flat line, within 3 dB from 40 Hz to 16 kHz");
    }

    {
        const auto white = mtxtest::makeNoise (30.0, 48000.0, 0.5f);
        const auto frame = analyse (white, white, 48000.0, mtx::averageAll);
        const auto slope = (frame.average[108] - frame.average[24]) / 7.0f;      // 80 Hz to 10.2 kHz
        std::printf ("    white noise: %.2f dB to the octave\n", slope);
        check (within (slope, 3.01, 0.3), "white noise rises by 3 dB to the octave");
    }

    for (const auto& [hz, point] : { std::pair<double, int> { 1000.0, 68 }, { 60.0, 19 }, { 10000.0, 108 }, { 30.0, 7 } })
    {
        const auto tone = mtxtest::makeTone (6.0, 48000.0, hz, -6.0);
        const auto frame = analyse (tone, tone, 48000.0, mtx::averageAll);
        const auto top = highestPoint (frame.average);

        std::printf ("    %.0f Hz at -6 dBFS: highest at %.0f Hz, %.2f dB; quick curve %.2f dB\n",
                     hz, mtx::spectrumHz (top), frame.average[top], frame.live[highestPoint (frame.live)]);
        // Under 40 Hz a third of an octave is narrower than what a third of
        // a second can tell apart: some of a pure tone falls outside it.
        check (std::abs (top - point) <= 1 && within (frame.average[top], -6.0, hz < 40.0 ? 1.0 : 0.5),
               "a sine stands at its frequency, at its level");
    }

    {
        // The lows inverted on one side, the highs the same on both.
        const auto count = (size_t) (48000 * 6);
        std::vector<float> left (count), right (count);

        for (size_t i = 0; i < count; ++i)
        {
            const auto t = (double) i / 48000.0;
            const auto low = 0.2 * std::sin (2.0 * mtxtest::kPi * 60.0 * t) + 0.2 * std::sin (2.0 * mtxtest::kPi * 85.0 * t);
            const auto high = 0.2 * std::sin (2.0 * mtxtest::kPi * 2000.0 * t) + 0.2 * std::sin (2.0 * mtxtest::kPi * 3100.0 * t);

            left[i] = (float) (low + high);
            right[i] = (float) (high - low);
        }

        const auto frame = analyse (left, right, 48000.0, mtx::averageLong);
        std::printf ("    lows inverted, highs alike: correlation %.2f at 60 Hz, %.2f at 2 kHz\n", frame.correlation[19], frame.correlation[80]);
        check (frame.correlation[19] < -0.9f && frame.correlation[80] > 0.9f, "the correlation is told frequency by frequency");
    }

    {
        const auto a = mtxtest::makePinkNoise (20.0, 48000.0, 0.5f, 5);
        const auto b = mtxtest::makePinkNoise (20.0, 48000.0, 0.5f, 9);
        const auto frame = analyse (a, b, 48000.0, mtx::averageLong);
        float worst = 0.0f;

        for (int p = 24; p < 117; ++p)
            worst = std::max (worst, std::abs (frame.correlation[p]));

        std::printf ("    two unrelated noises: the correlation stays within %.2f of zero\n", worst);
        check (worst < 0.35f, "two unrelated sides are near zero everywhere");
    }

    {
        // The averages: the short one forgets, the one since the reset does not.
        auto loud = mtxtest::makePinkNoise (10.0, 48000.0, 0.5f);
        const auto soft = mtxtest::makePinkNoise (20.0, 48000.0, 0.05f, 2);
        mtxtest::append (loud, soft);

        const auto quick = analyse (loud, loud, 48000.0, mtx::averageShort);
        const auto all = analyse (loud, loud, 48000.0, mtx::averageAll);
        const auto only = analyse (soft, soft, 48000.0, mtx::averageAll);

        std::printf ("    at 1 kHz, 10 s loud then 20 s soft: %.1f dB over 3 s, %.1f dB since the reset (soft alone: %.1f dB)\n",
                     quick.average[68], all.average[68], only.average[68]);
        check (within (quick.average[68], only.average[68], 1.0), "the 3 s average has forgotten the loud part");
        check (all.average[68] > only.average[68] + 12.0f, "the average since the reset has not");
    }

    {
        // A silence leaves the average as it was.
        auto signal = mtxtest::makePinkNoise (10.0, 48000.0, 0.5f);
        const auto before = analyse (signal, signal, 48000.0, mtx::averageShort);
        signal.resize (signal.size() + (size_t) (48000 * 20), 0.0f);
        const auto after = analyse (signal, signal, 48000.0, mtx::averageShort);

        check (within (after.average[68], before.average[68], 0.5) && after.live[68] <= -100.0f,
               "a silence empties the quick curve and leaves the average where it was");
    }
}

void testTonal()
{
    std::printf ("\nTonal balance\n");

    for (int i = 0; i < mtx::kNumStockTargets; ++i)
    {
        const auto target = mtx::stockTarget (i);
        float spectrum[mtx::kSpectrumPoints], deviation[mtx::kNumRegions], tolerance[mtx::kNumRegions];

        for (int p = 0; p < mtx::kSpectrumPoints; ++p)
            spectrum[p] = target.mean[p] - 31.0f;

        const auto offset = mtx::fitTarget (target, spectrum);
        mtx::compareRegions (target, spectrum, offset, deviation, tolerance);

        bool level = within (offset, -31.0, 0.01);

        for (int r = 0; r < mtx::kNumRegions; ++r)
            level = level && within (deviation[r], 0.0, 0.01) && tolerance[r] > 1.0f;

        // 6 dB more under 250 Hz.
        for (int p = 0; p < mtx::kSpectrumPoints; ++p)
            if (mtx::spectrumHz (p) < 250.0f)
                spectrum[p] += 6.0f;

        const auto raised = mtx::fitTarget (target, spectrum);
        mtx::compareRegions (target, spectrum, raised, deviation, tolerance);

        std::printf ("    %-9s 6 dB more lows: LOW %+.1f  LOW-MID %+.1f  HIGH-MID %+.1f  HIGH %+.1f dB\n",
                     mtx::stockTargetName (i), deviation[0], deviation[1], deviation[2], deviation[3]);
        check (level, "a spectrum that has the shape of the target lies on it, whatever its level");
        check (deviation[0] > tolerance[0] && deviation[0] > 2.5f && deviation[2] < 0.0f, "more lows put the lows over and the rest under");
    }

    {
        float spectrum[mtx::kSpectrumPoints], deviation[mtx::kNumRegions], tolerance[mtx::kNumRegions];

        for (int p = 0; p < mtx::kSpectrumPoints; ++p)
            spectrum[p] = -40.0f + 5.0f * std::sin ((float) p * 0.2f);

        const auto target = mtx::targetFrom (spectrum);
        mtx::compareRegions (target, spectrum, mtx::fitTarget (target, spectrum), deviation, tolerance);

        check (within (deviation[0], 0.0, 0.01) && within (deviation[3], 0.0, 0.01), "a target captured from a spectrum fits it");
    }
}

void testMeter()
{
    std::printf ("\nThe meter as a whole\n");

    mtx::Meter meter;
    meter.prepare (48000.0, 2);

    const auto tone = mtxtest::makeTone (10.0, 48000.0, 1000.0, -18.0);

    auto play = [&] (double seconds, bool running)
    {
        const auto count = (size_t) (seconds * 48000.0);

        for (size_t at = 0; at + 480 <= count; at += 480)
        {
            const float* in[2] = { tone.data() + at, tone.data() + at };
            meter.process (in, 2, 480, running);
            meter.analyse();
        }
    };

    play (5.0, true);
    check (meter.historyEnd() == 50 && meter.historyStart() == 0, "ten points of history to the second");
    check (within (meter.loudness().integrated, -18.0, 0.1) && within (meter.historyAt (49).shortTerm, -18.0, 0.1),
           "and they hold what was measured");

    {
        // -18 dBFS: 0.126 at the crest, 0.089 RMS.
        const auto point = meter.historyAt (49);
        check (within (point.peakLeft, 0.1259, 0.001) && within (point.rmsRight, 0.0890, 0.001), "with the waveform of each tenth of a second");
    }

    play (3.0, false);
    check (meter.historyEnd() == 50 && within (meter.loudness().momentary, -18.0, 0.1),
           "paused, the history stops and the momentary loudness goes on");

    uint32_t counted = 0;

    for (int bin = 0; bin < mtx::kDistributionBins; ++bin)
        counted += meter.distribution (bin);

    // -18 LUFS is the edge between two bins.
    check (counted == 21 && meter.distribution (41) + meter.distribution (42) == 21,
           "the distribution holds every short-term reading, in its LU");

    float left[512], right[512];
    check (meter.readScope (left, right, 512) == 512 && left[0] == right[0], "the goniometer has the latest samples");

    mtx::SpectrumFrame frame;
    meter.readSpectrum (frame);
    check (frame.measured && within (frame.average[68], -18.0, 0.5), "the spectrum has been analysed");

    meter.requestReset();
    play (1.0, true);
    meter.readSpectrum (frame);
    check (meter.historyEnd() == 10 && meter.loudness().shortTermMax <= mtx::kFloorDb, "a reset starts everything again");

    {
        // One channel.
        mtx::Meter mono;
        mono.prepare (48000.0, 1);

        for (size_t at = 0; at + 480 <= tone.size(); at += 480)
        {
            const float* in[1] = { tone.data() + at };
            mono.process (in, 1, 480, true);
        }

        check (within (mono.loudness().integrated, -21.01, 0.1) && within (mono.stereo().correlation, 1.0, 0.001),
               "one channel is measured as one channel");
    }

    {
        // More than an hour: the history keeps the latest hour.
        mtx::Meter long_;
        long_.prepare (4800.0, 2);      // a low rate, to get there quickly

        const std::vector<float> quiet (480, 0.01f);
        const float* in[2] = { quiet.data(), quiet.data() };

        for (uint32_t step = 0; step < mtx::kHistoryCapacity + 500; ++step)
            long_.process (in, 2, 480, true);

        check (long_.historyEnd() == mtx::kHistoryCapacity + 500 && long_.historyEnd() - long_.historyStart() < mtx::kHistoryCapacity
                   && long_.historyStart() > 500,
               "past an hour, the history holds the latest hour");
    }

    {
        // How much of one core it takes.
        mtx::Meter timed;
        timed.prepare (48000.0, 2);

        std::vector<float> l, r;
        mtxtest::makeMusic (60.0, 48000.0, l, r);

        const auto start = std::chrono::steady_clock::now();

        for (size_t at = 0; at + 256 <= l.size(); at += 256)
        {
            const float* in[2] = { l.data() + at, r.data() + at };
            timed.process (in, 2, 256, true);
        }

        const auto audio = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
        const auto then = std::chrono::steady_clock::now();

        // The transforms, on what the last 2.7 s left to be analysed.
        timed.analyse();

        const auto transforms = std::chrono::duration<double> (std::chrono::steady_clock::now() - then).count();
        const auto v = timed.loudness();

        std::printf ("    a minute of music: %.2f %% of one core on the audio thread, %.2f %% for the transforms\n",
                     100.0 * audio / 60.0, 100.0 * transforms / (131072.0 / 48000.0));
        std::printf ("    it reads I %.1f LUFS, LRA %.1f LU, true peak %.1f dBTP, PLR %.1f\n",
                     v.integrated, v.range, std::max (v.truePeak[0], v.truePeak[1]), v.peakToLoudness);
        check (audio < 60.0 * 0.05, "the audio thread's share is under 5 % of one core");
    }
}
}

int main()
{
    testLoudness();
    testRange();
    testTruePeak();
    testStereo();
    testSpectrum();
    testTonal();
    testMeter();

    std::printf (failures == 0 ? "\nall passed\n" : "\n%d FAILED\n", failures);
    return failures;
}
