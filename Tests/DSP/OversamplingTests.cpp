/*
    Oversampling tests.

    The oversampler exists for one measurable reason — it should stop a
    nonlinear stage from folding harmonics back into the audible band — so the
    central test here does exactly that: it clips a sine with and without
    oversampling and compares the aliasing that results. Everything else
    establishes that the conversion itself is trustworthy enough for that
    comparison to mean something.

    The polyphase decomposition is checked against a deliberately naive
    reference (zero-stuff, then convolve with the full impulse response,
    including all the zero taps). The efficient form is index algebra that is
    easy to get subtly wrong and hard to spot by reading, and a slightly wrong
    version still produces plausible-sounding audio. The reference cannot be
    wrong in the same way, because it is the textbook definition.
*/

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "DSP/Oversampling/HalfbandFilter.h"
#include "DSP/Oversampling/Oversampler.h"

using namespace apollo::dsp;

namespace
{

constexpr double testSampleRate = 48000.0;
constexpr double pi = 3.14159265358979323846;

constexpr int fftOrder = 14;
constexpr int fftSize = 1 << fftOrder;

/** @returns the magnitude spectrum of @p signal in dB, relative to its loudest
    bin.

    A Blackman-Harris window is used for the same reason the wavetable tests use
    one: an unwindowed bin leaks into its neighbours far above the level being
    measured, and that leakage would be reported as aliasing.
*/
[[nodiscard]] std::vector<float> spectrumDb (const std::vector<float>& signal)
{
    std::vector<float> fftData (static_cast<std::size_t> (fftSize) * 2, 0.0f);

    const auto count = std::min (static_cast<std::size_t> (fftSize), signal.size());

    for (std::size_t i = 0; i < count; ++i)
    {
        const auto t = static_cast<double> (i) / static_cast<double> (fftSize - 1);
        const auto w = 0.35875
                     - 0.48829 * std::cos (2.0 * pi * t)
                     + 0.14128 * std::cos (4.0 * pi * t)
                     - 0.01168 * std::cos (6.0 * pi * t);

        fftData[i] = signal[i] * static_cast<float> (w);
    }

    juce::dsp::FFT fft (fftOrder);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    std::vector<float> magnitudes (static_cast<std::size_t> (fftSize / 2), 0.0f);

    auto peak = 0.0f;

    for (std::size_t i = 0; i < magnitudes.size(); ++i)
        peak = std::max (peak, fftData[i]);

    if (peak <= 0.0f)
        peak = 1.0f;

    for (std::size_t i = 0; i < magnitudes.size(); ++i)
        magnitudes[i] = juce::Decibels::gainToDecibels (fftData[i] / peak, -160.0f);

    return magnitudes;
}

/** @returns the bin nearest @p frequencyHz at @p rate. */
[[nodiscard]] int binFor (double frequencyHz, double rate)
{
    return static_cast<int> (std::lround (frequencyHz / rate * static_cast<double> (fftSize)));
}

/** Worst non-harmonic content, in dBc, ignoring bins near the harmonics of
    @p fundamentalHz and near DC.

    A hard clipper produces only odd harmonics of the input, so anything else
    above the floor is fold-back. Harmonic bins are excluded with a small guard
    either side to allow for the window's main lobe.
*/
[[nodiscard]] float worstAlias (const std::vector<float>& spectrum,
                                double fundamentalHz,
                                double rate)
{
    constexpr int guard = 12;

    auto worst = -160.0f;

    for (std::size_t bin = 8; bin < spectrum.size(); ++bin)
    {
        const auto frequency = static_cast<double> (bin) * rate / static_cast<double> (fftSize);
        const auto ratio = frequency / fundamentalHz;
        const auto nearestHarmonic = std::lround (ratio);

        const auto harmonicBin = binFor (static_cast<double> (nearestHarmonic) * fundamentalHz, rate);

        if (nearestHarmonic >= 1 && std::abs (static_cast<int> (bin) - harmonicBin) <= guard)
            continue;

        worst = std::max (worst, spectrum[bin]);
    }

    return worst;
}

/** The textbook upsample: insert a zero between every input sample, then
    convolve with the complete impulse response and double.

    Deliberately the slow, obvious form. It is the yardstick the polyphase
    implementation is measured against.
*/
[[nodiscard]] std::vector<float> naiveUpsample (const std::vector<float>& input,
                                                const std::vector<float>& impulse)
{
    std::vector<float> stuffed (input.size() * 2, 0.0f);

    for (std::size_t i = 0; i < input.size(); ++i)
        stuffed[i * 2] = input[i];

    std::vector<float> output (stuffed.size(), 0.0f);

    for (std::size_t n = 0; n < stuffed.size(); ++n)
    {
        auto accumulator = 0.0f;

        for (std::size_t k = 0; k < impulse.size(); ++k)
            if (n >= k)
                accumulator += impulse[k] * stuffed[n - k];

        output[n] = accumulator * 2.0f;
    }

    return output;
}

/** The textbook downsample: convolve with the complete impulse response, then
    keep every second sample.
*/
[[nodiscard]] std::vector<float> naiveDownsample (const std::vector<float>& input,
                                                  const std::vector<float>& impulse)
{
    std::vector<float> filtered (input.size(), 0.0f);

    for (std::size_t n = 0; n < input.size(); ++n)
    {
        auto accumulator = 0.0f;

        for (std::size_t k = 0; k < impulse.size(); ++k)
            if (n >= k)
                accumulator += impulse[k] * input[n - k];

        filtered[n] = accumulator;
    }

    std::vector<float> output (input.size() / 2, 0.0f);

    for (std::size_t i = 0; i < output.size(); ++i)
        output[i] = filtered[i * 2];

    return output;
}

[[nodiscard]] std::vector<float> makeSine (double frequencyHz, double rate, int numSamples, float amplitude = 1.0f)
{
    std::vector<float> signal (static_cast<std::size_t> (numSamples), 0.0f);

    for (int i = 0; i < numSamples; ++i)
        signal[static_cast<std::size_t> (i)] =
            amplitude * static_cast<float> (std::sin (2.0 * pi * frequencyHz * static_cast<double> (i) / rate));

    return signal;
}

/** Hard clip: the harshest common nonlinearity, and therefore the one that
    generates the most fold-back for a given input.
*/
[[nodiscard]] float hardClip (float x) noexcept
{
    constexpr float threshold = 0.3f;
    return std::clamp (x, -threshold, threshold);
}

//==============================================================================

class OversamplingTests final : public juce::UnitTest
{
public:
    OversamplingTests()
        : juce::UnitTest ("Oversampling", "DSP")
    {
    }

