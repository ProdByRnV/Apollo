/*
    The filter controls as matrix destinations, and a routing's depth
    modulated through its via source (Phase 13e-4, PRD §16).

      - a slot with no via adds exactly what it always added, a via scales it,
        a bipolar via turns it round, and a via out of range is ignored;
      - routing a constant to drive, mix or key tracking sounds the same as
        setting the control there — the destination means what the control
        means;
      - one macro scaling another's routing does, at the bottom, the middle and
        the top of its travel;
      - and an LFO used only as a via runs, so it moves what it scales.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

#include "Audio/ApolloAudioProcessor.h"
#include "DSP/Modulation/ModulationTypes.h"

using namespace apollo;

namespace
{

constexpr double rate = 48000.0;
constexpr int block = 512;

void setPlain (ApolloAudioProcessor& processor, const juce::String& id, float plain)
{
    auto* parameter = processor.getValueTreeState().getParameter (id);
    jassert (parameter != nullptr);

    if (parameter != nullptr)
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
}

[[nodiscard]] float number (dsp::ModSource source) { return static_cast<float> (source); }
[[nodiscard]] float number (dsp::ModDestination destination) { return static_cast<float> (destination); }

/** Oscillator 1's brightest saw, held, through whatever @p configure sets;
    the left channel from @p fromBlock on. */
[[nodiscard]] std::vector<float> render (const std::function<void (ApolloAudioProcessor&)>& configure,
                                         int note = 57, int blocks = 40, int fromBlock = 0)
{
    ApolloAudioProcessor processor;
    setPlain (processor, "osc1_wavetable", 0.0f);
    setPlain (processor, "osc1_position", 1.0f);
    setPlain (processor, "env1_attack", 0.0f);
    setPlain (processor, "env1_sustain", 1.0f);
    configure (processor);
    processor.prepareToPlay (rate, block);

    juce::AudioBuffer<float> buffer (2, block);
    std::vector<float> rendered;

    for (int b = 0; b < blocks; ++b)
    {
        buffer.clear();
        juce::MidiBuffer midi;

        if (b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.9f), 0);

        processor.processBlock (buffer, midi);

        if (b >= fromBlock)
            rendered.insert (rendered.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + block);
    }

    return rendered;
}

[[nodiscard]] double rmsOf (const std::vector<float>& signal, std::size_t from = 0, std::size_t to = 0)
{
    to = to == 0 ? signal.size() : to;
    double sum = 0.0;

    for (auto i = from; i < to; ++i)
        sum += static_cast<double> (signal[i]) * static_cast<double> (signal[i]);

    return std::sqrt (sum / static_cast<double> (to - from));
}

[[nodiscard]] float worstDifference (const std::vector<float>& a, const std::vector<float>& b)
{
    auto worst = 0.0f;

    for (std::size_t i = 0; i < std::min (a.size(), b.size()); ++i)
        worst = std::max (worst, std::abs (a[i] - b[i]));

    return worst;
}

/** Macro 1, held at @p value, routed to @p destination at @p depth. */
[[nodiscard]] std::function<void (ApolloAudioProcessor&)> macroTo (dsp::ModDestination destination, float depth,
                                                                    float value = 1.0f)
{
    return [=] (ApolloAudioProcessor& processor)
    {
        setPlain (processor, "macro1_value", value);
        setPlain (processor, "mod01_source", number (dsp::ModSource::macro1));
        setPlain (processor, "mod01_destination", number (destination));
        setPlain (processor, "mod01_depth", depth);
    };
}

class FilterModulationTests final : public juce::UnitTest
{
public:
    FilterModulationTests()
        : juce::UnitTest ("Filter controls and depth as destinations", "Engine")
    {
    }

