/*
    The noise source: white, pink, and played at a rate (Phase 13d-1, PRD §10).

    What is asserted, and why each is the thing that could go wrong:

      - untracked white noise is the generator's stream, sample for sample, so
        no patch from before this changed;
      - pink is pink — equal power in every octave, measured over eight of them
        — and as loud as white, so the type moves the colour and not the level;
      - its two channels are independent, as white's are;
      - a rate below 1 darkens the noise without making it quieter, and a rate
        at or above 1, or one that is not a number, changes nothing;
      - and a reseed reproduces the stream exactly, interpolation included.

    The spectra are averaged over many frames (Welch's method), because a
    single transform of noise is noise: its bins scatter by several decibels
    whatever the generator is doing.
*/

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "DSP/Noise/NoiseSource.h"

using namespace apollo::dsp;

namespace
{

constexpr int order = 12;
constexpr int frameSize = 1 << order;
constexpr int frames = 256;
constexpr int length = frameSize * frames;

struct Stereo
{
    std::vector<float> left;
    std::vector<float> right;
};

[[nodiscard]] Stereo render (NoiseSource& source, int numSamples)
{
    Stereo out { std::vector<float> (static_cast<std::size_t> (numSamples), 0.0f),
                 std::vector<float> (static_cast<std::size_t> (numSamples), 0.0f) };

    for (std::size_t i = 0; i < out.left.size(); ++i)
        source.addNextStereoSample (out.left[i], out.right[i], 1.0f, 1.0f);

    return out;
}

[[nodiscard]] Stereo render (NoiseType type, float rate, std::uint32_t seed = 7u, int numSamples = length)
{
    NoiseSource source;
    source.setSeed (seed);
    source.setType (type);
    source.setRate (rate);
    return render (source, numSamples);
}

[[nodiscard]] double rmsOf (const std::vector<float>& signal)
{
    double sum = 0.0;

    for (const auto sample : signal)
        sum += static_cast<double> (sample) * static_cast<double> (sample);

    return std::sqrt (sum / static_cast<double> (signal.size()));
}

[[nodiscard]] double decibels (double ratio)
{
    return 10.0 * std::log10 (std::max (ratio, 1.0e-30));
}

/** Power per bin, averaged over every frame, Hann-windowed. */
[[nodiscard]] std::vector<double> averagedPower (const std::vector<float>& signal)
{
    juce::dsp::FFT fft (order);
    std::vector<double> power (static_cast<std::size_t> (frameSize / 2 + 1), 0.0);
    std::vector<float> buffer (static_cast<std::size_t> (2 * frameSize));

    const auto count = static_cast<int> (signal.size()) / frameSize;

    for (int f = 0; f < count; ++f)
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);

        for (int i = 0; i < frameSize; ++i)
        {
            const auto window = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / frameSize);
            buffer[static_cast<std::size_t> (i)]
                = signal[static_cast<std::size_t> (f * frameSize + i)] * static_cast<float> (window);
        }

        fft.performFrequencyOnlyForwardTransform (buffer.data());

        for (std::size_t b = 0; b < power.size(); ++b)
            power[b] += static_cast<double> (buffer[b]) * static_cast<double> (buffer[b]);
    }

    return power;
}

/** Power in each octave, highest first: [N/2, N], [N/4, N/2], ... as
    fractions of Nyquist, for @p octaves octaves. */
[[nodiscard]] std::vector<double> octaveBands (const std::vector<float>& signal, int octaves)
{
    const auto power = averagedPower (signal);
    const auto nyquistBin = power.size() - 1;
    std::vector<double> bands;

    for (int k = 0; k < octaves; ++k)
    {
        const auto top = nyquistBin >> k;
        const auto bottom = nyquistBin >> (k + 1);
        double sum = 0.0;

        for (auto b = bottom; b < top; ++b)
            sum += power[b];

        bands.push_back (sum);
    }

    return bands;
}

/** The fraction of the power above @p fraction of Nyquist. */
[[nodiscard]] double powerAbove (const std::vector<float>& signal, double fraction)
{
    const auto power = averagedPower (signal);
    const auto split = static_cast<std::size_t> (fraction * static_cast<double> (power.size() - 1));
    double above = 0.0;
    double total = 0.0;

    for (std::size_t b = 1; b < power.size(); ++b)
    {
        total += power[b];

        if (b >= split)
            above += power[b];
    }

    return above / total;
}

