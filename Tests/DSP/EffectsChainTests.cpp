/*
    Whole-chain validation: the claims that only mean something once every
    effect is in the rack at the same time.

    The other effect test files each hold one processor to its own contract, and
    `EffectsRackTests.cpp` holds the rack to the rules it defines about slots,
    duplicates, latency and bypass. Neither can catch what this file is for: a
    property that is true of each effect alone and false of six of them in
    series.

    Phase 8's exit criteria are the shape of this file. "Every effect operates
    independently" is tested by switching each one out of a full chain and
    hearing the difference. "FX order can be changed safely" is tested by
    rendering **every** ordering rather than a few representative ones — 720 of
    them, which is cheap and removes the question of whether the representative
    ones were the interesting ones. "Feedback effects remain stable" is tested
    with the delay and the reverb at the top of their ranges, in the same chain,
    with a distortion in front of them.

    WHAT THIS FILE FOUND. The rack reported a chain's tail as the longest tail
    in it. That is right for one effect and wrong for two in series: a delay that
    keeps repeating for three seconds is still feeding the reverb after it, and
    the reverb then rings for its own decay beyond that. The measurement is in
    `testSeriesTailsAdd`, and ADR-0060 records the fix.
*/

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

#include "DSP/Effects/EffectsRack.h"

using namespace apollo::dsp;

namespace
{

constexpr double testSampleRate = 48000.0;
constexpr double pi = 3.14159265358979323846;
constexpr int blockSize = 256;

/** The six effects that can occupy a slot, in enum order. */
constexpr std::array<EffectType, 6> allEffects {
    EffectType::distortion, EffectType::delay, EffectType::reverb,
    EffectType::gate, EffectType::compressor, EffectType::equaliser
};

/** A block of stereo pointers into two buffers, for the rack to work on. */
struct StereoBlock
{
    explicit StereoBlock (int numSamples)
        : left (static_cast<std::size_t> (numSamples), 0.0f),
          right (static_cast<std::size_t> (numSamples), 0.0f)
    {
    }

    [[nodiscard]] float* const* channels() noexcept
    {
        pointers[0] = left.data();
        pointers[1] = right.data();

        return pointers.data();
    }

    void fillWithSine (double frequencyHz, float amplitude, int startSample = 0)
    {
        const auto increment = 2.0 * pi * frequencyHz / testSampleRate;

        for (std::size_t i = 0; i < left.size(); ++i)
        {
            const auto phase = increment * static_cast<double> (startSample + static_cast<int> (i));
            const auto value = amplitude * static_cast<float> (std::sin (phase));

            left[i] = value;
            right[i] = value;
        }
    }

    void clear()
    {
        std::fill (left.begin(), left.end(), 0.0f);
        std::fill (right.begin(), right.end(), 0.0f);
    }

    [[nodiscard]] float peak() const
    {
        auto highest = 0.0f;

        for (const auto sample : left) highest = std::max (highest, std::abs (sample));
        for (const auto sample : right) highest = std::max (highest, std::abs (sample));

        return highest;
    }

    [[nodiscard]] bool allFinite() const
    {
        for (const auto sample : left) if (! std::isfinite (sample)) return false;
        for (const auto sample : right) if (! std::isfinite (sample)) return false;

        return true;
    }

