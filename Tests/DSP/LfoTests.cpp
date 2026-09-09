/*
    LFO tests.

    An LFO is judged by whether its shape is the shape it claims, whether a cycle
    takes as long as it was told to, and whether the things that exist to prevent
    clicks — smoothing, fade-in — actually do. Those are what these measure.

    The sample-and-hold and free-running tests are the ones worth reading: both
    are about behaviour across voices rather than within one, and both are places
    where an implementation can look right in isolation and be wrong in the
    instrument.
*/

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "DSP/LFO/Lfo.h"

using namespace apollo::dsp;

namespace
{

constexpr double testSampleRate = 48000.0;

[[nodiscard]] std::vector<float> render (Lfo& lfo, int numSamples)
{
    std::vector<float> output (static_cast<std::size_t> (numSamples), 0.0f);

    for (int i = 0; i < numSamples; ++i)
        output[static_cast<std::size_t> (i)] = lfo.getNextValue();

    return output;
}

[[nodiscard]] LfoSettings settingsFor (LfoShape shape, float rateHz = 1.0f)
{
    LfoSettings settings;

    settings.shape = shape;
    settings.rateHz = rateHz;
    settings.smoothing = 0.0f;
    settings.fadeInSeconds = 0.0f;
    settings.bipolar = true;
    settings.retrigger = true;

    return settings;
}

/** @returns how many times @p values crosses zero going upward.

    Counting cycles this way rather than trusting the phase means the rate is
    measured from the output a consumer would actually see.
*/
[[nodiscard]] int countRisingZeroCrossings (const std::vector<float>& values)
{
    if (values.size() < 2)
        return 0;

    int count = 0;

    // A shape that begins exactly at zero and rising has already completed a
    // crossing at the first sample, and there is no earlier sample to detect it
    // against. Counting it explicitly is the difference between measuring the
    // rate and measuring the rate minus one cycle per window.
    if (values[0] >= 0.0f && values[1] > values[0])
        ++count;

    for (std::size_t i = 1; i < values.size(); ++i)
        if (values[i - 1] < 0.0f && values[i] >= 0.0f)
            ++count;

    return count;
}

//==============================================================================

class LfoTests final : public juce::UnitTest
{
public:
    LfoTests()
        : juce::UnitTest ("LFO", "DSP")
    {
    }

    void runTest() override
    {
        testShapesStayInRange();
        testShapeValues();
        testRateIsAccurate();
        testPhaseOffset();
        testRetriggerAndFreeRunning();
        testFadeIn();
        testSmoothing();
        testPolarity();
        testSampleAndHold();
        testStep();
        testTempoSync();
        testSampleRateIndependence();
        testExtremeSettings();
    }

private:
    [[nodiscard]] static const char* nameOf (LfoShape shape)
    {
        switch (shape)
        {
            case LfoShape::sine:          return "sine";
            case LfoShape::triangle:      return "triangle";
            case LfoShape::saw:           return "saw";
            case LfoShape::reverseSaw:    return "reverse saw";
            case LfoShape::square:        return "square";
            case LfoShape::sampleAndHold: return "sample and hold";
            case LfoShape::step:          return "step";
            default:                      return "unknown";
        }
    }

    void testShapesStayInRange()
    {
        beginTest ("Every shape stays inside its range and stays finite");

        for (const auto shape : { LfoShape::sine, LfoShape::triangle, LfoShape::saw,
                                  LfoShape::reverseSaw, LfoShape::square,
                                  LfoShape::sampleAndHold, LfoShape::step })
        {
            Lfo lfo;
            lfo.prepare (testSampleRate);
            lfo.setSettings (settingsFor (shape, 7.0f));
            lfo.noteOn (0.0);

            for (const auto value : render (lfo, static_cast<int> (testSampleRate)))
            {
                expect (std::isfinite (value),
                        juce::String (nameOf (shape)) + " produced a non-finite value");

                expect (value >= -1.0f - 1.0e-6f && value <= 1.0f + 1.0e-6f,
                        juce::String (nameOf (shape)) + " escaped its range at "
                            + juce::String (value, 6));
            }
        }
    }

