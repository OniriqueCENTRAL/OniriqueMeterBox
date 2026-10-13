// Signals for the offline tests and the snapshot tool: no dependency.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mtxtest
{

constexpr double kPi = 3.14159265358979323846;

/** Deterministic white noise in -1..1. */
struct Noise
{
    explicit Noise (uint32_t seed = 0x1234567u) : state (seed) {}

    float next() noexcept
    {
        state = state * 1664525u + 1013904223u;
        return (float) (int32_t) state / 2147483648.0f;
    }

    uint32_t state;
};

inline std::vector<float> makeNoise (double seconds, double sampleRate, float amplitude, uint32_t seed = 1)
{
    Noise noise (seed);
    std::vector<float> out ((size_t) (seconds * sampleRate));

    for (auto& v : out)
        v = amplitude * noise.next();

    return out;
}

/** Noise with the same power in every octave (Paul Kellet's filter). */
inline std::vector<float> makePinkNoise (double seconds, double sampleRate, float amplitude, uint32_t seed = 1)
{
    Noise noise (seed);
    std::vector<float> out ((size_t) (seconds * sampleRate));
    double b0 = 0.0, b1 = 0.0, b2 = 0.0, b3 = 0.0, b4 = 0.0, b5 = 0.0, b6 = 0.0;

    for (auto& v : out)
    {
        const double white = noise.next();

        b0 = 0.99886 * b0 + white * 0.0555179;
        b1 = 0.99332 * b1 + white * 0.0750759;
        b2 = 0.96900 * b2 + white * 0.1538520;
        b3 = 0.86650 * b3 + white * 0.3104856;
        b4 = 0.55000 * b4 + white * 0.5329522;
        b5 = -0.7616 * b5 - white * 0.0168980;

        v = amplitude * (float) (0.2 * (b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362));
        b6 = white * 0.115926;
    }

    return out;
}

/** A sine, its level given in dB under full scale, starting at a phase in degrees. */
inline std::vector<float> makeTone (double seconds, double sampleRate, double hz, double levelDb, double phaseDegrees = 0.0)
{
    std::vector<float> out ((size_t) std::llround (seconds * sampleRate));
    const auto amplitude = std::pow (10.0, levelDb / 20.0);

    for (size_t i = 0; i < out.size(); ++i)
        out[i] = (float) (amplitude * std::sin (2.0 * kPi * hz * (double) i / sampleRate + phaseDegrees * kPi / 180.0));

    return out;
}

inline void append (std::vector<float>& to, const std::vector<float>& more)
{
    to.insert (to.end(), more.begin(), more.end());
}

/** Something with the shape of a record, for the pictures: a kick and a bass
    in the middle, a bed of noise that is not the same on both sides and
    falls away towards the top as music does, a hat on the off-beats, and a
    quieter stretch every eight seconds. Its highest sample is at -1.5 dBFS. */
inline void makeMusic (double seconds, double sampleRate, std::vector<float>& left, std::vector<float>& right)
{
    const auto count = (size_t) (seconds * sampleRate);
    const auto bedL = makePinkNoise (seconds, sampleRate, 1.0f, 11);
    const auto bedR = makePinkNoise (seconds, sampleRate, 1.0f, 23);
    const auto bedM = makePinkNoise (seconds, sampleRate, 1.0f, 37);
    Noise hiss (51);

    left.assign (count, 0.0f);
    right.assign (count, 0.0f);

    double hatL = 0.0, hatR = 0.0, previousL = 0.0, previousR = 0.0;

    // The bed: pink noise with more under 250 Hz and less over 3.5 kHz.
    struct Shaped
    {
        double low = 0.0, out = 0.0;

        double next (double x, double lowK, double highK) noexcept
        {
            low += lowK * (x - low);
            out += highK * (x + 1.2 * low - out);
            return out;
        }
    };

    Shaped shapeL, shapeR, shapeM;
    const auto lowK = 1.0 - std::exp (-2.0 * kPi * 250.0 / sampleRate);
    const auto highK = 1.0 - std::exp (-2.0 * kPi * 3500.0 / sampleRate);

    for (size_t i = 0; i < count; ++i)
    {
        const auto t = (double) i / sampleRate;
        const auto beat = std::fmod (t, 0.5);
        const auto quiet = std::fmod (t, 8.0) > 6.0 ? 0.4 : 1.0;

        // The kick: a sine that falls from 110 to 45 Hz as it dies away.
        const auto kick = 0.55 * std::exp (-beat * 9.0) * std::sin (2.0 * kPi * (45.0 * beat + 65.0 / 30.0 * (1.0 - std::exp (-beat * 30.0))));
        const auto bass = 0.22 * std::sin (2.0 * kPi * 55.0 * t) * (0.6 + 0.4 * std::sin (2.0 * kPi * 0.25 * t));

        // The hat: noise with the lows taken out, wide.
        const auto off = std::fmod (t + 0.25, 0.5);
        const auto strike = 0.16 * std::exp (-off * 40.0);
        const double whiteL = hiss.next(), whiteR = hiss.next();
        hatL = strike * (whiteL - previousL);
        hatR = strike * (whiteR - previousR);
        previousL = whiteL;
        previousR = whiteR;

        const auto middle = kick + bass + 0.30 * quiet * shapeM.next (bedM[i], lowK, highK);

        left[i] = (float) (quiet * (middle + 0.22 * shapeL.next (bedL[i], lowK, highK) + hatL));
        right[i] = (float) (quiet * (middle + 0.22 * shapeR.next (bedR[i], lowK, highK) + hatR));
    }

    float peak = 1.0e-9f;

    for (size_t i = 0; i < count; ++i)
        peak = std::max (peak, std::max (std::abs (left[i]), std::abs (right[i])));

    for (size_t i = 0; i < count; ++i)
    {
        left[i] *= 0.84f / peak;
        right[i] *= 0.84f / peak;
    }
}

} // namespace mtxtest
