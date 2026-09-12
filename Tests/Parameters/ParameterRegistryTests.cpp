/*
    Parameter registry tests.

    Parameter IDs, ranges and defaults are permanent contracts (UI_BINDINGS.md
    §3, Docs/PARAMETER-CONVENTIONS.md §1). These tests pin the registry against
    accidental change and check that the APVTS layout generated from it really
    matches the definitions.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <set>
#include <string>

#include "Audio/ApolloAudioProcessor.h"
#include "Parameters/ParameterDefinitions.h"
#include "Parameters/ParameterId.h"
#include "Parameters/ParameterLayout.h"

using namespace apollo::params;

// The registry is constexpr, so its structural invariants are proven at compile
// time and a bad definition fails the build rather than a test run.
//
// Nine from the initial registry documented in UI_BINDINGS.md §3; osc1_position
// added in Phase 4a when the wavetable engine gave it meaning; fifteen more in
// Phase 4b with the source section — oscillator 1's spread, level and pan, the
// whole of oscillator 2, the sub oscillator and the noise generator; and seven
// in Phase 5a for envelope 1, which shapes the voice amplitude. Envelopes 2-4
// are deliberately absent until the modulation matrix gives them a destination.
//
// Phase 5b then replaced the three un-indexed filter parameters with nine: four
// per filter plus the routing between them. That is a migrated rename rather
// than an addition, and schema version 2 performs it (ADR-0032).
//
// Phase 5d added a hundred and one at once, which is what a modulation matrix
// costs: envelopes 2-4, four LFOs, and sixteen routing slots of a source, a
// destination and a depth. They are generated rather than typed (ADR-0035).
//
// Phase 6b added four: the pitch-bend range, which had been a hard-coded ±2
// since Phase 3, and the three that describe an MPE zone. All four are setup
// rather than sound — they describe the controller on the desk, not the patch —
// and none is automatable, because a pitch-bend range moving on an automation
// lane is a bug being recorded rather than a musical gesture.
//
// Phase 8a added eleven: the rack's six slot selectors, and the five the
// distortion unit needs beyond the `fx_distortion_mix` that had been sitting
// inert since the initial registry. The slots are enumerated for all six
// effects from the start, including the five whose phases have not landed,
// because a discrete parameter's range is permanent (ADR-0054).
//
// Phase 8b added eight more for the delay: a bypass, the free-or-synced switch
// and the note division it reads, feedback, the two filters inside the feedback
// path, ping-pong, and a mix. `fx_delay_time` was already there, inert since the
// initial registry, and 8b is what connected it.
//
// This assertion is deliberately exact: growing the registry is a permanent
// change to the automation and preset contract, so it should never happen by
// accident (Docs/PARAMETER-CONVENTIONS.md §1).
static_assert (parameterCount() == 162, "the registry has a hundred and sixty-two parameters");
static_assert (findParameter ("master_gain") != nullptr);
static_assert (findParameter ("does_not_exist") == nullptr);

namespace
{

class ParameterRegistryTests final : public juce::UnitTest
{
public:
    ParameterRegistryTests()
        : juce::UnitTest ("Parameter registry", "Parameters")
    {
    }

    void runTest() override
    {
        testIdsFollowConventions();
        testIdsAreUnique();
        testDefaultsWithinRange();
        testRangesAreSane();
        testDocumentedRegistryIsPresent();
        testLayoutMatchesDefinitions();
        testNormalisationRoundTrip();
        testDiscreteParametersKeepTheirSteps();
    }

private:
    static juce::String idOf (const ParameterDefinition& definition)
    {
        return toJuceString (definition.id);
    }

    void testIdsFollowConventions()
    {
        beginTest ("Every registered ID satisfies the ID conventions");

        for (const auto& definition : parameterDefinitions)
        {
            const std::string id { definition.id };
            const auto issue = validateParameterId (id);

            expect (issue == ParameterIdIssue::none,
                    "invalid id '" + idOf (definition) + "': "
                        + juce::String (std::string (describeParameterIdIssue (issue))));
        }
    }

    /** A duplicate ID would make one parameter permanently unreachable and
        corrupt state round-trips, so it is checked rather than trusted.
    */
    void testIdsAreUnique()
    {
        beginTest ("Registered IDs are unique");

        std::set<std::string> seen;

        for (const auto& definition : parameterDefinitions)
            expect (seen.insert (std::string { definition.id }).second,
                    "duplicate parameter id: " + idOf (definition));
    }

    void testDefaultsWithinRange()
    {
        beginTest ("Defaults lie within their declared range");

        for (const auto& definition : parameterDefinitions)
        {
            expect (definition.defaultValue >= definition.minimum,
                    idOf (definition) + ": default is below the minimum");
            expect (definition.defaultValue <= definition.maximum,
                    idOf (definition) + ": default is above the maximum");
        }
    }

    void testRangesAreSane()
    {
        beginTest ("Ranges and skews are well-formed");

        for (const auto& definition : parameterDefinitions)
        {
            expect (definition.minimum < definition.maximum,
                    idOf (definition) + ": minimum must be below maximum");
            expect (definition.skew > 0.0f,
                    idOf (definition) + ": skew must be positive");
            expect (definition.stepSize >= 0.0f,
                    idOf (definition) + ": step size must not be negative");
            expect (! definition.name.empty(),
                    idOf (definition) + ": display name must not be empty");
        }
    }

    /** Ties the registry to the published contract: every ID documented in
        UI_BINDINGS.md §3 must still exist here.
    */
    void testDocumentedRegistryIsPresent()
    {
        beginTest ("Every parameter documented in UI_BINDINGS.md is registered");

        static constexpr std::string_view documented[] {
            "osc1_wavetable", "osc1_position", "osc1_unison", "osc1_detune",
            "osc1_spread", "osc1_level", "osc1_pan",
            "osc2_wavetable", "osc2_position", "osc2_unison", "osc2_detune",
            "osc2_spread", "osc2_level", "osc2_pan", "osc2_semitones", "osc2_fine",
            "sub_level", "sub_octave", "noise_level",
            "env1_delay", "env1_attack", "env1_hold", "env1_decay",
            "env1_sustain", "env1_release", "env1_curve",
            "filter1_type", "filter1_cutoff", "filter1_resonance", "filter1_drive",
            "filter2_type", "filter2_cutoff", "filter2_resonance", "filter2_drive",
            "filter_routing",
            "fx_distortion_mix", "fx_delay_time", "master_gain"
        };

        for (const auto id : documented)
            expect (findParameter (id) != nullptr,
                    "documented parameter missing from the registry: "
                        + juce::String (std::string (id)));
    }

    /** The APVTS layout is generated from the registry; this checks the
        generation preserves identity, range and default.
    */
    void testLayoutMatchesDefinitions()
    {
        beginTest ("The APVTS layout matches the registry");

        apollo::ApolloAudioProcessor processor;
        auto& apvts = processor.getValueTreeState();

        for (const auto& definition : parameterDefinitions)
        {
            auto* parameter = apvts.getParameter (idOf (definition));

            expect (parameter != nullptr, idOf (definition) + ": missing from the APVTS layout");

            if (parameter == nullptr)
                continue;

            const auto& range = parameter->getNormalisableRange();

            expectWithinAbsoluteError (range.start, definition.minimum, 1.0e-4f);
            expectWithinAbsoluteError (range.end, definition.maximum, 1.0e-4f);

            // The default must survive the normalise/denormalise round trip, or
            // a freshly instantiated plugin would not sound as designed.
            const auto defaultPlain = range.convertFrom0to1 (parameter->getDefaultValue());
            const auto tolerance = std::max (1.0e-3f, std::abs (definition.defaultValue) * 1.0e-3f);

            expectWithinAbsoluteError (defaultPlain, definition.defaultValue, tolerance,
                                       idOf (definition) + ": default did not round-trip");
        }
    }

    void testNormalisationRoundTrip()
    {
        beginTest ("Normalised and plain values round-trip");

        for (const auto& definition : parameterDefinitions)
        {
            const auto range = makeRange (definition);

            for (const float normalised : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                const auto plain = range.convertFrom0to1 (normalised);

                expect (plain >= definition.minimum - 1.0e-3f
                            && plain <= definition.maximum + 1.0e-3f,
                        idOf (definition) + ": denormalised value escaped its range");

                expectWithinAbsoluteError (range.convertTo0to1 (plain), normalised, 1.0e-3f,
                                           idOf (definition) + ": normalisation is not reversible");
            }
        }
    }

    /** UI_BINDINGS.md §4: a discrete parameter must preserve its step count and
        must never be treated as an arbitrary continuous float.
    */
    void testDiscreteParametersKeepTheirSteps()
    {
        beginTest ("Discrete parameters preserve their step count");

        apollo::ApolloAudioProcessor processor;
        auto& apvts = processor.getValueTreeState();

        for (const auto& definition : parameterDefinitions)
        {
            if (definition.type != ParameterType::integer)
                continue;

            auto* parameter = apvts.getParameter (idOf (definition));

            expect (parameter != nullptr, idOf (definition) + ": missing from the APVTS layout");

            if (parameter == nullptr)
                continue;

            const auto expectedSteps = static_cast<int> (definition.maximum - definition.minimum) + 1;

            // getNumSteps is the contract that matters: it is what the VST3 and
            // standalone wrappers publish as the step count, and therefore what
            // stops a host treating an integer control as continuous.
            //
            // Note that juce::AudioParameterInt does NOT override isDiscrete();
            // only AudioParameterChoice does. Asserting on isDiscrete() here
            // would test a JUCE implementation detail rather than the behaviour
            // Apollo depends on.
            expectEquals (parameter->getNumSteps(), expectedSteps,
                          idOf (definition) + ": wrong number of discrete steps");

            // Every normalised step position must land on a whole value, which
            // is the observable half of "discrete".
            for (int step = 0; step < expectedSteps; ++step)
            {
                const auto normalised = static_cast<float> (step)
                                      / static_cast<float> (expectedSteps - 1);
                const auto plain = parameter->getNormalisableRange().convertFrom0to1 (normalised);

                expectWithinAbsoluteError (plain, std::round (plain), 1.0e-4f,
                                           idOf (definition) + ": step " + juce::String (step)
                                               + " did not land on a whole value");
            }
        }
    }
};

ParameterRegistryTests parameterRegistryTests;

} // namespace
