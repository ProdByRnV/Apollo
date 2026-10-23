/*
    Delay tests.

    A delay is easy to write and easy to get subtly wrong, and the ways it goes
    wrong are all measurable, so this file measures them rather than describing
    them.

    **Where the repeat lands.** An impulse in must come back out after exactly
    the delay asked for, in free time and in synced time, and the synced case is
    arithmetic a listener would notice immediately: a quarter note at 120 BPM is
    500 ms and nothing else.

    **Whether it stops.** `testFeedbackIsStable` runs the loop at the top of its
    range for thirty seconds of audio with nothing going in, which is the case
    ROADMAP calls "validate feedback stability". A delay that grows here is one
    that would destroy someone's hearing in a session, so the assertion is on
    the trend rather than on a threshold: every window must be quieter than the
    one before it.

    **Whether it clicks.** The time is glided rather than jumped (`Delay.h`), so
    sweeping it must not produce a discontinuity. The test sweeps the time as
    fast as a control can move and asserts no sample-to-sample jump larger than
    a real signal could contain.
*/

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

#include "DSP/Delay/Delay.h"

using namespace apollo::dsp;

namespace
{

constexpr double testSampleRate = 48000.0;
constexpr double pi = 3.14159265358979323846;

using Division = Delay::Division;

/** Runs @p signal through @p delay in place, stereo, in one block. */
void render (Delay& delay, std::vector<float>& left, std::vector<float>& right)
{
    float* channels[2] { left.data(), right.data() };

    delay.process (channels, 2, static_cast<int> (left.size()));
}

/** @returns the index of the loudest sample at or after @p from. */
[[nodiscard]] int peakIndex (const std::vector<float>& signal, int from)
{
    auto best = from;
    auto loudest = 0.0f;

    for (auto i = static_cast<std::size_t> (from); i < signal.size(); ++i)
    {
        const auto magnitude = std::abs (signal[i]);

        if (magnitude > loudest)
        {
            loudest = magnitude;
            best = static_cast<int> (i);
        }
    }

    return best;
}

[[nodiscard]] float peakOf (const std::vector<float>& signal, std::size_t from, std::size_t to)
{
    auto peak = 0.0f;

    for (auto i = from; i < std::min (to, signal.size()); ++i)
        peak = std::max (peak, std::abs (signal[i]));

    return peak;
}

/** Settings with the delay fully wet and everything else out of the way. */
[[nodiscard]] Delay::Settings wetSettings (float timeMs, float feedback = 0.0f)
{
    Delay::Settings settings;

    settings.timeMs = timeMs;
    settings.feedback = feedback;
    settings.mix = 1.0f;
    settings.dampingHz = 20000.0f;
    settings.lowCutHz = 20.0f;

    return settings;
}

class DelayTests final : public juce::UnitTest
{
public:
    DelayTests()
        : juce::UnitTest ("Delay", "DSP")
    {
    }

    void runTest() override
    {
        testDivisionArithmetic();
        testTempoSync();
        testRepeatLandsOnTime();
        testFeedbackDecays();
        testFeedbackIsStable();
        testDryMixIsUntouched();
        testPingPongAlternates();
        testDampingDarkensEachRepeat();
        testTimeChangesDoNotClick();
        testBypassEmptiesTheLine();
        testResetClearsTheTail();
        testExtremeInput();
        testTailReporting();
        testSplitSidesLandOnTheirOwnTimes();
        testSplitSyncedSides();
        testSplitPingPong();
        testUnmodulatedIsUnchanged();
        testModulationMovesTheReadPosition();
        testModulationHoldsOverALongSession();
        testShortDelaysSwingLess();
        testSplitAndModulatedTail();
        testModulationRefusesNonsense();
    }

private:
    void testDivisionArithmetic()
    {
        beginTest ("a note value is the number of beats it says it is");

        expectEquals (Delay::beatsFor (Division::whole), 4.0);
        expectEquals (Delay::beatsFor (Division::half), 2.0);
        expectEquals (Delay::beatsFor (Division::quarter), 1.0);
        expectEquals (Delay::beatsFor (Division::eighth), 0.5);
        expectEquals (Delay::beatsFor (Division::sixteenth), 0.25);
        expectEquals (Delay::beatsFor (Division::thirtySecond), 0.125);

        // A dotted note is half as long again; a triplet is two thirds.
        expectEquals (Delay::beatsFor (Division::quarterDotted), 1.5);
        expectWithinAbsoluteError (Delay::beatsFor (Division::quarterTriplet), 2.0 / 3.0, 1.0e-9);
        expectEquals (Delay::beatsFor (Division::eighthDotted), 0.75);
        expectWithinAbsoluteError (Delay::beatsFor (Division::eighthTriplet), 1.0 / 3.0, 1.0e-9);
    }

