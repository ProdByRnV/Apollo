/*
    The noise's built-in transients (Phase 13d-2, PRD §10).

    The library: four samples, each finite, peaking at exactly 1, ending in
    silence, and the length it was designed to be.

    The mipmap, given a signal whose spectrum is known: a tone inside a level's
    band survives into it at its level, and a tone above the band is gone.
    Then the reason for the mipmap, through the player: a 15 kHz tone played
    an octave up would fold back to 18 kHz from the full-band level, and from
    the level the player picks it is not there to fold.

    The player: at its own pitch and rate it returns the sample exactly; it
    plays once and stops; a trigger restarts it; it takes twice as many
    samples at twice the rate; and it picks the level the pitch needs.
*/

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

#include "DSP/Noise/NoiseSource.h"
#include "DSP/Noise/TransientLibrary.h"

using namespace apollo::dsp;

namespace
{

constexpr double referenceRate = TransientSample::referenceRate;

[[nodiscard]] std::vector<double> tone (double hz, int frames)
{
    std::vector<double> out (static_cast<std::size_t> (frames));

    for (std::size_t n = 0; n < out.size(); ++n)
        out[n] = std::sin (2.0 * std::numbers::pi * hz * static_cast<double> (n) / referenceRate);

    return out;
}

/** The level of a tone at @p hz in @p signal, sampled at @p sampleRate, in dB
    against a full-scale sine — measured by correlating against the tone over
    the middle of the signal, away from the edges the filters smear. */
[[nodiscard]] double toneLevel (const std::vector<float>& signal, double hz, double sampleRate)
{
    const auto from = signal.size() / 4;
    const auto to = signal.size() * 3 / 4;
    double re = 0.0, im = 0.0;

    for (auto n = from; n < to; ++n)
    {
        const auto phase = 2.0 * std::numbers::pi * hz * static_cast<double> (n) / sampleRate;
        re += static_cast<double> (signal[n]) * std::cos (phase);
        im += static_cast<double> (signal[n]) * std::sin (phase);
    }

    const auto amplitude = 2.0 * std::sqrt (re * re + im * im) / static_cast<double> (to - from);
    return 20.0 * std::log10 (std::max (amplitude, 1.0e-12));
}

[[nodiscard]] double rmsOf (const std::vector<float>& signal, std::size_t from, std::size_t to)
{
    double sum = 0.0;

    for (auto n = from; n < to; ++n)
        sum += static_cast<double> (signal[n]) * static_cast<double> (signal[n]);

    return std::sqrt (sum / static_cast<double> (std::max<std::size_t> (to - from, 1)));
}

/** Plays @p sample from a trigger, one channel, for @p frames samples. */
[[nodiscard]] std::vector<float> play (const TransientSample& sample, float pitch, double sampleRate, int frames)
{
    NoiseSource source;
    source.prepare (sampleRate);
    source.setType (NoiseType::click, &sample);
    source.setRate (pitch);
    source.trigger();

    std::vector<float> left (static_cast<std::size_t> (frames), 0.0f);
    std::vector<float> right (static_cast<std::size_t> (frames), 0.0f);

    for (std::size_t n = 0; n < left.size(); ++n)
        source.addNextStereoSample (left[n], right[n], 1.0f, 1.0f);

    return left;
}

class TransientTests final : public juce::UnitTest
{
public:
    TransientTests()
        : juce::UnitTest ("Noise transients", "DSP")
    {
    }

    void runTest() override
    {
        testTheLibrary();
        testEachLevelKeepsItsBand();
        testPitchingUpDoesNotAlias();
        testPitchingDownKeepsTheTone();
        testPlaysTheSampleExactly();
        testPlaysOnceAndRetriggers();
        testFollowsTheSampleRate();
        testPicksTheLevelThePitchNeeds();
        testMisuseIsSilentNotUndefined();
    }

private:
    void testTheLibrary()
    {
        beginTest ("Four transients, finite, peaking at 1, ending in silence, as long as designed");

        const auto& library = TransientLibrary::shared();
        const double designed[] { 0.015, 0.08, 0.25, 0.3 };
        const char* names[] { "click", "pick", "thump", "chiff" };

        for (int i = 0; i < TransientLibrary::numTransients; ++i)
        {
            const auto& sample = library.get (i);
            const auto& top = sample.levels[0];
            auto peak = 0.0f;
            auto finite = true;

            for (const auto* channel : { &top.left, &top.right })
                for (const auto value : *channel)
                {
                    finite = finite && std::isfinite (value);
                    peak = std::max (peak, std::abs (value));
                }

            const auto seconds = static_cast<double> (sample.length()) / referenceRate;
            const auto last = std::max (std::abs (top.left.back()), std::abs (top.right.back()));

            logMessage ("  " + juce::String (names[i]) + ": " + juce::String (seconds * 1000.0, 1)
                        + " ms, last sample " + juce::String (last, 6));

            expect (finite, juce::String (names[i]) + " is not finite");
            expectWithinAbsoluteError (peak, 1.0f, 1.0e-6f, juce::String (names[i]) + " does not peak at 1");
            expectWithinAbsoluteError (seconds, designed[i], 1.0e-3, juce::String (names[i]) + " is the wrong length");
            expect (last < 1.0e-3f, juce::String (names[i]) + " ends on a step");

            for (int k = 1; k < TransientSample::numLevels; ++k)
                expectEquals (static_cast<int> (sample.levels[static_cast<std::size_t> (k)].left.size()),
                              (sample.length() + (1 << k) - 1) >> k,
                              juce::String (names[i]) + " level " + juce::String (k) + " is the wrong length");
        }
    }

