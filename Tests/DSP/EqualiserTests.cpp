/*
    Parametric equaliser tests.

    An equaliser makes one kind of promise — "at this frequency you will get this
    many decibels" — and it makes it seven times over in eight different shapes.
    Nearly every test here is therefore the same experiment: drive a settled
    filter with a steady tone at a known frequency, measure the gain with a
    single-bin correlation, and hold it against the number on the dial.

    **The drawn curve and the heard curve are the same curve.**
    `testCurveMatchesTheRender` is the test the interface depends on: the display
    in 8e-ii draws `magnitudeDbAt`, and if that arithmetic disagreed with what
    the filter actually does then the picture would be a decoration rather than
    an instrument. It is checked across every shape at fifteen frequencies.

    **Transparent means bit-transparent.** A band at 0 dB, a band switched off
    and a muted band are all the exact identity, and `testDefaultIsTransparent`
    compares sample against sample rather than within a tolerance. This is what
    justifies leaving the equaliser in the chain and what makes skipping those
    bands an optimisation rather than an approximation.

    **Order is instances, and instances multiply.** Fruity Parametric EQ 2
    describes its slope control as the number of instances of the filter, so a
    +4 dB bell at order 3 is +12 dB. `testOrderMultipliesGain` and
    `testSlopeIsTwelveDecibelsPerInstance` pin both halves of that down, because
    an EQ that quietly normalised the gain when the slope changed would be an EQ
    whose displayed gain is not its gain.

    **Stability is checked where it can actually fail.** Not by listening for
    explosions, but by reading the pole positions out of the coefficients across
    every type, the whole frequency range, both bandwidth extremes and four
    sample rates — 1,344 designs, each of which must land inside the unit circle.

    **Coming back in must not bring the past with it.** The reverb's bypass bug
    in 8c was old audio pouring out of a filter that had been left full;
    `testUnmutingDoesNotReplayThePast` is the same trap set for the equaliser.
*/

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "DSP/EQ/Equaliser.h"

using namespace apollo::dsp;

namespace
{

constexpr double testSampleRate = 48000.0;
constexpr double pi = 3.14159265358979323846;
constexpr int blockSize = 256;

using Type = EqualiserBand::Type;

/** Every shape a band can take, off included. */
constexpr std::array<Type, 8> allTypes {
    Type::off, Type::lowPass, Type::bandPass, Type::highPass,
    Type::notch, Type::lowShelf, Type::peaking, Type::highShelf
};

/** Settings with one band configured and the other six left transparent. */
[[nodiscard]] Equaliser::Settings withBand (int index,
                                            Type type,
                                            float frequencyHz,
                                            float gainDb = 0.0f,
                                            float bandwidthOctaves = 1.0f,
                                            int order = 1)
{
    auto settings = Equaliser::defaultSettings();

    auto& band = settings.bands[static_cast<std::size_t> (index)];

    band.type = type;
    band.frequencyHz = frequencyHz;
    band.gainDb = gainDb;
    band.bandwidthOctaves = bandwidthOctaves;
    band.order = order;

    return settings;
}

/** Runs @p signal through @p equaliser in place, in blocks. */
void render (Equaliser& equaliser, std::vector<float>& signal)
{
    auto* data = signal.data();
    const auto total = static_cast<int> (signal.size());

    for (auto offset = 0; offset < total; offset += blockSize)
    {
        float* channels[1] { data + offset };
        equaliser.process (channels, 1, std::min (blockSize, total - offset));
    }
}

/** @returns the frequency at which @p band is boosting most, right now, found
    by scanning the curve it is currently applying on a fine logarithmic grid.

    Where a *moving* band has reached cannot be read from `getSettings`, which
    returns the target it is travelling towards. It has to be measured from the
    response, which is also the only version of it a listener hears.
*/
[[nodiscard]] double peakFrequencyHz (const EqualiserBand& band)
{
    constexpr auto steps = 2000;

    const auto lowest = std::log (20.0);
    const auto highest = std::log (20000.0);

    auto bestHz = 20.0;
    auto bestDb = -1.0e6;

    for (auto i = 0; i <= steps; ++i)
    {
        const auto hz = std::exp (lowest + (highest - lowest) * i / steps);
        const auto db = band.magnitudeDbAt (hz);

        if (db > bestDb)
        {
            bestDb = db;
            bestHz = hz;
        }
    }

    return bestHz;
}

/** Measures the gain a settled equaliser applies at one frequency, in decibels.

    A single-bin correlation rather than an RMS ratio, and deliberately: RMS
    would also count the filter's own ringing and any harmonic the arithmetic
    produced, whereas correlating against the exact tone that was sent measures
    the one thing being asked about. The analysis window is a whole number of
    cycles, so the bin is exact rather than leaking into its neighbours.

    The warm-up is generous — a fifth of a second — because a narrow band low in
    the spectrum takes that long to stop ringing from being switched on, and a
    measurement taken during the transient would be measuring the transient.
*/
[[nodiscard]] double measureGainDb (const Equaliser::Settings& settings,
                                    double frequencyHz,
                                    double sampleRate = testSampleRate)
{
    const auto samplesPerCycle = sampleRate / frequencyHz;

    // At least sixty cycles and at least eight thousand samples, rounded to a
    // whole number of cycles so the correlation closes exactly.
    const auto cycles = std::max (60.0, std::ceil (8000.0 / samplesPerCycle));
    const auto analysis = static_cast<int> (std::llround (cycles * samplesPerCycle));
    const auto warmUp = static_cast<int> (sampleRate * 0.2);

    const auto total = warmUp + analysis;
    const auto amplitude = 0.25;
    const auto increment = 2.0 * pi * frequencyHz / sampleRate;

    std::vector<float> signal (static_cast<std::size_t> (total), 0.0f);

    for (auto i = 0; i < total; ++i)
        signal[static_cast<std::size_t> (i)] =
            static_cast<float> (amplitude * std::sin (increment * static_cast<double> (i)));

    Equaliser runner;
    runner.setSettings (settings);
    runner.prepare (sampleRate, blockSize);

    render (runner, signal);

    auto real = 0.0;
    auto imaginary = 0.0;

    for (auto i = warmUp; i < total; ++i)
    {
        const auto phase = increment * static_cast<double> (i);
        const auto value = static_cast<double> (signal[static_cast<std::size_t> (i)]);

        real += value * std::sin (phase);
        imaginary += value * std::cos (phase);
    }

    const auto scale = 2.0 / static_cast<double> (analysis);
    const auto magnitude = scale * std::sqrt (real * real + imaginary * imaginary);
    const auto gain = magnitude / amplitude;

    // The floor is far lower than any audible measurement needs, and that is
    // deliberate: four instances of a low pass thirty-two octaves into its
    // stopband is -240 dB, which is a perfectly ordinary float and an entirely
    // measurable double. A floor at -180 dB silently turned two different deep
    // attenuations into the same number and made a slope look like zero.
    return gain > 1.0e-15 ? 20.0 * std::log10 (gain) : -300.0;
}

/** Finds where a band's response peaks, by scanning the curve it reports.

    Used to ask where a smoothed band has actually travelled to, which is a
    question about the band's centre rather than about its gain at any one
    frequency — and a narrow band a third of the way through a sweep gives a
    misleading answer to the second question.
*/
[[nodiscard]] double peakFrequency (const EqualiserBand& band)
{
    constexpr auto steps = 2000;

    auto best = 20.0;
    auto bestDb = -1.0e9;

    for (auto i = 0; i <= steps; ++i)
    {
        const auto hz = 20.0 * std::pow (1000.0, static_cast<double> (i) / steps);
        const auto db = band.magnitudeDbAt (hz);

        if (db > bestDb)
        {
            bestDb = db;
            best = hz;
        }
    }

    return best;
}

/** True when a set of coefficients has both poles strictly inside the unit
    circle, which is the definition of a stable second-order section.

    The Jury test for a second-order polynomial, which is cheaper and exact
    compared with actually finding the roots: |a2| < 1 and |a1| < 1 + a2.
*/
[[nodiscard]] bool isStable (const BiquadCoefficients& c)
{
    const auto finite = std::isfinite (c.b0) && std::isfinite (c.b1) && std::isfinite (c.b2)
                     && std::isfinite (c.a1) && std::isfinite (c.a2);

    return finite && std::abs (c.a2) < 1.0 && std::abs (c.a1) < 1.0 + c.a2;
}

class EqualiserTests final : public juce::UnitTest
{
public:
    EqualiserTests()
        : juce::UnitTest ("Equaliser", "DSP")
    {
    }

