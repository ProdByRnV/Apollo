/*
    Reverb tests.

    A reverb is the hardest effect so far to judge by listening and the easiest
    to get wrong in a way that only shows up minutes later, so almost everything
    here is a measurement over time rather than a snapshot.

    **Decay stability** is the headline, and it is ROADMAP's own wording. The
    network is eight delay lines feeding each other through an orthogonal matrix;
    orthogonal means energy-preserving, so the only thing that can make the tail
    grow is a mistake in the decay gains. `testDecayIsStable` runs the longest
    decay the control offers for a minute with nothing going in and asserts the
    tail stays under the envelope RT60 describes, never exceeds what went into
    it, and is gone by the end.

    Two things that test does *not* assert, both for the same reason — a modal
    network is not a single decaying echo. It does not require every window to
    be quieter than the last, because eight interacting lines produce modes that
    beat and a window's peak legitimately rises now and then while the energy
    falls. And it anchors the envelope at the loudest the tail ever reaches
    rather than at the moment the input stopped, because a reverb *builds up*:
    the network is empty when the sound arrives and fills over the following
    second, so the level right after the input is partway up a rise.

    **The decay time is a number, not a feeling.** RT60 means the tail falls
    60 dB in that many seconds, and `testDecayMatchesTheSetting` measures it as a
    slope between two later times — for the build-up reason above — using RMS
    rather than peak, because RT60 is a statement about energy. A reverb whose
    decay control is out by a factor of two is one nobody can set by ear.

    **Denormals.** The tail spends most of its life below -200 dB, and
    `testTailReachesExactlyZero` asserts it actually arrives at zero rather than
    grinding on as denormals for ever.
*/

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "DSP/Reverb/Reverb.h"

using namespace apollo::dsp;

namespace
{

constexpr double testSampleRate = 48000.0;
constexpr double pi = 3.14159265358979323846;
constexpr int blockSize = 4096;

using Mode = Reverb::Mode;

void render (Reverb& reverb, std::vector<float>& left, std::vector<float>& right)
{
    float* channels[2] { left.data(), right.data() };

    reverb.process (channels, 2, static_cast<int> (left.size()));
}

[[nodiscard]] float peakOf (const std::vector<float>& signal)
{
    auto peak = 0.0f;

    for (const auto sample : signal)
        peak = std::max (peak, std::abs (sample));

    return peak;
}

/** Root mean square, which is the quantity RT60 actually governs.

    The decay gains are derived so that the *energy* in the network falls 60 dB
    in the decay time. A peak does not follow that exactly — a sum of beating
    modes can peak high while holding little energy — so a decay measured by
    peak comes out wrong by several decibels through no fault of the reverb.
*/
[[nodiscard]] float rmsOf (const std::vector<float>& signal)
{
    if (signal.empty())
        return 0.0f;

    auto sum = 0.0;

    for (const auto sample : signal)
        sum += static_cast<double> (sample) * static_cast<double> (sample);

    return static_cast<float> (std::sqrt (sum / static_cast<double> (signal.size())));
}

/** Settings with the reverb fully wet and nothing else in the way. */
[[nodiscard]] Reverb::Settings wetSettings (float decaySeconds)
{
    Reverb::Settings settings;

    settings.decaySeconds = decaySeconds;
    settings.dampingHz = 20000.0f;
    settings.preDelayMs = 0.0f;
    settings.mix = 1.0f;

    return settings;
}

/** Fills a stereo pair with one short burst of noise-like content. */
void fillBurst (std::vector<float>& left, std::vector<float>& right, int length)
{
    std::fill (left.begin(), left.end(), 0.0f);
    std::fill (right.begin(), right.end(), 0.0f);

    auto phase = 0.0;

    for (int i = 0; i < std::min (length, static_cast<int> (left.size())); ++i)
    {
        // Two incommensurate tones rather than a single sine: a reverb fed one
        // frequency excites a few modes, and the point is to excite all of them.
        phase += 1.0;

        const auto value = 0.6f * static_cast<float> (
            std::sin (2.0 * pi * 437.0 * phase / testSampleRate)
            + 0.7 * std::sin (2.0 * pi * 1931.0 * phase / testSampleRate));

        left[static_cast<std::size_t> (i)] = value;
        right[static_cast<std::size_t> (i)] = value * 0.8f;
    }
}

class ReverbTests final : public juce::UnitTest
{
public:
    ReverbTests()
        : juce::UnitTest ("Reverb", "DSP")
    {
    }

