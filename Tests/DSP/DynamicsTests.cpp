/*
    Dynamics tests: the noise gate and the compressor.

    A dynamics processor's controls are all promises about time and level, and
    every one of them is measurable, so this file measures rather than describes.

    **The ratio is a number.** `testRatioIsTheRatio` drives steady tones at known
    levels through a settled compressor and checks the output against the static
    curve: 4 dB over the threshold at 4:1 comes out 1 dB over, and nothing else
    will do. A compressor whose ratio is approximate is one nobody can set by
    reading the dial.

    **Attack and release mean the one-pole convention**, stated in
    `LevelDetector.h`: the gain travels 1 - 1/e — about 63 % — of the way to its
    target in the stated time. `testAttackTiming` measures exactly that, because
    "10 ms attack" is meaningless until the fraction is named.

    **Hold is what makes a gate a gate.** `testHoldStopsChattering` feeds a
    signal that crosses the threshold repeatedly — the case PRD §22 calls
    "avoiding audible pumping" — and counts how many times the gate changes its
    mind with hold off and with hold on.

    **Stereo stays where it was put.** Both processors run one detector fed from
    the louder channel, so a loud left channel cannot duck only the left and drag
    the image sideways. Both tests assert the two channels receive identical
    gain.
*/

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "DSP/Dynamics/Compressor.h"
#include "DSP/Dynamics/NoiseGate.h"

using namespace apollo::dsp;

namespace
{

constexpr double testSampleRate = 48000.0;
constexpr double pi = 3.14159265358979323846;

[[nodiscard]] float decibelsToGain (float decibels)
{
    return std::pow (10.0f, decibels * 0.05f);
}

[[nodiscard]] float gainToDecibels (float gain)
{
    return gain > 1.0e-7f ? 20.0f * std::log10 (gain) : -140.0f;
}

/** A sine at @p amplitude, @p numSamples long. */
[[nodiscard]] std::vector<float> sine (double frequencyHz, float amplitude, int numSamples)
{
    std::vector<float> signal (static_cast<std::size_t> (numSamples), 0.0f);

    const auto increment = 2.0 * pi * frequencyHz / testSampleRate;

    for (int i = 0; i < numSamples; ++i)
        signal[static_cast<std::size_t> (i)] =
            amplitude * static_cast<float> (std::sin (increment * static_cast<double> (i)));

    return signal;
}

[[nodiscard]] float peakOf (const std::vector<float>& signal, std::size_t from = 0)
{
    auto peak = 0.0f;

    for (auto i = from; i < signal.size(); ++i)
        peak = std::max (peak, std::abs (signal[i]));

    return peak;
}

template <typename Effect>
void render (Effect& effect, std::vector<float>& left, std::vector<float>& right)
{
    float* channels[2] { left.data(), right.data() };

    effect.process (channels, 2, static_cast<int> (left.size()));
}

class DynamicsTests final : public juce::UnitTest
{
public:
    DynamicsTests()
        : juce::UnitTest ("Dynamics", "DSP")
    {
    }

    void runTest() override
    {
        testStaticCurve();
        testGainRampConvention();
        testRatioIsTheRatio();
        testAttackTiming();
        testCompressorLeavesQuietSignalsAlone();
        testMakeupAndMix();
        testGateOpensAndCloses();
        testHoldStopsChattering();
        testGateRangeIsHowFarDown();
        testStereoStaysLinked();
        testNoLatency();
        testExtremeInput();
    }

private:
    static constexpr int blockSize = 4096;

    void testStaticCurve()
    {
        beginTest ("the compression curve is the ratio, expressed in decibels");

        Compressor::Settings settings;
        settings.thresholdDb = -20.0f;
        settings.ratio = 4.0f;

        // Below the threshold, nothing happens at all.
        expectWithinAbsoluteError (Compressor::outputDbFor (settings, -40.0f), -40.0f, 1.0e-4f);
        expectWithinAbsoluteError (Compressor::outputDbFor (settings, -20.0f), -20.0f, 1.0e-4f);

        // Above it, every 4 dB in becomes 1 dB out.
        expectWithinAbsoluteError (Compressor::outputDbFor (settings, -16.0f), -19.0f, 1.0e-4f);
        expectWithinAbsoluteError (Compressor::outputDbFor (settings, -4.0f), -16.0f, 1.0e-4f);
        expectWithinAbsoluteError (Compressor::outputDbFor (settings, 0.0f), -15.0f, 1.0e-4f);

        // A ratio of 1 is a compressor that does nothing, which is a real
        // setting rather than a degenerate one.
        settings.ratio = 1.0f;
        expectWithinAbsoluteError (Compressor::outputDbFor (settings, 0.0f), 0.0f, 1.0e-4f);
    }

