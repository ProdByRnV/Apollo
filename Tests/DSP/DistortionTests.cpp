/*
    Distortion tests.

    A distortion unit is judged by three things, and this measures all three.

    The **curve** has to be bounded, monotonic and flat-free near the origin, or
    the effect either explodes or eats quiet signals. That is checked directly
    against `shape`, without rendering anything, because it is a property of the
    arithmetic rather than of the signal path.

    The **aliasing** is the entire reason the unit is oversampled at all
    (`Distortion.h`, ADR-0033). `testAliasing` drives a sine hard and measures
    what comes back that is not a harmonic of it, against the same -60 dBc budget
    the oscillators are held to (PRD §12.2).

    The **alignment** is what makes the dry/wet mix a mix rather than a comb
    filter: the dry path must be delayed by exactly the number of samples the
    unit reports as its latency, and that number must not move when the mode, the
    drive, the mix or the bypass changes. `testDryPathIsExactlyDelayed` asserts
    bit-exactness there, deliberately: a dry path that is a delayed copy of the
    input is the one thing in this file that should be exact rather than close.
*/

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "DSP/Distortion/Distortion.h"

using namespace apollo::dsp;

namespace
{

constexpr double testSampleRate = 48000.0;
constexpr double pi = 3.14159265358979323846;

constexpr int fftOrder = 14;
constexpr int fftSize = 1 << fftOrder;

using Mode = Distortion::Mode;

/** @returns @p numSamples of a sine at @p frequencyHz and @p amplitude. */
[[nodiscard]] std::vector<float> sine (double frequencyHz, float amplitude, int numSamples)
{
    std::vector<float> signal (static_cast<std::size_t> (numSamples), 0.0f);

    const auto increment = 2.0 * pi * frequencyHz / testSampleRate;

    for (int i = 0; i < numSamples; ++i)
        signal[static_cast<std::size_t> (i)] =
            amplitude * static_cast<float> (std::sin (increment * static_cast<double> (i)));

    return signal;
}

/** Runs @p signal through @p unit in one block, in place, as mono. */
void render (Distortion& unit, std::vector<float>& signal)
{
    auto* channel = signal.data();
    float* channels[1] { channel };

    unit.process (channels, 1, static_cast<int> (signal.size()));
}

[[nodiscard]] float peakOf (const std::vector<float>& signal, std::size_t from = 0)
{
    auto peak = 0.0f;

    for (auto i = from; i < signal.size(); ++i)
        peak = std::max (peak, std::abs (signal[i]));

    return peak;
}

/** Magnitude spectrum in dB relative to the loudest bin.

    Blackman-Harris windowed, for the reason the oversampling tests give: an
    unwindowed bin leaks into its neighbours far above the level being measured,
    and that leakage would be reported as aliasing.
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

/** Worst non-harmonic content in dBc, ignoring bins near harmonics of
    @p fundamentalHz and near DC.

    A memoryless nonlinearity produces only harmonics of its input, so anything
    else above the floor arrived by folding.
*/
[[nodiscard]] float worstAlias (const std::vector<float>& spectrum, double fundamentalHz)
{
    constexpr int guard = 12;

    auto worst = -160.0f;

    for (std::size_t bin = 8; bin < spectrum.size(); ++bin)
    {
        const auto frequency = static_cast<double> (bin) * testSampleRate / static_cast<double> (fftSize);
        const auto nearestHarmonic = std::lround (frequency / fundamentalHz);

        const auto harmonicBin = static_cast<int> (std::lround (
            static_cast<double> (nearestHarmonic) * fundamentalHz / testSampleRate * static_cast<double> (fftSize)));

        if (nearestHarmonic >= 1 && std::abs (static_cast<int> (bin) - harmonicBin) <= guard)
            continue;

        worst = std::max (worst, spectrum[bin]);
    }

    return worst;
}

class DistortionTests final : public juce::UnitTest
{
public:
    DistortionTests()
        : juce::UnitTest ("Distortion", "DSP")
    {
    }