    std::vector<float> left;
    std::vector<float> right;
    std::array<float*, 2> pointers {};
};

/** Settings that make every effect audibly do something.

    Every one of them is off or transparent by default, which is right for an
    instrument and useless for a test: a chain of six effects at their defaults
    is a chain of six pass-throughs. These are deliberately *not* extreme —
    extremes get their own test — they are what someone might actually dial.
*/
void configureAudibly (EffectsRack& rack)
{
    Distortion::Settings distortion;
    distortion.mode = Distortion::Mode::soft;
    distortion.driveDb = 18.0f;
    distortion.mix = 1.0f;
    rack.distortion().setSettings (distortion);

    Delay::Settings delay;
    delay.timeMs = 60.0f;
    delay.feedback = 0.5f;
    delay.dampingHz = 8000.0f;
    delay.mix = 0.5f;
    rack.delay().setSettings (delay);

    Reverb::Settings reverb;
    reverb.decaySeconds = 1.5f;
    reverb.mix = 0.4f;
    rack.reverb().setSettings (reverb);

    // A threshold low enough that the gate stays open on the test signal: a gate
    // that shut would make every downstream assertion a measurement of silence.
    NoiseGate::Settings gate;
    gate.thresholdDb = -70.0f;
    gate.rangeDb = -40.0f;
    rack.gate().setSettings (gate);

    Compressor::Settings compressor;
    compressor.thresholdDb = -24.0f;
    compressor.ratio = 4.0f;
    compressor.makeupDb = 3.0f;
    rack.compressor().setSettings (compressor);

    auto equaliser = Equaliser::defaultSettings();
    equaliser.bands[1].gainDb = 6.0f;
    equaliser.bands[4].gainDb = -6.0f;
    equaliser.bands[4].bandwidthOctaves = 0.8f;
    rack.equaliser().setSettings (equaliser);
}

/** A chain holding @p order, in order, with nothing bypassed. */
[[nodiscard]] EffectsRack::Chain chainOf (const std::vector<EffectType>& order)
{
    EffectsRack::Chain chain {};

    for (std::size_t slot = 0; slot < chain.size() && slot < order.size(); ++slot)
        chain[slot].effect = order[slot];

    return chain;
}

class EffectsChainTests final : public juce::UnitTest
{
public:
    EffectsChainTests()
        : juce::UnitTest ("Effects chain", "DSP")
    {
    }

    void runTest() override
    {
        testEveryOrderingIsStable();
        testLatencyBelongsToTheSetNotTheOrder();
        testEveryEffectIsAudiblyInTheChain();
        testSeriesTailsAdd();
        testTheFullChainSettlesToSilence();
        testReorderingWhileRunningIsSafe();
        testOneEffectNamedSixTimesRunsOnce();
    }

private:
    /** Renders a short burst followed by silence through @p rack.

        @returns the highest sample seen anywhere, or a non-finite value if one
                 appeared — so a caller can assert both things with one number.
    */
    [[nodiscard]] float renderBurst (EffectsRack& rack, int burstBlocks, int silentBlocks)
    {
        StereoBlock block (blockSize);

        auto highest = 0.0f;

        for (auto i = 0; i < burstBlocks + silentBlocks; ++i)
        {
            if (i < burstBlocks)
                block.fillWithSine (330.0, 0.5f, i * blockSize);
            else
                block.clear();

            rack.process (block.channels(), 2, blockSize);

            if (! block.allFinite())
                return std::numeric_limits<float>::quiet_NaN();

            highest = std::max (highest, block.peak());
        }

        return highest;
    }

    void testEveryOrderingIsStable()
    {
        beginTest ("every one of the 720 orderings of six effects is finite and bounded");

        // Every permutation rather than a handful, because "we tested the
        // interesting orderings" begs the question of which ones those are. Six
        // effects is 720 arrangements, each a couple of blocks, which costs less
        // than one of the reverb's own decay tests.
        std::vector<EffectType> order (allEffects.begin(), allEffects.end());
        std::sort (order.begin(), order.end());

        EffectsRack rack;
        rack.prepare (testSampleRate, blockSize);
        configureAudibly (rack);

        auto permutations = 0;
        auto worst = 0.0f;

        do
        {
            rack.reset();
            rack.setChain (chainOf (order));

            const auto peak = renderBurst (rack, 3, 1);

            if (! std::isfinite (peak))
            {
                expect (false, "ordering " + describe (order) + " produced a non-finite sample");
                return;
            }

            if (peak > worst)
                worst = peak;

            if (peak > 4.0f)
            {
                expect (false, "ordering " + describe (order) + " reached "
                               + juce::String (peak, 3));
                return;
            }

            // Every ordering must also still make a sound. Checked here rather
            // than once over the whole sweep, because an arrangement that
            // silenced the chain — a gate that shut because what fed it was
            // quieter than it expected, say — would otherwise hide behind the
            // 719 that did not.
            if (peak < 0.01f)
            {
                expect (false, "ordering " + describe (order) + " went silent, at "
                               + juce::String (peak, 5));
                return;
            }

            ++permutations;
        }
        while (std::next_permutation (order.begin(), order.end()));

        expectEquals (permutations, 720, "every arrangement of six effects must have been rendered");

        logMessage ("  720 orderings, loudest sample anywhere " + juce::String (worst, 4));

        // The ceiling above is a real bound rather than a formality. The input
        // is 0.5, the distortion is driven and compensated, the compressor has
        // 3 dB of makeup and the equaliser boosts a band by 6: a chain that
        // stays under 4.0 through all of that is one whose effects are combining
        // rather than compounding.
    }