    void testShapeValues()
    {
        beginTest ("Each shape has the values its name implies");

        // One cycle over exactly 1000 samples, so a quarter of the way through
        // is sample 250 and the arithmetic is exact.
        constexpr int cycleSamples = 1000;
        const auto rate = static_cast<float> (testSampleRate) / static_cast<float> (cycleSamples);

        const auto sampleShape = [this, rate] (LfoShape shape)
        {
            Lfo lfo;
            lfo.prepare (testSampleRate);
            lfo.setSettings (settingsFor (shape, rate));
            lfo.noteOn (0.0);

            return render (lfo, cycleSamples);
        };

        const auto sine = sampleShape (LfoShape::sine);
        expectWithinAbsoluteError (sine[0], 0.0f, 1.0e-4f, "a sine starts at zero");
        expectWithinAbsoluteError (sine[250], 1.0f, 1.0e-2f, "and peaks a quarter through");
        expectWithinAbsoluteError (sine[750], -1.0f, 1.0e-2f, "and troughs three quarters through");

        const auto saw = sampleShape (LfoShape::saw);
        expectWithinAbsoluteError (saw[0], -1.0f, 1.0e-3f, "a saw starts at the bottom");
        expectWithinAbsoluteError (saw[500], 0.0f, 1.0e-2f, "and is centred halfway");
        expect (saw[999] > 0.99f, "and ends at the top");

        const auto reverse = sampleShape (LfoShape::reverseSaw);
        expectWithinAbsoluteError (reverse[0], 1.0f, 1.0e-3f, "a reverse saw starts at the top");
        expect (reverse[999] < -0.99f, "and ends at the bottom");

        const auto triangle = sampleShape (LfoShape::triangle);
        expectWithinAbsoluteError (triangle[0], -1.0f, 1.0e-3f, "a triangle starts at the bottom");
        expectWithinAbsoluteError (triangle[500], 1.0f, 1.0e-2f, "peaks halfway");
        expect (triangle[999] < -0.99f, "and returns to the bottom");

        const auto square = sampleShape (LfoShape::square);
        expectWithinAbsoluteError (square[0], 1.0f, 0.0f, "a square is exactly high...");
        expectWithinAbsoluteError (square[499], 1.0f, 0.0f, "...for exactly half the cycle...");
        expectWithinAbsoluteError (square[500], -1.0f, 0.0f, "...then exactly low");
        expectWithinAbsoluteError (square[999], -1.0f, 0.0f);
    }

    void testRateIsAccurate()
    {
        beginTest ("A cycle takes as long as the rate says it should");

        for (const auto rate : { 0.5f, 1.0f, 5.0f, 20.0f })
        {
            Lfo lfo;
            lfo.prepare (testSampleRate);
            lfo.setSettings (settingsFor (LfoShape::sine, rate));
            lfo.noteOn (0.0);

            // Four seconds, so even the slowest rate completes several cycles.
            const auto values = render (lfo, static_cast<int> (testSampleRate * 4.0));
            const auto cycles = countRisingZeroCrossings (values);
            const auto measured = static_cast<float> (cycles) / 4.0f;

            logMessage ("  " + juce::String (rate, 2) + " Hz: measured "
                        + juce::String (measured, 3) + " Hz");

            expectWithinAbsoluteError (measured, rate, 0.3f,
                                       "rate drifted at " + juce::String (rate, 2) + " Hz");
        }
    }

    void testPhaseOffset()
    {
        beginTest ("A phase offset starts the shape where it says");

        auto settings = settingsFor (LfoShape::saw, 1.0f);

        for (const auto offset : { 0.0f, 0.25f, 0.5f, 0.75f })
        {
            settings.phaseOffset = offset;

            Lfo lfo;
            lfo.prepare (testSampleRate);
            lfo.setSettings (settings);
            lfo.noteOn (0.0);

            // A saw at phase p is 2p - 1.
            const auto expected = 2.0f * offset - 1.0f;

            expectWithinAbsoluteError (lfo.getNextValue(), expected, 1.0e-3f,
                                       "offset " + juce::String (offset, 2)
                                           + " did not start where it should");
        }
    }

