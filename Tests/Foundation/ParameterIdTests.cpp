/*
    Tests for the parameter identifier conventions defined in
    Source/Parameters/ParameterId.h and Docs/PARAMETER-CONVENTIONS.md.

    These matter beyond their size: a parameter ID is a permanent part of the
    host-automation and preset contracts (ARCHITECTURE.md §6.1), so the rules
    that govern their shape need to be pinned by tests before a registry exists
    and IDs start shipping.
*/

#include <juce_core/juce_core.h>

#include <string>
#include <string_view>

#include "Parameters/ParameterId.h"

using namespace apollo::params;

// The conventions are constexpr, so the core cases are proven at compile time
// as well as at run time.
static_assert (isValidParameterId ("master_gain"));
static_assert (isValidParameterId ("osc1_wavetable"));
static_assert (isValidParameterId ("fx_distortion_mix"));
static_assert (! isValidParameterId (""));
static_assert (! isValidParameterId ("gain"));
static_assert (! isValidParameterId ("Master_Gain"));
static_assert (! isValidParameterId ("master__gain"));
static_assert (! isValidParameterId ("master_gain_"));
static_assert (! isValidParameterId ("_master_gain"));
static_assert (! isValidParameterId ("master gain"));
static_assert (! isValidParameterId ("osc_1_gain"));

namespace
{

class ParameterIdTests final : public juce::UnitTest
{
public:
    ParameterIdTests()
        : juce::UnitTest ("Parameter identifier conventions", "Foundation")
    {
    }

    void runTest() override
    {
        testDocumentedRegistry();
        testWellFormedIdentifiers();
        testRejectedIdentifiers();
        testLengthBoundary();
        testIssueDescriptions();
    }

private:
    /** Every identifier published in UI_BINDINGS.md §3 must satisfy the
        conventions. This is the test that stops the documented contract and the
        implemented rules from drifting apart.
    */
    void testDocumentedRegistry()
    {
        beginTest ("Identifiers documented in UI_BINDINGS.md are well-formed");

        static constexpr std::string_view documentedIds[] {
            "osc1_wavetable",
            "osc1_unison",
            "osc1_detune",
            "filter1_type",
            "filter1_cutoff",
            "filter1_resonance",
            "filter1_drive",
            "filter2_type",
            "filter2_cutoff",
            "filter2_resonance",
            "filter2_drive",
            "filter_routing",
            "fx_distortion_mix",
            "fx_delay_time",
            "master_gain"
        };

        for (const auto id : documentedIds)
        {
            const auto issue = validateParameterId (id);

            expect (issue == ParameterIdIssue::none,
                    juce::String ("documented id rejected: ")
                        + juce::String (std::string (id))
                        + " -- "
                        + juce::String (std::string (describeParameterIdIssue (issue))));
        }
    }

    void testWellFormedIdentifiers()
    {
        beginTest ("Well-formed identifiers are accepted");

        static constexpr std::string_view valid[] {
            "master_gain",           // minimum: domain + name
            "osc1_wavetable",        // 1-based index fused to the domain
            "osc16_unison_detune",   // multi-digit index, three segments
            "fx_delay_feedback",     // effect-scoped parameter
            "env4_attack",
            "lfo2_rate",
            "a_b"                    // shortest legal form
        };

        for (const auto id : valid)
            expect (isValidParameterId (id),
                    juce::String ("expected valid: ") + juce::String (std::string (id)));
    }

    void testRejectedIdentifiers()
    {
        beginTest ("Malformed identifiers are rejected with the correct reason");

        struct Case
        {
            std::string_view id;
            ParameterIdIssue expected;
        };

        static constexpr Case cases[] {
            { "",                ParameterIdIssue::empty },
            { "gain",            ParameterIdIssue::tooFewSegments },
            { "Master_gain",     ParameterIdIssue::invalidFirstCharacter },
            { "1osc_gain",       ParameterIdIssue::invalidFirstCharacter },
            { "_master_gain",    ParameterIdIssue::invalidFirstCharacter },
            { "master_gain_",    ParameterIdIssue::trailingSeparator },
            { "master__gain",    ParameterIdIssue::emptySegment },
            { "master gain",     ParameterIdIssue::invalidCharacter },
            { "master-gain",     ParameterIdIssue::invalidCharacter },
            { "master.gain",     ParameterIdIssue::invalidCharacter },
            { "masterGain_x",    ParameterIdIssue::invalidCharacter },
            { "osc_1_gain",      ParameterIdIssue::numericOnlySegment },
            { "osc1_2",          ParameterIdIssue::numericOnlySegment }
        };

        for (const auto& testCase : cases)
        {
            const auto issue = validateParameterId (testCase.id);

            expect (issue == testCase.expected,
                    juce::String ("wrong rejection reason for: ")
                        + juce::String (std::string (testCase.id)));
        }
    }

    void testLengthBoundary()
    {
        beginTest ("Identifier length is bounded");

        const std::string atLimit = "a_" + std::string (maxParameterIdLength - 2, 'x');
        const std::string overLimit = atLimit + "x";

        expectEquals (static_cast<int> (atLimit.size()),
                      static_cast<int> (maxParameterIdLength));

        expect (isValidParameterId (atLimit), "an identifier at the length limit must be accepted");

        expect (validateParameterId (overLimit) == ParameterIdIssue::tooLong,
                "an identifier past the length limit must be rejected as tooLong");
    }

    /** Issue descriptions are surfaced through the UI bridge, so they must be
        present and must not leak the offending value (UI_BINDINGS.md §13).
    */
    void testIssueDescriptions()
    {
        beginTest ("Every issue has a safe, non-empty description");

        static constexpr ParameterIdIssue allIssues[] {
            ParameterIdIssue::none,
            ParameterIdIssue::empty,
            ParameterIdIssue::tooLong,
            ParameterIdIssue::invalidFirstCharacter,
            ParameterIdIssue::invalidCharacter,
            ParameterIdIssue::trailingSeparator,
            ParameterIdIssue::emptySegment,
            ParameterIdIssue::numericOnlySegment,
            ParameterIdIssue::tooFewSegments
        };

        for (const auto issue : allIssues)
        {
            const auto description = describeParameterIdIssue (issue);

            expect (! description.empty(), "issue description must not be empty");
            expect (description != "identifier is not valid",
                    "every enumerated issue needs its own description, not the fallback");
        }
    }
};

ParameterIdTests parameterIdTests;

} // namespace