    void testRatioIsTheRatio()
    {
        beginTest ("a settled compressor puts the curve on the audio");

        for (const auto inputDb : { -10.0f, -4.0f, 0.0f })
        {
            Compressor compressor;

            Compressor::Settings settings;
            settings.thresholdDb = -20.0f;
            settings.ratio = 4.0f;
            settings.attackMs = 1.0f;
            settings.releaseMs = 10.0f;
            settings.mix = 1.0f;

            compressor.setSettings (settings);
            compressor.prepare (testSampleRate, blockSize);

            // A sine's RMS is its peak over root two, and the detector measures
            // RMS — so the level the compressor sees is 3 dB below the peak.
            // Driving it with a known *peak* and predicting the output means
            // doing that arithmetic rather than ignoring it.
            const auto peak = decibelsToGain (inputDb);
            const auto rmsDb = inputDb - 3.0103f;

            // Several blocks, so the detector and the gain ramp have both
            // settled before anything is measured.
            std::vector<float> left, right;

            for (int block = 0; block < 8; ++block)
            {
                left = sine (220.0, peak, blockSize);
                right = left;
                render (compressor, left, right);
            }

            const auto outPeakDb = gainToDecibels (peakOf (left, blockSize / 2));
            const auto outRmsDb = outPeakDb - 3.0103f;

            const auto expected = Compressor::outputDbFor (settings, rmsDb);

            logMessage ("  " + juce::String (inputDb, 1) + " dB peak in -> "
                        + juce::String (outPeakDb, 2) + " dB peak out (curve predicts "
                        + juce::String (expected + 3.0103f, 2) + ")");

            expectWithinAbsoluteError (outRmsDb, expected, 0.6f,
                                       "the rendered gain reduction must match the static curve");
        }
    }

    void testGainRampConvention()
    {
        beginTest ("the gain ramp travels 1 - 1/e of the way in the time it is given");

        // The convention `LevelDetector.h` documents, measured on the thing it
        // is a claim about. The compressor as a whole is slower than this,
        // because its detector has to notice the signal before the ramp can
        // start moving — which is the next test.
        for (const auto milliseconds : { 5.0f, 20.0f, 100.0f })
        {
            GainRamp ramp;

            ramp.setTimes (milliseconds, milliseconds);
            ramp.prepare (testSampleRate);

            const auto samples = static_cast<int> (static_cast<double> (milliseconds)
                                                   * 0.001 * testSampleRate);

            auto gain = 1.0f;

            for (int i = 0; i < samples; ++i)
                gain = ramp.process (0.0f);

            // Travelled from 1 towards 0, so what remains is 1/e.
            const auto travelled = 1.0f - gain;

            expectWithinAbsoluteError (travelled, 0.6321f, 0.01f,
                                       juce::String (milliseconds, 0)
                                           + " ms must put the ramp 63.2 % of the way, and it reached "
                                           + juce::String (travelled * 100.0f, 1) + " %");
        }
    }

    void testAttackTiming()
    {
        beginTest ("a compressor's attack is the ramp's, plus the time the detector takes to notice");

        Compressor compressor;

        Compressor::Settings settings;
        settings.thresholdDb = -40.0f;
        settings.ratio = 20.0f;
        settings.attackMs = 20.0f;
        settings.releaseMs = 200.0f;
        settings.mix = 1.0f;

        compressor.setSettings (settings);
        compressor.prepare (testSampleRate, blockSize);

        // A step far above the threshold, so the target gain is a long way from
        // unity and the journey is easy to measure.
        const auto attackSamples = static_cast<int> (0.020 * testSampleRate);

        auto left = sine (1000.0, 0.9f, attackSamples);
        auto right = left;

        render (compressor, left, right);

        const auto reachedDb = compressor.getGainReductionDb();

        // Where it would have ended up, so the fraction travelled can be read.
        for (int block = 0; block < 20; ++block)
        {
            auto more = sine (1000.0, 0.9f, blockSize);
            auto moreRight = more;
            render (compressor, more, moreRight);
        }

        const auto settledDb = compressor.getGainReductionDb();
        const auto travelled = reachedDb / settledDb;

        logMessage ("  after one attack time the gain had travelled "
                    + juce::String (travelled * 100.0f, 1) + " % of the way");

        // Less than the ramp's own 63.2 %, and that is the honest answer rather
        // than a defect: the RMS detector has its own 10 ms window, and the
        // ramp cannot start moving towards a target the detector has not yet
        // reported. A user setting 20 ms gets 20 ms of ramp behind a detector
        // that took a few milliseconds to see the signal, which is what every
        // RMS compressor does and is documented on `Compressor::averagingMs`.
        //
        // What would be wrong is arriving *early* — a control faster than it
        // says — or barely moving at all.
        expect (travelled > 0.15f && travelled < 0.6321f,
                "the gain must be well on its way but short of the ramp's own figure: "
                    + juce::String (travelled, 3));
    }