    void testTempoSync()
    {
        beginTest ("a synced delay follows the host, and keeps working without one");

        Delay delay;

        auto settings = wetSettings (500.0f);
        settings.tempoSynced = true;
        settings.division = Division::quarter;

        delay.setSettings (settings);
        delay.prepare (testSampleRate, 512);

        delay.setTempo (120.0);
        expectWithinAbsoluteError (delay.getDelaySeconds(), 0.5, 1.0e-9,
                                   "a quarter note at 120 BPM is half a second");

        delay.setTempo (140.0);
        expectWithinAbsoluteError (delay.getDelaySeconds(), 60.0 / 140.0, 1.0e-9);

        settings.division = Division::eighth;
        delay.setSettings (settings);
        expectWithinAbsoluteError (delay.getDelaySeconds(), 30.0 / 140.0, 1.0e-9);

        // A host with no transport, or a nonsensical tempo, must not stop the
        // delay or make it infinitely long (CLAUDE.md §38).
        delay.setTempo (0.0);
        expectWithinAbsoluteError (delay.getDelaySeconds(),
                                   Delay::beatsFor (Division::eighth) * 60.0 / Delay::fallbackBpm,
                                   1.0e-9,
                                   "no tempo falls back to 120 BPM rather than to silence");

        // A whole note at a very slow tempo is longer than the buffer, and is
        // clamped rather than wrapped.
        settings.division = Division::whole;
        delay.setSettings (settings);
        delay.setTempo (30.0);

        expectWithinAbsoluteError (delay.getDelaySeconds(), Delay::maximumDelaySeconds, 1.0e-9);

        // A free delay must not be dragged around by a tempo it is not
        // following.
        settings.tempoSynced = false;
        settings.timeMs = 250.0f;
        delay.setSettings (settings);
        delay.setTempo (200.0);

        expectWithinAbsoluteError (delay.getDelaySeconds(), 0.25, 1.0e-9);
    }

    void testRepeatLandsOnTime()
    {
        beginTest ("a repeat arrives after exactly the delay that was asked for");

        for (const auto timeMs : { 10.0f, 125.0f, 500.0f })
        {
            Delay delay;
            delay.setSettings (wetSettings (timeMs));
            delay.prepare (testSampleRate, 1 << 16);

            const auto length = 1 << 16;
            std::vector<float> left (static_cast<std::size_t> (length), 0.0f);
            std::vector<float> right (static_cast<std::size_t> (length), 0.0f);

            left[0] = 1.0f;
            right[0] = 1.0f;

            render (delay, left, right);

            const auto expected = static_cast<int> (std::lround (static_cast<double> (timeMs) * 0.001 * testSampleRate));
            const auto found = peakIndex (left, 1);

            // One sample of tolerance: the read is interpolated, so a delay that
            // is not a whole number of samples puts most of the impulse on one
            // side of the boundary and the rest on the other.
            expect (std::abs (found - expected) <= 1,
                    "a " + juce::String (timeMs, 0) + " ms delay put its repeat at sample "
                        + juce::String (found) + " rather than " + juce::String (expected));
        }
    }

    void testFeedbackDecays()
    {
        beginTest ("each repeat is quieter than the one before it");

        Delay delay;
        delay.setSettings (wetSettings (50.0f, 0.7f));
        delay.prepare (testSampleRate, 1 << 16);

        const auto length = 1 << 16;
        std::vector<float> left (static_cast<std::size_t> (length), 0.0f);
        std::vector<float> right (static_cast<std::size_t> (length), 0.0f);

        left[0] = 1.0f;
        right[0] = 1.0f;

        render (delay, left, right);

        const auto step = static_cast<std::size_t> (0.05 * testSampleRate);

        auto previous = 2.0f;

        for (int repeat = 1; repeat <= 4; ++repeat)
        {
            const auto from = step * static_cast<std::size_t> (repeat) - 8;
            const auto peak = peakOf (left, from, from + 16);

            expect (peak < previous,
                    "repeat " + juce::String (repeat) + " must be quieter than the one before it");
            expect (peak > 0.0f, "a repeat must actually be there");

            previous = peak;
        }
    }