    void testLatencyBelongsToTheSetNotTheOrder()
    {
        beginTest ("latency is a property of which effects are in the chain, not their order");

        // Worth asserting rather than assuming: latency is summed over the
        // chain, and addition commutes, so this cannot fail today. It is here
        // because the *host* depends on it — an ordering that changed the
        // reported latency would make delay compensation wrong every time
        // somebody rearranged the rack, and a future effect whose latency
        // depended on what fed it would break this silently.
        std::vector<EffectType> order (allEffects.begin(), allEffects.end());
        std::sort (order.begin(), order.end());

        EffectsRack rack;
        rack.prepare (testSampleRate, blockSize);
        configureAudibly (rack);

        rack.setChain (chainOf (order));
        const auto expected = rack.getLatencySamples();

        expect (expected > 0, "a chain containing the distortion has a round trip to report");

        while (std::next_permutation (order.begin(), order.end()))
        {
            rack.setChain (chainOf (order));

            if (rack.getLatencySamples() != expected)
            {
                expect (false, "ordering " + describe (order) + " reported "
                               + juce::String (rack.getLatencySamples())
                               + " samples rather than " + juce::String (expected));
                return;
            }
        }

        expectEquals (rack.distortion().getLatencySamples(), expected,
                      "and the figure is the distortion's, because nothing else adds any");
    }

    void testEveryEffectIsAudiblyInTheChain()
    {
        beginTest ("switching any one effect out of a full chain changes what is heard");

        // Phase 8's "every effect operates independently", as a measurement
        // rather than a claim. Six effects in series give plenty of room for one
        // to be quietly swallowed — a gate that never opens, an equaliser whose
        // settings never reach it, a slot resolved away — and none of those would
        // fail any test that looks at one effect on its own.
        const auto renderWith = [this] (int bypassedIndex)
        {
            EffectsRack rack;
            rack.prepare (testSampleRate, blockSize);
            configureAudibly (rack);

            auto chain = chainOf (std::vector<EffectType> (allEffects.begin(), allEffects.end()));

            if (bypassedIndex >= 0)
                chain[static_cast<std::size_t> (bypassedIndex)].bypassed = true;

            rack.setChain (chain);

            StereoBlock block (blockSize);

            // Four blocks, so the delay and the reverb have something in them by
            // the time the measured one is rendered.
            for (auto i = 0; i < 4; ++i)
            {
                block.fillWithSine (330.0, 0.5f, i * blockSize);
                rack.process (block.channels(), 2, blockSize);
            }

            return block.left;
        };

        const auto everything = renderWith (-1);

        for (std::size_t i = 0; i < allEffects.size(); ++i)
        {
            const auto without = renderWith (static_cast<int> (i));

            auto difference = 0.0f;

            for (std::size_t sample = 0; sample < everything.size(); ++sample)
                difference = std::max (difference, std::abs (everything[sample] - without[sample]));

            expect (difference > 1.0e-4f,
                    "bypassing " + name (allEffects[i]) + " in a full chain changed the output by only "
                        + juce::String (difference, 7) + ", so it may not be in the path at all");
        }
    }