    void testEachLevelKeepsItsBand()
    {
        beginTest ("Each level keeps a tone inside its band and removes one above it");

        constexpr int frames = 48000;

        for (int k = 1; k < TransientSample::numLevels; ++k)
        {
            const auto rate = referenceRate / static_cast<double> (1 << k);
            const auto top = TransientSample::referenceBandwidth / static_cast<double> (1 << k);

            // Inside: 80 % of the band. Above: just past the old band's half,
            // where it would land if the decimation let it through.
            const auto inside = makeTransient (tone (0.8 * top, frames), tone (0.8 * top, frames));
            const auto above = makeTransient (tone (1.25 * top, frames), tone (1.25 * top, frames));

            // Both against the same tone at level 0, which is not quite full
            // scale: normalising puts the loudest *sample* at 1, and a sine
            // sampled at 48 kHz rarely has a sample on its peak.
            const auto kept = toneLevel (inside.levels[static_cast<std::size_t> (k)].left, 0.8 * top, rate)
                            - toneLevel (inside.levels[0].left, 0.8 * top, referenceRate);

            const auto& removedLevel = above.levels[static_cast<std::size_t> (k)].left;
            const auto removed = 20.0 * std::log10 (std::max (rmsOf (removedLevel, removedLevel.size() / 4,
                                                                     removedLevel.size() * 3 / 4) * std::sqrt (2.0),
                                                              1.0e-12))
                               - toneLevel (above.levels[0].left, 1.25 * top, referenceRate);

            logMessage ("  level " + juce::String (k) + ": a tone at 80 % of its band "
                        + juce::String (kept, 2) + " dB, one at 125 % " + juce::String (removed, 1) + " dB");

            expect (std::abs (kept) < 0.1, "level " + juce::String (k) + " lost a tone inside its band");
            expect (removed < -70.0, "level " + juce::String (k) + " kept a tone above its band");
        }
    }

    void testPitchingUpDoesNotAlias()
    {
        beginTest ("A 15 kHz tone played an octave up at 48 kHz does not fold back");

        const auto sample = makeTransient (tone (15000.0, 48000), tone (15000.0, 48000));

        // From level 0 it would be read at 30 kHz and fold to 18 kHz. From the
        // level the player picks for an octave up, which stops at 10 kHz, it
        // is not there at all.
        const auto played = play (sample, 2.0f, 48000.0, 24000);
        const auto folded = toneLevel (played, 18000.0, 48000.0);

        logMessage ("  at 18 kHz, where it would fold: " + juce::String (folded, 1) + " dB");
        expect (folded < -70.0, "the tone folded back at " + juce::String (folded, 1) + " dB");
    }

    void testPitchingDownKeepsTheTone()
    {
        beginTest ("A tone played an octave down lands an octave down, at its level");

        const auto sample = makeTransient (tone (3000.0, 48000), tone (3000.0, 48000));
        const auto played = play (sample, 0.5f, 48000.0, 96000);
        const auto level = toneLevel (played, 1500.0, 48000.0);

        logMessage ("  3 kHz an octave down: 1.5 kHz at " + juce::String (level, 3) + " dB");
        expect (std::abs (level) < 0.1, "the tone came out at " + juce::String (level, 2) + " dB");
    }

    void testPlaysTheSampleExactly()
    {
        beginTest ("At its own pitch and rate, the player returns the sample exactly");

        const auto& sample = TransientLibrary::shared().get (1);
        const auto played = play (sample, 1.0f, referenceRate, sample.length());

        expect (played == sample.levels[0].left, "the sample did not come back exactly");
    }