    void runTest() override
    {
        testDefaultIsTransparent();
        testOffAndMuteAreTransparent();
        testPeakingGainIsTheDial();
        testCurveMatchesTheRender();
        testReferenceResponse();
        testBandwidthWidensTheBell();
        testOrderMultipliesGain();
        testSlopeIsTwelveDecibelsPerInstance();
        testShelvesReachTheirGain();
        testNotchRemovesItsFrequency();
        testBandPassIsUnityAtItsCentre();
        testShapesWithoutGainIgnoreIt();
        testBandsCombineByAddingDecibels();
        testLevelTrimIsExact();
        testStabilityAcrossTheParameterSpace();
        testSampleRateDoesNotChangeTheResponse();
        testUnmutingDoesNotReplayThePast();
        testTailBelongsOnlyToResonance();
        testDenormalsAreFlushed();
        testExtremeInputStaysFinite();
        testParameterChangesAreSmoothed();
        testSweepingStaysBounded();
        testNoLatency();
    }

private:
    //--------------------------------------------------------------------------
    // Transparency.

    void testDefaultIsTransparent()
    {
        beginTest ("a fresh equaliser passes its input through bit for bit");

        Equaliser equaliser;
        equaliser.prepare (testSampleRate, blockSize);

        std::vector<float> input (4096, 0.0f);

        for (std::size_t i = 0; i < input.size(); ++i)
            input[i] = static_cast<float> (0.7 * std::sin (0.013 * static_cast<double> (i))
                                           + 0.2 * std::sin (0.31 * static_cast<double> (i)));

        auto signal = input;
        render (equaliser, signal);

        // Bit-exact, not close. Seven bells at 0 dB are the exact identity, and
        // an equaliser that quietly re-quantised everything passing through it
        // would be a stage in the signal path rather than no stage at all.
        for (std::size_t i = 0; i < signal.size(); ++i)
            expect (signal[i] == input[i], "a transparent equaliser must not touch the signal");

        expectEquals (equaliser.getLatencySamples(), 0, "an equaliser adds no latency");
        expectEquals (equaliser.getTailSeconds(), 0.0, "a transparent equaliser has no tail");
    }

