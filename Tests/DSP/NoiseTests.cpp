/*
    Noise generator tests.

    A noise source is easy to get subtly wrong in ways that sound fine in
    isolation: a generator that locks at zero for one seed, two channels that
    are secretly the same stream, or a distribution biased away from zero and
    therefore carrying DC into everything downstream. All three are cheap to
    assert and expensive to discover later.
*/

#include <juce_core/juce_core.h>

#include <cmath>
#include <vector>

#include "DSP/Noise/NoiseGenerator.h"

using namespace apollo::dsp;

namespace
{

/** Renders a stereo noise block at unity gain. */
struct NoiseBlock
{
    std::vector<float> left;
    std::vector<float> right;

    explicit NoiseBlock (NoiseGenerator& generator, int numSamples)
        : left (static_cast<std::size_t> (numSamples), 0.0f),
          right (static_cast<std::size_t> (numSamples), 0.0f)
    {
        for (int i = 0; i < numSamples; ++i)
            generator.addNextStereoSample (left[static_cast<std::size_t> (i)],
                                           right[static_cast<std::size_t> (i)],
                                           1.0f,
                                           1.0f);
    }
};

[[nodiscard]] double meanOf (const std::vector<float>& samples)
{
    double total = 0.0;

    for (const auto sample : samples)
        total += static_cast<double> (sample);

    return total / static_cast<double> (samples.size());
}

[[nodiscard]] double rmsOf (const std::vector<float>& samples)
{
    double total = 0.0;

    for (const auto sample : samples)
        total += static_cast<double> (sample) * static_cast<double> (sample);

    return std::sqrt (total / static_cast<double> (samples.size()));
}

/** @returns the Pearson correlation between two equally long signals. */
[[nodiscard]] double correlationOf (const std::vector<float>& a, const std::vector<float>& b)
{
    const auto meanA = meanOf (a);
    const auto meanB = meanOf (b);

    double covariance = 0.0;
    double varianceA = 0.0;
    double varianceB = 0.0;

    for (std::size_t i = 0; i < a.size(); ++i)
    {
        const auto da = static_cast<double> (a[i]) - meanA;
        const auto db = static_cast<double> (b[i]) - meanB;

        covariance += da * db;
        varianceA += da * da;
        varianceB += db * db;
    }

    if (varianceA <= 0.0 || varianceB <= 0.0)
        return 1.0;

    return covariance / std::sqrt (varianceA * varianceB);
}

class NoiseTests final : public juce::UnitTest
{
public:
    NoiseTests()
        : juce::UnitTest ("Noise generator", "DSP")
    {
    }

    void runTest() override
    {
        testValuesStayInRangeAndCentred();
        testChannelsAreDecorrelated();
        testSeedingIsDeterministic();
        testDifferentSeedsProduceDifferentStreams();
        testEverySeedProducesSignal();
        testGainScalesLinearly();
    }

private:
    static constexpr int blockSize = 1 << 16;

    void testValuesStayInRangeAndCentred()
    {
        beginTest ("Noise is bounded, centred and full amplitude");

        NoiseGenerator generator;
        generator.setSeed (12345u);

        const NoiseBlock block (generator, blockSize);

        for (const auto sample : block.left)
        {
            expect (std::isfinite (sample), "noise produced a non-finite sample");
            expect (sample >= -1.0f && sample < 1.0f, "noise escaped [-1, 1)");
        }

        // Any DC offset here would be added to every note a patch plays, so it
        // is asserted rather than assumed. The tolerance is generous relative to
        // the expected sampling error of a 65536-sample block.
        expect (std::abs (meanOf (block.left)) < 0.01,
                "noise carries a DC offset of " + juce::String (meanOf (block.left), 5));

        // A uniform distribution over [-1, 1) has an RMS of 1/sqrt(3) = 0.577.
        // Checking it catches a generator that is technically random but only
        // exercising a fraction of its range.
        expectWithinAbsoluteError (rmsOf (block.left), 0.5774, 0.01,
                                   "noise is not uniformly distributed across its range");
    }