    void testFeedbackIsStable()
    {
        beginTest ("the loop decays to nothing at maximum feedback");

        Delay delay;

        auto settings = wetSettings (100.0f, 1.0f);
        settings.dampingHz = 20000.0f;   // every filter out of the way, so the
        settings.lowCutHz = 20.0f;       // decay is the loop gain and nothing else
        delay.setSettings (settings);
        delay.prepare (testSampleRate, 4096);

        const auto blockSize = 4096;
        std::vector<float> left (static_cast<std::size_t> (blockSize), 0.0f);
        std::vector<float> right (static_cast<std::size_t> (blockSize), 0.0f);

        // One loud block in, then thirty seconds of silence: whatever is still
        // circulating after that is what the loop does on its own.
        for (int i = 0; i < blockSize; ++i)
        {
            const auto value = 0.9f * static_cast<float> (std::sin (2.0 * pi * 220.0
                                                                   * static_cast<double> (i) / testSampleRate));
            left[static_cast<std::size_t> (i)] = value;
            right[static_cast<std::size_t> (i)] = value;
        }

        render (delay, left, right);

        const auto blocks = static_cast<int> (30.0 * testSampleRate / blockSize);

        auto previousWindow = 2.0f;
        auto worst = 0.0f;

        for (int block = 0; block < blocks; ++block)
        {
            std::fill (left.begin(), left.end(), 0.0f);
            std::fill (right.begin(), right.end(), 0.0f);

            render (delay, left, right);

            const auto peak = peakOf (left, 0, left.size());
            worst = std::max (worst, peak);

            expect (std::isfinite (peak), "the loop must not produce a NaN or an infinity");

            // Checked every second rather than every block: a single delay line
            // is not full at every block boundary, so block-to-block peaks
            // legitimately rise and fall inside one repeat.
            if (block % 12 == 11)
            {
                expect (peak < previousWindow,
                        "the tail must be quieter every second, and went from "
                            + juce::String (previousWindow, 5) + " to " + juce::String (peak, 5));

                previousWindow = peak;
            }
        }

        expect (worst <= 1.0f,
                "nothing in the loop may exceed what went into it, and reached "
                    + juce::String (worst, 4));

        expect (previousWindow < 0.01f,
                "thirty seconds of maximum feedback must have decayed to near silence, and left "
                    + juce::String (previousWindow, 5));
    }

    void testDryMixIsUntouched()
    {
        beginTest ("a fully dry mix is the input, sample for sample");

        Delay delay;

        auto settings = wetSettings (200.0f, 0.5f);
        settings.mix = 0.0f;
        delay.setSettings (settings);
        delay.prepare (testSampleRate, 2048);

        std::vector<float> left (2048, 0.0f);
        std::vector<float> right (2048, 0.0f);

        for (std::size_t i = 0; i < left.size(); ++i)
        {
            left[i] = 0.6f * static_cast<float> (std::sin (0.05 * static_cast<double> (i)));
            right[i] = left[i];
        }

        const auto input = left;

        render (delay, left, right);

        // Bit-exact: a delay adds no latency, so at a dry mix it must be doing
        // nothing at all to the signal that passes through it.
        for (std::size_t i = 0; i < left.size(); ++i)
            expect (left[i] == input[i], "a dry delay must not touch the signal");

        expectEquals (delay.getLatencySamples(), 0, "a delay is not a lookahead");
    }

    void testPingPongAlternates()
    {
        beginTest ("ping-pong puts each repeat on the other side");

        Delay delay;

        auto settings = wetSettings (50.0f, 0.7f);
        settings.pingPong = true;
        delay.setSettings (settings);
        delay.prepare (testSampleRate, 1 << 15);

        const auto length = 1 << 15;
        std::vector<float> left (static_cast<std::size_t> (length), 0.0f);
        std::vector<float> right (static_cast<std::size_t> (length), 0.0f);

        // Into the left only, which is what makes the bouncing visible.
        left[0] = 1.0f;

        render (delay, left, right);

        const auto step = static_cast<std::size_t> (0.05 * testSampleRate);

        const auto firstLeft = peakOf (left, step - 8, step + 8);
        const auto firstRight = peakOf (right, step - 8, step + 8);
        const auto secondLeft = peakOf (left, step * 2 - 8, step * 2 + 8);
        const auto secondRight = peakOf (right, step * 2 - 8, step * 2 + 8);

        expect (firstLeft > firstRight * 4.0f,
                "the first repeat belongs to the side the sound came in on");
        expect (secondRight > secondLeft * 4.0f,
                "the second repeat must have crossed to the other side");
    }