    void testOffAndMuteAreTransparent()
    {
        beginTest ("a band switched off or muted is the exact identity");

        std::vector<float> input (2048, 0.0f);

        for (std::size_t i = 0; i < input.size(); ++i)
            input[i] = static_cast<float> (0.6 * std::sin (0.05 * static_cast<double> (i)));

        // A boost large enough that failing to switch it off would be obvious.
        const auto loud = withBand (2, Type::peaking, 1000.0f, 15.0f, 0.5f, 2);

        for (const auto muted : { false, true })
        {
            auto settings = loud;

            if (muted)
                settings.bands[2].muted = true;
            else
                settings.bands[2].type = Type::off;

            Equaliser equaliser;
            equaliser.setSettings (settings);
            equaliser.prepare (testSampleRate, blockSize);

            auto signal = input;
            render (equaliser, signal);

            for (std::size_t i = 0; i < signal.size(); ++i)
                expect (signal[i] == input[i],
                        muted ? "a muted band must be the identity"
                              : "a band switched off must be the identity");
        }
    }

    //--------------------------------------------------------------------------
    // The gain is the number on the dial.

    void testPeakingGainIsTheDial()
    {
        beginTest ("a bell applies exactly the gain it was set to, at its own frequency");

        // Across the spectrum and in both directions, because the bilinear
        // transform's error grows towards Nyquist and a test that only looked at
        // 1 kHz would never see it.
        for (const auto frequency : { 40.0f, 200.0f, 1000.0f, 5000.0f, 12000.0f })
        {
            for (const auto gainDb : { -18.0f, -6.0f, 3.0f, 12.0f, 18.0f })
            {
                const auto settings = withBand (3, Type::peaking, frequency, gainDb);
                const auto measured = measureGainDb (settings, static_cast<double> (frequency));

                expectWithinAbsoluteError (measured, static_cast<double> (gainDb), 0.05,
                                           "a bell's centre gain must be the gain that was asked for");
            }
        }
    }

    void testCurveMatchesTheRender()
    {
        beginTest ("the curve the display draws is the curve the filter applies");

        // The frequencies are spread logarithmically, which is both how the
        // display is drawn and where a shape's interesting parts are.
        const std::array<double, 15> probes {
            25.0, 45.0, 80.0, 140.0, 250.0, 440.0, 800.0, 1400.0,
            2500.0, 4400.0, 6300.0, 9000.0, 12000.0, 15000.0, 17000.0
        };

        for (const auto type : allTypes)
        {
            if (type == Type::off)
                continue;

            // A gain the shapes that have one will actually use, and a bandwidth
            // wide enough that a small frequency error does not turn into a
            // large gain error on the skirt of a narrow band.
            const auto settings = withBand (2, type, 1000.0f, 9.0f, 1.5f, 2);

            for (const auto hz : probes)
            {
                const auto predicted = Equaliser::magnitudeDbAt (settings, testSampleRate, hz);
                const auto measured = measureGainDb (settings, hz);

                // A tenth of a decibel. The residual is measurement, not
                // disagreement: the correlation window is finite and the filter
                // is still ringing a little at the quietest probes.
                expectWithinAbsoluteError (measured, predicted, 0.1,
                                           "the analytic response must match the rendered one");
            }
        }
    }

    void testReferenceResponse()
    {
        beginTest ("a table of reference settings produces exactly these decibels");

        // WHAT THIS IS FOR. The interface draws the equaliser's curve in
        // TypeScript, from a transcription of the cookbook in
        // `WebUI/src/params/eqCurve.ts`, because a continuous function of
        // frequency cannot be sent as points every time a knob moves. That makes
        // two implementations of one set of formulae, and this table is the
        // contract between them: every number below was produced by the
        // TypeScript and is asserted here against the C++, so the two were equal
        // when they were written and neither can move without this failing.
        //
        // The last two rows are the reason the interface is told the sample rate
        // at all rather than assuming one. They are the same shelf at the same
        // frequency, and they differ by a whole decibel at 20 kHz — the bilinear
        // transform compresses the top of the spectrum, and how much it
        // compresses depends on where Nyquist is.
        struct Reference
        {
            Type type;
            double frequencyHz;
            double gainDb;
            double bandwidthOctaves;
            int order;
            double sampleRate;
            double probeHz;
            double expectedDb;
        };

        const std::array<Reference, 13> references { {
            { Type::peaking,   1000.0,   6.0, 1.0, 1, 48000.0,  1000.0,   6.000000 },
            { Type::peaking,   1000.0,   6.0, 1.0, 1, 48000.0,   500.0,   1.137368 },
            { Type::peaking,    250.0,  -9.0, 0.5, 1, 48000.0,   250.0,  -9.000000 },
            { Type::peaking,   1000.0,   6.0, 1.0, 3, 48000.0,  1000.0,  18.000000 },
            { Type::lowShelf,   200.0,   6.0, 1.0, 1, 48000.0,    20.0,   6.045968 },
            { Type::highShelf, 6000.0, -12.0, 1.0, 1, 48000.0, 20000.0, -12.109937 },
            { Type::lowPass,   1000.0,   0.0, 1.0, 1, 48000.0,  2000.0, -10.506912 },
            { Type::lowPass,   1000.0,   0.0, 1.0, 2, 48000.0,  2000.0, -21.013825 },
            { Type::highPass,  1000.0,   0.0, 1.0, 1, 48000.0,   500.0, -10.440628 },
            { Type::bandPass,  1000.0,   0.0, 1.0, 1, 48000.0,  1000.0,   0.000000 },
            { Type::peaking,   1000.0,   6.0, 1.0, 1, 96000.0,  1000.0,   6.000000 },
            { Type::highShelf,15000.0,   6.0, 1.0, 1, 44100.0, 20000.0,   5.598727 },
            { Type::highShelf,15000.0,   6.0, 1.0, 1, 96000.0, 20000.0,   6.621699 },
        } };

        for (const auto& reference : references)
        {
            EqualiserBand::Settings settings;

            settings.type = reference.type;
            settings.frequencyHz = static_cast<float> (reference.frequencyHz);
            settings.gainDb = static_cast<float> (reference.gainDb);
            settings.bandwidthOctaves = static_cast<float> (reference.bandwidthOctaves);
            settings.order = reference.order;

            const auto db = EqualiserBand::magnitudeDbAt (settings, reference.sampleRate,
                                                          reference.probeHz);

            // A ten-thousandth of a decibel: both sides are double precision
            // doing the same arithmetic in the same order, so the only residual
            // is the decimal places the table is written to.
            expectWithinAbsoluteError (db, reference.expectedDb, 0.0001,
                                       "the reference response must be reproduced exactly");
        }

        // The notch is left out of the table because its null is a cancellation
        // of nearly equal quantities, so its depth is the last bits of a double
        // rather than a number worth pinning. What can be pinned is that it is a
        // null at all.
        {
            EqualiserBand::Settings settings;

            settings.type = Type::notch;
            settings.frequencyHz = 1000.0f;
            settings.bandwidthOctaves = 1.0f;

            expect (EqualiserBand::magnitudeDbAt (settings, testSampleRate, 1000.0) < -80.0,
                    "a notch must null its own frequency");
        }
    }