    void runTest() override
    {
        testHalfbandStructure();
        testHalfbandResponse();
        testPolyphaseMatchesNaive();
        testLatencyIsExactAndWhole();
        testRoundTripPreservesSignal();
        testPassThrough();
        testAliasingIsReduced();
        testBlockSizeInvariance();
        testExtremeInput();
    }

private:
    void testHalfbandStructure()
    {
        beginTest ("A halfband design has the zeros and the centre tap it should");

        for (const auto taps : { 31, 63, 79, 81, 127 })
        {
            HalfbandCoefficients coefficients;
            coefficients.design (taps, 100.0);

            const auto& impulse = coefficients.getImpulseResponse();
            const auto centre = coefficients.getCentre();

            expect (impulse.size() % 2 == 1, "length must be odd");
            expectWithinAbsoluteError (impulse[static_cast<std::size_t> (centre)], 0.5f, 1.0e-6f,
                                       "the centre tap of a halfband is exactly one half");

            for (int n = 0; n < static_cast<int> (impulse.size()); ++n)
            {
                if (n != centre && ((n - centre) % 2) == 0)
                    expect (impulse[static_cast<std::size_t> (n)] == 0.0f,
                            "every second tap either side of the centre must be exactly zero");
            }

            // Symmetry is what makes the phase linear, which is the whole reason
            // this filter was chosen over a cheaper allpass cascade.
            for (std::size_t n = 0; n < impulse.size(); ++n)
                expectWithinAbsoluteError (impulse[n], impulse[impulse.size() - 1 - n], 1.0e-7f,
                                           "the impulse response must be symmetric");
        }
    }