    void testRetriggerAndFreeRunning()
    {
        beginTest ("Retrigger restarts the shape; free-running does not");

        auto settings = settingsFor (LfoShape::saw, 1.0f);

        // Retriggering: whatever the LFO was doing, a note starts it again.
        settings.retrigger = true;

        Lfo retriggering;
        retriggering.prepare (testSampleRate);
        retriggering.setSettings (settings);
        retriggering.noteOn (0.0);

        static_cast<void> (render (retriggering, static_cast<int> (testSampleRate * 0.4)));
        expect (retriggering.getCurrentValue() > -0.5f, "the setup should leave it part-way up");

        retriggering.noteOn (0.0);
        expectWithinAbsoluteError (retriggering.getNextValue(), -1.0f, 1.0e-3f,
                                   "a retriggering LFO must restart at the beginning");

        // Free-running: the note adopts the phase it is handed, which is how
        // several voices share one motion instead of each starting its own.
        settings.retrigger = false;

        Lfo freeRunning;
        freeRunning.prepare (testSampleRate);
        freeRunning.setSettings (settings);

        freeRunning.noteOn (0.75);

        // Read without advancing, so the comparison below starts both LFOs from
        // the same place. Consuming a sample here and not there was worth one
        // phase increment of difference, which is exactly what the test was
        // built to detect.
        expectWithinAbsoluteError (freeRunning.getCurrentValue(), 0.5f, 1.0e-3f,
                                   "a free-running LFO must adopt the phase it was given");

        // And two voices given the same phase must agree, which is the whole
        // point of the mode.
        Lfo other;
        other.prepare (testSampleRate);
        other.setSettings (settings);
        other.noteOn (0.75);

        const auto a = render (freeRunning, 512);
        const auto b = render (other, 512);

        for (std::size_t i = 0; i < a.size(); ++i)
            expectWithinAbsoluteError (a[i], b[i], 1.0e-6f,
                                       "two free-running LFOs sharing a phase must stay together");
    }

    void testFadeIn()
    {
        beginTest ("Fade-in widens the shape from its centre");

        auto settings = settingsFor (LfoShape::square, 10.0f);
        settings.fadeInSeconds = 0.5f;

        Lfo lfo;
        lfo.prepare (testSampleRate);
        lfo.setSettings (settings);
        lfo.noteOn (0.0);

        const auto values = render (lfo, static_cast<int> (testSampleRate * 1.0));

        // At the very start there should be almost no modulation at all.
        expect (std::abs (values[0]) < 0.05f,
                "the first sample should be near the centre, not at full depth");

        // A quarter of the way through the fade, depth should be around a
        // quarter. A square's magnitude is exactly the fade gain, which makes
        // this measurable without averaging.
        const auto quarter = std::abs (values[static_cast<std::size_t> (testSampleRate * 0.125)]);
        expectWithinAbsoluteError (quarter, 0.25f, 0.05f, "the fade should be roughly linear");

        // And past the fade it must be at full depth and stay there.
        const auto late = std::abs (values[static_cast<std::size_t> (testSampleRate * 0.9)]);
        expectWithinAbsoluteError (late, 1.0f, 1.0e-3f, "after the fade, full depth");

        // No fade means full depth immediately.
        settings.fadeInSeconds = 0.0f;

        Lfo immediate;
        immediate.prepare (testSampleRate);
        immediate.setSettings (settings);
        immediate.noteOn (0.0);

        expectWithinAbsoluteError (std::abs (immediate.getNextValue()), 1.0f, 1.0e-6f,
                                   "with no fade the first sample is already at full depth");
    }

    void testSmoothing()
    {
        beginTest ("Smoothing rounds the edges that would otherwise click");

        auto settings = settingsFor (LfoShape::square, 2.0f);

        // With no smoothing a square must be exactly square: every sample is
        // either +1 or -1, with nothing in between.
        settings.smoothing = 0.0f;

        Lfo sharp;
        sharp.prepare (testSampleRate);
        sharp.setSettings (settings);
        sharp.noteOn (0.0);

        for (const auto value : render (sharp, static_cast<int> (testSampleRate)))
            expect (std::abs (std::abs (value) - 1.0f) < 1.0e-6f,
                    "an unsmoothed square must have no intermediate values");

        // With smoothing the transition must take time. Measured as the largest
        // step between consecutive samples: that is exactly what a click is.
        settings.smoothing = 0.5f;

        Lfo smooth;
        smooth.prepare (testSampleRate);
        smooth.setSettings (settings);
        smooth.noteOn (0.0);

        const auto values = render (smooth, static_cast<int> (testSampleRate));

        auto largestStep = 0.0f;

        for (std::size_t i = 1; i < values.size(); ++i)
            largestStep = std::max (largestStep, std::abs (values[i] - values[i - 1]));

        logMessage ("  largest step with smoothing at 0.5: " + juce::String (largestStep, 6));

        expect (largestStep < 0.01f,
                "smoothing left a step of " + juce::String (largestStep, 6)
                    + ", which is still a click");
    }