    void runTest() override
    {
        testTheViaArithmetic();
        testDriveMeansDrive();
        testMixMeansMix();
        testKeyTrackMeansKeyTrack();
        testAMacroScalesARouting();
        testAViaLfoRuns();
    }

private:
    void testTheViaArithmetic()
    {
        beginTest ("A via scales a slot, turns it round when bipolar, and is ignored out of range");

        dsp::ModSourceValues sources {};
        sources[static_cast<std::size_t> (dsp::ModSource::macro1)] = 0.8f;
        sources[static_cast<std::size_t> (dsp::ModSource::macro2)] = 0.5f;
        sources[static_cast<std::size_t> (dsp::ModSource::pitchBend)] = -1.0f;

        const auto offsetWith = [&sources] (dsp::ModSource via)
        {
            dsp::ModulationRouting routing;
            routing.slots[0] = { dsp::ModSource::macro1, dsp::ModDestination::osc1Level, 0.6f, via };

            dsp::ModDestinationValues destinations {};
            dsp::evaluateModulation (routing, sources, destinations);
            return destinations[static_cast<std::size_t> (dsp::ModDestination::osc1Level)];
        };

        const auto plain = offsetWith (dsp::ModSource::none);
        expectEquals (plain, 0.8f * 0.6f * 1.0f, "a slot with no via changed what it adds");
        expectWithinAbsoluteError (offsetWith (dsp::ModSource::macro2), plain * 0.5f, 1.0e-7f, "a via of a half");
        expectWithinAbsoluteError (offsetWith (dsp::ModSource::pitchBend), -plain, 1.0e-7f, "a via of -1");
        expectEquals (offsetWith (static_cast<dsp::ModSource> (99)), plain, "a via out of range was not ignored");
    }

    void testDriveMeansDrive()
    {
        beginTest ("Routing a constant to drive is setting the drive there");

        const auto lowpass = [] (ApolloAudioProcessor& processor)
        {
            setPlain (processor, "filter1_type", 1.0f);
            setPlain (processor, "filter1_cutoff", 2000.0f);
        };

        const auto routed = render ([&] (ApolloAudioProcessor& p) { lowpass (p); macroTo (dsp::ModDestination::filter1Drive, 0.7f) (p); });
        const auto set = render ([&] (ApolloAudioProcessor& p) { lowpass (p); setPlain (p, "filter1_drive", 0.7f); });
        const auto dry = render (lowpass);

        const auto worst = worstDifference (routed, set);
        logMessage ("  routed drive against set drive: worst " + juce::String (worst, 9)
                    + "; drive against none: " + juce::String (worstDifference (set, dry), 4));

        expect (worstDifference (set, dry) > 0.01f, "drive did nothing to compare against");
        expect (worst < 1.0e-6f, "a routed drive is not the drive: " + juce::String (worst, 9));
    }

    void testMixMeansMix()
    {
        beginTest ("Routing a constant to mix is setting the mix there");

        const auto lowpass = [] (ApolloAudioProcessor& processor)
        {
            setPlain (processor, "filter1_type", 1.0f);
            setPlain (processor, "filter1_cutoff", 300.0f);
        };

        // From the fifth block, once both mixes have glided to where they are
        // going: the routed one starts gliding with the note, the set one when
        // the parameters are first read, and the two can differ by a block.
        const auto routed = render ([&] (ApolloAudioProcessor& p) { lowpass (p); macroTo (dsp::ModDestination::filter1Mix, -0.6f) (p); }, 57, 40, 5);
        const auto set = render ([&] (ApolloAudioProcessor& p) { lowpass (p); setPlain (p, "filter1_mix", 0.4f); }, 57, 40, 5);
        const auto wet = render (lowpass, 57, 40, 5);

        const auto worst = worstDifference (routed, set);
        logMessage ("  routed mix against set mix: worst " + juce::String (worst, 9)
                    + "; mix against wet: " + juce::String (worstDifference (set, wet), 4));

        expect (worstDifference (set, wet) > 0.01f, "mix did nothing to compare against");
        expect (worst < 1.0e-6f, "a routed mix is not the mix: " + juce::String (worst, 9));
    }

