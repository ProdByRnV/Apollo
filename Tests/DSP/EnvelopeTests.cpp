/*
    DAHDSR envelope tests.

    An envelope is easy to write and easy to get subtly wrong in ways that only
    show up as a click, a note that never frees its voice, or a sustain that
    sits a fraction below where the user set it. These tests go after exactly
    those: stage order, stage duration, exact endpoints, and behaviour when a
    stage is asked to take no time at all.
*/

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "DSP/Envelopes/Envelope.h"

using namespace apollo::dsp;

namespace
{

constexpr double testSampleRate = 48000.0;

/** Runs an envelope for @p numSamples and returns every value it produced. */
[[nodiscard]] std::vector<float> render (Envelope& envelope, int numSamples)
{
    std::vector<float> output (static_cast<std::size_t> (numSamples), 0.0f);

    for (int i = 0; i < numSamples; ++i)
        output[static_cast<std::size_t> (i)] = envelope.getNextValue();

    return output;
}

/** @returns how many samples pass before @p envelope leaves @p stage. */
[[nodiscard]] int samplesInStage (Envelope& envelope, EnvelopeStage stage, int limit)
{
    int count = 0;

    while (envelope.getStage() == stage && count < limit)
    {
        static_cast<void> (envelope.getNextValue());
        ++count;
    }

    return count;
}

[[nodiscard]] EnvelopeSettings plainSettings()
{
    EnvelopeSettings settings;

    settings.delaySeconds = 0.0f;
    settings.attackSeconds = 0.010f;
    settings.holdSeconds = 0.0f;
    settings.decaySeconds = 0.020f;
    settings.sustainLevel = 0.5f;
    settings.releaseSeconds = 0.030f;
    settings.curve = 0.0f; // Linear, so timings and levels are exactly predictable.

    return settings;
}

//==============================================================================

class EnvelopeTests final : public juce::UnitTest
{
public:
    EnvelopeTests()
        : juce::UnitTest ("Envelope", "DSP")
    {
    }

    void runTest() override
    {
        testStageOrder();
        testStageDurations();
        testEndpointsAreExact();
        testZeroLengthStages();
        testCurveShape();
        testReleaseFromAnyStage();
        testRetriggerDoesNotJumpToZero();
        testSustainFollowsItsControl();
        testSettingsChangeRetimesInFlight();
        testSampleRateIndependence();
        testExtremeSettings();
    }

private:
    void testStageOrder()
    {
        beginTest ("The envelope visits its stages in order");

        Envelope envelope;
        envelope.prepare (testSampleRate);

        auto settings = plainSettings();
        settings.delaySeconds = 0.005f;
        settings.holdSeconds = 0.005f;
        envelope.setSettings (settings);

        expect (envelope.getStage() == EnvelopeStage::idle, "an untriggered envelope is idle");
        expect (! envelope.isActive());

        envelope.noteOn();
        expect (envelope.getStage() == EnvelopeStage::delay, "note-on enters the delay stage");

        static_cast<void> (samplesInStage (envelope, EnvelopeStage::delay, 48000));
        expect (envelope.getStage() == EnvelopeStage::attack, "delay is followed by attack");

        static_cast<void> (samplesInStage (envelope, EnvelopeStage::attack, 48000));
        expect (envelope.getStage() == EnvelopeStage::hold, "attack is followed by hold");

        static_cast<void> (samplesInStage (envelope, EnvelopeStage::hold, 48000));
        expect (envelope.getStage() == EnvelopeStage::decay, "hold is followed by decay");

        static_cast<void> (samplesInStage (envelope, EnvelopeStage::decay, 48000));
        expect (envelope.getStage() == EnvelopeStage::sustain, "decay is followed by sustain");

        // Sustain is untimed: it must not advance on its own, however long the
        // key is held.
        static_cast<void> (render (envelope, 96000));
        expect (envelope.getStage() == EnvelopeStage::sustain, "sustain must not time out");

        envelope.noteOff();
        expect (envelope.getStage() == EnvelopeStage::release, "note-off enters release");

        static_cast<void> (samplesInStage (envelope, EnvelopeStage::release, 48000));
        expect (envelope.getStage() == EnvelopeStage::idle, "release is followed by idle");
        expect (! envelope.isActive(), "a finished envelope frees its voice");
        expectWithinAbsoluteError (envelope.getCurrentValue(), 0.0f, 0.0f,
                                   "a finished envelope is exactly silent");
    }