    void testSeriesTailsAdd()
    {
        beginTest ("a chain's tail covers the whole chain ringing out, not its longest effect");

        // THE MEASUREMENT THIS FILE EXISTS FOR. A delay whose repeats last three
        // seconds is still feeding the reverb behind it at the three-second
        // mark, and the reverb then takes its own decay to fall silent from
        // there. The chain rings for longer than either effect does, which is
        // why a chain's tail is the sum along the series path rather than the
        // longest single figure (ADR-0060).
        EffectsRack rack;
        rack.prepare (testSampleRate, blockSize);

        Delay::Settings delay;
        delay.timeMs = 250.0f;
        delay.feedback = 0.75f;
        delay.dampingHz = 20000.0f;
        delay.mix = 1.0f;
        rack.delay().setSettings (delay);

        Reverb::Settings reverb;
        reverb.decaySeconds = 2.0f;
        reverb.mix = 1.0f;
        rack.reverb().setSettings (reverb);

        const auto delayTail = rack.delay().getTailSeconds();
        const auto reverbTail = rack.reverb().getTailSeconds();

        EffectsRack::Chain chain {};
        chain[0].effect = EffectType::delay;
        chain[1].effect = EffectType::reverb;
        rack.setChain (chain);

        const auto reported = rack.getTailSeconds();

        logMessage ("  delay tail " + juce::String (delayTail, 3) + " s, reverb tail "
                    + juce::String (reverbTail, 3) + " s, chain reports "
                    + juce::String (reported, 3) + " s");

        expectWithinAbsoluteError (reported, delayTail + reverbTail, 1.0e-9,
                                   "two tail-producing effects in series must report the sum");

        // And the measurement that makes the arithmetic more than a convention:
        // the chain really is still sounding after the longer of the two has
        // elapsed. Rendered with a burst and then silence, and sampled at the
        // point the old max-based rule would have declared it finished.
        rack.reset();

        StereoBlock block (blockSize);

        for (auto i = 0; i < 8; ++i)
        {
            block.fillWithSine (330.0, 0.5f, i * blockSize);
            rack.process (block.channels(), 2, blockSize);
        }

        const auto blocksFor = [] (double seconds)
        {
            return static_cast<int> (seconds * testSampleRate / static_cast<double> (blockSize));
        };

        const auto longest = std::max (delayTail, reverbTail);

        auto peakAfterLongest = 0.0f;

        for (auto i = 0; i < blocksFor (longest) + 2; ++i)
        {
            block.clear();
            rack.process (block.channels(), 2, blockSize);

            // The last few blocks are the ones that matter: by this point the
            // longest single tail has elapsed.
            if (i >= blocksFor (longest) - 2)
                peakAfterLongest = std::max (peakAfterLongest, block.peak());
        }

        logMessage ("  still sounding at " + juce::String (longest, 3) + " s, at a peak of "
                    + juce::String (peakAfterLongest, 6));

        expect (peakAfterLongest > 1.0e-5f,
                "the chain must still be audible after its longest single tail has elapsed, "
                "which is what makes the sum the honest figure");
    }