    void testHalfbandResponse()
    {
        beginTest ("The halfband passes its passband and rejects its stopband");

        HalfbandCoefficients coefficients;
        coefficients.design (HalfbandCoefficients::defaultNumTaps, 100.0);

        const auto& impulse = coefficients.getImpulseResponse();

        // Evaluate the response directly rather than through an FFT of a padded
        // impulse: a 63-tap response is short enough that the exact sum is both
        // cheaper and free of windowing questions.
        const auto responseAt = [&impulse] (double normalisedFrequency)
        {
            double real = 0.0;
            double imag = 0.0;

            for (std::size_t n = 0; n < impulse.size(); ++n)
            {
                const auto angle = 2.0 * pi * normalisedFrequency * static_cast<double> (n);
                real += static_cast<double> (impulse[n]) * std::cos (angle);
                imag -= static_cast<double> (impulse[n]) * std::sin (angle);
            }

            // The filter's own response, undoubled. A unity-passband lowpass has a
            // DC gain of one; the factor of two that appears in the upsampler
            // belongs to zero-stuffing, not to the filter, and applying it here
            // too reports a flat passband as 6.02 dB of ripple.
            return std::sqrt (real * real + imag * imag);
        };

        // Passband: everything below a comfortable margin under a quarter rate.
        auto worstPassbandDb = 0.0;

        for (double f = 0.0; f <= 0.205; f += 0.0025)
        {
            const auto db = 20.0 * std::log10 (std::max (responseAt (f), 1.0e-12));
            worstPassbandDb = std::max (worstPassbandDb, std::abs (db));
        }

        // Stopband: everything above a comfortable margin over a quarter rate.
        auto worstStopbandDb = -200.0;

        for (double f = 0.295; f <= 0.5; f += 0.0025)
        {
            const auto db = 20.0 * std::log10 (std::max (responseAt (f), 1.0e-12));
            worstStopbandDb = std::max (worstStopbandDb, db);
        }

        logMessage ("  passband ripple  +/- " + juce::String (worstPassbandDb, 4) + " dB");
        logMessage ("  stopband worst      " + juce::String (worstStopbandDb, 1) + " dB");

        expect (worstPassbandDb < 0.1,
                "passband ripple " + juce::String (worstPassbandDb, 4) + " dB is too high");

        expect (worstStopbandDb < -80.0,
                "stopband attenuation " + juce::String (worstStopbandDb, 1) + " dB is not enough");

        // The response must be exactly -6 dB at the halfband point. That is the
        // defining property of a halfband filter, and it is what makes two of
        // them sum to unity across the crossover.
        // Exactly half amplitude at the halfband point is the defining property:
        // it is what makes the passband and its mirror image sum to unity across
        // the crossover.
        const auto atQuarter = responseAt (0.25);
        expectWithinAbsoluteError (atQuarter, 0.5, 1.0e-4,
                                   "a halfband must pass its own quarter-rate point at exactly half amplitude");
    }