    void testDampingDarkensEachRepeat()
    {
        beginTest ("damping takes a little more top off every repeat");

        Delay delay;

        auto settings = wetSettings (50.0f, 0.8f);
        settings.dampingHz = 1000.0f;
        delay.setSettings (settings);
        delay.prepare (testSampleRate, 1 << 15);

        const auto length = 1 << 15;
        std::vector<float> left (static_cast<std::size_t> (length), 0.0f);
        std::vector<float> right (static_cast<std::size_t> (length), 0.0f);

        // A short burst of something bright, so there is treble to lose.
        for (int i = 0; i < 64; ++i)
        {
            const auto value = 0.8f * static_cast<float> (std::sin (2.0 * pi * 6000.0
                                                                   * static_cast<double> (i) / testSampleRate));
            left[static_cast<std::size_t> (i)] = value;
            right[static_cast<std::size_t> (i)] = value;
        }

        render (delay, left, right);

        const auto step = static_cast<std::size_t> (0.05 * testSampleRate);

        const auto first = peakOf (left, step, step + 64);
        const auto second = peakOf (left, step * 2, step * 2 + 64);
        const auto third = peakOf (left, step * 3, step * 3 + 64);

        // At 1 kHz the damping filter is well below the tone, so each pass
        // through the loop removes a great deal more of it than the feedback
        // gain alone would.
        expect (second < first * 0.8f,
                "a damped repeat must lose more than the feedback gain takes");
        expect (third < second * 0.8f,
                "and must keep losing it, rather than settling after one pass");
    }

    void testTimeChangesDoNotClick()
    {
        beginTest ("sweeping the delay time glides rather than jumping");

        Delay delay;
        delay.setSettings (wetSettings (400.0f, 0.6f));
        delay.prepare (testSampleRate, 512);

        std::vector<float> left (512, 0.0f);
        std::vector<float> right (512, 0.0f);

        // Fill the line with something continuous first.
        for (int block = 0; block < 80; ++block)
        {
            for (std::size_t i = 0; i < left.size(); ++i)
            {
                const auto t = static_cast<double> (block * 512 + static_cast<int> (i));
                left[i] = 0.5f * static_cast<float> (std::sin (2.0 * pi * 330.0 * t / testSampleRate));
                right[i] = left[i];
            }

            render (delay, left, right);
        }

        // Then move the time as fast as a control can be moved, and watch the
        // output for a discontinuity.
        auto worstJump = 0.0f;
        auto previous = left.back();

        for (int block = 0; block < 60; ++block)
        {
            auto settings = wetSettings (400.0f - static_cast<float> (block) * 5.0f, 0.6f);
            delay.setSettings (settings);

            for (std::size_t i = 0; i < left.size(); ++i)
            {
                const auto t = static_cast<double> ((80 + block) * 512 + static_cast<int> (i));
                left[i] = 0.5f * static_cast<float> (std::sin (2.0 * pi * 330.0 * t / testSampleRate));
                right[i] = left[i];
            }

            render (delay, left, right);

            for (const auto sample : left)
            {
                worstJump = std::max (worstJump, std::abs (sample - previous));
                previous = sample;
            }
        }

        // A 330 Hz sine at this level moves by at most 0.023 per sample, and the
        // delayed copy adds its own. A glide stays inside that; a jump in the
        // read pointer would land far outside it.
        expect (worstJump < 0.1f,
                "sweeping the time must not step the read pointer, and the worst jump was "
                    + juce::String (worstJump, 4));
    }

    void testBypassEmptiesTheLine()
    {
        beginTest ("a bypassed delay does not save its repeats for later");

        Delay delay;
        delay.setSettings (wetSettings (100.0f, 0.5f));
        delay.prepare (testSampleRate, 4096);

        std::vector<float> left (4096, 0.0f);
        std::vector<float> right (4096, 0.0f);

        for (std::size_t i = 0; i < left.size(); ++i)
        {
            left[i] = 0.8f * static_cast<float> (std::sin (0.05 * static_cast<double> (i)));
            right[i] = left[i];
        }

        render (delay, left, right);

        // Bypassed for longer than the delay time, with silence going in.
        float* channels[2] { left.data(), right.data() };

        for (int block = 0; block < 4; ++block)
        {
            std::fill (left.begin(), left.end(), 0.0f);
            std::fill (right.begin(), right.end(), 0.0f);

            delay.processBypassed (channels, 2, static_cast<int> (left.size()));

            for (const auto sample : left)
                expect (sample == 0.0f, "bypass must pass the signal through untouched");
        }

        // Back in circuit: what comes out is what the line holds now, which
        // should be the silence it was fed rather than the tone from before.
        std::fill (left.begin(), left.end(), 0.0f);
        std::fill (right.begin(), right.end(), 0.0f);

        render (delay, left, right);

        expect (peakOf (left, 0, left.size()) < 0.001f,
                "switching a delay back in must not replay what was playing when it left");
    }