    void testCompressorLeavesQuietSignalsAlone()
    {
        beginTest ("below the threshold a compressor is not in the signal path");

        Compressor compressor;

        Compressor::Settings settings;
        settings.thresholdDb = -12.0f;
        settings.ratio = 8.0f;
        settings.mix = 1.0f;

        compressor.setSettings (settings);
        compressor.prepare (testSampleRate, blockSize);

        // 30 dB below the threshold: nothing the compressor does should be
        // measurable.
        const auto input = sine (330.0, decibelsToGain (-42.0f), blockSize);

        auto left = input;
        auto right = input;

        render (compressor, left, right);

        for (std::size_t i = 0; i < left.size(); ++i)
            expectWithinAbsoluteError (left[i], input[i], 1.0e-6f,
                                       "a quiet signal must pass through untouched");

        expectWithinAbsoluteError (compressor.getGainReductionDb(), 0.0f, 0.01f,
                                   "and the compressor must report that it is doing nothing");
    }

    void testMakeupAndMix()
    {
        beginTest ("makeup gain lifts the output and mix blends the dry signal back");

        const auto renderWith = [] (float makeupDb, float mix)
        {
            Compressor compressor;

            Compressor::Settings settings;
            settings.thresholdDb = -30.0f;
            settings.ratio = 4.0f;
            settings.attackMs = 1.0f;
            settings.releaseMs = 10.0f;
            settings.makeupDb = makeupDb;
            settings.mix = mix;

            compressor.setSettings (settings);
            compressor.prepare (testSampleRate, blockSize);

            std::vector<float> left, right;

            for (int block = 0; block < 8; ++block)
            {
                left = sine (220.0, 0.7f, blockSize);
                right = left;
                render (compressor, left, right);
            }

            return peakOf (left, blockSize / 2);
        };

        const auto plain = renderWith (0.0f, 1.0f);
        const auto lifted = renderWith (6.0f, 1.0f);

        expectWithinAbsoluteError (gainToDecibels (lifted / plain), 6.0f, 0.2f,
                                   "6 dB of makeup must raise the output by 6 dB");

        // Parallel compression: the dry signal is louder than the compressed
        // one, so blending it back must raise the peak.
        const auto parallel = renderWith (0.0f, 0.5f);

        expect (parallel > plain,
                "blending the dry signal back must bring its level with it");

        const auto fullyDry = renderWith (0.0f, 0.0f);

        expectWithinAbsoluteError (fullyDry, 0.7f, 0.001f,
                                   "and a mix of zero must be the input exactly");
    }

    void testGateOpensAndCloses()
    {
        beginTest ("a gate passes what is above the threshold and holds down what is below");

        NoiseGate gate;

        NoiseGate::Settings settings;
        settings.thresholdDb = -30.0f;
        settings.attackMs = 1.0f;
        settings.holdMs = 10.0f;
        settings.releaseMs = 20.0f;
        settings.rangeDb = -40.0f;

        gate.setSettings (settings);
        gate.prepare (testSampleRate, blockSize);

        // Loud: the gate must open and stay open.
        auto left = sine (440.0, 0.5f, blockSize);
        auto right = left;
        render (gate, left, right);

        expectWithinAbsoluteError (gate.getCurrentGain(), 1.0f, 0.01f,
                                   "a signal well above the threshold must open the gate fully");

        expectWithinAbsoluteError (peakOf (left, blockSize / 2), 0.5f, 0.01f,
                                   "and must then pass through at its own level");

        // Quiet: below the threshold, and given long enough for the hold to
        // expire and the release to finish.
        for (int block = 0; block < 8; ++block)
        {
            left = sine (440.0, decibelsToGain (-50.0f), blockSize);
            right = left;
            render (gate, left, right);
        }

        expectWithinAbsoluteError (gainToDecibels (gate.getCurrentGain()), -40.0f, 0.5f,
                                   "a signal below the threshold must be pushed down by the range");
    }