    void testPolyphaseMatchesNaive()
    {
        beginTest ("The polyphase form matches a naive zero-stuff convolution");

        // Both parities: 79 centres on an odd index, 81 on an even one, and the
        // decimator handles them differently.
        for (const auto taps : { 79, 81 })
        {
            HalfbandCoefficients coefficients;
            coefficients.design (taps, 100.0);

            constexpr int numSamples = 512;

            // A chirp rather than a sine: it excites every frequency, so an
            // index error that only shows up at one part of the spectrum cannot
            // hide.
            std::vector<float> input (numSamples, 0.0f);

            for (int i = 0; i < numSamples; ++i)
            {
                const auto t = static_cast<double> (i) / static_cast<double> (numSamples);
                const auto phase = 2.0 * pi * (0.02 + 0.20 * t) * static_cast<double> (i);
                input[static_cast<std::size_t> (i)] = static_cast<float> (std::sin (phase));
            }

            HalfbandUpsampler up;
            up.prepare (coefficients, numSamples);

            std::vector<float> fast (static_cast<std::size_t> (numSamples) * 2, 0.0f);
            up.process (input.data(), fast.data(), numSamples);

            const auto reference = naiveUpsample (input, coefficients.getImpulseResponse());

            auto worstUp = 0.0f;

            for (std::size_t i = 0; i < fast.size(); ++i)
                worstUp = std::max (worstUp, std::abs (fast[i] - reference[i]));

            logMessage ("  " + juce::String (taps) + " taps: worst upsample difference "
                        + juce::String (worstUp, 9));

            expect (worstUp < 1.0e-5f,
                    "polyphase upsampling differs from the reference by " + juce::String (worstUp, 9));

            // And the same for the decimator.
            HalfbandDownsampler down;
            down.prepare (coefficients, numSamples);

            std::vector<float> wide (static_cast<std::size_t> (numSamples) * 2, 0.0f);

            for (int i = 0; i < numSamples * 2; ++i)
            {
                const auto t = static_cast<double> (i) / static_cast<double> (numSamples * 2);
                const auto phase = 2.0 * pi * (0.01 + 0.10 * t) * static_cast<double> (i);
                wide[static_cast<std::size_t> (i)] = static_cast<float> (std::sin (phase));
            }

            std::vector<float> fastDown (static_cast<std::size_t> (numSamples), 0.0f);
            down.process (wide.data(), fastDown.data(), numSamples);

            const auto referenceDown = naiveDownsample (wide, coefficients.getImpulseResponse());

            auto worstDown = 0.0f;

            for (std::size_t i = 0; i < fastDown.size(); ++i)
                worstDown = std::max (worstDown, std::abs (fastDown[i] - referenceDown[i]));

            logMessage ("  " + juce::String (taps) + " taps: worst downsample difference "
                        + juce::String (worstDown, 9));

            expect (worstDown < 1.0e-5f,
                    "polyphase downsampling differs from the reference by " + juce::String (worstDown, 9));
        }
    }

    void testLatencyIsExactAndWhole()
    {
        beginTest ("Reported latency is whole, and is where the impulse actually lands");

        for (const auto factor : { Oversampler::Factor::x2, Oversampler::Factor::x4 })
        {
            constexpr int numSamples = 1024;

            Oversampler oversampler;
            oversampler.prepare (factor, numSamples);

            std::vector<float> input (numSamples, 0.0f);
            input[0] = 1.0f;

            auto* wide = oversampler.upsample (input.data(), numSamples);
            expect (wide != nullptr, "upsample returned nothing");

            std::vector<float> output (numSamples, 0.0f);
            oversampler.downsample (output.data(), numSamples);

            // Where did the impulse end up?
            auto peakIndex = 0;
            auto peak = 0.0f;

            for (int i = 0; i < numSamples; ++i)
            {
                if (std::abs (output[static_cast<std::size_t> (i)]) > peak)
                {
                    peak = std::abs (output[static_cast<std::size_t> (i)]);
                    peakIndex = i;
                }
            }

            const auto reported = oversampler.getLatencySamples();

            logMessage ("  " + juce::String (oversampler.getFactorAsInt()) + "x: reported latency "
                        + juce::String (reported) + ", impulse peak at " + juce::String (peakIndex));

            expect (peakIndex == reported,
                    "reported latency " + juce::String (reported)
                        + " but the impulse peaked at " + juce::String (peakIndex));
        }
    }

