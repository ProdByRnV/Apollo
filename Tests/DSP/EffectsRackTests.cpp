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
        testLatencyFollowsMembershipNotBypass();
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

        for (const auto type : { EffectType::delay, EffectType::reverb, EffectType::gate,
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

        // With a single effect implemented this is the whole of what ordering
        // can be asserted: the slot an effect occupies is its position, not a
        // different configuration of it. Ordering between effects becomes
        // testable in 8b, when there are two things to order.
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

        // The distortion has no tail, so this asserts the rule rather than a
        // number: nothing in the chain yet rings out, and the reported tail is
        // zero in every arrangement of it. The rule earns its keep in 8b and 8c.
        rack.setChain (chainWith (1, EffectType::distortion));
        expectEquals (rack.getTailSeconds(), 0.0);

        rack.setChain (chainWith (1, EffectType::distortion, /*bypassed*/ true));
        expectEquals (rack.getTailSeconds(), 0.0);
    }
};

EffectsRackTests effectsRackTests;

} // namespace
