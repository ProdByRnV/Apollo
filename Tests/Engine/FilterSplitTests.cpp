/*
    Split filter routing (Phase 13c-3, PRD §13).

    Under split routing each source chooses filter 1, filter 2 or neither, and
    the results are summed. Most of what that has to be can be said exactly,
    because each path is a routing that already exists:

      - a source sent to filter 1 is, sample for sample, that source through
        filter 1 alone in series;
      - a source sent past both is, sample for sample, the source unfiltered;
      - with both filters off, split is the series bypass, sample for sample —
        which is also the proof that split is not halved;
      - two sources on two filters are the sum of each on its own;
      - series and parallel ignore the inputs entirely;
      - and an input that is not one of the three cannot index past the buses.

    The defaults are checked as the parameters a user starts from, because they
    decide what choosing split does before anything else is touched.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

#include "Audio/ApolloAudioProcessor.h"
#include "Engine/VoiceEngine.h"

using namespace apollo;

namespace
{

constexpr double rate = 48000.0;
constexpr int block = 512;
constexpr int blocks = 24;

void setPlain (ApolloAudioProcessor& processor, const juce::String& id, float plain)
{
    auto* parameter = processor.getValueTreeState().getParameter (id);
    jassert (parameter != nullptr);

    if (parameter != nullptr)
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
}

/** Oscillator 1's brightest saw and, when asked for, oscillator 2's pulse a
    fifth above — two sources that differ in pitch and in spectrum, so a filter
    on the wrong one would show. */
void makeSources (ApolloAudioProcessor& processor, bool osc1, bool osc2)
{
    setPlain (processor, "env1_attack", 0.0f);
    setPlain (processor, "env1_sustain", 1.0f);

    setPlain (processor, "osc1_wavetable", 0.0f);
    setPlain (processor, "osc1_position", 1.0f);
    setPlain (processor, "osc1_level", osc1 ? 1.0f : 0.0f);

    setPlain (processor, "osc2_wavetable", 1.0f);
    setPlain (processor, "osc2_position", 0.5f);
    setPlain (processor, "osc2_semitones", 7.0f);
    setPlain (processor, "osc2_level", osc2 ? 1.0f : 0.0f);
}

/** A lowpass at 300 Hz on filter 1 and a highpass at 3 kHz on filter 2: two
    filters that leave very little of each other's signal. */
void makeFilters (ApolloAudioProcessor& processor)
{
    setPlain (processor, "filter1_type", 1.0f);
    setPlain (processor, "filter1_cutoff", 300.0f);
    setPlain (processor, "filter1_resonance", 2.0f);

    setPlain (processor, "filter2_type", 2.0f);
    setPlain (processor, "filter2_cutoff", 3000.0f);
    setPlain (processor, "filter2_resonance", 2.0f);
}

/** Both channels of a held note, interleaved, so a difference in either shows. */
[[nodiscard]] std::vector<float> render (const std::function<void (ApolloAudioProcessor&)>& configure)
{
    ApolloAudioProcessor processor;
    configure (processor);
    processor.prepareToPlay (rate, block);

    juce::AudioBuffer<float> buffer (2, block);
    std::vector<float> rendered;

    for (int b = 0; b < blocks; ++b)
    {
        buffer.clear();
        juce::MidiBuffer midi;

        if (b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 57, 0.9f), 0);

        processor.processBlock (buffer, midi);

        for (int i = 0; i < block; ++i)
        {
            rendered.push_back (buffer.getSample (0, i));
            rendered.push_back (buffer.getSample (1, i));
        }
    }

    return rendered;
}

[[nodiscard]] double rmsOf (const std::vector<float>& signal)
{
    double sum = 0.0;

    for (const auto sample : signal)
        sum += static_cast<double> (sample) * static_cast<double> (sample);

    return std::sqrt (sum / static_cast<double> (signal.size()));
}

/** The largest sample-by-sample difference. */
[[nodiscard]] float worstDifference (const std::vector<float>& a, const std::vector<float>& b)
{
    auto worst = 0.0f;

    for (std::size_t i = 0; i < std::min (a.size(), b.size()); ++i)
        worst = std::max (worst, std::abs (a[i] - b[i]));

    return a.size() == b.size() ? worst : 1.0e9f;
}

class FilterSplitTests final : public juce::UnitTest
{
public:
    FilterSplitTests()
        : juce::UnitTest ("Split filter routing", "Engine")
    {
    }