    void testRoundTripPreservesSignal()
    {
        beginTest ("A round trip returns the signal, delayed by the reported latency");

        for (const auto factor : { Oversampler::Factor::x2, Oversampler::Factor::x4 })
        {
            constexpr int numSamples = 4096;

            Oversampler oversampler;
            oversampler.prepare (factor, numSamples);

            // Comfortably inside the passband: a round trip is only expected to
            // preserve what the conversion filters are designed to pass.
            const auto input = makeSine (1000.0, testSampleRate, numSamples, 0.8f);

            auto* wide = oversampler.upsample (input.data(), numSamples);
            expect (wide != nullptr);

            std::vector<float> output (numSamples, 0.0f);
            oversampler.downsample (output.data(), numSamples);

            const auto latency = oversampler.getLatencySamples();

            auto worst = 0.0f;

            // Skip the start, where the filters are still filling.
            for (int i = latency + 256; i < numSamples; ++i)
            {
                const auto expectedValue = input[static_cast<std::size_t> (i - latency)];
                worst = std::max (worst, std::abs (output[static_cast<std::size_t> (i)] - expectedValue));
            }

            logMessage ("  " + juce::String (oversampler.getFactorAsInt())
                        + "x: worst round-trip error " + juce::String (worst, 7));

            expect (worst < 1.0e-3f,
                    "round trip changed the signal by " + juce::String (worst, 7));
        }
    }

    void testPassThrough()
    {
        beginTest ("Factor::none is an exact pass-through with no latency");

        constexpr int numSamples = 256;

        Oversampler oversampler;
        oversampler.prepare (Oversampler::Factor::none, numSamples);

        expect (oversampler.getLatencySamples() == 0, "bypass must not add latency");
        expect (oversampler.getFactorAsInt() == 1);
        expect (oversampler.getOversampledLength (numSamples) == numSamples);

        const auto input = makeSine (3000.0, testSampleRate, numSamples, 0.7f);

        auto* wide = oversampler.upsample (input.data(), numSamples);
        expect (wide != nullptr, "bypass must still hand back a writable buffer");

        std::vector<float> output (numSamples, 0.0f);
        oversampler.downsample (output.data(), numSamples);

        for (int i = 0; i < numSamples; ++i)
            expectWithinAbsoluteError (output[static_cast<std::size_t> (i)],
                                       input[static_cast<std::size_t> (i)], 0.0f,
                                       "bypass must return the input bit for bit");
    }

    void testAliasingIsReduced()
    {
        beginTest ("Oversampling reduces the aliasing a hard clipper produces");

        // 4 kHz clipped hard generates harmonics every 8 kHz. At 48 kHz the
        // fifth harmonic is already at 20 kHz and the seventh at 28 kHz, which
        // folds to 20 kHz; higher ones fold well into the audible range. This is
        // the case oversampling exists for.
        // Deliberately inharmonic with the sample rate. 4 kHz divides 48 kHz
        // exactly, so every folded harmonic lands precisely on another harmonic
        // and the measurement excludes the very thing it is looking for — the
        // first version of this test measured -105 dBc of aliasing that was
        // really just harmonics hiding each other. This tone sits on an FFT bin
        // but is not a rational fraction of the rate.
        const double toneHz = testSampleRate * 1129.0 / static_cast<double> (fftSize);
        constexpr int numSamples = fftSize;

        const auto input = makeSine (toneHz, testSampleRate, numSamples, 0.9f);

        // Baseline: clip at the base rate.
        std::vector<float> direct (numSamples, 0.0f);

        for (int i = 0; i < numSamples; ++i)
            direct[static_cast<std::size_t> (i)] = hardClip (input[static_cast<std::size_t> (i)]);

        const auto directWorst = worstAlias (spectrumDb (direct), toneHz, testSampleRate);

        logMessage ("  no oversampling: worst non-harmonic " + juce::String (directWorst, 1) + " dBc");

        auto previous = directWorst;

        for (const auto factor : { Oversampler::Factor::x2, Oversampler::Factor::x4 })
        {
            Oversampler oversampler;
            oversampler.prepare (factor, numSamples);

            auto* wide = oversampler.upsample (input.data(), numSamples);
            expect (wide != nullptr);

            const auto wideLength = oversampler.getOversampledLength (numSamples);

            for (int i = 0; i < wideLength; ++i)
                wide[i] = hardClip (wide[i]);

            std::vector<float> output (numSamples, 0.0f);
            oversampler.downsample (output.data(), numSamples);

            const auto worst = worstAlias (spectrumDb (output), toneHz, testSampleRate);

            logMessage ("  " + juce::String (oversampler.getFactorAsInt())
                        + "x oversampling: worst non-harmonic " + juce::String (worst, 1) + " dBc");

            expect (worst < previous,
                    juce::String (oversampler.getFactorAsInt())
                        + "x oversampling did not improve on the previous setting ("
                        + juce::String (worst, 1) + " dBc vs " + juce::String (previous, 1) + " dBc)");

            previous = worst;
        }

        // The improvement must be worth the CPU, not merely measurable.
        expect (previous < directWorst - 10.0f,
                "4x oversampling improved aliasing by less than 10 dB, which would not justify its cost");
    }

