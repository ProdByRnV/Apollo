/*
    State variable filter tests.

    A filter is judged by its response, so most of this measures one: drive a
    sine through, compare its amplitude to the input, and check the shape that
    comes out matches the mode that was asked for.

    Two of these tests exist because of specific claims made elsewhere in the
    code, and would be the first things to fail if those claims stopped being
    true. `testDriveAliasing` measures the fold-back from the filter drive,
    which `FilterDrive.h` asserts is mild enough not to need oversampling.
    `testStabilityUnderModulation` sweeps the cutoff as fast as it can be swept,
    which is the case the TPT topology was chosen over a biquad to survive.
*/

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "DSP/Filters/FilterDrive.h"
#include "DSP/Filters/StateVariableFilter.h"

using namespace apollo::dsp;

namespace
{

constexpr double testSampleRate = 48000.0;
constexpr double pi = 3.14159265358979323846;

using Mode = StateVariableFilter::Mode;

/** @returns the steady-state gain of @p filter at @p frequencyHz.

    Measured rather than derived: a sine goes in, the first part of the output is
    discarded while the filter settles, and the peak of what remains is compared
    to the input's. That tests the filter as it actually runs, including its
    state handling, rather than testing an algebraic transfer function that the
    implementation might not match.
*/
[[nodiscard]] float gainAt (Mode mode, float cutoffHz, float q, double frequencyHz)
{
    SvfCoefficients coefficients;
    coefficients.set (cutoffHz, q, testSampleRate);

    StateVariableFilter filter;
    filter.setMode (mode);
    filter.setCoefficients (coefficients);
    filter.reset();

    const auto settle = static_cast<int> (testSampleRate * 0.5);
    const auto measure = static_cast<int> (testSampleRate * 0.2);

    auto peak = 0.0f;

    for (int i = 0; i < settle + measure; ++i)
    {
        const auto phase = 2.0 * pi * frequencyHz * static_cast<double> (i) / testSampleRate;
        const auto output = filter.processSample (static_cast<float> (std::sin (phase)));

        if (i >= settle)
            peak = std::max (peak, std::abs (output));
    }

    return peak;
}

[[nodiscard]] float toDecibels (float gain)
{
    return juce::Decibels::gainToDecibels (gain, -120.0f);
}

//==============================================================================

class FilterTests final : public juce::UnitTest
{
public:
    FilterTests()
        : juce::UnitTest ("Filter", "DSP")
    {
    }

    void runTest() override
    {
        testOffIsTransparent();
        testLowpassShape();
        testHighpassShape();
        testBandpassShape();
        testNotchShape();
        testCutoffLandsWhereItWasSet();
        testResonanceRaisesThePeak();
        testStabilityUnderModulation();
        testStabilityAcrossSampleRates();
        testExtremeInput();
        testDriveIsBoundedAndGentle();
        testDriveAliasing();
    }

private:
    void testOffIsTransparent()
    {
        beginTest ("An off filter passes its input through untouched");

        StateVariableFilter filter;
        filter.setMode (Mode::off);

        SvfCoefficients coefficients;
        coefficients.set (500.0f, 4.0f, testSampleRate);
        filter.setCoefficients (coefficients);

        for (int i = 0; i < 1000; ++i)
        {
            const auto input = static_cast<float> (std::sin (0.07 * static_cast<double> (i)));

            expectWithinAbsoluteError (filter.processSample (input), input, 0.0f,
                                       "a bypassed filter must be bit-exact, not merely close");
        }
    }

    void testLowpassShape()
    {
        beginTest ("A lowpass passes what is below its cutoff and stops what is above");

        constexpr float cutoff = 1000.0f;
        constexpr float q = 0.707f;

        const auto low = toDecibels (gainAt (Mode::lowpass, cutoff, q, 100.0));
        const auto at = toDecibels (gainAt (Mode::lowpass, cutoff, q, cutoff));
        const auto high = toDecibels (gainAt (Mode::lowpass, cutoff, q, 10000.0));

        logMessage ("  lowpass 1 kHz: 100 Hz " + juce::String (low, 2)
                    + " dB, 1 kHz " + juce::String (at, 2)
                    + " dB, 10 kHz " + juce::String (high, 2) + " dB");

        expectWithinAbsoluteError (low, 0.0f, 0.5f, "the passband must be flat");
        expect (high < -35.0f, "a decade above cutoff must be well attenuated");

        // Two poles is 12 dB per octave, so a decade is about 40 dB.
        expect (high < low - 35.0f, "the stopband must fall away from the passband");
    }