    void testStageDurations()
    {
        beginTest ("Each stage lasts as long as it was told to");

        Envelope envelope;
        envelope.prepare (testSampleRate);

        auto settings = plainSettings();
        settings.delaySeconds = 0.005f;  // 240 samples
        settings.attackSeconds = 0.010f; // 480
        settings.holdSeconds = 0.020f;   // 960
        settings.decaySeconds = 0.040f;  // 1920
        settings.releaseSeconds = 0.080f; // 3840
        envelope.setSettings (settings);

        envelope.noteOn();

        const auto delaySamples = samplesInStage (envelope, EnvelopeStage::delay, 480000);
        const auto attackSamples = samplesInStage (envelope, EnvelopeStage::attack, 480000);
        const auto holdSamples = samplesInStage (envelope, EnvelopeStage::hold, 480000);
        const auto decaySamples = samplesInStage (envelope, EnvelopeStage::decay, 480000);

        envelope.noteOff();
        const auto releaseSamples = samplesInStage (envelope, EnvelopeStage::release, 480000);

        // One sample of tolerance: a stage length is a rounded sample count.
        const auto check = [this] (const char* stageName, int measured, int expected)
        {
            logMessage ("  " + juce::String (stageName) + ": " + juce::String (measured)
                        + " samples, expected " + juce::String (expected));

            expect (std::abs (measured - expected) <= 1,
                    juce::String (stageName) + " lasted " + juce::String (measured)
                        + " samples, expected " + juce::String (expected));
        };

        check ("delay", delaySamples, 240);
        check ("attack", attackSamples, 480);
        check ("hold", holdSamples, 960);
        check ("decay", decaySamples, 1920);
        check ("release", releaseSamples, 3840);
    }

    void testEndpointsAreExact()
    {
        beginTest ("Segments land exactly on their endpoints, at every curve");

        for (const auto curve : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f })
        {
            Envelope envelope;
            envelope.prepare (testSampleRate);

            auto settings = plainSettings();
            settings.sustainLevel = 0.37f;
            settings.curve = curve;
            envelope.setSettings (settings);

            envelope.noteOn();

            // The attack must reach exactly full scale. Anything less and a
            // patch is quietly quieter than it should be; anything more and the
            // engine's headroom measurements stop holding (ADR-0025).
            auto peak = 0.0f;

            while (envelope.getStage() == EnvelopeStage::attack)
                peak = std::max (peak, envelope.getNextValue());

            expectWithinAbsoluteError (peak, 1.0f, 1.0e-6f,
                                       "curve " + juce::String (curve, 2)
                                           + ": the attack must reach exactly full scale");

            while (envelope.getStage() == EnvelopeStage::decay)
                static_cast<void> (envelope.getNextValue());

            expectWithinAbsoluteError (envelope.getCurrentValue(), 0.37f, 1.0e-6f,
                                       "curve " + juce::String (curve, 2)
                                           + ": the decay must land exactly on the sustain level");

            envelope.noteOff();

            while (envelope.getStage() == EnvelopeStage::release)
                static_cast<void> (envelope.getNextValue());

            expectWithinAbsoluteError (envelope.getCurrentValue(), 0.0f, 0.0f,
                                       "curve " + juce::String (curve, 2)
                                           + ": the release must reach true silence");
        }
    }

    void testZeroLengthStages()
    {
        beginTest ("Stages of zero length are skipped, not stretched to a sample");

        Envelope envelope;
        envelope.prepare (testSampleRate);

        EnvelopeSettings settings;
        settings.delaySeconds = 0.0f;
        settings.attackSeconds = 0.0f;
        settings.holdSeconds = 0.0f;
        settings.decaySeconds = 0.0f;
        settings.sustainLevel = 0.8f;
        settings.releaseSeconds = 0.0f;
        envelope.setSettings (settings);

        envelope.noteOn();

        // Every timed stage is instant, so the envelope should already be
        // sitting on its sustain level without a single sample having passed.
        expect (envelope.getStage() == EnvelopeStage::sustain,
                "an all-zero envelope must arrive at sustain immediately");
        expectWithinAbsoluteError (envelope.getCurrentValue(), 0.8f, 1.0e-6f,
                                   "and at exactly the sustain level");

        envelope.noteOff();

        expect (envelope.getStage() == EnvelopeStage::idle,
                "a zero-length release must finish at once");
        expectWithinAbsoluteError (envelope.getCurrentValue(), 0.0f, 0.0f);

        // A zero sustain with zero times is the shortest possible envelope, and
        // must not spin or produce anything.
        Envelope silent;
        silent.prepare (testSampleRate);
        settings.sustainLevel = 0.0f;
        silent.setSettings (settings);
        silent.noteOn();

        expect (silent.getStage() == EnvelopeStage::sustain);
        expectWithinAbsoluteError (silent.getCurrentValue(), 0.0f, 0.0f);
    }

