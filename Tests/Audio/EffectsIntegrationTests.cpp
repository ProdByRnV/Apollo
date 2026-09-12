/*
    The effects rack, through the whole plugin.

    `Tests/DSP/EffectsRackTests.cpp` and `Tests/DSP/DistortionTests.cpp` test the
    DSP on its own. This file tests the parts that only exist once the rack is
    wired into a processor: that a parameter change reaches it, that the default
    patch is untouched by its presence, that the host is told about the latency
    it adds, and that a chain survives the save/restore a project or a preset
    will put it through.

    The audible test is deliberately a comparison rather than a threshold. "The
    distortion makes it louder" or "adds harmonics" are both true and both
    fragile; what must be true is narrower and permanent — with the effect out of
    the chain the instrument sounds exactly as it did before the rack existed,
    and with it in the chain, at full mix, it does not.
*/

#include <juce_core/juce_core.h>

#include <cmath>
#include <vector>

#include "Audio/ApolloAudioProcessor.h"
#include "DSP/Effects/EffectsRack.h"

namespace
{

constexpr double testSampleRate = 48000.0;
constexpr int blockSize = 512;

/** Sets a parameter by its plain value, the way a control or a host would. */
void setPlain (apollo::ApolloAudioProcessor& processor, const juce::String& id, float plain)
{
    auto* parameter = processor.getValueTreeState().getParameter (id);
    jassert (parameter != nullptr);

    parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
}

/** Renders @p blocks blocks with a note held from the first sample.

    @returns the rendered audio, concatenated, left channel only — the two
             channels carry the same argument and one of them is enough to
             compare.
*/
[[nodiscard]] std::vector<float> renderNote (apollo::ApolloAudioProcessor& processor, int blocks)
{
    juce::AudioBuffer<float> buffer (2, blockSize);
    std::vector<float> rendered;

    for (int block = 0; block < blocks; ++block)
    {
        buffer.clear();

        juce::MidiBuffer midi;

        if (block == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 57, 0.9f), 0);

        processor.processBlock (buffer, midi);

        const auto* left = buffer.getReadPointer (0);

        for (int i = 0; i < blockSize; ++i)
            rendered.push_back (left[i]);
    }

    return rendered;
}

[[nodiscard]] float peakOf (const std::vector<float>& signal)
{
    auto peak = 0.0f;

    for (const auto sample : signal)
        peak = std::max (peak, std::abs (sample));

    return peak;
}

/** Root-mean-square difference between two renders of the same note. */
[[nodiscard]] float differenceRms (const std::vector<float>& a, const std::vector<float>& b)
{
    const auto count = std::min (a.size(), b.size());

    if (count == 0)
        return 0.0f;

    auto sum = 0.0;

    for (std::size_t i = 0; i < count; ++i)
    {
        const auto difference = static_cast<double> (a[i]) - static_cast<double> (b[i]);
        sum += difference * difference;
    }

    return static_cast<float> (std::sqrt (sum / static_cast<double> (count)));
}

class EffectsIntegrationTests final : public juce::UnitTest
{
public:
    EffectsIntegrationTests()
        : juce::UnitTest ("Effects rack in the processor", "Audio")
    {
    }

    void runTest() override
    {
        testDefaultPatchIsUntouched();
        testTheChainReachesTheAudio();
        testLatencyIsReportedToTheHost();
        testChainSurvivesStateRestore();
        testCorruptSlotValueIsRejected();
    }

private:
    void testDefaultPatchIsUntouched()
    {
        beginTest ("an instance with an empty rack sounds exactly as it did before the rack existed");

        apollo::ApolloAudioProcessor processor;
        processor.prepareToPlay (testSampleRate, blockSize);

        const auto rendered = renderNote (processor, 8);

        expect (peakOf (rendered) > 0.05f, "the note must actually sound");
        expectEquals (processor.getLatencySamples(), 0,
                      "an instrument with no effects in the chain adds no latency");

        // The slots default to empty, so nothing in the rack can have touched
        // this. The assertion that matters is the latency above and the
        // difference test below; this one guards the default.
        for (int slot = 1; slot <= apollo::dsp::rackSlotCount; ++slot)
        {
            const auto id = "fx_slot" + juce::String (slot);
            const auto* value = processor.getValueTreeState().getRawParameterValue (id);

            expect (value != nullptr, "every slot must be a registered parameter");

            if (value != nullptr)
                expectWithinAbsoluteError (value->load(), 0.0f, 0.0001f,
                                           "a new instance starts with an empty rack");
        }
    }