    void testBandwidthWidensTheBell()
    {
        beginTest ("a wider bandwidth reaches further from the centre");

        constexpr auto centre = 1000.0f;
        constexpr auto gain = 12.0f;

        // One octave away, where a narrow bell has already given up and a wide
        // one has not.
        const auto narrow = measureGainDb (withBand (2, Type::peaking, centre, gain, 0.4f), 2000.0);
        const auto wide = measureGainDb (withBand (2, Type::peaking, centre, gain, 3.0f), 2000.0);

        expect (wide > narrow + 4.0,
                "a three-octave bell must still be lifting an octave out, where a "
                "0.4-octave one has fallen away");

        // Both must still be exactly right at the centre: widening a band
        // changes where it reaches, never how much it does at its own frequency.
        for (const auto bandwidth : { 0.4f, 3.0f })
            expectWithinAbsoluteError (
                measureGainDb (withBand (2, Type::peaking, centre, gain, bandwidth), centre),
                static_cast<double> (gain), 0.05,
                "bandwidth must not change the gain at the centre frequency");
    }

    void testOrderMultipliesGain()
    {
        beginTest ("order is instances, so a bell's gain applies that many times");

        constexpr auto centre = 1000.0f;
        constexpr auto gainDb = 4.0f;

        for (auto order = 1; order <= EqualiserBand::maxOrder; ++order)
        {
            const auto settings = withBand (2, Type::peaking, centre, gainDb, 1.0f, order);
            const auto measured = measureGainDb (settings, centre);

            expectWithinAbsoluteError (measured, static_cast<double> (gainDb * order), 0.05,
                                       "N instances of a +4 dB bell must give +4N dB");
        }
    }

    void testSlopeIsTwelveDecibelsPerInstance()
    {
        beginTest ("a pass filter loses twelve decibels per octave per instance");

        // Both the cutoff and the sample rate here are chosen rather than
        // convenient, and the first version of this test got both wrong.
        //
        // "Twelve decibels per octave" describes the analogue prototype's
        // *asymptote*. Two octaves out from the corner the real response is
        // still 0.4 dB per instance steeper than that, because it has not
        // finished turning yet — so the probes are sixteen and thirty-two times
        // the cutoff, where it has.
        //
        // And the bilinear transform that produces these coefficients adds a
        // zero at Nyquist, which pulls the response down harder the closer to
        // Nyquist the probe sits (Biquad.h). Measured at 4 and 8 kHz at 48 kHz,
        // one instance comes out at 13.6 dB per octave — real behaviour, not an
        // error. A 100 Hz cutoff at 192 kHz puts both probes below a sixtieth of
        // the sample rate, where that contribution is under a hundredth of a
        // decibel.
        constexpr auto cutoff = 100.0f;
        constexpr auto measurementRate = 192000.0;

        for (auto order = 1; order <= EqualiserBand::maxOrder; ++order)
        {
            const auto settings = withBand (2, Type::lowPass, cutoff, 0.0f, 1.0f, order);

            const auto near = measureGainDb (settings, 16.0 * cutoff, measurementRate);
            const auto far = measureGainDb (settings, 32.0 * cutoff, measurementRate);

            expectWithinAbsoluteError (near - far, 12.0 * order, 0.5,
                                       "each instance of a low pass must contribute "
                                       "twelve decibels per octave");

            // And at an ordinary sample rate the filter must fall at least that
            // fast. The warp only ever steepens a low pass, so this is the claim
            // that holds everywhere, while the figure above is the one that needs
            // a measurement taken where the warp is not.
            const auto ordinaryRate = measureGainDb (settings, 16.0 * cutoff)
                                    - measureGainDb (settings, 32.0 * cutoff);

            expect (ordinaryRate >= 12.0 * order - 0.5,
                    "a low pass must never fall more slowly than its stated slope");
        }
    }