    void testCurveShape()
    {
        beginTest ("Curve tension bends the segment without breaking it");

        const auto attackHalfway = [] (float curve)
        {
            Envelope envelope;
            envelope.prepare (testSampleRate);

            auto settings = plainSettings();
            settings.attackSeconds = 0.100f;
            settings.curve = curve;
            envelope.setSettings (settings);

            envelope.noteOn();

            const auto halfway = static_cast<int> (0.050 * testSampleRate);
            const auto values = render (envelope, halfway);

            return values.back();
        };

        const auto linear = attackHalfway (0.0f);
        const auto positive = attackHalfway (0.75f);
        const auto negative = attackHalfway (-0.75f);

        logMessage ("  level at half the attack: linear " + juce::String (linear, 4)
                    + ", curve +0.75 " + juce::String (positive, 4)
                    + ", curve -0.75 " + juce::String (negative, 4));

        expectWithinAbsoluteError (linear, 0.5f, 0.01f,
                                   "a zero curve must be a straight line");

        expect (positive > linear + 0.05f,
                "positive tension must rise faster early — the analog shape");

        expect (negative < linear - 0.05f,
                "negative tension must rise more slowly early");

        // Whatever the curve, a rising segment must only rise. A shaping
        // function that overshoots or dips would be audible as a click.
        for (const auto curve : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f })
        {
            Envelope envelope;
            envelope.prepare (testSampleRate);

            auto settings = plainSettings();
            settings.attackSeconds = 0.050f;
            settings.curve = curve;
            envelope.setSettings (settings);
            envelope.noteOn();

            auto previous = -1.0f;

            while (envelope.getStage() == EnvelopeStage::attack)
            {
                const auto value = envelope.getNextValue();

                expect (value >= previous - 1.0e-6f,
                        "curve " + juce::String (curve, 2) + ": the attack dipped");
                expect (value <= 1.0f + 1.0e-6f,
                        "curve " + juce::String (curve, 2) + ": the attack overshot full scale");

                previous = value;
            }
        }
    }

    void testReleaseFromAnyStage()
    {
        beginTest ("Note-off releases from wherever the envelope is, with no jump");

        for (const auto stopAfter : { 0.001, 0.004, 0.012, 0.030 })
        {
            Envelope envelope;
            envelope.prepare (testSampleRate);

            auto settings = plainSettings();
            settings.curve = 0.5f;
            envelope.setSettings (settings);

            envelope.noteOn();

            const auto values = render (envelope, static_cast<int> (stopAfter * testSampleRate));
            const auto before = values.empty() ? 0.0f : values.back();

            envelope.noteOff();

            // The first sample of the release must continue from where the
            // envelope was, not restart from full scale or from silence.
            const auto after = envelope.getNextValue();

            expect (std::abs (after - before) < 0.02f,
                    "note-off after " + juce::String (stopAfter, 3) + " s stepped from "
                        + juce::String (before, 4) + " to " + juce::String (after, 4));

            expect (envelope.getStage() == EnvelopeStage::release);
        }
    }

    void testRetriggerDoesNotJumpToZero()
    {
        beginTest ("Retriggering a sounding envelope continues from its level");

        Envelope envelope;
        envelope.prepare (testSampleRate);

        auto settings = plainSettings();
        settings.attackSeconds = 0.050f;
        envelope.setSettings (settings);

        envelope.noteOn();
        static_cast<void> (render (envelope, static_cast<int> (0.025 * testSampleRate)));

        const auto before = envelope.getCurrentValue();
        expect (before > 0.2f, "the setup should leave the envelope part-way up");

        envelope.noteOn();
        const auto after = envelope.getNextValue();

        expect (std::abs (after - before) < 0.02f,
                "a retrigger dropped the level from " + juce::String (before, 4)
                    + " to " + juce::String (after, 4) + ", which is a click");
    }

    void testSustainFollowsItsControl()
    {
        beginTest ("Changing the sustain level moves a held note");

        Envelope envelope;
        envelope.prepare (testSampleRate);

        auto settings = plainSettings();
        settings.sustainLevel = 0.5f;
        envelope.setSettings (settings);

        envelope.noteOn();

        while (envelope.getStage() != EnvelopeStage::sustain)
            static_cast<void> (envelope.getNextValue());

        expectWithinAbsoluteError (envelope.getNextValue(), 0.5f, 1.0e-6f);

        settings.sustainLevel = 0.25f;
        envelope.setSettings (settings);

        expectWithinAbsoluteError (envelope.getNextValue(), 0.25f, 1.0e-6f,
                                   "a held note must follow its sustain control");
    }

    void testSettingsChangeRetimesInFlight()
    {
        beginTest ("A time changed mid-segment retimes it instead of being ignored");

        Envelope envelope;
        envelope.prepare (testSampleRate);

        auto settings = plainSettings();
        settings.attackSeconds = 1.0f;
        settings.curve = 0.0f;
        envelope.setSettings (settings);

        envelope.noteOn();

        // A tenth of the way into a one-second attack.
        static_cast<void> (render (envelope, static_cast<int> (0.100 * testSampleRate)));
        expect (envelope.getStage() == EnvelopeStage::attack);

        // Shorten the attack drastically. The remaining travel should now take
        // a fraction of a second rather than the remaining nine tenths.
        settings.attackSeconds = 0.100f;
        envelope.setSettings (settings);

        const auto remaining = samplesInStage (envelope, EnvelopeStage::attack,
                                               static_cast<int> (2.0 * testSampleRate));

        logMessage ("  attack shortened mid-flight finished in " + juce::String (remaining)
                    + " samples");

        expect (remaining < static_cast<int> (0.150 * testSampleRate),
                "the shortened attack still took " + juce::String (remaining)
                    + " samples, so the change was ignored");

        expect (envelope.getStage() == EnvelopeStage::hold
                    || envelope.getStage() == EnvelopeStage::decay
                    || envelope.getStage() == EnvelopeStage::sustain,
                "the attack must still complete rather than stalling");
    }

    void testSampleRateIndependence()
    {
        beginTest ("The same envelope takes the same time at any sample rate");

        for (const auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            Envelope envelope;
            envelope.prepare (rate);

            auto settings = plainSettings();
            settings.attackSeconds = 0.020f;
            settings.curve = 0.0f;
            envelope.setSettings (settings);

            envelope.noteOn();

            const auto samples = samplesInStage (envelope, EnvelopeStage::attack,
                                                 static_cast<int> (rate));

            const auto seconds = static_cast<double> (samples) / rate;

            logMessage ("  " + juce::String (rate, 0) + " Hz: attack took "
                        + juce::String (seconds * 1000.0, 3) + " ms");

            expectWithinAbsoluteError (seconds, 0.020, 0.0005,
                                       "attack duration drifted at " + juce::String (rate, 0) + " Hz");
        }
    }

    void testExtremeSettings()
    {
        beginTest ("Hostile settings stay bounded and finite");

        Envelope envelope;
        envelope.prepare (testSampleRate);

        EnvelopeSettings settings;
        settings.delaySeconds = -5.0f;
        settings.attackSeconds = std::numeric_limits<float>::quiet_NaN();
        settings.holdSeconds = 1.0e30f;
        settings.decaySeconds = -0.0f;
        settings.sustainLevel = 40.0f;
        settings.releaseSeconds = std::numeric_limits<float>::infinity();
        settings.curve = std::numeric_limits<float>::quiet_NaN();

        envelope.setSettings (settings);
        envelope.noteOn();

        const auto values = render (envelope, 4096);

        for (const auto value : values)
        {
            expect (std::isfinite (value), "a hostile setting produced a non-finite level");
            expect (value >= 0.0f && value <= 1.0f,
                    "level " + juce::String (value, 6) + " escaped the 0 to 1 range");
        }

        envelope.noteOff();

        for (const auto value : render (envelope, 4096))
        {
            expect (std::isfinite (value));
            expect (value >= 0.0f && value <= 1.0f);
        }

        // And a reset must always return it to a known, silent state.
        envelope.reset();
        expect (envelope.getStage() == EnvelopeStage::idle);
        expectWithinAbsoluteError (envelope.getNextValue(), 0.0f, 0.0f);
    }
};

EnvelopeTests envelopeTests;

} // namespace