    void testBlockSizeInvariance()
    {
        beginTest ("Output does not depend on how the input is divided into blocks");

        constexpr int numSamples = 2048;

        const auto input = makeSine (2500.0, testSampleRate, numSamples, 0.6f);

        std::vector<float> whole (numSamples, 0.0f);

        {
            Oversampler oversampler;
            oversampler.prepare (Oversampler::Factor::x4, numSamples);

            auto* wide = oversampler.upsample (input.data(), numSamples);

            for (int i = 0; i < oversampler.getOversampledLength (numSamples); ++i)
                wide[i] = hardClip (wide[i]);

            oversampler.downsample (whole.data(), numSamples);
        }

        for (const auto blockSize : { 1, 7, 64, 333 })
        {
            Oversampler oversampler;
            oversampler.prepare (Oversampler::Factor::x4, numSamples);

            std::vector<float> pieces (numSamples, 0.0f);

            for (int start = 0; start < numSamples; start += blockSize)
            {
                const auto count = std::min (blockSize, numSamples - start);

                auto* wide = oversampler.upsample (input.data() + start, count);
                expect (wide != nullptr, "upsample failed at block size " + juce::String (blockSize));

                for (int i = 0; i < oversampler.getOversampledLength (count); ++i)
                    wide[i] = hardClip (wide[i]);

                oversampler.downsample (pieces.data() + start, count);
            }

            auto worst = 0.0f;

            for (int i = 0; i < numSamples; ++i)
                worst = std::max (worst, std::abs (pieces[static_cast<std::size_t> (i)]
                                                   - whole[static_cast<std::size_t> (i)]));

            expect (worst < 1.0e-6f,
                    "block size " + juce::String (blockSize) + " changed the output by "
                        + juce::String (worst, 9));
        }
    }

    void testExtremeInput()
    {
        beginTest ("Extreme and non-finite input stays contained");

        constexpr int numSamples = 512;

        Oversampler oversampler;
        oversampler.prepare (Oversampler::Factor::x4, numSamples);

        // Very loud, and alternating at Nyquist: the worst case for a filter's
        // internal accumulation.
        std::vector<float> input (numSamples, 0.0f);

        for (int i = 0; i < numSamples; ++i)
            input[static_cast<std::size_t> (i)] = (i % 2 == 0) ? 1000.0f : -1000.0f;

        auto* wide = oversampler.upsample (input.data(), numSamples);
        expect (wide != nullptr);

        std::vector<float> output (numSamples, 0.0f);
        oversampler.downsample (output.data(), numSamples);

        for (int i = 0; i < numSamples; ++i)
            expect (std::isfinite (output[static_cast<std::size_t> (i)]),
                    "extreme input produced a non-finite sample");

        // A block that is longer than the prepared maximum must be refused
        // rather than overrun the buffers.
        expect (oversampler.upsample (input.data(), numSamples * 2) == nullptr,
                "an oversized block must be refused");

        expect (oversampler.upsample (nullptr, numSamples) == nullptr,
                "a null input must be refused");

        // Resetting must clear the tails rather than leave the extreme values in
        // the delay lines.
        oversampler.reset();

        std::vector<float> silence (numSamples, 0.0f);
        auto* quiet = oversampler.upsample (silence.data(), numSamples);
        expect (quiet != nullptr);

        oversampler.downsample (output.data(), numSamples);

        for (int i = 0; i < numSamples; ++i)
            expectWithinAbsoluteError (output[static_cast<std::size_t> (i)], 0.0f, 1.0e-6f,
                                       "reset must leave no tail behind");
    }
};

OversamplingTests oversamplingTests;

} // namespace