[[nodiscard]] double correlation (const std::vector<float>& a, const std::vector<float>& b)
{
    double ab = 0.0, aa = 0.0, bb = 0.0;

    for (std::size_t i = 0; i < a.size(); ++i)
    {
        ab += static_cast<double> (a[i]) * static_cast<double> (b[i]);
        aa += static_cast<double> (a[i]) * static_cast<double> (a[i]);
        bb += static_cast<double> (b[i]) * static_cast<double> (b[i]);
    }

    return ab / std::sqrt (aa * bb);
}

[[nodiscard]] bool identical (const Stereo& a, const Stereo& b)
{
    return a.left == b.left && a.right == b.right;
}

class NoiseSourceTests final : public juce::UnitTest
{
public:
    NoiseSourceTests()
        : juce::UnitTest ("Noise source", "DSP")
    {
    }

    void runTest() override
    {
        testWhiteIsTheGeneratorsStream();
        testPinkIsPink();
        testWhiteIsWhite();
        testPinkIsAsLoudAsWhite();
        testPinkChannelsAreIndependent();
        testSlowerIsDarkerNotQuieter();
        testTheLevelDoesNotPump();
        testFasterChangesNothing();
        testReseedReproduces();
    }

private:
    void testWhiteIsTheGeneratorsStream()
    {
        beginTest ("Untracked white noise is the generator's stream, sample for sample");

        NoiseGenerator generator;
        generator.setSeed (7u);

        Stereo expected { std::vector<float> (4096, 0.0f), std::vector<float> (4096, 0.0f) };

        for (std::size_t i = 0; i < expected.left.size(); ++i)
            generator.addNextStereoSample (expected.left[i], expected.right[i], 1.0f, 1.0f);

        expect (identical (render (NoiseType::white, 1.0f, 7u, 4096), expected),
                "white noise at rate 1 is not the generator's stream");
    }

    void testPinkIsPink()
    {
        beginTest ("Pink noise has equal power in every octave");

        const auto bands = octaveBands (render (NoiseType::pink, 1.0f).left, 9);

        // Every octave against the mean of them all. The top octave is the one
        // a filter designed at 44.1 kHz is least sure of, and is included.
        double mean = 0.0;

        for (const auto band : bands)
            mean += band;

        mean /= static_cast<double> (bands.size());

        auto worst = 0.0;
        juce::String levels;

        for (const auto band : bands)
        {
            const auto level = decibels (band / mean);
            worst = std::max (worst, std::abs (level));
            levels << juce::String (level, 2) << " ";
        }

        logMessage ("  pink, octaves from Nyquist down, dB against their mean: " + levels);
        expect (worst < 1.0, "an octave of pink is " + juce::String (worst, 2) + " dB from the rest");
    }

    void testWhiteIsWhite()
    {
        beginTest ("White noise rises 3 dB an octave, as the reference pink is measured against");

        const auto bands = octaveBands (render (NoiseType::white, 1.0f).left, 9);
        auto worst = 0.0;

        for (std::size_t k = 0; k + 1 < bands.size(); ++k)
            worst = std::max (worst, std::abs (decibels (bands[k] / bands[k + 1]) - 3.0103));

        expect (worst < 0.5, "white noise is not white: " + juce::String (worst, 2) + " dB off");
    }

    void testPinkIsAsLoudAsWhite()
    {
        beginTest ("Pink noise is as loud as white");

        const auto white = rmsOf (render (NoiseType::white, 1.0f).left);
        const auto pink = rmsOf (render (NoiseType::pink, 1.0f).left);
        const auto difference = 20.0 * std::log10 (pink / white);

        logMessage ("  pink against white, RMS: " + juce::String (difference, 3) + " dB");
        expect (std::abs (difference) < 0.25, "pink is " + juce::String (difference, 2) + " dB from white");
    }

    void testPinkChannelsAreIndependent()
    {
        beginTest ("Pink noise's channels are independent");

        const auto pink = render (NoiseType::pink, 1.0f);
        const auto r = correlation (pink.left, pink.right);

        logMessage ("  pink, left against right: correlation " + juce::String (r, 4));
        expect (std::abs (r) < 0.05, "the channels correlate at " + juce::String (r, 3));
    }