    void testTheFullChainSettlesToSilence()
    {
        beginTest ("a full chain at the top of its ranges settles to silence with nothing going in");

        // "Feedback effects remain stable" at chain scale. The delay and the
        // reverb each prove this alone; what is new here is that they are in
        // series behind a distortion driven hard enough to lift anything that
        // reaches it, which is the arrangement in which a marginal loop gain
        // stops being marginal.
        EffectsRack rack;
        rack.prepare (testSampleRate, blockSize);

        Distortion::Settings distortion;
        distortion.mode = Distortion::Mode::hard;
        distortion.driveDb = 36.0f;
        distortion.mix = 1.0f;
        rack.distortion().setSettings (distortion);

        Delay::Settings delay;
        delay.timeMs = 120.0f;
        delay.feedback = 1.0f;          // the top of the control, which is 0.95 of unity
        delay.dampingHz = 20000.0f;
        delay.lowCutHz = 20.0f;
        delay.mix = 1.0f;
        rack.delay().setSettings (delay);

        Reverb::Settings reverb;
        reverb.decaySeconds = 20.0f;    // the top of the control
        reverb.size = 1.0f;
        reverb.dampingHz = 20000.0f;
        reverb.mix = 1.0f;
        rack.reverb().setSettings (reverb);

        Compressor::Settings compressor;
        compressor.thresholdDb = -40.0f;
        compressor.ratio = 1.5f;
        compressor.makeupDb = 12.0f;    // pushing the level back up every pass
        rack.compressor().setSettings (compressor);

        auto equaliser = Equaliser::defaultSettings();
        for (auto& band : equaliser.bands) band.gainDb = 12.0f;
        equaliser.levelDb = 6.0f;
        rack.equaliser().setSettings (equaliser);

        NoiseGate::Settings gate;
        gate.thresholdDb = -80.0f;
        rack.gate().setSettings (gate);

        rack.setChain (chainOf (std::vector<EffectType> (allEffects.begin(), allEffects.end())));

        StereoBlock block (blockSize);

        auto loudest = 0.0f;

        for (auto i = 0; i < 16; ++i)
        {
            block.fillWithSine (220.0, 0.7f, i * blockSize);
            rack.process (block.channels(), 2, blockSize);

            loudest = std::max (loudest, block.peak());
        }

        // WHAT IS AND IS NOT BEING CLAIMED. Not "never louder than what went in":
        // this chain has twelve decibels of makeup gain, twelve on every
        // equaliser band and six more of output trim, so it is *supposed* to come
        // out louder, and a test that forbade it would be testing the settings
        // rather than the stability. The claim is that it is bounded, that it
        // turns around, and that it ends at silence.
        //
        // The peak keeps climbing for a few seconds after the input stops, which
        // is also not a fault: the delay is 120 ms with the feedback at the top
        // of its range and the reverb takes seconds to build, so the chain is
        // still filling long after the last sample went in. The turnaround is
        // given five seconds, and after that every second must be quieter than
        // the one before.
        const auto blocksPerSecond = static_cast<int> (testSampleRate / blockSize);
        const auto buildUpSeconds = 5;

        auto previousSecond = loudest;
        auto highestEver = loudest;

        for (auto second = 0; second < 60; ++second)
        {
            auto peakThisSecond = 0.0f;

            for (auto i = 0; i < blocksPerSecond; ++i)
            {
                block.clear();
                rack.process (block.channels(), 2, blockSize);

                expect (block.allFinite(), "a full chain must stay finite while it rings out");

                peakThisSecond = std::max (peakThisSecond, block.peak());
            }

            highestEver = std::max (highestEver, peakThisSecond);

            // A ceiling, because "it decays eventually" is not stability on its
            // own — a loop that reached a thousand before it turned around would
            // pass that and clip everything downstream of it on the way.
            expect (peakThisSecond < 64.0f,
                    "second " + juce::String (second) + " reached "
                        + juce::String (peakThisSecond, 4)
                        + ", which is a runaway rather than a ring-out");

            if (second >= buildUpSeconds)
                expect (peakThisSecond <= previousSecond * 1.02f + 1.0e-6f,
                        "second " + juce::String (second) + " grew from "
                            + juce::String (previousSecond, 6) + " to "
                            + juce::String (peakThisSecond, 6)
                            + ", after the chain should have finished filling");

            previousSecond = peakThisSecond;
        }

        logMessage ("  loudest " + juce::String (loudest, 4) + " going in, peaking at "
                    + juce::String (highestEver, 4) + " as the chain filled, "
                    + juce::String (previousSecond, 8) + " after a minute of silence");

        expect (previousSecond < loudest * 0.01f,
                "after a minute with nothing going in, a full chain must have decayed away");
    }