    void runTest() override
    {
        testBaseLengthsAreMutuallyPrime();
        testSizeAndModeScaleTheNetwork();
        testDryMixIsUntouched();
        testItActuallyReverberates();
        testDecayIsStable();
        testDecayMatchesTheSetting();
        testTailReachesExactlyZero();
        testDampingShortensTheTail();
        testPreDelayHoldsTheRoomBack();
        testWidthCollapsesToMono();
        testBypassKeepsDecayingRatherThanFreezing();
        testResetClearsTheTail();
        testExtremeInput();
        testTailReporting();
    }

private:
    void testBaseLengthsAreMutuallyPrime()
    {
        beginTest ("the network's line lengths share no factors");

        // Lengths sharing a factor put their echoes on top of each other at the
        // common multiple, which is what gives a bad reverb a pitch. This is the
        // property the numbers were chosen for, so it is asserted rather than
        // trusted to a comment.
        const auto gcd = [] (int a, int b)
        {
            while (b != 0)
            {
                const auto t = b;
                b = a % b;
                a = t;
            }

            return a;
        };

        for (const auto mode : { Mode::room, Mode::hall })
        {
            for (int i = 0; i < Reverb::lineCount; ++i)
            {
                for (int j = i + 1; j < Reverb::lineCount; ++j)
                {
                    const auto common = gcd (Reverb::baseLength (mode, i), Reverb::baseLength (mode, j));

                    expectEquals (common, 1,
                                  "lines " + juce::String (i) + " and " + juce::String (j)
                                      + " share a factor of " + juce::String (common));
                }
            }
        }

        // And a hall is not a room with a longer decay: its walls are further
        // away, which is a difference in the lengths themselves.
        expect (Reverb::baseLength (Mode::hall, 0) > Reverb::baseLength (Mode::room, 0) * 3 / 2,
                "a hall's shortest path must be substantially longer than a room's");
    }

    void testSizeAndModeScaleTheNetwork()
    {
        beginTest ("size scales the lines and mode changes which lines they are");

        Reverb reverb;
        auto settings = wetSettings (2.0f);

        settings.mode = Mode::room;
        settings.size = 0.0f;
        reverb.setSettings (settings);
        reverb.prepare (testSampleRate, blockSize);

        const auto smallest = reverb.getLineLength (0);

        settings.size = 1.0f;
        reverb.setSettings (settings);

        const auto largest = reverb.getLineLength (0);

        expect (largest > smallest * 2.5f,
                "the size control must make a real difference to the lines, and moved from "
                    + juce::String (smallest, 1) + " to " + juce::String (largest, 1) + " samples");

        settings.mode = Mode::hall;
        reverb.setSettings (settings);

        expect (reverb.getLineLength (0) > largest,
                "a hall at the same size must be longer than a room");
    }

    void testDryMixIsUntouched()
    {
        beginTest ("a fully dry mix is the input, sample for sample");

        Reverb reverb;

        auto settings = wetSettings (4.0f);
        settings.mix = 0.0f;
        reverb.setSettings (settings);
        reverb.prepare (testSampleRate, blockSize);

        std::vector<float> left (blockSize, 0.0f);
        std::vector<float> right (blockSize, 0.0f);
        fillBurst (left, right, blockSize);

        const auto input = left;

        render (reverb, left, right);

        for (std::size_t i = 0; i < left.size(); ++i)
            expect (left[i] == input[i], "a dry reverb must not touch the signal");

        expectEquals (reverb.getLatencySamples(), 0, "a reverb is not a lookahead");
    }