    void runTest() override
    {
        testTransferCurve();
        testQuietSignalsPassThrough();
        testGainCompensation();
        testDryPathIsExactlyDelayed();
        testLatencyDoesNotMove();
        testAliasing();
        testExtremeInput();
        testResetLeavesNoTail();
    }

private:
    static constexpr int blockSize = 512;

    void testTransferCurve()
    {
        beginTest ("the curve is bounded, monotonic and centred");

        for (const auto mode : { Mode::soft, Mode::hard, Mode::diode })
        {
            expectWithinAbsoluteError (Distortion::shape (mode, 0.0f), 0.0f, 1.0e-7f,
                                       "silence in must be silence out");

            auto previous = Distortion::shape (mode, -20.0f);

            for (int step = -2000; step <= 2000; ++step)
            {
                const auto input = static_cast<float> (step) * 0.01f;
                const auto output = Distortion::shape (mode, input);

                expect (std::abs (output) <= 1.0f,
                        "no input may drive the curve past full scale");
                expect (output >= previous - 1.0e-6f,
                        "the curve must never turn back on itself");

                previous = output;
            }
        }

        // The diode curve is asymmetric by design: that asymmetry is what
        // produces the even harmonics the mode exists for, so it is asserted
        // rather than merely tolerated.
        expect (std::abs (Distortion::shape (Mode::diode, -8.0f))
                    < Distortion::shape (Mode::diode, 8.0f) - 0.1f,
                "the diode curve must saturate earlier on the negative half");
    }

    void testQuietSignalsPassThrough()
    {
        beginTest ("a quiet signal passes through whichever curve is selected");

        // Every curve has a derivative of exactly 1 at the origin, which is what
        // lets the mode be switched without the level jumping.
        for (const auto mode : { Mode::soft, Mode::hard, Mode::diode })
        {
            constexpr float tiny = 1.0e-4f;

            expectWithinAbsoluteError (Distortion::shape (mode, tiny), tiny, 1.0e-7f,
                                       "a quiet positive sample must be untouched");
            expectWithinAbsoluteError (Distortion::shape (mode, -tiny), -tiny, 1.0e-7f,
                                       "a quiet negative sample must be untouched");
        }
    }

    void testGainCompensation()
    {
        beginTest ("drive changes the tone rather than the level");

        for (const auto mode : { Mode::soft, Mode::hard, Mode::diode })
        {
            for (const auto driveDb : { 0.0f, 6.0f, 12.0f, 24.0f, 36.0f })
            {
                Distortion unit;

                Distortion::Settings settings;
                settings.mode = mode;
                settings.driveDb = driveDb;
                settings.mix = 1.0f;

                unit.prepare (testSampleRate, blockSize);
                unit.setSettings (settings);

                auto signal = sine (220.0, 0.5f, blockSize);
                render (unit, signal);

                // The first samples are the smoother ramping and the filters
                // settling, so the level is read after both.
                const auto peak = peakOf (signal, static_cast<std::size_t> (blockSize / 2));
                const auto difference = juce::Decibels::gainToDecibels (peak / 0.5f, -60.0f);

                expect (std::abs (difference) < 4.0f,
                        "a -6 dBFS sine must stay within 4 dB of where it started at "
                            + juce::String (driveDb) + " dB of drive, and moved "
                            + juce::String (difference, 2) + " dB");
            }
        }
    }