    void testHighpassShape()
    {
        beginTest ("A highpass is the mirror of the lowpass");

        constexpr float cutoff = 1000.0f;
        constexpr float q = 0.707f;

        const auto low = toDecibels (gainAt (Mode::highpass, cutoff, q, 100.0));
        const auto high = toDecibels (gainAt (Mode::highpass, cutoff, q, 10000.0));

        logMessage ("  highpass 1 kHz: 100 Hz " + juce::String (low, 2)
                    + " dB, 10 kHz " + juce::String (high, 2) + " dB");

        expectWithinAbsoluteError (high, 0.0f, 0.5f, "the passband must be flat");
        expect (low < -35.0f, "a decade below cutoff must be well attenuated");
    }

    void testBandpassShape()
    {
        beginTest ("A bandpass peaks at its cutoff and falls away either side");

        constexpr float cutoff = 1000.0f;
        constexpr float q = 2.0f;

        const auto below = toDecibels (gainAt (Mode::bandpass, cutoff, q, 100.0));
        const auto at = toDecibels (gainAt (Mode::bandpass, cutoff, q, cutoff));
        const auto above = toDecibels (gainAt (Mode::bandpass, cutoff, q, 10000.0));

        logMessage ("  bandpass 1 kHz Q2: 100 Hz " + juce::String (below, 2)
                    + " dB, 1 kHz " + juce::String (at, 2)
                    + " dB, 10 kHz " + juce::String (above, 2) + " dB");

        expect (at > below + 20.0f, "the band must stand well above the low side");
        expect (at > above + 20.0f, "the band must stand well above the high side");
    }

    void testNotchShape()
    {
        beginTest ("A notch removes its cutoff and passes everything else");

        constexpr float cutoff = 1000.0f;
        constexpr float q = 2.0f;

        const auto below = toDecibels (gainAt (Mode::notch, cutoff, q, 100.0));
        const auto at = toDecibels (gainAt (Mode::notch, cutoff, q, cutoff));
        const auto above = toDecibels (gainAt (Mode::notch, cutoff, q, 10000.0));

        logMessage ("  notch 1 kHz Q2: 100 Hz " + juce::String (below, 2)
                    + " dB, 1 kHz " + juce::String (at, 2)
                    + " dB, 10 kHz " + juce::String (above, 2) + " dB");

        expectWithinAbsoluteError (below, 0.0f, 1.0f, "below the notch must pass");
        expectWithinAbsoluteError (above, 0.0f, 1.0f, "above the notch must pass");
        expect (at < -20.0f, "the notch must actually notch");
    }

    void testCutoffLandsWhereItWasSet()
    {
        beginTest ("The cutoff is where it was asked for, across the range");

        // At Butterworth Q a two-pole lowpass is -3 dB at its cutoff. That is
        // the check that the frequency warping is being undone correctly: a
        // bilinear-transform filter without compensation drifts here, and drifts
        // more the higher the cutoff goes.
        for (const auto cutoff : { 100.0f, 500.0f, 2000.0f, 8000.0f, 15000.0f })
        {
            const auto at = toDecibels (gainAt (Mode::lowpass, cutoff, 0.707f,
                                                static_cast<double> (cutoff)));

            logMessage ("  cutoff " + juce::String (cutoff, 0) + " Hz: "
                        + juce::String (at, 2) + " dB at the cutoff");

            expectWithinAbsoluteError (at, -3.0f, 1.0f,
                                       "a Butterworth lowpass must be -3 dB at its cutoff, but "
                                       + juce::String (cutoff, 0) + " Hz measured "
                                       + juce::String (at, 2) + " dB");
        }
    }

    void testResonanceRaisesThePeak()
    {
        beginTest ("Resonance raises the peak, monotonically and finitely");

        constexpr float cutoff = 1000.0f;

        auto previous = -120.0f;

        for (const auto q : { 0.707f, 1.0f, 2.0f, 5.0f, 10.0f })
        {
            const auto at = toDecibels (gainAt (Mode::lowpass, cutoff, q, cutoff));

            logMessage ("  Q " + juce::String (q, 3) + ": " + juce::String (at, 2)
                        + " dB at cutoff");

            expect (at > previous, "Q " + juce::String (q, 3) + " did not raise the peak");
            expect (at < 40.0f, "the resonant peak must stay finite and bounded");

            previous = at;
        }
    }