    void testChannelsAreDecorrelated()
    {
        beginTest ("The two channels are independent, not one stream duplicated");

        NoiseGenerator generator;
        generator.setSeed (7u);

        const NoiseBlock block (generator, blockSize);

        const auto correlation = correlationOf (block.left, block.right);

        expect (std::abs (correlation) < 0.02,
                "the channels correlate at " + juce::String (correlation, 4)
                    + ", which is a mono signal wearing a stereo label");
    }

    void testSeedingIsDeterministic()
    {
        beginTest ("The same seed produces the same stream");

        NoiseGenerator first;
        NoiseGenerator second;

        first.setSeed (99u);
        second.setSeed (99u);

        const NoiseBlock a (first, 4096);
        const NoiseBlock b (second, 4096);

        for (std::size_t i = 0; i < a.left.size(); ++i)
        {
            expectEquals (a.left[i], b.left[i]);
            expectEquals (a.right[i], b.right[i]);
        }
    }

    void testDifferentSeedsProduceDifferentStreams()
    {
        beginTest ("Neighbouring seeds produce independent streams");

        // Neighbouring rather than distant seeds on purpose: the engine seeds
        // its voices with consecutive integers, so this is the case that
        // actually occurs. A generator whose consecutive seeds correlate would
        // make a chord of noise sum coherently.
        for (std::uint32_t seed = 1; seed < 8; ++seed)
        {
            NoiseGenerator first;
            NoiseGenerator second;

            first.setSeed (seed);
            second.setSeed (seed + 1u);

            const NoiseBlock a (first, 1 << 14);
            const NoiseBlock b (second, 1 << 14);

            const auto correlation = correlationOf (a.left, b.left);

            expect (std::abs (correlation) < 0.05,
                    "seeds " + juce::String (static_cast<int> (seed)) + " and "
                        + juce::String (static_cast<int> (seed) + 1) + " correlate at "
                        + juce::String (correlation, 4));
        }
    }

    void testEverySeedProducesSignal()
    {
        beginTest ("No seed silences the generator");

        // A xorshift locks at zero forever if its state ever reaches zero, and
        // the obvious seed for that is zero itself. The generator maps such
        // seeds away; this is the assertion that says so.
        for (const std::uint32_t seed : { 0u, 1u, 0xFFFFFFFFu, 0x80000000u })
        {
            NoiseGenerator generator;
            generator.setSeed (seed);

            const NoiseBlock block (generator, 4096);

            expect (rmsOf (block.left) > 0.4,
                    "seed " + juce::String (static_cast<juce::int64> (seed))
                        + " produced little or no signal on the left channel");
            expect (rmsOf (block.right) > 0.4,
                    "seed " + juce::String (static_cast<juce::int64> (seed))
                        + " produced little or no signal on the right channel");
        }
    }

    void testGainScalesLinearly()
    {
        beginTest ("Gain scales the output linearly and independently per channel");

        NoiseGenerator reference;
        NoiseGenerator scaled;

        reference.setSeed (55u);
        scaled.setSeed (55u);

        std::vector<float> left (1024, 0.0f);
        std::vector<float> right (1024, 0.0f);
        std::vector<float> scaledLeft (1024, 0.0f);
        std::vector<float> scaledRight (1024, 0.0f);

        for (std::size_t i = 0; i < left.size(); ++i)
        {
            reference.addNextStereoSample (left[i], right[i], 1.0f, 1.0f);
            scaled.addNextStereoSample (scaledLeft[i], scaledRight[i], 0.25f, 0.5f);
        }

        for (std::size_t i = 0; i < left.size(); ++i)
        {
            expectWithinAbsoluteError (scaledLeft[i], left[i] * 0.25f, 1.0e-6f);
            expectWithinAbsoluteError (scaledRight[i], right[i] * 0.5f, 1.0e-6f);
        }
    }
};

NoiseTests noiseTests;

} // namespace