    void testPlaysOnceAndRetriggers()
    {
        beginTest ("A transient plays once, stops, and a trigger starts it again");

        const auto& sample = TransientLibrary::shared().get (0);
        const auto length = static_cast<std::size_t> (sample.length());

        NoiseSource source;
        source.prepare (referenceRate);
        source.setType (NoiseType::click, &sample);

        std::vector<float> left (3 * length, 0.0f), right (3 * length, 0.0f);

        // Not yet triggered: silent, and says so.
        expect (! source.isSounding(), "an untriggered transient claims to be sounding");

        source.trigger();

        for (std::size_t n = 0; n < 2 * length; ++n)
            source.addNextStereoSample (left[n], right[n], 1.0f, 1.0f);

        expect (! source.isSounding(), "a finished transient claims to be sounding");
        expect (rmsOf (left, 0, length) > 0.01, "the transient did not play");
        expectEquals (rmsOf (left, length, 2 * length), 0.0, "the transient did not stop");

        source.trigger();

        for (auto n = 2 * length; n < 3 * length; ++n)
            source.addNextStereoSample (left[n], right[n], 1.0f, 1.0f);

        expect (std::equal (left.begin(), left.begin() + static_cast<std::ptrdiff_t> (length),
                            left.begin() + static_cast<std::ptrdiff_t> (2 * length)),
                "a retrigger did not play the same transient again");
    }

    void testFollowsTheSampleRate()
    {
        beginTest ("At twice the rate a transient takes twice the samples and sounds the same");

        const auto& sample = TransientLibrary::shared().get (2);
        const auto at96 = play (sample, 1.0f, 96000.0, 2 * sample.length() + 100);

        std::size_t last = 0;

        for (std::size_t n = 0; n < at96.size(); ++n)
            if (at96[n] != 0.0f)
                last = n;

        expectWithinAbsoluteError (static_cast<double> (last), 2.0 * sample.length(), 3.0,
                                   "the transient is the wrong length at 96 kHz");

        // Every other sample at 96 kHz is a sample of the original.
        auto worst = 0.0f;

        for (std::size_t n = 0; n < static_cast<std::size_t> (sample.length()); ++n)
            worst = std::max (worst, std::abs (at96[2 * n] - sample.levels[0].left[n]));

        expectEquals (worst, 0.0f, "at 96 kHz the original samples did not come through");
    }

    void testPicksTheLevelThePitchNeeds()
    {
        beginTest ("The player reads the first level whose top lands below Nyquist");

        const auto& sample = TransientLibrary::shared().get (0);

        const auto levelFor = [&sample] (float pitch, double sampleRate)
        {
            NoiseSource source;
            source.prepare (sampleRate);
            source.setType (NoiseType::click, &sample);
            source.setRate (pitch);
            return source.getTransientLevel();
        };

        expectEquals (levelFor (1.0f, 48000.0), 0, "at its own pitch");
        expectEquals (levelFor (1.0f, 44100.0), 0, "at 44.1 kHz, where 20 kHz still fits");
        expectEquals (levelFor (0.25f, 48000.0), 0, "below its own pitch");
        expectEquals (levelFor (1.1f, 48000.0), 0, "a little above, where 22 kHz still fits");
        expectEquals (levelFor (1.3f, 48000.0), 1, "26 kHz does not fit");
        expectEquals (levelFor (2.0f, 48000.0), 1, "an octave up");
        expectEquals (levelFor (8.0f, 48000.0), 3, "three octaves up");
        expectEquals (levelFor (2.0f, 96000.0), 0, "an octave up at 96 kHz, where 40 kHz fits");
        expectEquals (levelFor (1000.0f, 48000.0), TransientSample::numLevels - 1, "far past the last level");
    }

    void testMisuseIsSilentNotUndefined()
    {
        beginTest ("A transient type without a sample, or a nonsense rate, is safe");

        NoiseSource source;
        source.prepare (48000.0);
        source.setType (NoiseType::chiff, nullptr);
        source.trigger();

        float left = 0.0f, right = 0.0f;

        for (int n = 0; n < 1000; ++n)
            source.addNextStereoSample (left, right, 1.0f, 1.0f);

        expectEquals (left, 0.0f, "a transient with no sample made a sound");

        const auto& sample = TransientLibrary::shared().get (3);

        for (const auto pitch : { 0.0f, -2.0f, std::numeric_limits<float>::quiet_NaN(),
                                  std::numeric_limits<float>::infinity() })
            expect (play (sample, pitch, 48000.0, 2000) == play (sample, 1.0f, 48000.0, 2000),
                    "rate " + juce::String (pitch) + " was not taken as 1");
    }
};

TransientTests transientTests;

} // namespace