    void runTest() override
    {
        testDefaults();
        testFilterOneIsFilterOneAlone();
        testFilterTwoIsFilterTwoAlone();
        testDirectIsUnfiltered();
        testBypassedSplitIsTheSeriesBypass();
        testTwoSourcesAreTheirSum();
        testSeriesAndParallelIgnoreTheInputs();
        testAnInputOutOfRangeStaysOnTheBuses();
    }

private:
    void testDefaults()
    {
        beginTest ("Split's defaults divide the voice in two");

        ApolloAudioProcessor processor;
        auto& state = processor.getValueTreeState();

        const auto defaultOf = [&state] (const char* id)
        {
            auto* parameter = state.getParameter (id);
            return parameter == nullptr ? -1.0f : parameter->convertFrom0to1 (parameter->getDefaultValue());
        };

        expectEquals (defaultOf ("filter_routing"), 0.0f, "routing no longer defaults to series");
        expectEquals (defaultOf ("osc1_filter"), 0.0f, "oscillator 1 should go to filter 1");
        expectEquals (defaultOf ("osc2_filter"), 1.0f, "oscillator 2 should go to filter 2");
        expectEquals (defaultOf ("sub_filter"), 0.0f, "the sub should go with oscillator 1");
        expectEquals (defaultOf ("noise_filter"), 1.0f, "the noise should go with oscillator 2");
    }

    void testFilterOneIsFilterOneAlone()
    {
        beginTest ("A source sent to filter 1 is that source through filter 1 alone");

        // Oscillator 1 at its default input, filter 1.
        const auto split = render ([] (ApolloAudioProcessor& processor)
        {
            makeSources (processor, true, false);
            makeFilters (processor);
            setPlain (processor, "filter_routing", 2.0f);
        });

        const auto alone = render ([] (ApolloAudioProcessor& processor)
        {
            makeSources (processor, true, false);
            makeFilters (processor);
            setPlain (processor, "filter2_type", 0.0f);
        });

        const auto unfiltered = render ([] (ApolloAudioProcessor& processor)
        {
            makeSources (processor, true, false);
        });

        expect (rmsOf (alone) > 1.0e-3, "the reference made no sound");
        expect (worstDifference (alone, unfiltered) > 1.0e-2f, "filter 1 did nothing to the saw");
        expectEquals (worstDifference (split, alone), 0.0f,
                      "split is not exactly filter 1 alone");
    }

    void testFilterTwoIsFilterTwoAlone()
    {
        beginTest ("A source sent to filter 2 is that source through filter 2 alone");

        const auto split = render ([] (ApolloAudioProcessor& processor)
        {
            makeSources (processor, true, false);
            makeFilters (processor);
            setPlain (processor, "filter_routing", 2.0f);
            setPlain (processor, "osc1_filter", 1.0f);
        });

        const auto alone = render ([] (ApolloAudioProcessor& processor)
        {
            makeSources (processor, true, false);
            makeFilters (processor);
            setPlain (processor, "filter1_type", 0.0f);
        });

        expect (rmsOf (alone) > 1.0e-3, "the reference made no sound");
        expectEquals (worstDifference (split, alone), 0.0f,
                      "split is not exactly filter 2 alone");
    }

    void testDirectIsUnfiltered()
    {
        beginTest ("A source sent past both filters is unfiltered");

        const auto split = render ([] (ApolloAudioProcessor& processor)
        {
            makeSources (processor, true, false);
            makeFilters (processor);
            setPlain (processor, "filter_routing", 2.0f);
            setPlain (processor, "osc1_filter", 2.0f);
        });

        const auto unfiltered = render ([] (ApolloAudioProcessor& processor)
        {
            makeSources (processor, true, false);
            setPlain (processor, "filter1_type", 0.0f);
        });

        expect (rmsOf (unfiltered) > 1.0e-3, "the reference made no sound");
        expectEquals (worstDifference (split, unfiltered), 0.0f,
                      "a direct source was touched by a filter");
    }

    void testBypassedSplitIsTheSeriesBypass()
    {
        beginTest ("With both filters off, split is the series bypass and is not halved");

        const auto configure = [] (float routing)
        {
            return [routing] (ApolloAudioProcessor& processor)
            {
                makeSources (processor, true, true);
                setPlain (processor, "filter1_type", 0.0f);
                setPlain (processor, "filter2_type", 0.0f);
                setPlain (processor, "filter_routing", routing);
            };
        };

        const auto split = render (configure (2.0f));
        const auto series = render (configure (0.0f));

        expect (rmsOf (series) > 1.0e-3, "the reference made no sound");
        expectEquals (worstDifference (split, series), 0.0f,
                      "bypassed split differs from the bypassed series");
    }