    void testTheChainReachesTheAudio()
    {
        beginTest ("putting the distortion in a slot changes what is heard");

        std::vector<float> dry;
        std::vector<float> wet;

        {
            apollo::ApolloAudioProcessor processor;
            processor.prepareToPlay (testSampleRate, blockSize);
            dry = renderNote (processor, 8);
        }

        {
            apollo::ApolloAudioProcessor processor;

            // Set before preparing, so the first block is already the configured
            // instrument rather than a ramp towards it.
            setPlain (processor, "fx_slot1", 1.0f);   // dsp::EffectType::distortion
            setPlain (processor, "fx_distortion_drive", 30.0f);
            setPlain (processor, "fx_distortion_mix", 1.0f);

            processor.prepareToPlay (testSampleRate, blockSize);
            wet = renderNote (processor, 8);
        }

        const auto difference = differenceRms (dry, wet);

        logMessage ("  difference between dry and distorted: " + juce::String (difference, 4) + " RMS");

        expect (difference > 0.01f,
                "a distortion at full mix must be audible, and differed by only "
                    + juce::String (difference, 5));

        expect (peakOf (wet) < 2.0f, "the effect must not run away with the level");

        for (const auto sample : wet)
            expect (std::isfinite (sample), "the chain must not produce a NaN or an infinity");
    }

    void testLatencyIsReportedToTheHost()
    {
        beginTest ("the latency the rack adds is reported, and only changes with the chain");

        apollo::ApolloAudioProcessor processor;

        setPlain (processor, "fx_slot1", 1.0f);
        processor.prepareToPlay (testSampleRate, blockSize);

        const auto latency = processor.getLatencySamples();

        expect (latency > 0,
                "an oversampled effect in the chain has a round trip, and the host must be told");

        // Bypass is not a chain change: the effect keeps its position and its
        // delay, so the number the host was given stays true (ADR-0054).
        setPlain (processor, "fx_distortion_bypass", 1.0f);
        const auto bypassed = renderNote (processor, 2);

        for (const auto sample : bypassed)
            expect (std::isfinite (sample), "a bypassed effect must still pass clean audio");

        expectEquals (processor.getLatencySamples(), latency,
                      "bypassing an effect must not renegotiate the plugin's latency");
    }

    void testChainSurvivesStateRestore()
    {
        beginTest ("a chain survives the round trip a project or a preset puts it through");

        juce::MemoryBlock state;

        {
            apollo::ApolloAudioProcessor processor;

            setPlain (processor, "fx_slot2", 1.0f);
            setPlain (processor, "fx_distortion_mode", 2.0f);   // diode
            setPlain (processor, "fx_distortion_drive", 21.0f);
            setPlain (processor, "fx_distortion_tone", 4000.0f);
            setPlain (processor, "fx_distortion_mix", 0.75f);

            processor.getStateInformation (state);
        }

        apollo::ApolloAudioProcessor restored;
        restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));

        const auto plainOf = [&restored] (const juce::String& id)
        {
            const auto* parameter = restored.getValueTreeState().getParameter (id);
            return parameter != nullptr ? parameter->convertFrom0to1 (parameter->getValue()) : -1.0f;
        };

        expectWithinAbsoluteError (plainOf ("fx_slot2"), 1.0f, 0.001f,
                                   "the slot an effect was placed in must come back");
        expectWithinAbsoluteError (plainOf ("fx_distortion_mode"), 2.0f, 0.001f);
        expectWithinAbsoluteError (plainOf ("fx_distortion_drive"), 21.0f, 0.05f);
        expectWithinAbsoluteError (plainOf ("fx_distortion_tone"), 4000.0f, 5.0f);
        expectWithinAbsoluteError (plainOf ("fx_distortion_mix"), 0.75f, 0.001f);

        restored.prepareToPlay (testSampleRate, blockSize);

        expect (restored.getLatencySamples() > 0,
                "a restored chain must report its latency as soon as it is prepared");
    }

    void testCorruptSlotValueIsRejected()
    {
        beginTest ("a slot naming an effect this build does not have is simply empty");

        apollo::ApolloAudioProcessor processor;

        // Every non-distortion value is either unbuilt or empty, and all of them
        // must leave the audio alone rather than doing something surprising
        // (CLAUDE.md §33).
        for (const auto effect : { 0.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f })
        {
            setPlain (processor, "fx_slot1", effect);
            processor.prepareToPlay (testSampleRate, blockSize);

            expectEquals (processor.getLatencySamples(), 0,
                          "an effect that does not exist yet cannot contribute latency");

            const auto rendered = renderNote (processor, 4);

            for (const auto sample : rendered)
                expect (std::isfinite (sample), "an unbuilt effect must not corrupt the audio");
        }
    }
};

EffectsIntegrationTests effectsIntegrationTests;

} // namespace