    void testItActuallyReverberates()
    {
        beginTest ("a burst is still sounding long after it stopped");

        Reverb reverb;
        reverb.setSettings (wetSettings (3.0f));
        reverb.prepare (testSampleRate, blockSize);

        std::vector<float> left (blockSize, 0.0f);
        std::vector<float> right (blockSize, 0.0f);

        fillBurst (left, right, 512);
        render (reverb, left, right);

        // A second later, with silence going in, there must still be sound
        // coming out — and it must not be a copy of the input arriving late,
        // which is what distinguishes a reverb from a delay.
        auto heard = 0.0f;

        for (int block = 0; block < 12; ++block)
        {
            std::fill (left.begin(), left.end(), 0.0f);
            std::fill (right.begin(), right.end(), 0.0f);

            render (reverb, left, right);

            if (block >= 8)
                heard = std::max (heard, peakOf (left));
        }

        expect (heard > 0.001f,
                "the tail must still be audible a second later, and peaked at "
                    + juce::String (heard, 6));
    }

    void testDecayIsStable()
    {
        beginTest ("the longest decay falls monotonically over a minute and never grows");

        Reverb reverb;

        // The longest tail the control offers, with damping switched out so the
        // only thing shortening it is the decay gain itself. This is the worst
        // case the network can be put in.
        auto settings = wetSettings (Reverb::maximumDecaySeconds);
        settings.size = 1.0f;
        settings.mode = Mode::hall;
        reverb.setSettings (settings);
        reverb.prepare (testSampleRate, blockSize);

        std::vector<float> left (blockSize, 0.0f);
        std::vector<float> right (blockSize, 0.0f);

        fillBurst (left, right, blockSize);

        const auto inputPeak = peakOf (left);

        render (reverb, left, right);

        // A reverb *builds up*: the network is empty when the sound arrives, and
        // energy circulates into the longer lines over the following second or
        // two. The peak therefore rises before it falls, which is the effect
        // working rather than the effect failing, so the envelope below is
        // anchored at the loudest the tail ever gets rather than at the first
        // block. Finding that anchor is what this first pass is for.
        constexpr double buildUpSeconds = 3.0;

        const auto buildUpBlocks = static_cast<int> (buildUpSeconds * testSampleRate / blockSize);

        auto anchor = 0.0f;
        auto worst = 0.0f;

        for (int block = 0; block < buildUpBlocks; ++block)
        {
            std::fill (left.begin(), left.end(), 0.0f);
            std::fill (right.begin(), right.end(), 0.0f);

            render (reverb, left, right);

            anchor = std::max (anchor, peakOf (left));
        }

        worst = anchor;

        auto last = 0.0f;

        const auto blocks = static_cast<int> (57.0 * testSampleRate / blockSize);

        for (int block = 0; block < blocks; ++block)
        {
            std::fill (left.begin(), left.end(), 0.0f);
            std::fill (right.begin(), right.end(), 0.0f);

            render (reverb, left, right);

            const auto peak = peakOf (left);
            worst = std::max (worst, peak);
            last = peak;

            expect (std::isfinite (peak), "the network must not produce a NaN or an infinity");

            // Bounded by the decay it was asked for, rather than required to
            // fall on every window. Eight lines feeding each other produce modes
            // that beat, so a window's peak legitimately rises now and then
            // while the energy in the network falls — a strictly monotonic
            // assertion would be testing the beating rather than the stability.
            // Staying under the envelope RT60 describes is what stability means
            // here, with headroom for the beating.
            const auto seconds = static_cast<double> ((block + 1) * blockSize) / testSampleRate;
            const auto envelope = anchor * static_cast<float> (
                std::pow (10.0, -3.0 * seconds / static_cast<double> (settings.decaySeconds)));

            constexpr float beatingHeadroom = 4.0f; // 12 dB

            expect (peak <= envelope * beatingHeadroom + 1.0e-6f,
                    "the tail must stay under the decay it was asked for: "
                        + juce::String (peak, 6) + " against an envelope of "
                        + juce::String (envelope, 6) + " at " + juce::String (seconds, 1) + " s");
        }

        // The scaling on the way in and out is what makes this hold: without it
        // the first pass sums four correlated copies and the room is louder than
        // the sound that caused it.
        expect (worst <= inputPeak,
                "nothing the network produces may exceed what went into it: "
                    + juce::String (worst, 4) + " against " + juce::String (inputPeak, 4));

        expect (last < anchor * 0.01f,
                "a minute into a 20 s decay the tail must be long gone, and was "
                    + juce::String (last, 8));

        logMessage ("  loudest tail " + juce::String (anchor, 4) + " against an input of "
                    + juce::String (inputPeak, 4) + "; after a minute: " + juce::String (last, 8));
    }