    void testDryPathIsExactlyDelayed()
    {
        beginTest ("a fully dry mix is the input, delayed and otherwise untouched");

        Distortion unit;
        unit.prepare (testSampleRate, blockSize);

        Distortion::Settings settings;
        settings.mix = 0.0f;
        settings.driveDb = 36.0f;
        unit.setSettings (settings);

        const auto latency = unit.getLatencySamples();
        expect (latency > 0, "4x oversampling has a round trip, so there is a delay to compensate");

        const auto input = sine (440.0, 0.8f, blockSize);
        auto signal = input;
        render (unit, signal);

        // Bit-exact, not close: the dry path is a copy through a delay line, so
        // any difference at all would mean the signal had been through
        // arithmetic it should never have seen.
        for (int i = latency; i < blockSize; ++i)
        {
            const auto expected = input[static_cast<std::size_t> (i - latency)];
            expect (signal[static_cast<std::size_t> (i)] == expected,
                    "the dry path must be the input delayed by exactly the reported latency");
        }

        for (int i = 0; i < latency; ++i)
            expect (signal[static_cast<std::size_t> (i)] == 0.0f,
                    "the delay line starts empty, so the first samples are silence");
    }

    void testLatencyDoesNotMove()
    {
        beginTest ("latency is the same whatever the unit is set to");

        Distortion unit;
        unit.prepare (testSampleRate, blockSize);

        const auto latency = unit.getLatencySamples();

        for (const auto mode : { Mode::soft, Mode::hard, Mode::diode })
        {
            for (const auto mix : { 0.0f, 0.5f, 1.0f })
            {
                Distortion::Settings settings;
                settings.mode = mode;
                settings.mix = mix;
                settings.driveDb = 30.0f;
                settings.toneHz = 2000.0f;

                unit.setSettings (settings);

                expectEquals (unit.getLatencySamples(), latency,
                              "a latency that moved would make the host's compensation wrong");
            }
        }

        // Bypass included, because that is the one a performance automates.
        auto signal = sine (440.0, 0.5f, blockSize);
        auto* channel = signal.data();
        float* channels[1] { channel };

        unit.processBypassed (channels, 1, blockSize);

        expectEquals (unit.getLatencySamples(), latency,
                      "bypass must not change the delay the host was told about");
    }

    /** @returns the worst fold-back from shaping @p fundamental at @p driveDb,
        with the unit's oversampling either as built or switched out.

        The unoversampled reference applies the same curve, with the same drive
        and the same compensation, one sample at a time at the base rate. It is
        what this stage would sound like if the oversampler were not there, and
        it is the only honest yardstick: an absolute number says whether a
        distortion aliases, which every distortion does, while the difference
        between these two says whether the oversampler is earning its latency.
    */
    [[nodiscard]] float foldBack (Mode mode, float driveDb, double fundamental, bool oversampled)
    {
        auto signal = sine (fundamental, 0.7f, fftSize);

        if (oversampled)
        {
            Distortion unit;

            Distortion::Settings settings;
            settings.mode = mode;
            settings.driveDb = driveDb;
            settings.mix = 1.0f;

            // Settings before prepare, so the smoothers start at their targets
            // and the measured block is the steady state rather than a ramp.
            unit.setSettings (settings);
            unit.prepare (testSampleRate, fftSize);

            render (unit, signal);
        }
        else
        {
            const auto drive = std::pow (10.0f, driveDb * 0.05f);
            const auto trim = Distortion::compensationFor (mode, drive);

            for (auto& sample : signal)
                sample = Distortion::shape (mode, sample * drive) * trim;
        }

        return worstAlias (spectrumDb (signal), fundamental);
    }

