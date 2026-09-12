/*
    Effects rack tests.

    The rack itself contains no DSP, so what is worth testing is the behaviour
    it defines: that an empty rack is exactly transparent, that the chain it was
    handed is the chain it runs, that a chain which cannot be honoured literally
    is resolved the same way every time, and that the two numbers the host is
    told — latency and tail — follow the documented rules rather than whatever
    happened to be convenient.

    The duplicate case is the one worth being explicit about. Nothing stops two
    slots naming the same effect: automation can do it, a preset from a future
    version can do it, and a user dragging quickly can do it. There is one
    distortion object, so running it twice would feed its own output back into
    its own filters. The rack resolves that to first-occurrence-wins, and this
    file is where that promise is kept.
*/

#include <juce_core/juce_core.h>

#include <cmath>
#include <vector>

#include "DSP/Effects/EffectsRack.h"

using namespace apollo::dsp;

namespace
{

constexpr double testSampleRate = 48000.0;
constexpr double pi = 3.14159265358979323846;
constexpr int blockSize = 256;

[[nodiscard]] std::vector<float> sine (double frequencyHz, float amplitude)
{
    std::vector<float> signal (static_cast<std::size_t> (blockSize), 0.0f);

    const auto increment = 2.0 * pi * frequencyHz / testSampleRate;

    for (int i = 0; i < blockSize; ++i)
        signal[static_cast<std::size_t> (i)] =
            amplitude * static_cast<float> (std::sin (increment * static_cast<double> (i)));

    return signal;
}

void render (EffectsRack& rack, std::vector<float>& signal)
{
    auto* channel = signal.data();
    float* channels[1] { channel };

    rack.process (channels, 1, blockSize);
}

/** A chain with @p type in slot @p slot (1-based) and nothing anywhere else. */
[[nodiscard]] EffectsRack::Chain chainWith (int slot, EffectType type, bool bypassed = false)
{
    EffectsRack::Chain chain;

    chain[static_cast<std::size_t> (slot - 1)].effect = type;
    chain[static_cast<std::size_t> (slot - 1)].bypassed = bypassed;

    return chain;
}

/** Distortion settings that are audibly doing something. */
[[nodiscard]] Distortion::Settings audibleSettings()
{
    Distortion::Settings settings;

    settings.driveDb = 24.0f;
    settings.mix = 1.0f;

    return settings;
}

class EffectsRackTests final : public juce::UnitTest
{
public:
    EffectsRackTests()
        : juce::UnitTest ("Effects rack", "DSP")
    {
    }

    void runTest() override
    {
        testEmptyRackIsTransparent();
        testUnimplementedEffectsLeaveTheSlotEmpty();
        testDuplicatesResolveToTheFirst();
        testPositionInTheChainDoesNotChangeTheSound();
        testOrderBetweenTwoEffectsIsAudible();
        testLatencyFollowsMembershipNotBypass();
        testLatencyIsUnaffectedByEffectsThatAddNone();
        testBypassIsPurelyDelay();
        testTailIgnoresBypassedEffects();
    }

private:
    void testEmptyRackIsTransparent()
    {
        beginTest ("an empty rack changes nothing and costs nothing");

        EffectsRack rack;
        rack.prepare (testSampleRate, blockSize);

        const auto input = sine (440.0, 0.6f);
        auto signal = input;

        render (rack, signal);

        // Bit-exact: a rack with nothing in it must not be a stage the signal
        // passes through, it must be no stage at all. This is what keeps the
        // default patch exactly what it was before the rack existed.
        for (std::size_t i = 0; i < signal.size(); ++i)
            expect (signal[i] == input[i], "an empty rack must not touch the signal");

        expectEquals (rack.getLatencySamples(), 0, "an empty rack adds no latency");
        expectEquals (rack.getTailSeconds(), 0.0, "an empty rack has no tail");
    }

    void testUnimplementedEffectsLeaveTheSlotEmpty()
    {
        beginTest ("an effect whose phase has not landed leaves its slot empty");

        EffectsRack rack;
        rack.prepare (testSampleRate, blockSize);

        for (const auto type : { EffectType::reverb, EffectType::gate,
                                 EffectType::compressor, EffectType::equaliser })
        {
            rack.setChain (chainWith (1, type));

            expect (rack.getChain()[0].effect == EffectType::none,
                    "selecting an effect that does not exist yet must resolve to empty");
            expectEquals (rack.getLatencySamples(), 0,
                          "an empty slot cannot contribute latency");
        }
    }

    void testDuplicatesResolveToTheFirst()
    {
        beginTest ("the same effect in two slots runs once, in the earlier slot");

        EffectsRack rack;
        rack.prepare (testSampleRate, blockSize);

        EffectsRack::Chain chain;
        chain[1].effect = EffectType::distortion;
        chain[3].effect = EffectType::distortion;

        rack.setChain (chain);

        expect (rack.getChain()[1].effect == EffectType::distortion,
                "the first occurrence keeps the effect");
        expect (rack.getChain()[3].effect == EffectType::none,
                "the later occurrence resolves to empty");

        expectEquals (rack.getLatencySamples(), rack.distortion().getLatencySamples(),
                      "one occurrence means one effect's worth of latency, not two");
    }