    void testResetClearsTheTail()
    {
        beginTest ("reset leaves no repeats behind");

        Delay delay;
        delay.setSettings (wetSettings (100.0f, 0.8f));
        delay.prepare (testSampleRate, 4096);

        std::vector<float> left (4096, 0.0f);
        std::vector<float> right (4096, 0.0f);

        for (std::size_t i = 0; i < left.size(); ++i)
        {
            left[i] = 0.9f * static_cast<float> (std::sin (0.05 * static_cast<double> (i)));
            right[i] = left[i];
        }

        render (delay, left, right);

        delay.reset();

        std::fill (left.begin(), left.end(), 0.0f);
        std::fill (right.begin(), right.end(), 0.0f);

        render (delay, left, right);

        for (const auto sample : left)
            expectWithinAbsoluteError (sample, 0.0f, 1.0e-6f,
                                       "silence in after a reset must be silence out");
    }

    void testExtremeInput()
    {
        beginTest ("extreme input stays finite, including through the feedback path");

        Delay delay;
        delay.setSettings (wetSettings (30.0f, 1.0f));
        delay.prepare (testSampleRate, 2048);

        std::vector<float> left (2048, 0.0f);
        std::vector<float> right (2048, 0.0f);

        for (std::size_t i = 0; i < left.size(); ++i)
        {
            left[i] = i % 2 == 0 ? 1.0e6f : -1.0e6f;
            right[i] = 1.0e-30f;
        }

        render (delay, left, right);

        for (std::size_t i = 0; i < left.size(); ++i)
        {
            expect (std::isfinite (left[i]), "no input may produce a NaN or an infinity");
            expect (std::isfinite (right[i]), "and not on the other channel either");
        }

        // And the loop must still settle afterwards rather than ringing for ever
        // on what it was fed.
        for (int block = 0; block < 200; ++block)
        {
            std::fill (left.begin(), left.end(), 0.0f);
            std::fill (right.begin(), right.end(), 0.0f);

            render (delay, left, right);
        }

        expect (std::isfinite (peakOf (left, 0, left.size())),
                "the tail of an absurd signal must still be a number");
    }

    void testTailReporting()
    {
        beginTest ("the reported tail is long enough to hold the repeats");

        Delay delay;

        auto settings = wetSettings (500.0f, 0.0f);
        delay.setSettings (settings);
        delay.prepare (testSampleRate, 512);

        expectWithinAbsoluteError (delay.getTailSeconds(), 0.5, 1.0e-9,
                                   "with no feedback the tail is one repeat");

        settings.feedback = 0.5f;
        delay.setSettings (settings);

        const auto tail = delay.getTailSeconds();

        expect (tail > 0.5, "feedback makes the tail longer than one repeat");
        expect (tail <= 30.0, "and the report is bounded rather than open-ended");

        // The estimate ignores the damping filter, which only ever shortens the
        // real tail: a host that renders a little too much silence has lost
        // nothing, one that truncates has lost the ending.
        settings.feedback = 1.0f;
        delay.setSettings (settings);

        expect (delay.getTailSeconds() >= tail, "more feedback cannot mean a shorter tail");
    }

    // Phase 13h-1 -----------------------------------------------------------

    void testSplitSidesLandOnTheirOwnTimes()
    {
        beginTest ("split, each side repeats after its own time; linked, the right side's own is ignored");

        for (const auto linked : { false, true })
        {
            Delay delay;

            auto settings = wetSettings (100.0f);
            settings.linked = linked;
            settings.rightTimeMs = 250.0f;
            delay.setSettings (settings);
            delay.prepare (testSampleRate, 1 << 15);

            const auto length = 1 << 15;
            std::vector<float> left (static_cast<std::size_t> (length), 0.0f);
            std::vector<float> right (static_cast<std::size_t> (length), 0.0f);

            left[0] = 1.0f;
            right[0] = 1.0f;

            render (delay, left, right);

            const auto expectedRight = linked ? 4800 : 12000;

            expect (std::abs (peakIndex (left, 1) - 4800) <= 1, "the left side keeps its own time");
            expect (std::abs (peakIndex (right, 1) - expectedRight) <= 1,
                    juce::String (linked ? "linked" : "split") + ": the right side's repeat landed at "
                        + juce::String (peakIndex (right, 1)) + " rather than " + juce::String (expectedRight));
        }
    }