    void testTwoSourcesAreTheirSum()
    {
        beginTest ("Two sources on two filters are the sum of each on its own");

        const auto configure = [] (bool osc1, bool osc2)
        {
            return [osc1, osc2] (ApolloAudioProcessor& processor)
            {
                makeSources (processor, osc1, osc2);
                makeFilters (processor);
                setPlain (processor, "filter_routing", 2.0f);
            };
        };

        const auto both = render (configure (true, true));
        const auto first = render (configure (true, false));
        const auto second = render (configure (false, true));

        std::vector<float> sum (first.size());

        for (std::size_t i = 0; i < sum.size(); ++i)
            sum[i] = first[i] + second[i];

        // Not exact: the amplifier and the master gain scale the sum where the
        // separate renders scaled each part. Rounding, at the float's epsilon.
        const auto worst = worstDifference (both, sum);
        logMessage ("  split against the sum of its parts: worst " + juce::String (worst, 10));
        expect (worst < 1.0e-6f, "split is not the sum of its parts: " + juce::String (worst, 10));

        // And it is a different thing from series, where both sources meet a
        // lowpass and then a highpass and very little survives.
        const auto series = render ([] (ApolloAudioProcessor& processor)
        {
            makeSources (processor, true, true);
            makeFilters (processor);
        });

        const auto ratio = rmsOf (both) / std::max (rmsOf (series), 1.0e-12);
        logMessage ("  split against series, level: " + juce::String (20.0 * std::log10 (ratio), 1) + " dB");
        expect (ratio > 2.0, "split should keep far more than series here");
    }

    void testSeriesAndParallelIgnoreTheInputs()
    {
        beginTest ("Series and parallel ignore every source's input");

        for (const auto routing : { 0.0f, 1.0f })
        {
            const auto configure = [routing] (bool moved)
            {
                return [routing, moved] (ApolloAudioProcessor& processor)
                {
                    makeSources (processor, true, true);
                    makeFilters (processor);
                    setPlain (processor, "filter_routing", routing);

                    if (moved)
                    {
                        setPlain (processor, "osc1_filter", 2.0f);
                        setPlain (processor, "osc2_filter", 2.0f);
                        setPlain (processor, "sub_filter", 1.0f);
                        setPlain (processor, "noise_filter", 0.0f);
                    }
                };
            };

            expectEquals (worstDifference (render (configure (true)), render (configure (false))), 0.0f,
                          "routing " + juce::String (routing) + " listened to a source's input");
        }
    }

    void testAnInputOutOfRangeStaysOnTheBuses()
    {
        beginTest ("An input that is not one of the three lands on filter 1");

        // The processor clamps, so this goes to the engine directly: a value the
        // engine was never meant to receive must not index past the buses.
        const auto renderWith = [] (engine::FilterInput input)
        {
            engine::VoiceEngine voiceEngine;
            voiceEngine.prepare (rate);

            engine::FilterParameters filters;
            filters.filter1 = { dsp::FilterType::lowpass, 300.0f, 2.0f, 0.0f };
            filters.filter2 = { dsp::FilterType::highpass, 3000.0f, 2.0f, 0.0f };
            filters.routing = engine::FilterRouting::split;
            filters.inputs.osc1 = input;
            voiceEngine.setFilterParameters (filters);

            engine::SourceParameters sources;
            sources.osc1.level = 1.0f;
            voiceEngine.setSourceParameters (sources);
            voiceEngine.noteOn (57, 0.9f);

            std::vector<float> left (block), right (block), output;
            float* channels[2] = { left.data(), right.data() };

            for (int b = 0; b < 8; ++b)
            {
                std::fill (left.begin(), left.end(), 0.0f);
                std::fill (right.begin(), right.end(), 0.0f);
                voiceEngine.render (channels, 2, 0, block);
                output.insert (output.end(), left.begin(), left.end());
            }

            return output;
        };

        const auto reference = renderWith (engine::FilterInput::filter1);

        expect (rmsOf (reference) > 1.0e-3, "the reference made no sound");
        expectEquals (worstDifference (renderWith (static_cast<engine::FilterInput> (7)), reference), 0.0f,
                      "an input past the end did not land on filter 1");
        expectEquals (worstDifference (renderWith (static_cast<engine::FilterInput> (-1)), reference), 0.0f,
                      "a negative input did not land on filter 1");
    }
};

FilterSplitTests filterSplitTests;

} // namespace