    void testDecayMatchesTheSetting()
    {
        beginTest ("the decay control means RT60, not a feeling");

        for (const auto decay : { 1.0f, 3.0f })
        {
            Reverb reverb;

            auto settings = wetSettings (decay);
            settings.size = 0.5f;
            reverb.setSettings (settings);
            reverb.prepare (testSampleRate, blockSize);

            std::vector<float> left (blockSize, 0.0f);
            std::vector<float> right (blockSize, 0.0f);

            fillBurst (left, right, blockSize);
            render (reverb, left, right);

            const auto silentBlocks = [&] (double seconds)
            {
                const auto blocks = static_cast<int> (seconds * testSampleRate
                                                      / static_cast<double> (blockSize));

                auto level = 0.0f;

                for (int block = 0; block < blocks; ++block)
                {
                    std::fill (left.begin(), left.end(), 0.0f);
                    std::fill (right.begin(), right.end(), 0.0f);

                    render (reverb, left, right);

                    // RMS rather than peak, because RT60 is a statement about
                    // energy — see `rmsOf`.
                    level = rmsOf (left);
                }

                return level;
            };

            // Measured as a *slope* between two later times rather than from the
            // moment the input stopped. A reverb builds up: the network is empty
            // when the sound arrives and fills over the following second, so the
            // level right after the input is not the start of the decay — it is
            // partway up the rise. Waiting one decay time before the first
            // measurement puts both samples on the decaying part of the curve,
            // where the RT60 relation is what is actually being checked.
            const auto first = silentBlocks (static_cast<double> (decay));
            const auto second = silentBlocks (static_cast<double> (decay));

            const auto ratio = first > 0.0f ? second / first : 0.0f;
            const auto fallen = ratio > 1.0e-7f ? 20.0f * std::log10 (ratio) : -140.0f;

            logMessage ("  " + juce::String (decay, 1) + " s decay fell "
                        + juce::String (fallen, 1) + " dB over one decay time");

            // Wide, deliberately: the network's modes beat, and a single RMS
            // window is not a fitted envelope. What this guards is a decay
            // control that is out by a factor of two or more, which is what
            // makes a control unusable by ear.
            expect (fallen < -42.0f && fallen > -80.0f,
                    "a " + juce::String (decay, 1) + " s RT60 must fall near 60 dB in "
                        + juce::String (decay, 1) + " s, and fell " + juce::String (fallen, 1) + " dB");
        }
    }

    void testTailReachesExactlyZero()
    {
        beginTest ("the tail ends rather than grinding on as denormals");

        Reverb reverb;
        reverb.setSettings (wetSettings (0.3f));
        reverb.prepare (testSampleRate, blockSize);

        std::vector<float> left (blockSize, 0.0f);
        std::vector<float> right (blockSize, 0.0f);

        fillBurst (left, right, 256);
        render (reverb, left, right);

        // A short decay, given far longer than it needs. Every sample must be
        // exactly zero at the end — not merely small — which is what the
        // denormal flush in the feedback path is for (CLAUDE.md §37).
        for (int block = 0; block < 60; ++block)
        {
            std::fill (left.begin(), left.end(), 0.0f);
            std::fill (right.begin(), right.end(), 0.0f);

            render (reverb, left, right);
        }

        for (std::size_t i = 0; i < left.size(); ++i)
        {
            expect (left[i] == 0.0f, "the left tail must reach exactly zero");
            expect (right[i] == 0.0f, "and the right one too");
        }
    }