    void testShelvesReachTheirGain()
    {
        beginTest ("a shelf arrives at its full gain on its own side and at nothing on the other");

        constexpr auto corner = 1000.0f;
        constexpr auto gainDb = 9.0f;

        const auto low = withBand (2, Type::lowShelf, corner, gainDb, 1.0f);
        const auto high = withBand (2, Type::highShelf, corner, gainDb, 1.0f);

        expectWithinAbsoluteError (measureGainDb (low, 40.0), static_cast<double> (gainDb), 0.3,
                                   "a low shelf must reach its gain well below the corner");
        expectWithinAbsoluteError (measureGainDb (low, 16000.0), 0.0, 0.3,
                                   "a low shelf must do nothing well above the corner");

        expectWithinAbsoluteError (measureGainDb (high, 16000.0), static_cast<double> (gainDb), 0.5,
                                   "a high shelf must reach its gain well above the corner");
        expectWithinAbsoluteError (measureGainDb (high, 40.0), 0.0, 0.3,
                                   "a high shelf must do nothing well below the corner");

        // Halfway up the shelf at the corner itself, which is what makes a shelf
        // a shelf rather than a step.
        expectWithinAbsoluteError (measureGainDb (low, corner), static_cast<double> (gainDb) * 0.5, 0.3,
                                   "a shelf must be at half its gain at its corner frequency");
    }

    void testNotchRemovesItsFrequency()
    {
        beginTest ("a notch removes its own frequency and leaves its neighbours alone");

        const auto settings = withBand (2, Type::notch, 1000.0f, 0.0f, 0.3f);

        expect (measureGainDb (settings, 1000.0) < -40.0,
                "a notch must take out the frequency it is placed on");

        expectWithinAbsoluteError (measureGainDb (settings, 250.0), 0.0, 0.3,
                                   "two octaves below a narrow notch must be untouched");
        expectWithinAbsoluteError (measureGainDb (settings, 4000.0), 0.0, 0.3,
                                   "two octaves above a narrow notch must be untouched");
    }

    void testBandPassIsUnityAtItsCentre()
    {
        beginTest ("a band pass is unity at its centre whatever its width");

        // The constant-peak-gain form, which is the one an equaliser wants: the
        // alternative would make a narrow band quieter than a wide one at its
        // own frequency, so narrowing it would also be turning it down.
        for (const auto bandwidth : { 0.2f, 1.0f, 4.0f })
            expectWithinAbsoluteError (
                measureGainDb (withBand (2, Type::bandPass, 1000.0f, 0.0f, bandwidth), 1000.0),
                0.0, 0.05,
                "a band pass must pass its centre frequency at unity");

        const auto settings = withBand (2, Type::bandPass, 1000.0f, 0.0f, 1.0f);

        expect (measureGainDb (settings, 125.0) < -15.0, "three octaves below must be well down");
        expect (measureGainDb (settings, 8000.0) < -15.0, "three octaves above must be well down");
    }

    void testShapesWithoutGainIgnoreIt()
    {
        beginTest ("the four shapes with no gain control ignore the gain parameter");

        // The reference disables its fader for exactly these four, and a value
        // left behind by a previous shape must not leak into them.
        for (const auto type : { Type::lowPass, Type::bandPass, Type::highPass, Type::notch })
        {
            const auto quiet = withBand (2, type, 1000.0f, 0.0f, 1.0f);
            const auto loud = withBand (2, type, 1000.0f, 18.0f, 1.0f);

            for (const auto hz : { 200.0, 1000.0, 5000.0 })
                expectWithinAbsoluteError (measureGainDb (loud, hz), measureGainDb (quiet, hz), 1.0e-6,
                                           "a shape without a gain control must ignore the gain");
        }
    }

    void testBandsCombineByAddingDecibels()
    {
        beginTest ("bands in series add in decibels");

        auto settings = Equaliser::defaultSettings();

        settings.bands[0].type = Type::lowShelf;
        settings.bands[0].frequencyHz = 120.0f;
        settings.bands[0].gainDb = 6.0f;

        settings.bands[3].type = Type::peaking;
        settings.bands[3].frequencyHz = 1000.0f;
        settings.bands[3].gainDb = -8.0f;
        settings.bands[3].bandwidthOctaves = 1.0f;

        settings.bands[6].type = Type::highShelf;
        settings.bands[6].frequencyHz = 8000.0f;
        settings.bands[6].gainDb = 4.0f;

        // Three bands at once, each doing something different, and the whole
        // curve still has to be the sum. This is the property that lets the
        // display draw one line instead of seven.
        for (const auto hz : { 50.0, 120.0, 400.0, 1000.0, 3000.0, 8000.0, 15000.0 })
            expectWithinAbsoluteError (measureGainDb (settings, hz),
                                       Equaliser::magnitudeDbAt (settings, testSampleRate, hz),
                                       0.1,
                                       "three bands in series must sum in decibels");
    }