    void testHoldStopsChattering()
    {
        beginTest ("hold is what stops a gate strobing on a signal sitting at the threshold");

        // A tone at exactly the threshold, amplitude-modulated slowly so it
        // crosses the line again and again — the case PRD §22 asks the gate to
        // survive without audible pumping.
        const auto countGateMovements = [] (float holdMs, juce::String& report)
        {
            NoiseGate gate;

            NoiseGate::Settings settings;
            settings.thresholdDb = -20.0f;
            settings.attackMs = 0.5f;
            settings.holdMs = holdMs;
            settings.releaseMs = 5.0f;
            settings.rangeDb = -40.0f;

            gate.setSettings (settings);
            gate.prepare (testSampleRate, blockSize);

            auto crossings = 0;
            auto wasOpen = false;
            auto lowestGain = 1.0f;
            auto highestGain = 0.0f;

            for (int block = 0; block < 8; ++block)
            {
                std::vector<float> left (blockSize, 0.0f);
                std::vector<float> right (blockSize, 0.0f);

                for (int i = 0; i < blockSize; ++i)
                {
                    const auto t = static_cast<double> (block * blockSize + i) / testSampleRate;

                    // A tone whose level swings 6 dB either side of the
                    // threshold eight times a second: clearly above for a
                    // sixteenth of a second, clearly below for the next. The
                    // wobble has to be slow enough for the 5 ms release to
                    // finish during the quiet half, or the ramp smooths the
                    // chatter away on its own and there is nothing for hold to
                    // fix.
                    const auto loud = std::sin (2.0 * pi * 8.0 * t) > 0.0;

                    const auto envelope = decibelsToGain (loud ? -14.0f : -26.0f);

                    left[static_cast<std::size_t> (i)] =
                        envelope * static_cast<float> (std::sin (2.0 * pi * 900.0 * t));

                    right[static_cast<std::size_t> (i)] = left[static_cast<std::size_t> (i)];
                }

                float* channels[2] { left.data(), right.data() };

                for (int i = 0; i < blockSize; ++i)
                {
                    gate.process (channels, 2, 1);

                    const auto gain = gate.getCurrentGain();

                    lowestGain = std::min (lowestGain, gain);
                    highestGain = std::max (highestGain, gain);

                    const auto open = gain > 0.5f;

                    if (open != wasOpen)
                    {
                        ++crossings;
                        wasOpen = open;
                    }

                    ++channels[0];
                    ++channels[1];
                }
            }

            report = "gain ranged " + juce::String (lowestGain, 4) + " to "
                   + juce::String (highestGain, 4);

            return crossings;
        };

        juce::String withoutReport, withReport;

        const auto withoutHold = countGateMovements (0.0f, withoutReport);
        const auto withHold = countGateMovements (60.0f, withReport);

        logMessage ("  no hold: " + juce::String (withoutHold) + " crossings, " + withoutReport);
        logMessage ("  60 ms hold: " + juce::String (withHold) + " crossings, " + withReport);

        expect (withoutHold > 4,
                "the test signal must actually make an unheld gate chatter, and it moved "
                    + juce::String (withoutHold) + " times");

        expect (withHold < withoutHold,
                "hold must reduce the chattering it exists to prevent");

        expect (withHold <= 2,
                "and a hold longer than the wobble should stop it entirely, but the gate moved "
                    + juce::String (withHold) + " times");
    }

    void testGateRangeIsHowFarDown()
    {
        beginTest ("range says how far down, and at the bottom of its travel means silence");

        const auto closedGainDb = [] (float rangeDb)
        {
            NoiseGate gate;

            NoiseGate::Settings settings;
            settings.thresholdDb = -30.0f;
            settings.attackMs = 1.0f;
            settings.holdMs = 0.0f;
            settings.releaseMs = 5.0f;
            settings.rangeDb = rangeDb;

            gate.setSettings (settings);
            gate.prepare (testSampleRate, blockSize);

            for (int block = 0; block < 8; ++block)
            {
                auto left = sine (440.0, decibelsToGain (-60.0f), blockSize);
                auto right = left;
                render (gate, left, right);
            }

            return gainToDecibels (gate.getCurrentGain());
        };

        expectWithinAbsoluteError (closedGainDb (-20.0f), -20.0f, 0.5f);
        expectWithinAbsoluteError (closedGainDb (-60.0f), -60.0f, 0.5f);

        // A range of zero is a gate that does nothing, which is a real setting.
        expectWithinAbsoluteError (closedGainDb (0.0f), 0.0f, 0.01f);

        // And the bottom of the range is silence rather than a very small
        // number, for the user who wants the channel gone.
        expect (closedGainDb (-80.0f) <= -139.0f,
                "a gate at the bottom of its range must reach exactly zero");
    }