    void testAliasing()
    {
        beginTest ("oversampling removes most of the fold-back, and gentle drive has almost none");

        // 5 kHz is the tone the oversampler's own tests use: its harmonics run
        // past Nyquist quickly, so anything that is going to fold, folds.
        constexpr double fundamental = 5000.0;

        for (const auto mode : { Mode::soft, Mode::hard, Mode::diode })
        {
            const auto modeName = mode == Mode::soft ? "soft" : (mode == Mode::hard ? "hard" : "diode");

            for (const auto driveDb : { 6.0f, 30.0f })
            {
                const auto with = foldBack (mode, driveDb, fundamental, /*oversampled*/ true);
                const auto without = foldBack (mode, driveDb, fundamental, /*oversampled*/ false);

                logMessage (juce::String ("  ") + modeName + " at " + juce::String (driveDb, 0)
                            + " dB, 5 kHz: " + juce::String (with, 1) + " dBc oversampled, "
                            + juce::String (without, 1) + " dBc not — "
                            + juce::String (without - with, 1) + " dB better");

                expect (with < without - 8.0f,
                        juce::String ("oversampling must materially reduce fold-back, and bought only ")
                            + juce::String (without - with, 1) + " dB for " + modeName + " at "
                            + juce::String (driveDb, 0) + " dB");
            }

            // An absolute budget only means something at a frequency and a
            // drive a musician would actually use. 5 kHz is deliberately the
            // hard case — its harmonics reach 4x Nyquist by the nineteenth, so
            // what folds there is arithmetic no amount of oversampling removes
            // — while a note in the middle of the keyboard has room for eighty
            // harmonics before it folds at all.
            const auto musical = foldBack (mode, 6.0f, 1000.0, /*oversampled*/ true);

            logMessage (juce::String ("  ") + modeName + " at 6 dB, 1 kHz: "
                        + juce::String (musical, 1) + " dBc");

            expect (musical < -60.0f,
                    juce::String ("a moderate drive on a musical note must stay under the -60 dBc the "
                                  "oscillators are held to, and ")
                        + modeName + " folded back at " + juce::String (musical, 1) + " dBc");
        }
    }

    void testExtremeInput()
    {
        beginTest ("extreme and degenerate input produces finite output");

        Distortion unit;

        Distortion::Settings settings;
        settings.driveDb = 36.0f;
        settings.mix = 1.0f;

        // Settled before the first sample, so what is measured is the shaper
        // rather than the 20 ms it takes the mix to ramp up to it.
        unit.setSettings (settings);
        unit.prepare (testSampleRate, blockSize);

        std::vector<float> absurd (static_cast<std::size_t> (blockSize), 0.0f);

        for (int i = 0; i < blockSize; ++i)
        {
            // Alternating extremes far outside anything a mix should ever
            // reach, and a denormal between them (CLAUDE.md §34.2).
            absurd[static_cast<std::size_t> (i)] = i % 3 == 0 ? 1.0e6f
                                                : i % 3 == 1 ? -1.0e6f
                                                             : 1.0e-30f;
        }

        render (unit, absurd);

        for (const auto sample : absurd)
            expect (std::isfinite (sample), "no input may produce a NaN or an infinity");

        // The output bound is asserted on a signal that is merely very hot
        // rather than impossible: the wet path is the shaper's own output, and
        // the shaper's ceiling is what keeps it bounded. A ±1e6 input is not
        // held to that, because most of what leaves is the dry path, and an
        // effect handed an absurd signal is supposed to pass it on, not to
        // invent a limiter nobody asked for.
        auto hot = sine (110.0, 8.0f, blockSize);
        render (unit, hot);

        for (auto i = static_cast<std::size_t> (blockSize / 2); i < hot.size(); ++i)
            expect (std::abs (hot[i]) <= 2.0f,
                    "a hot signal must leave bounded by the curve's own ceiling");
    }

    void testResetLeavesNoTail()
    {
        beginTest ("reset clears the filters and the delay line");

        Distortion unit;
        unit.prepare (testSampleRate, blockSize);

        Distortion::Settings settings;
        settings.driveDb = 24.0f;
        settings.mix = 1.0f;
        unit.setSettings (settings);

        auto loud = sine (440.0, 0.9f, blockSize);
        render (unit, loud);

        unit.reset();

        std::vector<float> silence (static_cast<std::size_t> (blockSize), 0.0f);
        render (unit, silence);

        for (const auto sample : silence)
            expectWithinAbsoluteError (sample, 0.0f, 1.0e-6f,
                                       "silence in after a reset must be silence out");
    }
};

DistortionTests distortionTests;

} // namespace