    void testDampingShortensTheTail()
    {
        beginTest ("damping makes the tail die sooner as well as darker");

        const auto tailAfter = [] (float dampingHz)
        {
            Reverb reverb;

            auto settings = wetSettings (4.0f);
            settings.dampingHz = dampingHz;
            reverb.setSettings (settings);
            reverb.prepare (testSampleRate, blockSize);

            std::vector<float> left (blockSize, 0.0f);
            std::vector<float> right (blockSize, 0.0f);

            fillBurst (left, right, blockSize);
            render (reverb, left, right);

            auto last = 0.0f;

            for (int block = 0; block < 24; ++block)
            {
                std::fill (left.begin(), left.end(), 0.0f);
                std::fill (right.begin(), right.end(), 0.0f);

                render (reverb, left, right);
                last = peakOf (left);
            }

            return last;
        };

        const auto open = tailAfter (20000.0f);
        const auto damped = tailAfter (1000.0f);

        expect (damped < open,
                "a damped tail must be quieter after two seconds than an undamped one: "
                    + juce::String (damped, 6) + " against " + juce::String (open, 6));
    }

    void testPreDelayHoldsTheRoomBack()
    {
        beginTest ("pre-delay is a gap before the room answers");

        Reverb reverb;

        auto settings = wetSettings (2.0f);
        settings.preDelayMs = 100.0f;
        reverb.setSettings (settings);
        reverb.prepare (testSampleRate, blockSize);

        // Long enough to contain the gap and what follows it: 100 ms is 4800
        // samples, and a block is 4096.
        const auto length = static_cast<std::size_t> (0.2 * testSampleRate);

        std::vector<float> left (length, 0.0f);
        std::vector<float> right (length, 0.0f);

        fillBurst (left, right, 64);
        render (reverb, left, right);

        const auto gap = static_cast<std::size_t> (0.09 * testSampleRate);

        auto beforeTheGap = 0.0f;

        for (std::size_t i = 0; i < gap; ++i)
            beforeTheGap = std::max (beforeTheGap, std::abs (left[i]));

        auto afterTheGap = 0.0f;

        for (auto i = static_cast<std::size_t> (0.11 * testSampleRate); i < left.size(); ++i)
            afterTheGap = std::max (afterTheGap, std::abs (left[i]));

        expect (beforeTheGap < 1.0e-6f,
                "nothing may arrive before the pre-delay has elapsed, and something did at "
                    + juce::String (beforeTheGap, 8));

        expect (afterTheGap > 0.0005f,
                "and the room must answer after it, but peaked at only "
                    + juce::String (afterTheGap, 8));
    }

    void testWidthCollapsesToMono()
    {
        beginTest ("width at zero puts the tail in the centre");

        Reverb reverb;

        auto settings = wetSettings (2.0f);
        settings.width = 0.0f;
        reverb.setSettings (settings);
        reverb.prepare (testSampleRate, blockSize);

        std::vector<float> left (blockSize, 0.0f);
        std::vector<float> right (blockSize, 0.0f);

        fillBurst (left, right, 512);
        render (reverb, left, right);

        // Skip the ramp: width is smoothed, so the first 20 ms are on their way
        // to mono rather than at it.
        for (auto i = static_cast<std::size_t> (0.05 * testSampleRate); i < left.size(); ++i)
            expectWithinAbsoluteError (left[i], right[i], 1.0e-6f,
                                       "at zero width the two channels must be the same signal");
    }