    void testSlowerIsDarkerNotQuieter()
    {
        beginTest ("A rate below 1 darkens the noise and keeps its level");

        for (const auto type : { NoiseType::white, NoiseType::pink })
        {
            const auto typeName = juce::String (type == NoiseType::white ? "white" : "pink");
            const auto full = render (type, 1.0f).left;

            for (const auto rate : { 0.5f, 0.25f })
            {
                const auto slow = render (type, rate).left;

                // Interpolation is a low-pass with its corner at the rate's
                // share of Nyquist: little should be left above it.
                const auto fullAbove = powerAbove (full, static_cast<double> (rate));
                const auto slowAbove = powerAbove (slow, static_cast<double> (rate));
                const auto level = 20.0 * std::log10 (rmsOf (slow) / rmsOf (full));

                logMessage ("  " + typeName + " at rate " + juce::String (rate)
                            + ": power above its corner " + juce::String (100.0 * slowAbove, 2)
                            + " % against " + juce::String (100.0 * fullAbove, 2)
                            + " %, level " + juce::String (level, 2) + " dB");

                expect (slowAbove < fullAbove * 0.25, typeName + " at rate " + juce::String (rate) + " is not darker");
                expect (std::abs (level) < 0.5,
                        typeName + " at rate " + juce::String (rate) + " changed level by " + juce::String (level, 2) + " dB");
            }
        }
    }

    void testTheLevelDoesNotPump()
    {
        beginTest ("A rate just below 1 holds its level from moment to moment");

        // At 0.9999 the interpolation point drifts through a whole sample every
        // ten thousand samples, lingering near each place on the way. A level
        // corrected only on average would sag by about 1.2 dB while the point
        // sat half way and rise by 1.8 dB while it sat on a sample.
        //
        // Measured against the same type at rate 1, because pink noise's own
        // level wanders by a few decibels over a window this short — most of
        // its power is in its lowest octaves — and that is pink, not pumping.
        const auto worstWindow = [] (const std::vector<float>& noise)
        {
            const auto overall = rmsOf (noise);
            constexpr std::size_t window = 4096;
            auto worst = 0.0;

            for (std::size_t start = 0; start + window <= noise.size(); start += window)
            {
                const std::vector<float> part (noise.begin() + static_cast<std::ptrdiff_t> (start),
                                               noise.begin() + static_cast<std::ptrdiff_t> (start + window));
                worst = std::max (worst, std::abs (20.0 * std::log10 (rmsOf (part) / overall)));
            }

            return worst;
        };

        for (const auto type : { NoiseType::white, NoiseType::pink })
        {
            const auto steady = worstWindow (render (type, 1.0f).left);
            const auto slowed = worstWindow (render (type, 0.9999f).left);

            logMessage ("  " + juce::String (type == NoiseType::white ? "white" : "pink")
                        + ": worst window from the whole, " + juce::String (slowed, 2)
                        + " dB at rate 0.9999 against " + juce::String (steady, 2) + " dB at rate 1");
            expect (slowed < steady + 0.5, "the level moved by " + juce::String (slowed, 2) + " dB");
        }
    }

    void testFasterChangesNothing()
    {
        beginTest ("A rate of 1 or more, or not a number, draws once a sample");

        for (const auto type : { NoiseType::white, NoiseType::pink })
        {
            const auto reference = render (type, 1.0f, 3u, 8192);

            for (const auto rate : { 2.0f, 37.0f, 0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(),
                                     std::numeric_limits<float>::infinity() })
                expect (identical (render (type, rate, 3u, 8192), reference),
                        "rate " + juce::String (rate) + " changed the stream");
        }
    }

    void testReseedReproduces()
    {
        beginTest ("A reseed reproduces the stream, interpolation included");

        for (const auto type : { NoiseType::white, NoiseType::pink })
        {
            NoiseSource source;
            source.setType (type);
            source.setRate (0.3f);
            source.setSeed (11u);
            const auto first = render (source, 8192);

            source.setSeed (11u);
            const auto second = render (source, 8192);

            expect (identical (first, second), "a reseeded source did not repeat itself");
        }
    }
};

NoiseSourceTests noiseSourceTests;

} // namespace