    void testPolarity()
    {
        beginTest ("Polarity selects the output range");

        auto settings = settingsFor (LfoShape::sine, 5.0f);

        settings.bipolar = true;

        Lfo bipolar;
        bipolar.prepare (testSampleRate);
        bipolar.setSettings (settings);
        bipolar.noteOn (0.0);

        auto lowest = 1.0f;
        auto highest = -1.0f;

        for (const auto value : render (bipolar, static_cast<int> (testSampleRate)))
        {
            lowest = std::min (lowest, value);
            highest = std::max (highest, value);
        }

        expectWithinAbsoluteError (lowest, -1.0f, 0.01f, "bipolar must reach -1");
        expectWithinAbsoluteError (highest, 1.0f, 0.01f, "bipolar must reach +1");

        settings.bipolar = false;

        Lfo unipolar;
        unipolar.prepare (testSampleRate);
        unipolar.setSettings (settings);
        unipolar.noteOn (0.0);

        lowest = 1.0f;
        highest = 0.0f;

        for (const auto value : render (unipolar, static_cast<int> (testSampleRate)))
        {
            expect (value >= -1.0e-6f && value <= 1.0f + 1.0e-6f,
                    "unipolar escaped 0 to 1 at " + juce::String (value, 6));

            lowest = std::min (lowest, value);
            highest = std::max (highest, value);
        }

        expectWithinAbsoluteError (lowest, 0.0f, 0.01f, "unipolar must reach 0");
        expectWithinAbsoluteError (highest, 1.0f, 0.01f, "unipolar must reach 1");
    }

    void testSampleAndHold()
    {
        beginTest ("Sample and hold holds for a cycle, and differs between voices");

        auto settings = settingsFor (LfoShape::sampleAndHold, 10.0f);

        Lfo lfo;
        lfo.prepare (testSampleRate);
        lfo.setSettings (settings);
        lfo.setSeed (1u);
        lfo.noteOn (0.0);

        const auto cycleSamples = static_cast<int> (testSampleRate / 10.0);
        const auto values = render (lfo, cycleSamples * 4);

        // Within a cycle the value must not move at all.
        for (int cycle = 0; cycle < 3; ++cycle)
        {
            const auto start = static_cast<std::size_t> (cycle * cycleSamples + 2);
            const auto held = values[start];

            for (int i = 2; i < cycleSamples - 2; ++i)
                expectWithinAbsoluteError (values[start + static_cast<std::size_t> (i) - 2], held, 0.0f,
                                           "sample and hold moved within a cycle");
        }

        // And between cycles it must.
        expect (std::abs (values[static_cast<std::size_t> (cycleSamples / 2)]
                          - values[static_cast<std::size_t> (cycleSamples + cycleSamples / 2)]) > 1.0e-6f,
                "sample and hold produced the same value two cycles running");

        // Reproducible from a seed: the same seed gives the same sequence, which
        // is what lets this be tested at all.
        Lfo again;
        again.prepare (testSampleRate);
        again.setSettings (settings);
        again.setSeed (1u);
        again.noteOn (0.0);

        const auto repeat = render (again, cycleSamples * 4);

        for (std::size_t i = 0; i < values.size(); ++i)
            expectWithinAbsoluteError (repeat[i], values[i], 0.0f,
                                       "the same seed must give the same sequence");

        // Different seeds must not. Several voices holding one random value is
        // one modulation applied N times, which is not what a per-voice random
        // source is for.
        Lfo different;
        different.prepare (testSampleRate);
        different.setSettings (settings);
        different.setSeed (2u);
        different.noteOn (0.0);

        const auto other = render (different, cycleSamples * 4);

        auto anyDifference = false;

        for (std::size_t i = 0; i < values.size(); ++i)
            if (std::abs (other[i] - values[i]) > 1.0e-6f)
                anyDifference = true;

        expect (anyDifference, "two seeds produced identical sequences");
    }

    void testStep()
    {
        beginTest ("The step shape is a staircase with the number of steps asked for");

        auto settings = settingsFor (LfoShape::step, 1.0f);
        settings.stepCount = 4;

        Lfo lfo;
        lfo.prepare (testSampleRate);
        lfo.setSettings (settings);
        lfo.noteOn (0.0);

        const auto values = render (lfo, static_cast<int> (testSampleRate));

        // Collect the distinct levels.
        std::vector<float> levels;

        for (const auto value : values)
            if (levels.empty() || std::abs (levels.back() - value) > 1.0e-6f)
                levels.push_back (value);

        expectEquals (static_cast<int> (levels.size()), 4,
                      "four steps should produce four distinct levels in a cycle");

        expectWithinAbsoluteError (levels.front(), -1.0f, 1.0e-6f, "the staircase starts at the bottom");
        expectWithinAbsoluteError (levels.back(), 1.0f, 1.0e-6f, "and ends at the top");

        for (std::size_t i = 1; i < levels.size(); ++i)
            expect (levels[i] > levels[i - 1], "the staircase must climb");

        // A nonsensical step count must not divide by zero or spin.
        settings.stepCount = 1;
        lfo.setSettings (settings);
        lfo.noteOn (0.0);

        for (const auto value : render (lfo, 4096))
            expect (std::isfinite (value), "a one-step shape must still be finite");
    }