    void testSplitSyncedSides()
    {
        beginTest ("split and synced, each side reads its own note value");

        Delay delay;

        auto settings = wetSettings (500.0f);
        settings.tempoSynced = true;
        settings.division = Division::quarter;
        settings.rightDivision = Division::eighthDotted;
        settings.rightTimeMs = 30.0f;
        settings.linked = false;
        delay.setSettings (settings);
        delay.setTempo (120.0);
        delay.prepare (testSampleRate, 512);

        expectWithinAbsoluteError (delay.getDelaySeconds(), 0.5, 1.0e-12);
        expectWithinAbsoluteError (delay.getRightDelaySeconds(), 0.375, 1.0e-12,
                                   "a dotted eighth at 120 BPM, not the right side's free time");

        delay.setTempo (90.0);
        expectWithinAbsoluteError (delay.getRightDelaySeconds(), 0.5, 1.0e-12, "and follows the tempo");

        settings.tempoSynced = false;
        delay.setSettings (settings);
        expectWithinAbsoluteError (delay.getRightDelaySeconds(), 0.03, 1.0e-9, "free, the right side's own time");

        settings.linked = true;
        delay.setSettings (settings);
        expectWithinAbsoluteError (delay.getRightDelaySeconds(), 0.5, 1.0e-9, "linked, the left side's");
    }

    void testSplitPingPong()
    {
        beginTest ("split ping-pong: each crossing takes the time of the side it lands on");

        Delay delay;

        auto settings = wetSettings (50.0f, 0.7f);
        settings.pingPong = true;
        settings.linked = false;
        settings.rightTimeMs = 120.0f;
        delay.setSettings (settings);
        delay.prepare (testSampleRate, 1 << 15);

        const auto length = 1 << 15;
        std::vector<float> left (static_cast<std::size_t> (length), 0.0f);
        std::vector<float> right (static_cast<std::size_t> (length), 0.0f);

        left[0] = 1.0f;

        render (delay, left, right);

        // In on the left, out on the left after 50 ms; crossed into the right
        // line, out on the right 120 ms after that.
        const auto first = peakIndex (left, 1);
        const auto second = peakIndex (right, 1);

        expect (std::abs (first - 2400) <= 1, "first repeat at " + juce::String (first));
        expect (std::abs (second - 8160) <= 1, "second repeat at " + juce::String (second));
    }

    /** Renders one stereo signal of a long note's worth through @p settings,
        in blocks, with a change of settings half way through.
    */
    [[nodiscard]] static std::vector<float> renderPhrase (const Delay::Settings& first, const Delay::Settings& second)
    {
        Delay delay;
        delay.setSettings (first);
        delay.prepare (testSampleRate, 256);

        const auto length = 48000;
        std::vector<float> left (static_cast<std::size_t> (length)), right (static_cast<std::size_t> (length));
        juce::Random random (5);

        for (int i = 0; i < length; ++i)
        {
            left[static_cast<std::size_t> (i)] = i < 4800 ? random.nextFloat() * 2.0f - 1.0f : 0.0f;
            right[static_cast<std::size_t> (i)] = i < 4800 ? std::sin (static_cast<float> (i) * 0.05f) : 0.0f;
        }

        for (int start = 0; start < length; start += 256)
        {
            if (start / 256 == length / 512)
                delay.setSettings (second);

            float* channels[2] { left.data() + start, right.data() + start };
            delay.process (channels, 2, std::min (256, length - start));
        }

        left.insert (left.end(), right.begin(), right.end());
        return left;
    }