    void testLevelTrimIsExact()
    {
        beginTest ("the output trim applies exactly the gain it names");

        for (const auto levelDb : { -18.0f, -6.0f, 0.0f, 7.5f, 18.0f })
        {
            auto settings = Equaliser::defaultSettings();
            settings.levelDb = levelDb;

            expectWithinAbsoluteError (measureGainDb (settings, 1000.0),
                                       static_cast<double> (levelDb), 0.01,
                                       "the trim must apply its stated gain");
        }

        // And at 0 dB it must be skipped entirely rather than multiplied by one,
        // which is what keeps an untouched equaliser bit-transparent.
        auto settings = Equaliser::defaultSettings();
        settings.levelDb = 0.0f;

        Equaliser equaliser;
        equaliser.setSettings (settings);
        equaliser.prepare (testSampleRate, blockSize);

        std::vector<float> input (1024, 0.0f);

        for (std::size_t i = 0; i < input.size(); ++i)
            input[i] = static_cast<float> (0.3333333 * std::sin (0.07 * static_cast<double> (i)));

        auto signal = input;
        render (equaliser, signal);

        for (std::size_t i = 0; i < signal.size(); ++i)
            expect (signal[i] == input[i], "a trim at unity must not touch the signal");
    }

    //--------------------------------------------------------------------------
    // Stability and robustness.

    void testStabilityAcrossTheParameterSpace()
    {
        beginTest ("every design in the whole parameter space has both poles inside the unit circle");

        // Read out of the coefficients rather than inferred from a render:
        // an unstable filter can take seconds to become audible, and a test that
        // waited for it to be audible would be a test that sometimes passed.
        const std::array<double, 4> sampleRates { 44100.0, 48000.0, 96000.0, 192000.0 };

        const std::array<float, 12> frequencies {
            20.0f, 25.0f, 40.0f, 80.0f, 200.0f, 500.0f,
            1000.0f, 3000.0f, 8000.0f, 15000.0f, 19000.0f, 20000.0f
        };

        const std::array<float, 4> bandwidths {
            EqualiserBand::minimumBandwidthOctaves, 0.25f, 1.0f,
            EqualiserBand::maximumBandwidthOctaves
        };

        const std::array<float, 3> gains { -EqualiserBand::maximumGainDb, 0.0f,
                                           EqualiserBand::maximumGainDb };

        auto designs = 0;

        for (const auto sampleRate : sampleRates)
            for (const auto type : allTypes)
                for (const auto frequency : frequencies)
                    for (const auto bandwidth : bandwidths)
                        for (const auto gain : gains)
                        {
                            EqualiserBand::Settings settings;

                            settings.type = type;
                            settings.frequencyHz = frequency;
                            settings.gainDb = gain;
                            settings.bandwidthOctaves = bandwidth;

                            ++designs;

                            expect (isStable (EqualiserBand::design (settings, sampleRate)),
                                    "every design must be stable");
                        }

        expect (designs == 4 * 8 * 12 * 4 * 3, "the sweep must cover the whole grid");

        // And the values a corrupt document could contain, which the registry's
        // ranges would never produce but a preset from somewhere else might.
        for (const auto frequency : { -1.0f, 0.0f, 1.0e9f })
            for (const auto bandwidth : { -1.0f, 0.0f, 1.0e6f })
            {
                EqualiserBand::Settings settings;

                settings.type = Type::peaking;
                settings.frequencyHz = frequency;
                settings.gainDb = 1000.0f;
                settings.bandwidthOctaves = bandwidth;

                expect (isStable (EqualiserBand::design (settings, testSampleRate)),
                        "a setting no control can produce must still design a stable filter");
            }
    }

    void testSampleRateDoesNotChangeTheResponse()
    {
        beginTest ("the same settings sound the same at every sample rate");

        const auto settings = withBand (2, Type::peaking, 1000.0f, 10.0f, 1.0f);

        for (const auto sampleRate : { 44100.0, 96000.0, 192000.0 })
        {
            expectWithinAbsoluteError (measureGainDb (settings, 1000.0, sampleRate),
                                       measureGainDb (settings, 1000.0, testSampleRate),
                                       0.05,
                                       "a band's own frequency must give the same gain everywhere");

            // Well below Nyquist at every rate, so this is the shape rather than
            // the warp. The warp itself is real and is documented in Biquad.h:
            // a band near the top of the band lands slightly low at 44.1 kHz and
            // almost exactly right at 192 kHz, which is why this probe is at
            // 2 kHz rather than at 18 kHz.
            expectWithinAbsoluteError (measureGainDb (settings, 2000.0, sampleRate),
                                       measureGainDb (settings, 2000.0, testSampleRate),
                                       0.1,
                                       "the skirt of the band must be the same shape everywhere");
        }
    }

    void testUnmutingDoesNotReplayThePast()
    {
        beginTest ("a band coming back in brings nothing with it");

        auto settings = withBand (2, Type::peaking, 300.0f, 18.0f, 0.3f, 4);

        Equaliser equaliser;
        equaliser.setSettings (settings);
        equaliser.prepare (testSampleRate, blockSize);

        // Fill the band's sections with something loud.
        std::vector<float> loud (static_cast<std::size_t> (blockSize) * 8, 0.0f);

        for (std::size_t i = 0; i < loud.size(); ++i)
            loud[i] = static_cast<float> (0.9 * std::sin (0.04 * static_cast<double> (i)));

        render (equaliser, loud);

        // Mute it and let silence go by. The band is now skipped, so its
        // sections keep whatever they were holding.
        settings.bands[2].muted = true;
        equaliser.setSettings (settings);

        std::vector<float> silence (static_cast<std::size_t> (blockSize) * 4, 0.0f);
        render (equaliser, silence);

        // Now bring it back with nothing at the input. If the sections were kept,
        // this is where seconds-old audio comes pouring out — the bug the reverb
        // had in 8c.
        settings.bands[2].muted = false;
        equaliser.setSettings (settings);

        std::vector<float> after (static_cast<std::size_t> (blockSize) * 8, 0.0f);
        render (equaliser, after);

        for (std::size_t i = 0; i < after.size(); ++i)
            expect (after[i] == 0.0f,
                    "silence in must be silence out, whatever the band was holding");
    }