    void testKeyTrackMeansKeyTrack()
    {
        beginTest ("Routing a constant to key tracking is setting the tracking there");

        const auto comb = [] (ApolloAudioProcessor& processor)
        {
            setPlain (processor, "filter1_type", 5.0f);
            setPlain (processor, "filter1_cutoff", 261.63f);
            setPlain (processor, "filter1_resonance", 5.0f);
        };

        const auto routed = render ([&] (ApolloAudioProcessor& p) { comb (p); macroTo (dsp::ModDestination::filter1KeyTrack, 1.0f) (p); }, 69);
        const auto set = render ([&] (ApolloAudioProcessor& p) { comb (p); setPlain (p, "filter1_keytrack", 1.0f); }, 69);
        const auto untracked = render (comb, 69);

        const auto worst = worstDifference (routed, set);
        logMessage ("  routed tracking against set tracking: worst " + juce::String (worst, 9)
                    + "; tracked against untracked: " + juce::String (worstDifference (set, untracked), 4));

        expect (worstDifference (set, untracked) > 0.01f, "tracking did nothing to compare against");
        expect (worst < 1.0e-6f, "routed tracking is not the tracking: " + juce::String (worst, 9));
    }

    void testAMacroScalesARouting()
    {
        beginTest ("One macro scales another's routing through the via");

        // Macro 2 at full turns oscillator 1 down by its whole level; macro 1,
        // as the via, says how much of that happens.
        const auto levelWith = [] (float via)
        {
            return rmsOf (render ([via] (ApolloAudioProcessor& p)
            {
                setPlain (p, "filter1_type", 0.0f);
                setPlain (p, "macro2_value", 1.0f);
                setPlain (p, "macro1_value", via);
                setPlain (p, "mod01_source", number (dsp::ModSource::macro2));
                setPlain (p, "mod01_destination", number (dsp::ModDestination::osc1Level));
                setPlain (p, "mod01_depth", -1.0f);
                setPlain (p, "mod01_via", number (dsp::ModSource::macro1));
            }, 57, 40, 10));
        };

        const auto closed = levelWith (0.0f);
        const auto half = levelWith (0.5f);
        const auto open = levelWith (1.0f);

        logMessage ("  level with the via at 0, 0.5 and 1: " + juce::String (closed, 4) + ", "
                    + juce::String (half, 4) + ", " + juce::String (open, 4));

        expect (closed > 0.01, "with the via at nothing, the routing still acted");
        expectWithinAbsoluteError (half, closed * 0.5, closed * 0.02, "a via of a half is not half the routing");
        expect (open < closed * 0.01, "with the via full, the routing did not act fully");
    }

    void testAViaLfoRuns()
    {
        beginTest ("An LFO used only as a via runs, and moves what it scales");

        // A tremolo made of a constant routing scaled by an LFO that nothing
        // else reads. If the voice did not run the LFO, the level would sit
        // still wherever the LFO happened to be.
        const auto tremolo = render ([] (ApolloAudioProcessor& p)
        {
            setPlain (p, "filter1_type", 0.0f);
            setPlain (p, "lfo1_rate", 4.0f);
            setPlain (p, "lfo1_polarity", 0.0f);
            setPlain (p, "macro1_value", 1.0f);
            setPlain (p, "mod01_source", number (dsp::ModSource::macro1));
            setPlain (p, "mod01_destination", number (dsp::ModDestination::osc1Level));
            setPlain (p, "mod01_depth", -1.0f);
            setPlain (p, "mod01_via", number (dsp::ModSource::lfo1));
        }, 57, 120, 10);

        // RMS in 20 ms windows across two LFO cycles.
        auto quietest = 1.0e9;
        auto loudest = 0.0;
        constexpr std::size_t window = 960;

        for (std::size_t start = 0; start + window <= tremolo.size(); start += window)
        {
            const auto level = rmsOf (tremolo, start, start + window);
            quietest = std::min (quietest, level);
            loudest = std::max (loudest, level);
        }

        logMessage ("  20 ms windows: quietest " + juce::String (quietest, 4) + ", loudest " + juce::String (loudest, 4));
        expect (loudest > 0.02 && quietest < loudest * 0.2, "the level did not move with the via's LFO");
    }
};

FilterModulationTests filterModulationTests;

} // namespace