    void testReorderingWhileRunningIsSafe()
    {
        beginTest ("rearranging the rack between blocks while audio is flowing stays bounded");

        // Automation can rearrange the chain at block rate, and a preset load can
        // rearrange all six slots at once. Neither is a special case in the rack —
        // reordering is six integers changing — and this is where that claim is
        // checked rather than stated.
        EffectsRack rack;
        rack.prepare (testSampleRate, blockSize);
        configureAudibly (rack);

        std::vector<EffectType> order (allEffects.begin(), allEffects.end());
        std::sort (order.begin(), order.end());

        StereoBlock block (blockSize);

        auto highest = 0.0f;
        auto rearrangements = 0;

        for (auto i = 0; i < 240; ++i)
        {
            // A different arrangement every single block, including arrangements
            // that take effects out of the chain entirely and put them back.
            std::next_permutation (order.begin(), order.end());
            ++rearrangements;

            auto chain = chainOf (order);

            // Every fifth block, empty the last two slots as well, so effects
            // leave the chain and rejoin it rather than only moving within it.
            if (i % 5 == 0)
            {
                chain[4].effect = EffectType::none;
                chain[5].effect = EffectType::none;
            }

            rack.setChain (chain);

            block.fillWithSine (330.0, 0.5f, i * blockSize);
            rack.process (block.channels(), 2, blockSize);

            expect (block.allFinite(),
                    "block " + juce::String (i) + " produced a non-finite sample after a rearrangement");

            highest = std::max (highest, block.peak());
        }

        logMessage ("  " + juce::String (rearrangements) + " rearrangements, loudest sample "
                    + juce::String (highest, 4));

        expect (highest < 4.0f,
                "rearranging the chain every block must not run away with the level, and reached "
                    + juce::String (highest, 4));
    }

    void testOneEffectNamedSixTimesRunsOnce()
    {
        beginTest ("a chain naming one effect in all six slots runs it once, in the first");

        // The duplicate rule at full size. Two slots are tested in
        // EffectsRackTests; six is the case a preset from a future version or a
        // stuck automation lane can actually produce, and the difference matters
        // because running the delay six times would feed its output back into
        // its own line five extra times.
        EffectsRack rack;
        rack.prepare (testSampleRate, blockSize);
        configureAudibly (rack);

        EffectsRack::Chain sixTimes {};
        for (auto& slot : sixTimes) slot.effect = EffectType::delay;

        rack.setChain (sixTimes);

        const auto& resolved = rack.getChain();

        expect (resolved[0].effect == EffectType::delay, "the first occurrence survives");

        for (std::size_t slot = 1; slot < resolved.size(); ++slot)
            expect (resolved[slot].effect == EffectType::none,
                    "slot " + juce::String (static_cast<int> (slot) + 1) + " must be left empty");

        // And it sounds like one delay, not six. Compared against a rack holding
        // exactly one, sample for sample.
        //
        // A rack of its own for each, rather than one rack reconfigured: a
        // reset settles the smoothed controls on their targets but cannot undo
        // the fact that the first render had already carried them there, so
        // reusing a rack would compare a pass that ramped against one that did
        // not. Building both the same way is what makes "sample for sample" a
        // claim about the duplicate rule rather than about the ramps.
        const auto renderChain = [] (const EffectsRack::Chain& chain)
        {
            EffectsRack rack;
            rack.prepare (testSampleRate, blockSize);
            configureAudibly (rack);
            rack.setChain (chain);

            StereoBlock block (blockSize);

            for (auto i = 0; i < 4; ++i)
            {
                block.fillWithSine (330.0, 0.5f, i * blockSize);
                rack.process (block.channels(), 2, blockSize);
            }

            return block.left;
        };

        const auto sixSlots = renderChain (sixTimes);

        EffectsRack::Chain once {};
        once[0].effect = EffectType::delay;

        const auto oneSlot = renderChain (once);

        for (std::size_t i = 0; i < sixSlots.size(); ++i)
            expect (sixSlots[i] == oneSlot[i],
                    "a delay named six times must sound exactly like a delay named once");
    }

    //--------------------------------------------------------------------------

    [[nodiscard]] static juce::String name (EffectType type)
    {
        switch (type)
        {
            case EffectType::distortion: return "the distortion";
            case EffectType::delay:      return "the delay";
            case EffectType::reverb:     return "the reverb";
            case EffectType::gate:       return "the gate";
            case EffectType::compressor: return "the compressor";
            case EffectType::equaliser:  return "the equaliser";
            case EffectType::none:
            default:                     return "an empty slot";
        }
    }

    /** An ordering written out, so a failure names the arrangement that caused
        it rather than leaving 720 candidates.
    */
    [[nodiscard]] static juce::String describe (const std::vector<EffectType>& order)
    {
        juce::String text;

        for (const auto type : order)
            text += (text.isEmpty() ? "" : " -> ") + name (type);

        return text;
    }
};

EffectsChainTests effectsChainTests;

} // namespace