    void testTailBelongsOnlyToResonance()
    {
        beginTest ("only a band that can ring reports a tail");

        const auto tailOf = [] (const Equaliser::Settings& settings)
        {
            Equaliser equaliser;
            equaliser.setSettings (settings);
            equaliser.prepare (testSampleRate, blockSize);

            return equaliser.getTailSeconds();
        };

        expectEquals (tailOf (Equaliser::defaultSettings()), 0.0,
                      "a transparent equaliser has nothing to ring");

        // A cut is adding damping, not resonance: there is nothing left over
        // once the input stops.
        expectEquals (tailOf (withBand (2, Type::peaking, 100.0f, -18.0f, 0.1f)), 0.0,
                      "a cut has no tail");

        // A narrow boost low in the spectrum genuinely rings, and an offline
        // render that stopped at the last sample of input would truncate it.
        const auto ringing = tailOf (withBand (2, Type::peaking, 100.0f, 18.0f, 0.1f));

        expect (ringing > 0.1, "a narrow low boost must report a tail worth waiting for");
        expect (ringing < 30.0, "the tail estimate must stay in the realm of the plausible");

        // A wide gentle boost barely rings at all, and must report far less than
        // a narrow one.
        expect (tailOf (withBand (2, Type::peaking, 100.0f, 18.0f, 4.0f)) < ringing * 0.5,
                "a wide band must report a much shorter tail than a narrow one");
    }

    void testDenormalsAreFlushed()
    {
        beginTest ("the ring reaches exactly zero rather than grinding on inaudibly");

        Equaliser equaliser;
        equaliser.setSettings (withBand (2, Type::peaking, 1000.0f, 15.0f, 0.5f, 2));
        equaliser.prepare (testSampleRate, blockSize);

        std::vector<float> loud (static_cast<std::size_t> (blockSize) * 4, 0.0f);

        for (std::size_t i = 0; i < loud.size(); ++i)
            loud[i] = static_cast<float> (0.8 * std::sin (0.13 * static_cast<double> (i)));

        render (equaliser, loud);

        // Half a second of silence, which is hundreds of times the decay of this
        // band. Exactly zero, not nearly: a denormal in a feedback path costs
        // orders of magnitude more than the sample it represents.
        std::vector<float> silence (static_cast<std::size_t> (testSampleRate * 0.5), 0.0f);
        render (equaliser, silence);

        const auto tail = silence.size() / 4;

        // This is the assertion that found a real bug rather than confirming a
        // hope. Flushing the two state words independently left a residual
        // sitting at 1e-16 for ever: the flush was zeroing whichever quadrature
        // happened to be crossing zero, which perturbed the resonator instead of
        // stopping it, and parked it in a limit cycle at the threshold. Flushing
        // the pair together is what makes "exactly zero" true (Biquad.h).
        for (auto i = silence.size() - tail; i < silence.size(); ++i)
            expect (silence[i] == 0.0f, "the filter's memory must reach exactly zero");
    }

    void testExtremeInputStaysFinite()
    {
        beginTest ("absurd input produces finite output");

        Equaliser equaliser;
        equaliser.setSettings (withBand (2, Type::peaking, 80.0f, 18.0f, 0.1f, 4));
        equaliser.prepare (testSampleRate, blockSize);

        std::vector<float> signal (static_cast<std::size_t> (blockSize) * 16, 0.0f);

        for (std::size_t i = 0; i < signal.size(); ++i)
            signal[i] = (i % 2 == 0 ? 1.0e6f : -1.0e6f);

        render (equaliser, signal);

        for (const auto value : signal)
            expect (std::isfinite (value), "an equaliser must never produce a NaN or an infinity");
    }

