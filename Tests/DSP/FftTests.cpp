/*
    The transform, and the analysis built on it.

    Apollo has its own because one thing needs one — turning a wavetable frame
    into the harmonics it is made of — and pulling in a DSP module for that
    would be a dependency bought with a single call site (Fft.h). Having written
    it, it has to be right: every loaded wavetable's spectrum comes from here,
    and an error in the transform is an error in the waveform that reaches the
    speakers.

    So it is checked against the definition rather than against itself. A naive
    discrete transform is four lines and is obviously correct; the fast one has
    to agree with it, on signals chosen to have no symmetry to hide behind.
*/

#include <juce_core/juce_core.h>

#include <cmath>
#include <complex>
#include <vector>

#include "DSP/Utilities/Fft.h"

using namespace apollo::dsp;

namespace
{

constexpr double twoPi = 6.283185307179586476925286766559;

/** The transform, written the way the definition is. */
[[nodiscard]] std::vector<std::complex<double>> naiveTransform (
    const std::vector<std::complex<double>>& input)
{
    const auto size = input.size();

    std::vector<std::complex<double>> output (size);

    for (std::size_t k = 0; k < size; ++k)
    {
        std::complex<double> sum { 0.0, 0.0 };

        for (std::size_t n = 0; n < size; ++n)
        {
            const auto angle = -twoPi * static_cast<double> (k) * static_cast<double> (n)
                             / static_cast<double> (size);

            sum += input[n] * std::complex<double> { std::cos (angle), std::sin (angle) };
        }

        output[k] = sum;
    }

    return output;
}

class FftTests final : public juce::UnitTest
{
public:
    FftTests()
        : juce::UnitTest ("Fourier transform", "DSP")
    {
    }

    void runTest() override
    {
        testAgreesWithTheDefinition();
        testRejectsLengthsItCannotDo();
        testAnalysesKnownWaveforms();
        testAnalysisSurvivesPhase();
        testAnalysisRoundTrip();
    }

private:
    void testAgreesWithTheDefinition()
    {
        beginTest ("The fast transform agrees with the slow one");

        juce::Random random { 20260915 };

        for (const std::size_t size : { std::size_t { 2 }, std::size_t { 8 },
                                        std::size_t { 64 }, std::size_t { 256 } })
        {
            std::vector<std::complex<double>> input (size);

            // Noise, not a tone. A sine is symmetric enough that a transform
            // with a sign error in half its butterflies can still produce it.
            for (std::size_t i = 0; i < size; ++i)
                input[i] = { random.nextDouble() * 2.0 - 1.0, random.nextDouble() * 2.0 - 1.0 };

            const auto expected = naiveTransform (input);

            auto actual = input;
            forwardTransform (actual);

            for (std::size_t k = 0; k < size; ++k)
            {
                const auto error = std::abs (actual[k] - expected[k]);

                expect (error < 1.0e-9,
                        "size " + juce::String (static_cast<int> (size)) + " bin "
                            + juce::String (static_cast<int> (k)) + " differs by "
                            + juce::String (error));
            }
        }
    }

    void testRejectsLengthsItCannotDo()
    {
        beginTest ("A length the transform cannot do is left alone, not approximated");

        expect (! isTransformSize (0));
        expect (! isTransformSize (1));
        expect (isTransformSize (2));
        expect (! isTransformSize (3));
        expect (isTransformSize (1024));
        expect (! isTransformSize (1000));

        // Untouched rather than mangled: a caller that passed the wrong length
        // asked the wrong question, and a plausible-looking answer is worse
        // than an obviously unchanged one.
        std::vector<std::complex<double>> odd (6, std::complex<double> { 1.0, 0.0 });
        const auto before = odd;

        forwardTransform (odd);

        for (std::size_t i = 0; i < odd.size(); ++i)
            expect (odd[i] == before[i], "a bad length must not be transformed");

        expect (analyseCycle (std::vector<double> (100, 1.0), 8).empty(),
                "analysis of a non-power-of-two cycle returns nothing");

        expect (analyseCycle (std::vector<double> (64, 1.0), 0).empty(),
                "asking for no harmonics returns nothing");
    }