    void testPositionInTheChainDoesNotChangeTheSound()
    {
        beginTest ("one effect sounds the same wherever in the chain it sits");

        // An effect alone in the rack is doing the same job in slot 1 and in
        // slot 6: the slot is its position, not a different configuration of
        // it. What position *does* change is the subject of the next test.
        std::vector<float> first;
        std::vector<float> last;

        for (const auto slot : { 1, 6 })
        {
            EffectsRack rack;
            rack.prepare (testSampleRate, blockSize);
            rack.distortion().setSettings (audibleSettings());
            rack.setChain (chainWith (slot, EffectType::distortion));

            auto signal = sine (440.0, 0.6f);
            render (rack, signal);

            (slot == 1 ? first : last) = signal;
        }

        for (std::size_t i = 0; i < first.size(); ++i)
            expect (first[i] == last[i], "moving an effect must not change what it does");
    }

    void testOrderBetweenTwoEffectsIsAudible()
    {
        beginTest ("the order two effects are in changes the result");

        // The first thing 8b makes testable, and the claim the rack exists for:
        // distorting a delayed signal is not the same as delaying a distorted
        // one. The first clips the repeats along with the note; the second
        // repeats what the clipper already flattened.
        const auto renderOrder = [] (EffectType first, EffectType second)
        {
            EffectsRack rack;
            rack.prepare (testSampleRate, blockSize);
            rack.distortion().setSettings (audibleSettings());

            Delay::Settings delay;
            delay.timeMs = 2.0f;        // short enough to overlap inside one block
            delay.feedback = 0.6f;
            delay.mix = 0.6f;
            rack.delay().setSettings (delay);

            EffectsRack::Chain chain;
            chain[0].effect = first;
            chain[1].effect = second;
            rack.setChain (chain);

            auto signal = sine (440.0, 0.7f);

            // Two blocks, so the delay line has something in it by the time the
            // measured one is rendered.
            render (rack, signal);

            signal = sine (440.0, 0.7f);
            render (rack, signal);

            return signal;
        };

        const auto distortionFirst = renderOrder (EffectType::distortion, EffectType::delay);
        const auto delayFirst = renderOrder (EffectType::delay, EffectType::distortion);

        auto difference = 0.0f;

        for (std::size_t i = 0; i < distortionFirst.size(); ++i)
            difference = std::max (difference, std::abs (distortionFirst[i] - delayFirst[i]));

        expect (difference > 0.01f,
                "swapping two effects must change the output, and differed by only "
                    + juce::String (difference, 5));
    }

    void testLatencyFollowsMembershipNotBypass()
    {
        beginTest ("latency counts what is in the chain, bypassed or not");

        EffectsRack rack;
        rack.prepare (testSampleRate, blockSize);

        const auto distortionLatency = rack.distortion().getLatencySamples();
        expect (distortionLatency > 0, "the distortion is oversampled, so it has a round trip");

        rack.setChain (chainWith (1, EffectType::distortion));
        expectEquals (rack.getLatencySamples(), distortionLatency);

        rack.setChain (chainWith (1, EffectType::distortion, /*bypassed*/ true));
        expectEquals (rack.getLatencySamples(), distortionLatency,
                      "bypassing must not change the number the host was given");

        rack.setChain (EffectsRack::Chain {});
        expectEquals (rack.getLatencySamples(), 0,
                      "removing the effect is the one thing that does change it");
    }

    void testBypassIsPurelyDelay()
    {
        beginTest ("a bypassed effect delays the signal and does nothing else");

        EffectsRack rack;
        rack.prepare (testSampleRate, blockSize);
        rack.distortion().setSettings (audibleSettings());
        rack.setChain (chainWith (1, EffectType::distortion, /*bypassed*/ true));

        const auto input = sine (440.0, 0.6f);
        auto signal = input;

        render (rack, signal);

        const auto latency = rack.getLatencySamples();

        for (int i = latency; i < blockSize; ++i)
            expect (signal[static_cast<std::size_t> (i)] == input[static_cast<std::size_t> (i - latency)],
                    "bypass must pass the signal through untouched apart from the delay");
    }

    void testTailIgnoresBypassedEffects()
    {
        beginTest ("tail is reported for the active chain only");

        EffectsRack rack;
        rack.prepare (testSampleRate, blockSize);

        // The distortion has nothing to ring out with.
        rack.setChain (chainWith (1, EffectType::distortion));
        expectEquals (rack.getTailSeconds(), 0.0);

        // The delay does, which is what makes the rule checkable rather than
        // merely stated.
        Delay::Settings delay;
        delay.timeMs = 400.0f;
        delay.feedback = 0.6f;
        delay.mix = 1.0f;
        rack.delay().setSettings (delay);

        rack.setChain (chainWith (1, EffectType::delay));

        const auto tail = rack.getTailSeconds();

        expect (tail > 0.4, "a delay in the chain must report a tail at least one repeat long");
        expectWithinAbsoluteError (tail, rack.delay().getTailSeconds(), 1.0e-9,
                                   "and the rack must report the effect's own figure");

        rack.setChain (chainWith (1, EffectType::delay, /*bypassed*/ true));
        expectEquals (rack.getTailSeconds(), 0.0,
                      "a bypassed effect is not ringing out, so it has no tail to wait for");
    }

    void testLatencyIsUnaffectedByEffectsThatAddNone()
    {
        beginTest ("an effect with no latency does not add any");

        EffectsRack rack;
        rack.prepare (testSampleRate, blockSize);

        const auto distortionLatency = rack.distortion().getLatencySamples();

        rack.setChain (chainWith (1, EffectType::delay));
        expectEquals (rack.getLatencySamples(), 0, "a delay is not a lookahead");

        EffectsRack::Chain both;
        both[0].effect = EffectType::delay;
        both[1].effect = EffectType::distortion;
        rack.setChain (both);

        expectEquals (rack.getLatencySamples(), distortionLatency,
                      "a chain's latency is the sum of what its effects add, and the delay adds nothing");
    }
};

EffectsRackTests effectsRackTests;

} // namespace