    void testUnmodulatedIsUnchanged()
    {
        beginTest ("linked and unmodulated is the delay as it was, sample for sample");

        // The delay as it was before 13h-1, by its old fields alone.
        auto before = wetSettings (180.0f, 0.6f);
        before.mix = 0.5f;
        before.pingPong = true;
        before.dampingHz = 6000.0f;

        auto beforeLater = before;
        beforeLater.timeMs = 90.0f;

        // The same, with every new control somewhere else but at rest: linked,
        // and no depth at any rate.
        auto after = before;
        after.rightTimeMs = 1300.0f;
        after.rightDivision = Division::sixteenth;
        after.modulationRateHz = 7.3f;

        auto afterLater = beforeLater;
        afterLater.rightTimeMs = 20.0f;
        afterLater.modulationRateHz = 0.2f;

        const auto reference = renderPhrase (before, beforeLater);
        const auto changed = renderPhrase (after, afterLater);

        auto differences = 0;

        for (std::size_t i = 0; i < reference.size(); ++i)
            if (reference[i] != changed[i])
                ++differences;

        expectEquals (differences, 0, "the new controls at rest must change nothing at all");
    }

    /** The delay actually read, in samples, at each sample of a block.

        A ramp is the one signal linear interpolation reproduces exactly, so
        what comes out of a fully wet delay with no feedback is the ramp's value
        that many samples ago, and subtracting it from where the ramp is now
        gives the delay that was read — modulation and all. The ramp wraps at
        16384, where a float still resolves a five-hundredth of a sample through
        the mix, and the reads that reach
        back across the wrap are reported as not a number.
    */
    [[nodiscard]] static std::vector<double> measuredDelays (Delay& delay, int skipSamples, int measureSamples)
    {
        constexpr int wrap = 16384;
        constexpr int block = 512;

        std::vector<double> leftDelays, rightDelays;
        std::vector<float> left (block), right (block);

        for (int start = 0; start < skipSamples + measureSamples; start += block)
        {
            for (int i = 0; i < block; ++i)
                left[static_cast<std::size_t> (i)] = right[static_cast<std::size_t> (i)]
                    = static_cast<float> ((start + i) % wrap);

            float* channels[2] { left.data(), right.data() };
            delay.process (channels, 2, block);

            for (int i = 0; i < block; ++i)
            {
                if (start + i < skipSamples)
                    continue;

                const auto now = static_cast<double> ((start + i) % wrap);

                for (auto* side : { &leftDelays, &rightDelays })
                {
                    const auto read = static_cast<double> ((side == &leftDelays ? left : right)[static_cast<std::size_t> (i)]);
                    auto measured = now - read;

                    if (measured < 0.0)
                        measured += wrap;

                    // Just after the wrap, a read reaches back across it and
                    // interpolates between its top and 0, which means nothing.
                    // Every delay measured here is under 4096 samples.
                    side->push_back (now < 4096.0 ? std::numeric_limits<double>::quiet_NaN() : measured);
                }
            }
        }

        leftDelays.insert (leftDelays.end(), rightDelays.begin(), rightDelays.end());
        return leftDelays;
    }

    /** The largest difference between the delays @p measured from sample
        @p from on, and a base of @p base swung by @p depth on a sine for the
        left side and a cosine for the right.
    */
    [[nodiscard]] static double worstModulationError (const std::vector<double>& measured, int from,
                                                     double base, double depth, double rateHz)
    {
        const auto half = measured.size() / 2;
        auto worst = 0.0;

        for (std::size_t i = 0; i < half; ++i)
        {
            const auto phase = 2.0 * pi * rateHz * static_cast<double> (from + static_cast<int> (i)) / testSampleRate;

            for (const auto [value, expected] : { std::pair { measured[i], base + depth * std::sin (phase) },
                                                  std::pair { measured[half + i], base + depth * std::cos (phase) } })
                if (! std::isnan (value))
                    worst = std::max (worst, std::abs (value - expected));
        }

        return worst;
    }

    void testModulationMovesTheReadPosition()
    {
        beginTest ("modulation swings each side's time by the depth, a quarter of a cycle apart");

        Delay delay;

        auto settings = wetSettings (20.0f);
        settings.modulationDepthMs = 2.0f;
        settings.modulationRateHz = 3.0f;
        delay.setSettings (settings);
        delay.prepare (testSampleRate, 512);

        const auto skip = 2048;
        const auto measured = measuredDelays (delay, skip, 48000);

        // 20 ms is 960 samples and 2 ms is 96. Within a hundredth of a sample
        // of that curve at every sample is the depth, the rate and the quarter
        // cycle between the sides all at once.
        const auto worst = worstModulationError (measured, skip, 960.0, 96.0, 3.0);

        logMessage ("  worst read position error: " + juce::String (worst, 5) + " samples");
        expect (worst < 0.01, "the read position is not a sine of the depth asked for: off by "
                                  + juce::String (worst, 4) + " samples");

        // And it is not a coincidence of a curve that barely moves.
        expect (worstModulationError (measured, skip, 960.0, 0.0, 3.0) > 95.0);
    }