    void testTempoSync()
    {
        beginTest ("Tempo-synced rates are the right hertz, and survive a missing tempo");

        // At 120 bpm a beat is half a second, so a one-beat cycle is 2 Hz and a
        // four-beat cycle is 0.5 Hz.
        expectWithinAbsoluteError (tempoSyncedRateHz (120.0, 1.0), 2.0f, 1.0e-4f);
        expectWithinAbsoluteError (tempoSyncedRateHz (120.0, 4.0), 0.5f, 1.0e-4f);
        expectWithinAbsoluteError (tempoSyncedRateHz (120.0, 0.25), 8.0f, 1.0e-4f);

        // A third of a beat is a triplet.
        expectWithinAbsoluteError (tempoSyncedRateHz (90.0, 1.0 / 3.0), 4.5f, 1.0e-4f);

        // A host that reports nothing usable must leave the LFO running at
        // something sensible rather than stopping it (CLAUDE.md §38).
        for (const auto bpm : { 0.0, -50.0, 1.0e9,
                                static_cast<double> (std::numeric_limits<float>::quiet_NaN()) })
        {
            const auto rate = tempoSyncedRateHz (bpm, 1.0);

            expect (std::isfinite (rate) && rate > 0.0f,
                    "a hostile tempo produced " + juce::String (rate, 6));

            expectWithinAbsoluteError (rate, 2.0f, 1.0e-4f,
                                       "an unusable tempo should fall back to 120 bpm");
        }

        expect (std::isfinite (tempoSyncedRateHz (120.0, 0.0)),
                "a zero division must not produce infinity");
    }

    void testSampleRateIndependence()
    {
        beginTest ("A cycle takes the same time at any sample rate");

        for (const auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            Lfo lfo;
            lfo.prepare (rate);
            lfo.setSettings (settingsFor (LfoShape::sine, 4.0f));
            lfo.noteOn (0.0);

            std::vector<float> values (static_cast<std::size_t> (rate * 2.0), 0.0f);

            for (auto& value : values)
                value = lfo.getNextValue();

            const auto measured = static_cast<float> (countRisingZeroCrossings (values)) / 2.0f;

            logMessage ("  " + juce::String (rate, 0) + " Hz: measured "
                        + juce::String (measured, 3) + " Hz");

            expectWithinAbsoluteError (measured, 4.0f, 0.1f,
                                       "rate drifted at " + juce::String (rate, 0) + " Hz");
        }
    }

    void testExtremeSettings()
    {
        beginTest ("Hostile settings stay bounded and finite");

        LfoSettings settings;

        settings.shape = LfoShape::sine;
        settings.rateHz = std::numeric_limits<float>::quiet_NaN();
        settings.phaseOffset = 1.0e9f;
        settings.fadeInSeconds = -5.0f;
        settings.smoothing = 40.0f;
        settings.stepCount = -3;
        settings.bipolar = true;

        Lfo lfo;
        lfo.prepare (testSampleRate);
        lfo.setSettings (settings);
        lfo.noteOn (std::numeric_limits<double>::infinity());

        for (const auto value : render (lfo, 8192))
        {
            expect (std::isfinite (value), "a hostile setting produced a non-finite value");
            expect (value >= -1.0f - 1.0e-6f && value <= 1.0f + 1.0e-6f,
                    "a hostile setting escaped the range at " + juce::String (value, 6));
        }

        expect (lfo.getPhase() >= 0.0 && lfo.getPhase() < 1.0,
                "the phase must stay inside its range");

        // A rate far above anything musical must still be finite and bounded.
        settings.rateHz = 1.0e9f;
        lfo.setSettings (settings);

        for (const auto value : render (lfo, 8192))
            expect (std::isfinite (value) && std::abs (value) <= 1.0f + 1.0e-6f);

        lfo.reset();
        expect (lfo.getPhase() >= 0.0 && lfo.getPhase() < 1.0);
    }
};

LfoTests lfoTests;

} // namespace