    void testStabilityUnderModulation()
    {
        beginTest ("A cutoff swept as fast as possible stays stable");

        // This is the case the TPT topology was chosen for. A biquad recomputed
        // per sample can go unstable here, because its feedback path assumes
        // coefficients that are no longer the ones in use.
        for (const auto q : { 0.707f, 5.0f, 10.0f })
        {
            StateVariableFilter filter;
            filter.setMode (Mode::lowpass);
            filter.reset();

            auto peak = 0.0f;

            for (int i = 0; i < 200000; ++i)
            {
                // A full sweep from 20 Hz to 20 kHz and back, every 10 ms —
                // faster than any LFO or envelope could ever drive it.
                const auto sweep = 0.5 + 0.5 * std::sin (2.0 * pi * 100.0
                                                         * static_cast<double> (i) / testSampleRate);
                const auto cutoff = static_cast<float> (20.0 * std::pow (1000.0, sweep));

                SvfCoefficients coefficients;
                coefficients.set (cutoff, q, testSampleRate);
                filter.setCoefficients (coefficients);

                const auto input = static_cast<float> (std::sin (2.0 * pi * 220.0
                                                                 * static_cast<double> (i) / testSampleRate));

                const auto output = filter.processSample (input);

                expect (std::isfinite (output),
                        "Q " + juce::String (q, 3) + ": a swept cutoff produced a non-finite sample");

                peak = std::max (peak, std::abs (output));
            }

            logMessage ("  Q " + juce::String (q, 3) + ": peak during sweep "
                        + juce::String (peak, 3));

            expect (peak < 20.0f,
                    "Q " + juce::String (q, 3) + ": the sweep peaked at " + juce::String (peak, 3)
                        + ", which is running away rather than resonating");
        }
    }

    void testStabilityAcrossSampleRates()
    {
        beginTest ("The same cutoff means the same thing at any sample rate");

        for (const auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            SvfCoefficients coefficients;
            coefficients.set (1000.0f, 0.707f, rate);

            StateVariableFilter filter;
            filter.setMode (Mode::lowpass);
            filter.setCoefficients (coefficients);
            filter.reset();

            const auto settle = static_cast<int> (rate * 0.5);
            const auto measure = static_cast<int> (rate * 0.2);

            auto peak = 0.0f;

            for (int i = 0; i < settle + measure; ++i)
            {
                const auto phase = 2.0 * pi * 1000.0 * static_cast<double> (i) / rate;
                const auto output = filter.processSample (static_cast<float> (std::sin (phase)));

                if (i >= settle)
                    peak = std::max (peak, std::abs (output));
            }

            const auto db = toDecibels (peak);

            logMessage ("  " + juce::String (rate, 0) + " Hz: " + juce::String (db, 2)
                        + " dB at a 1 kHz cutoff");

            expectWithinAbsoluteError (db, -3.0f, 1.0f,
                                       "the cutoff drifted at " + juce::String (rate, 0) + " Hz");
        }
    }

    void testExtremeInput()
    {
        beginTest ("Hostile input and settings stay contained");

        StateVariableFilter filter;
        filter.setMode (Mode::lowpass);

        SvfCoefficients coefficients;

        // A cutoff above Nyquist, a negative one, and a NaN must all resolve to
        // something finite rather than to an infinite coefficient.
        for (const auto cutoff : { -100.0f, 0.0f, 1.0e9f, std::numeric_limits<float>::quiet_NaN() })
        {
            for (const auto q : { -1.0f, 0.0f, 1.0e9f, std::numeric_limits<float>::quiet_NaN() })
            {
                coefficients.set (cutoff, q, testSampleRate);

                expect (std::isfinite (coefficients.g) && std::isfinite (coefficients.k)
                            && std::isfinite (coefficients.a1) && std::isfinite (coefficients.a2)
                            && std::isfinite (coefficients.a3),
                        "a hostile cutoff or Q produced a non-finite coefficient");
            }
        }

        coefficients.set (1000.0f, 10.0f, testSampleRate);
        filter.setCoefficients (coefficients);
        filter.reset();

        for (int i = 0; i < 10000; ++i)
        {
            const auto input = (i % 2 == 0) ? 500.0f : -500.0f;
            expect (std::isfinite (filter.processSample (input)),
                    "extreme input produced a non-finite sample");
        }
    }

    void testDriveIsBoundedAndGentle()
    {
        beginTest ("Filter drive is bounded, odd-symmetric and transparent at zero");

        // Nothing at all should happen at zero drive: the caller skips the
        // stage, and the maths agrees.
        expectWithinAbsoluteError (driveGain (0.0f), 1.0f, 0.0f);

        for (const auto input : { -1000.0f, -1.5f, -0.5f, 0.0f, 0.5f, 1.5f, 1000.0f })
        {
            const auto output = softClip (input);

            expect (std::isfinite (output), "the clipper must never produce a non-finite value");
            expect (std::abs (output) <= 2.0f / 3.0f + 1.0e-6f,
                    "the clipper must never exceed its ceiling");

            // Odd symmetry is what keeps the harmonics odd, which is what makes
            // the fold-back mild enough to leave un-oversampled.
            expectWithinAbsoluteError (softClip (-input), -output, 1.0e-6f,
                                       "the clipper must be odd-symmetric");
        }

        // Small signals must pass through essentially untouched, so a drive
        // control does nothing until it is turned up.
        expectWithinAbsoluteError (softClip (0.001f), 0.001f, 1.0e-6f);
    }