    void testAnalysesKnownWaveforms()
    {
        beginTest ("Analysis reports the harmonics a known waveform actually has");

        constexpr std::size_t size = 1024;
        constexpr int wanted = 16;

        // A sine at the fundamental: one sine coefficient of 1, nothing else.
        std::vector<double> sine (size);

        for (std::size_t i = 0; i < size; ++i)
            sine[i] = std::sin (twoPi * static_cast<double> (i) / static_cast<double> (size));

        const auto sineSpectrum = analyseCycle (sine, wanted);

        expectEquals (static_cast<int> (sineSpectrum.size()), wanted);
        expectWithinAbsoluteError (sineSpectrum[0].imag(), 1.0, 1.0e-9, "sine coefficient");
        expectWithinAbsoluteError (sineSpectrum[0].real(), 0.0, 1.0e-9, "cosine coefficient");

        for (int k = 2; k <= wanted; ++k)
            expect (std::abs (sineSpectrum[static_cast<std::size_t> (k - 1)]) < 1.0e-9,
                    "a sine has no harmonic " + juce::String (k));

        // A saw: every harmonic, falling as 1/k. Built from its own series so
        // the test is checking the transform rather than a formula against
        // itself in the time domain.
        std::vector<double> saw (size, 0.0);

        for (int k = 1; k <= 32; ++k)
            for (std::size_t i = 0; i < size; ++i)
                saw[i] += std::sin (twoPi * static_cast<double> (k) * static_cast<double> (i)
                                    / static_cast<double> (size)) / static_cast<double> (k);

        const auto sawSpectrum = analyseCycle (saw, wanted);

        for (int k = 1; k <= wanted; ++k)
            expectWithinAbsoluteError (sawSpectrum[static_cast<std::size_t> (k - 1)].imag(),
                                       1.0 / static_cast<double> (k), 1.0e-9,
                                       "saw harmonic " + juce::String (k));
    }

    /** The claim that makes a loaded wavetable the waveform the user supplied. */
    void testAnalysisSurvivesPhase()
    {
        beginTest ("Two waveforms with the same harmonics and different phases stay different");

        constexpr std::size_t size = 512;

        std::vector<double> asSine (size, 0.0);
        std::vector<double> asCosine (size, 0.0);

        for (std::size_t i = 0; i < size; ++i)
        {
            const auto phase = twoPi * static_cast<double> (i) / static_cast<double> (size);

            asSine[i] = std::sin (phase) + 0.5 * std::sin (3.0 * phase);
            asCosine[i] = std::sin (phase) + 0.5 * std::cos (3.0 * phase);
        }

        const auto sineSpectrum = analyseCycle (asSine, 4);
        const auto cosineSpectrum = analyseCycle (asCosine, 4);

        // Identical amplitudes at the third harmonic...
        expectWithinAbsoluteError (std::abs (sineSpectrum[2]), std::abs (cosineSpectrum[2]),
                                   1.0e-9, "the two should have the same amount of third");

        // ...and it is in a different place. A spectrum that only carried
        // amplitude would call these the same waveform, and a wavetable built
        // from it would not be the one the user drew.
        expectWithinAbsoluteError (sineSpectrum[2].imag(), 0.5, 1.0e-9, "sine phase");
        expectWithinAbsoluteError (cosineSpectrum[2].real(), 0.5, 1.0e-9, "cosine phase");
        expect (std::abs (sineSpectrum[2].real()) < 1.0e-9);
        expect (std::abs (cosineSpectrum[2].imag()) < 1.0e-9);
    }

    void testAnalysisRoundTrip()
    {
        beginTest ("A waveform survives being taken apart and put back together");

        constexpr std::size_t size = 256;
        constexpr int wanted = static_cast<int> (size / 2) - 1;

        // An arbitrary shape with no symmetry at all.
        std::vector<double> original (size, 0.0);

        for (std::size_t i = 0; i < size; ++i)
        {
            const auto phase = twoPi * static_cast<double> (i) / static_cast<double> (size);

            original[i] = std::sin (phase) + 0.3 * std::cos (2.0 * phase)
                        + 0.2 * std::sin (5.0 * phase + 1.1)
                        + 0.05 * std::cos (11.0 * phase - 0.4);
        }

        const auto spectrum = analyseCycle (original, wanted);

        expectEquals (static_cast<int> (spectrum.size()), wanted);

        for (std::size_t i = 0; i < size; ++i)
        {
            auto sum = 0.0;

            for (int k = 1; k <= wanted; ++k)
            {
                const auto harmonic = spectrum[static_cast<std::size_t> (k - 1)];
                const auto phase = twoPi * static_cast<double> (k) * static_cast<double> (i)
                                 / static_cast<double> (size);

                sum += harmonic.real() * std::cos (phase) + harmonic.imag() * std::sin (phase);
            }

            expectWithinAbsoluteError (sum, original[i], 1.0e-9,
                                       "sample " + juce::String (static_cast<int> (i))
                                           + " did not come back");
        }
    }
};

FftTests fftTests;

} // namespace