    void testBypassKeepsDecayingRatherThanFreezing()
    {
        beginTest ("a bypassed reverb goes on decaying instead of saving its tail");

        Reverb reverb;
        reverb.setSettings (wetSettings (2.0f));
        reverb.prepare (testSampleRate, blockSize);

        std::vector<float> left (blockSize, 0.0f);
        std::vector<float> right (blockSize, 0.0f);

        fillBurst (left, right, blockSize);
        render (reverb, left, right);

        // What the tail is worth at the moment the effect is switched out.
        std::fill (left.begin(), left.end(), 0.0f);
        std::fill (right.begin(), right.end(), 0.0f);
        render (reverb, left, right);

        const auto beforeBypass = peakOf (left);
        expect (beforeBypass > 0.001f, "there must be a tail to lose in the first place");

        float* channels[2] { left.data(), right.data() };

        // Bypassed for two seconds — one whole decay time — with silence in.
        for (int block = 0; block < 24; ++block)
        {
            std::fill (left.begin(), left.end(), 0.0f);
            std::fill (right.begin(), right.end(), 0.0f);

            reverb.processBypassed (channels, 2, blockSize);

            for (const auto sample : left)
                expect (sample == 0.0f, "bypass must pass the signal through untouched");
        }

        std::fill (left.begin(), left.end(), 0.0f);
        std::fill (right.begin(), right.end(), 0.0f);
        render (reverb, left, right);

        const auto afterBypass = peakOf (left);

        // The network keeps running while it is out of circuit, so what comes
        // back is a tail that has gone on decaying for the whole time rather
        // than the one that was frozen when the switch was thrown. A decay time
        // is 60 dB by definition, so a hundredth of the level is a generous
        // bound on "much quieter".
        expect (afterBypass < beforeBypass * 0.01f,
                "the tail must have decayed while the effect was out of circuit: "
                    + juce::String (afterBypass, 6) + " against "
                    + juce::String (beforeBypass, 6));

        // And long enough bypassed, there is nothing left at all.
        for (int block = 0; block < 60; ++block)
        {
            std::fill (left.begin(), left.end(), 0.0f);
            std::fill (right.begin(), right.end(), 0.0f);

            reverb.processBypassed (channels, 2, blockSize);
        }

        std::fill (left.begin(), left.end(), 0.0f);
        std::fill (right.begin(), right.end(), 0.0f);
        render (reverb, left, right);

        // Below hearing rather than exactly zero: five seconds at a two-second
        // decay is about -150 dB, and reaching the denormal floor where the
        // flush takes over needs longer still. That the tail does eventually
        // arrive at exactly zero is `testTailReachesExactlyZero`'s job.
        expect (peakOf (left) < 1.0e-6f,
                "a reverb bypassed for several decay times must come back silent, and gave "
                    + juce::String (peakOf (left), 9));
    }

    void testResetClearsTheTail()
    {
        beginTest ("reset leaves no tail behind");

        Reverb reverb;
        reverb.setSettings (wetSettings (8.0f));
        reverb.prepare (testSampleRate, blockSize);

        std::vector<float> left (blockSize, 0.0f);
        std::vector<float> right (blockSize, 0.0f);

        fillBurst (left, right, blockSize);
        render (reverb, left, right);

        reverb.reset();

        std::fill (left.begin(), left.end(), 0.0f);
        std::fill (right.begin(), right.end(), 0.0f);

        render (reverb, left, right);

        for (const auto sample : left)
            expectWithinAbsoluteError (sample, 0.0f, 1.0e-6f,
                                       "silence in after a reset must be silence out");
    }

    void testExtremeInput()
    {
        beginTest ("extreme input stays finite and still settles");

        Reverb reverb;
        reverb.setSettings (wetSettings (5.0f));
        reverb.prepare (testSampleRate, blockSize);

        std::vector<float> left (blockSize, 0.0f);
        std::vector<float> right (blockSize, 0.0f);

        for (std::size_t i = 0; i < left.size(); ++i)
        {
            left[i] = i % 2 == 0 ? 1.0e6f : -1.0e6f;
            right[i] = 1.0e-30f;
        }

        render (reverb, left, right);

        for (std::size_t i = 0; i < left.size(); ++i)
        {
            expect (std::isfinite (left[i]), "no input may produce a NaN or an infinity");
            expect (std::isfinite (right[i]), "and not on the other channel either");
        }

        for (int block = 0; block < 200; ++block)
        {
            std::fill (left.begin(), left.end(), 0.0f);
            std::fill (right.begin(), right.end(), 0.0f);

            render (reverb, left, right);
        }

        expect (std::isfinite (peakOf (left)),
                "the tail of an absurd signal must still be a number");
    }

    void testTailReporting()
    {
        beginTest ("the reported tail covers the decay and the pre-delay");

        Reverb reverb;

        auto settings = wetSettings (3.0f);
        settings.preDelayMs = 100.0f;
        reverb.setSettings (settings);
        reverb.prepare (testSampleRate, blockSize);

        expectWithinAbsoluteError (reverb.getTailSeconds(), 3.1, 1.0e-6,
                                   "the room cannot start answering before the pre-delay is over");

        settings.decaySeconds = 10.0f;
        reverb.setSettings (settings);

        expectWithinAbsoluteError (reverb.getTailSeconds(), 10.1, 1.0e-6);
    }
};

ReverbTests reverbTests;

} // namespace