    void testDriveAliasing()
    {
        beginTest ("Filter drive's fold-back is measured, and is why its range is small");

        // This test exists to hold a number that a design decision rests on.
        //
        // The -60 dBc budget the wavetable engine meets is an *oscillator*
        // budget: it applies to a stage whose entire job is to be clean. A drive
        // control's job is the opposite, and the measurements below say plainly
        // that no audible amount of it meets that budget at 48 kHz without
        // oversampling. The response was to keep the range small rather than to
        // quietly widen the budget; ADR-0033 records the alternatives and why an
        // oversampled, harder drive belongs to the FX rack in Phase 8, where one
        // stage can carry the latency instead of thirty-two voices carrying it
        // each.
        constexpr int fftOrder = 14;
        constexpr int fftSize = 1 << fftOrder;

        // Inharmonic with the sample rate, so folded partials do not land on
        // harmonics and hide there.
        const double toneHz = testSampleRate * 1129.0 / static_cast<double> (fftSize);

        const auto foldBackAt = [&] (float drive)
        {
            std::vector<float> signal (static_cast<std::size_t> (fftSize), 0.0f);

            const auto gain = driveGain (drive);
            const auto makeup = driveCompensation (drive);

            for (int i = 0; i < fftSize; ++i)
            {
                const auto phase = 2.0 * pi * toneHz * static_cast<double> (i) / testSampleRate;

                // Full scale: the worst case a voice can present to the filter.
                const auto input = 0.9f * static_cast<float> (std::sin (phase));

                signal[static_cast<std::size_t> (i)] = softClip (input * gain) * makeup;
            }

            std::vector<float> fftData (static_cast<std::size_t> (fftSize) * 2, 0.0f);

            for (int i = 0; i < fftSize; ++i)
            {
                const auto t = static_cast<double> (i) / static_cast<double> (fftSize - 1);
                const auto w = 0.35875 - 0.48829 * std::cos (2.0 * pi * t)
                             + 0.14128 * std::cos (4.0 * pi * t)
                             - 0.01168 * std::cos (6.0 * pi * t);

                fftData[static_cast<std::size_t> (i)] = signal[static_cast<std::size_t> (i)]
                                                      * static_cast<float> (w);
            }

            juce::dsp::FFT fft (fftOrder);
            fft.performFrequencyOnlyForwardTransform (fftData.data());

            auto peak = 0.0f;

            for (int bin = 0; bin < fftSize / 2; ++bin)
                peak = std::max (peak, fftData[static_cast<std::size_t> (bin)]);

            if (peak <= 0.0f)
                peak = 1.0f;

            auto worst = -160.0f;

            for (int bin = 8; bin < fftSize / 2; ++bin)
            {
                const auto frequency = static_cast<double> (bin) * testSampleRate
                                     / static_cast<double> (fftSize);

                const auto nearest = std::lround (frequency / toneHz);
                const auto harmonicBin = static_cast<int> (std::lround (
                    static_cast<double> (nearest) * toneHz / testSampleRate
                    * static_cast<double> (fftSize)));

                if (nearest >= 1 && std::abs (bin - harmonicBin) <= 12)
                    continue;

                worst = std::max (worst,
                                  juce::Decibels::gainToDecibels (
                                      fftData[static_cast<std::size_t> (bin)] / peak, -160.0f));
            }

            return worst;
        };

        auto worstOverall = -160.0f;

        for (const auto drive : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            const auto worst = foldBackAt (drive);

            logMessage ("  drive " + juce::String (drive, 2) + " (gain "
                        + juce::String (driveGain (drive), 2) + "x): fold-back "
                        + juce::String (worst, 1) + " dBc");

            worstOverall = std::max (worstOverall, worst);
        }

        // Zero drive must be exactly transparent, so a patch that does not ask
        // for drive cannot be paying for it in aliasing.
        expect (foldBackAt (0.0f) < -140.0f,
                "zero drive must not distort at all");

        // The bound this stage is held to. It is looser than the oscillators'
        // and deliberately so; the comment above says why, and the logged curve
        // above says what widening the range would cost.
        expect (worstOverall < -40.0f,
                "filter drive folded to " + juce::String (worstOverall, 1)
                    + " dBc at full drive. Past -40 dBc the range is too wide for an"
                      " un-oversampled stage and either the range or ADR-0033 needs revisiting");
    }
};

FilterTests filterTests;

} // namespace