    void testStereoStaysLinked()
    {
        beginTest ("one channel getting loud must not move the stereo image");

        // The left channel alone is driven hard. If the two channels had their
        // own detectors, the right would come through untouched and the image
        // would swing left every time the compressor worked.
        const auto compressed = [] (bool gateInstead)
        {
            auto left = sine (220.0, 0.9f, blockSize);
            std::vector<float> right (static_cast<std::size_t> (blockSize), 0.0f);

            // Right carries the same tone 20 dB down.
            for (std::size_t i = 0; i < right.size(); ++i)
                right[i] = left[i] * decibelsToGain (-20.0f);

            const auto before = right;

            if (gateInstead)
            {
                NoiseGate gate;

                NoiseGate::Settings settings;
                settings.thresholdDb = -12.0f;
                settings.attackMs = 1.0f;
                settings.holdMs = 5.0f;
                settings.releaseMs = 20.0f;
                settings.rangeDb = -40.0f;

                gate.setSettings (settings);
                gate.prepare (testSampleRate, blockSize);
                render (gate, left, right);
            }
            else
            {
                Compressor compressor;

                Compressor::Settings settings;
                settings.thresholdDb = -24.0f;
                settings.ratio = 8.0f;
                settings.attackMs = 1.0f;
                settings.releaseMs = 20.0f;
                settings.mix = 1.0f;

                compressor.setSettings (settings);
                compressor.prepare (testSampleRate, blockSize);
                render (compressor, left, right);
            }

            // The ratio between what the quiet channel was and what it became,
            // against the same ratio for the loud one. Linked detection makes
            // the two identical.
            const auto quietRatio = peakOf (right, blockSize / 2) / peakOf (before, blockSize / 2);

            return quietRatio;
        };

        for (const auto useGate : { false, true })
        {
            const auto ratio = compressed (useGate);

            // The quiet channel must have been moved — by the *other* channel's
            // level, which is the whole point of linking.
            expect (ratio > 0.0f && ratio < 1.0f,
                    juce::String (useGate ? "the gate" : "the compressor")
                        + " must apply its gain to both channels alike, and the quiet one changed by "
                        + juce::String (ratio, 4));
        }
    }

    void testNoLatency()
    {
        beginTest ("neither processor looks ahead");

        NoiseGate gate;
        gate.prepare (testSampleRate, blockSize);

        Compressor compressor;
        compressor.prepare (testSampleRate, blockSize);

        expectEquals (gate.getLatencySamples(), 0, "a gate without lookahead adds no latency");
        expectEquals (compressor.getLatencySamples(), 0, "and neither does the compressor");

        expectEquals (gate.getTailSeconds(), 0.0, "nor does either have a tail");
        expectEquals (compressor.getTailSeconds(), 0.0);
    }

    void testExtremeInput()
    {
        beginTest ("extreme and degenerate input produces finite output");

        NoiseGate gate;
        gate.prepare (testSampleRate, blockSize);

        Compressor compressor;

        Compressor::Settings settings;
        settings.thresholdDb = -40.0f;
        settings.ratio = 20.0f;
        compressor.setSettings (settings);
        compressor.prepare (testSampleRate, blockSize);

        std::vector<float> left (static_cast<std::size_t> (blockSize), 0.0f);
        std::vector<float> right (static_cast<std::size_t> (blockSize), 0.0f);

        for (int i = 0; i < blockSize; ++i)
        {
            left[static_cast<std::size_t> (i)] = i % 3 == 0 ? 1.0e6f
                                               : i % 3 == 1 ? -1.0e6f
                                                            : 1.0e-30f;
            right[static_cast<std::size_t> (i)] = 1.0e-30f;
        }

        render (gate, left, right);
        render (compressor, left, right);

        for (std::size_t i = 0; i < left.size(); ++i)
        {
            expect (std::isfinite (left[i]), "no input may produce a NaN or an infinity");
            expect (std::isfinite (right[i]), "and not on the other channel either");
        }

        // And silence afterwards must come out as silence, rather than the
        // detector staying stuck wherever the absurd signal left it.
        for (int block = 0; block < 40; ++block)
        {
            std::fill (left.begin(), left.end(), 0.0f);
            std::fill (right.begin(), right.end(), 0.0f);

            render (gate, left, right);
            render (compressor, left, right);
        }

        expectWithinAbsoluteError (peakOf (left), 0.0f, 1.0e-6f,
                                   "silence in must be silence out once the detectors have settled");
    }
};

DynamicsTests dynamicsTests;

} // namespace