    void testParameterChangesAreSmoothed()
    {
        beginTest ("a band travels to a new setting rather than arriving at it");

        // The first version of this test looked for a large sample-to-sample
        // step during a frequency sweep, and it was measuring the wrong thing: a
        // resonant filter swept across the spectrum *rings*, and the ringing it
        // produces at 9 kHz has a large step between consecutive samples by
        // arithmetic rather than by fault. That is what a filter sweep sounds
        // like, not a click.
        //
        // So this asks the question directly instead. After one block the
        // response must have moved towards the new setting without reaching it,
        // and after ten time constants it must have arrived. A smoother that was
        // removed would fail the first assertion; one that was stuck would fail
        // the second.
        const auto blocksToSeconds = static_cast<double> (blockSize) / testSampleRate;

        expect (blocksToSeconds < 0.02,
                "a block must be shorter than the smoothing time for this test to mean anything");

        {
            auto settings = withBand (2, Type::peaking, 1000.0f, 1.0f, 1.0f);

            Equaliser equaliser;
            equaliser.setSettings (settings);
            equaliser.prepare (testSampleRate, blockSize);

            std::vector<float> block (static_cast<std::size_t> (blockSize), 0.0f);

            settings.bands[2].gainDb = 18.0f;
            equaliser.setSettings (settings);

            render (equaliser, block);

            const auto afterOne = equaliser.getBand (2).magnitudeDbAt (1000.0);

            expect (afterOne > 1.0, "the gain must have started moving");
            expect (afterOne < 10.0,
                    "the gain must not cover most of a seventeen-decibel jump in one block");

            for (auto i = 0; i < 40; ++i)
                render (equaliser, block);

            expectWithinAbsoluteError (equaliser.getBand (2).magnitudeDbAt (1000.0), 18.0, 0.05,
                                       "the gain must arrive exactly, not asymptotically for ever");
        }

        {
            auto settings = withBand (2, Type::peaking, 200.0f, 18.0f, 0.5f);

            Equaliser equaliser;
            equaliser.setSettings (settings);
            equaliser.prepare (testSampleRate, blockSize);

            std::vector<float> block (static_cast<std::size_t> (blockSize), 0.0f);

            settings.bands[2].frequencyHz = 9000.0f;
            equaliser.setSettings (settings);

            render (equaliser, block);

            // Measured along the logarithmic axis the band actually travels,
            // rather than by asking what it is still doing at its old centre:
            // a band half an octave wide stops boosting its old frequency as
            // soon as it moves at all, so that measurement reports "jumped" for
            // any smoother that is working. What matters is the fraction of the
            // distance one block covers — some of it, and not most of it.
            const auto travelled =
                (std::log (peakFrequencyHz (equaliser.getBand (2))) - std::log (200.0))
                / (std::log (9000.0) - std::log (200.0));

            expect (travelled > 0.05, "the band must have started moving");
            expect (travelled < 0.5,
                    "the band must not have covered most of the distance in one block");
            expect (equaliser.getBand (2).magnitudeDbAt (9000.0) < 1.0,
                    "the band must not have arrived at its new frequency in one block");

            for (auto i = 0; i < 40; ++i)
                render (equaliser, block);

            expectWithinAbsoluteError (equaliser.getBand (2).magnitudeDbAt (9000.0), 18.0, 0.05,
                                       "the band must settle exactly on its new frequency");
            expect (equaliser.getBand (2).magnitudeDbAt (200.0) < 0.5,
                    "and must have let go of the old one");
        }
    }

    void testSweepingStaysBounded()
    {
        beginTest ("sweeping a band across the spectrum keeps the output in range");

        // A continuously moving resonant band is the hardest thing an equaliser
        // is asked to do — the coefficients change under state that is already
        // ringing — and the property that has to hold through it is not that the
        // output is smooth (a swept resonance legitimately rings and chirps) but
        // that it stays in proportion to what the band's gain allows.
        Equaliser equaliser;
        auto settings = withBand (2, Type::peaking, 200.0f, 18.0f, 0.5f, 2);

        equaliser.setSettings (settings);
        equaliser.prepare (testSampleRate, blockSize);

        std::vector<float> signal (static_cast<std::size_t> (blockSize) * 400, 0.0f);

        const auto increment = 2.0 * pi * 300.0 / testSampleRate;

        for (std::size_t i = 0; i < signal.size(); ++i)
            signal[i] = static_cast<float> (0.25 * std::sin (increment * static_cast<double> (i)));

        auto* data = signal.data();
        const auto total = static_cast<int> (signal.size());
        auto block = 0;

        for (auto offset = 0; offset < total; offset += blockSize, ++block)
        {
            // Swept back and forth across the whole range, repeatedly, rather
            // than moved once: a smoother that mishandles a reversal would be
            // missed by a single jump.
            const auto position = 0.5 - 0.5 * std::cos (0.05 * static_cast<double> (block));

            settings.bands[2].frequencyHz =
                static_cast<float> (20.0 * std::pow (1000.0, position));

            equaliser.setSettings (settings);

            float* channels[1] { data + offset };
            equaliser.process (channels, 1, std::min (blockSize, total - offset));
        }

        // Two instances of an 18 dB bell is 36 dB, so a 0.25 input can reach
        // about 15.8. Twice that leaves generous room for the ringing a sweep
        // legitimately produces while still catching a filter that has started
        // to run away.
        for (const auto value : signal)
        {
            expect (std::isfinite (value), "a swept band must never produce a NaN");
            expect (std::abs (value) < 32.0f,
                    "a swept band must stay within reach of the gain it was given");
        }
    }

    void testNoLatency()
    {
        beginTest ("an equaliser declares and adds no latency");

        Equaliser equaliser;
        equaliser.setSettings (withBand (2, Type::peaking, 1000.0f, 12.0f));
        equaliser.prepare (testSampleRate, blockSize);

        expectEquals (equaliser.getLatencySamples(), 0, "an IIR equaliser has nothing to compensate");

        // And it really does respond on the first sample rather than declaring
        // zero and quietly buffering: an impulse must produce output immediately.
        std::vector<float> impulse (static_cast<std::size_t> (blockSize), 0.0f);
        impulse[0] = 1.0f;

        render (equaliser, impulse);

        expect (std::abs (impulse[0]) > 1.0e-6f,
                "the first sample out must depend on the first sample in");
    }
};

const EqualiserTests equaliserTests;

} // namespace