    void testModulationHoldsOverALongSession()
    {
        beginTest ("the modulation is the same depth and rate after a minute as at the start");

        Delay delay;

        auto settings = wetSettings (40.0f);
        settings.modulationDepthMs = 5.0f;
        settings.modulationRateHz = 9.7f;
        delay.setSettings (settings);
        delay.prepare (testSampleRate, 512);

        // Sixty seconds of rotation, then a second measured.
        const auto skip = 60 * 48000 - (60 * 48000) % 512;
        const auto measured = measuredDelays (delay, skip, 48000);
        // At the rate the delay was given, which is a float: 9.7f is 9.6999998
        // Hz, and over a minute the difference is itself a fiftieth of a sample.
        const auto worst = worstModulationError (measured, skip, 1920.0, 240.0, static_cast<double> (9.7f));

        logMessage ("  after a minute, worst read position error: " + juce::String (worst, 5) + " samples");
        expect (worst < 0.01, "the oscillator drifted: off by " + juce::String (worst, 4) + " samples");
    }

    void testShortDelaysSwingLess()
    {
        beginTest ("a delay shorter than the depth swings less rather than pinning to the write");

        Delay delay;

        auto settings = wetSettings (1.0f);
        settings.modulationDepthMs = 10.0f;
        settings.modulationRateHz = 5.0f;
        delay.setSettings (settings);
        delay.prepare (testSampleRate, 512);

        const auto measured = measuredDelays (delay, 1024, 9600);
        auto lowest = 1.0e9, highest = 0.0;

        for (const auto value : measured)
        {
            if (std::isnan (value))
                continue;

            lowest = std::min (lowest, value);
            highest = std::max (highest, value);
        }

        // 1 ms is 48 samples, so the swing is held to 47 either way.
        expect (lowest >= 1.0 - 1.0e-3, "never reads closer than one sample: " + juce::String (lowest, 3));
        expectWithinAbsoluteError (highest, 95.0, 0.05, "and swings as far the other way");
        expectWithinAbsoluteError (lowest, 1.0, 0.05);
    }

    void testSplitAndModulatedTail()
    {
        beginTest ("the reported tail follows the longer side and the depth");

        Delay delay;

        auto settings = wetSettings (300.0f);
        settings.linked = false;
        settings.rightTimeMs = 1000.0f;
        delay.setSettings (settings);
        delay.prepare (testSampleRate, 512);

        expectWithinAbsoluteError (delay.getTailSeconds(), 1.0, 1.0e-9, "the right side is the longer");

        settings.modulationDepthMs = 10.0f;
        delay.setSettings (settings);
        expectWithinAbsoluteError (delay.getTailSeconds(), 1.01, 1.0e-6, "plus the furthest it can wander");

        settings.timeMs = 2000.0f;
        settings.linked = true;
        delay.setSettings (settings);
        expectWithinAbsoluteError (delay.getTailSeconds(), 2.01, 1.0e-6, "linked, the left side's for both");
    }

    void testModulationRefusesNonsense()
    {
        beginTest ("a NaN or absurd modulation setting leaves the delay finite");

        for (const auto [rate, depth] : { std::pair { std::numeric_limits<float>::quiet_NaN(), 5.0f },
                                          std::pair { 3.0f, std::numeric_limits<float>::quiet_NaN() },
                                          std::pair { 1.0e9f, 1.0e9f },
                                          std::pair { -5.0f, -5.0f } })
        {
            Delay delay;

            auto settings = wetSettings (4000.0f, 1.0f);
            settings.modulationRateHz = rate;
            settings.modulationDepthMs = depth;
            delay.setSettings (settings);
            delay.prepare (testSampleRate, 4096);

            std::vector<float> left (1 << 16), right (1 << 16);
            juce::Random random (9);

            for (auto& sample : left)
                sample = random.nextFloat() * 2.0f - 1.0f;

            right = left;
            render (delay, left, right);

            auto finite = true;
            for (std::size_t i = 0; i < left.size(); ++i)
                finite = finite && std::isfinite (left[i]) && std::isfinite (right[i]);

            expect (finite);
            expect (delay.getTailSeconds() <= 30.0 && delay.getTailSeconds() > 0.0);
        }
    }
};

DelayTests delayTests;

} // namespace
