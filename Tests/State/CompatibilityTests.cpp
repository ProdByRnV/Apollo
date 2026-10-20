/*
    What Apollo promises about documents written by other versions of itself
    (Docs/COMPATIBILITY.md), each promise tested rather than asserted.

    The serialization tests show that a document round-trips through the build
    that wrote it. That is the easy half. The half users meet is a preset from
    last year opened in this year's Apollo, or this year's project opened in an
    older one, and those are three different questions:

      - a document that does not mention a parameter this build has;
      - a document that mentions one this build does not have;
      - a document written by an earlier build, kept verbatim.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include "Audio/ApolloAudioProcessor.h"
#include "Parameters/ParameterDefinitions.h"
#include "Parameters/ParameterLayout.h"
#include "State/PresetDocument.h"
#include "State/StateSerialization.h"

#include "State/CompatibilityFixtures.h"

using namespace apollo;

namespace
{

/** The value a parameter holds in its own units, as a preset file writes it. */
float plainValue (juce::AudioProcessorValueTreeState& apvts, const char* id)
{
    if (const auto* value = apvts.getRawParameterValue (id))
        return value->load();

    return -12345.0f;
}

/** A current-version document holding only the PARAM entries given. */
juce::String documentWith (const juce::String& params)
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<ApolloState schemaVersion=\"" + juce::String (state::currentSchemaVersion)
           + "\" product=\"Apollo\" productVersion=\"0.1.0\">\n" + params + "</ApolloState>\n";
}

class CompatibilityTests final : public juce::UnitTest
{
public:
    CompatibilityTests()
        : juce::UnitTest ("State compatibility", "State")
    {
    }

    void runTest() override
    {
        testAnUnmentionedParameterTakesItsDefault();
        testAnUnknownParameterIsIgnored();
        testAPresetFrom010StillLoads();
    }

private:
    /** An older build wrote this document before some parameter existed. The
        parameter must come back at its *default*, not at whatever the user had
        before loading — otherwise the same preset would sound different
        depending on what was loaded ahead of it, and a preset would not be a
        sound at all.

        The serialization tests asserted this in a comment ("the loader fills the
        rest from defaults"). This is the test of it.
    */
    void testAnUnmentionedParameterTakesItsDefault()
    {
        beginTest ("A parameter the document does not mention returns to its default");

        ApolloAudioProcessor processor;
        auto& apvts = processor.getValueTreeState();

        // Every parameter moved away from its default first, so a parameter
        // that kept its old value cannot pass by coincidence.
        for (const auto& definition : params::parameterDefinitions)
            if (auto* parameter = apvts.getParameter (params::toJuceString (definition.id)))
                parameter->setValueNotifyingHost (parameter->getDefaultValue() < 0.5f ? 0.9f : 0.1f);

        const auto xml = juce::parseXML (documentWith (
            "  <PARAM id=\"master_gain\" value=\"-6.0\"/>\n"));

        expect (xml != nullptr);

        if (xml == nullptr)
            return;

        expect (state::readStateXml (apvts, *xml) == state::StateLoadResult::ok,
                "a document mentioning one parameter is a valid document");

        expectWithinAbsoluteError (plainValue (apvts, "master_gain"), -6.0f, 1.0e-4f,
                                   "the parameter it mentions takes the value it gives");

        int unmentioned = 0;

        for (const auto& definition : params::parameterDefinitions)
        {
            const auto id = params::toJuceString (definition.id);

            if (id == "master_gain")
                continue;

            const auto* parameter = apvts.getParameter (id);
            expect (parameter != nullptr, id);

            if (parameter == nullptr)
                continue;

            expectWithinAbsoluteError (parameter->getValue(), parameter->getDefaultValue(), 1.0e-5f,
                                       id + ": a parameter the document does not mention must "
                                            "return to its default");
            ++unmentioned;
        }

        expectEquals (unmentioned, static_cast<int> (params::parameterCount()) - 1);
    }

    /** A newer build wrote this document, with a parameter this build has never
        heard of. Same schema version, because adding a parameter does not move
        it (Docs/VERSIONING.md §3), so it reaches the loader. It must load, apply
        everything it can, and not stumble on the stranger.
    */
    void testAnUnknownParameterIsIgnored()
    {
        beginTest ("A parameter this build does not know is ignored, and the rest loads");

        ApolloAudioProcessor processor;
        auto& apvts = processor.getValueTreeState();

        const auto xml = juce::parseXML (documentWith (
            "  <PARAM id=\"osc1_warp_amount\" value=\"0.5\"/>\n"
            "  <PARAM id=\"master_gain\" value=\"-3.0\"/>\n"
            "  <PARAM id=\"filter1_cutoff\" value=\"880.0\"/>\n"
            "  <SOMETHING_NEWER kind=\"not yet invented\"/>\n"));

        expect (xml != nullptr);

        if (xml == nullptr)
            return;

        expect (state::readStateXml (apvts, *xml) == state::StateLoadResult::ok,
                "an unknown parameter must not make a document unreadable");

        expectWithinAbsoluteError (plainValue (apvts, "master_gain"), -3.0f, 1.0e-4f);
        expectWithinAbsoluteError (plainValue (apvts, "filter1_cutoff"), 880.0f, 0.5f);

        expect (apvts.getParameter ("osc1_warp_amount") == nullptr,
                "an unknown identifier does not become a parameter");

        // And the instrument can still save itself and load what it saved, with
        // the stranger along for the ride.
        juce::MemoryBlock saved;
        processor.getStateInformation (saved);

        ApolloAudioProcessor reopened;
        reopened.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));

        expect (reopened.getLastStateLoadResult() == state::StateLoadResult::ok);
        expectWithinAbsoluteError (plainValue (reopened.getValueTreeState(), "master_gain"), -3.0f, 1.0e-4f);
    }

    /** The promise itself. This is a preset Apollo 0.1.0 wrote, captured from
        the standalone and never to be edited. Every later build must read it
        and play the sound it describes; a schema change that breaks this needs a
        migration step, not a new fixture.
    */
    void testAPresetFrom010StillLoads()
    {
        beginTest ("A preset written by Apollo 0.1.0 still loads as the sound it was");

        const juce::String text { juce::CharPointer_UTF8 { fixtures::preset_0_1_0 } };

        ApolloAudioProcessor processor;
        auto& apvts = processor.getValueTreeState();

        expect (presets::read (apvts, text) == state::StateLoadResult::ok,
                "a 0.1.0 preset must load in this build");

        // Settings Wire Pluck makes, chosen to cover each kind of parameter: a
        // continuous value, a skewed frequency, an integer, a stepped choice, a
        // boolean, a rack slot and a modulation routing.
        const struct { const char* id; float plain; float tolerance; } expected[] {
            { "osc1_wavetable",    3.0f,    0.0f   },
            { "osc1_position",     0.25f,   1.0e-4f },
            { "osc1_unison",       3.0f,    0.0f   },
            { "filter1_type",      1.0f,    0.0f   },
            { "filter1_cutoff",    3200.0f, 0.5f   },
            { "env1_attack",       1.0f,    1.0e-3f },
            { "fx_slot1",          2.0f,    0.0f   },
            { "fx_delay_pingpong", 1.0f,    0.0f   },
            { "mod01_source",      2.0f,    0.0f   },
            { "mod01_destination", 12.0f,   0.0f   },
            { "mod01_depth",       0.6f,    1.0e-4f },
            { "master_gain",       -2.0f,   1.0e-4f },
        };

        for (const auto& item : expected)
            expectWithinAbsoluteError (plainValue (apvts, item.id), item.plain, item.tolerance,
                                       juce::String (item.id) + " did not come back as 0.1.0 saved it");

        const auto metadata = presets::getMetadata (apvts);
        expectEquals (metadata.name, juce::String ("Compatibility Fixture"));
        expectEquals (metadata.author, juce::String ("ProdByRnV"));
        expectEquals (metadata.category, juce::String ("Pluck"));

        // And through the host's reader, which is the same validator by design
        // (ADR-0061): a 0.1.0 project must open as surely as a 0.1.0 preset.
        const auto xml = juce::parseXML (text);
        expect (xml != nullptr);

        if (xml != nullptr)
        {
            ApolloAudioProcessor host;
            expect (state::readStateXml (host.getValueTreeState(), *xml) == state::StateLoadResult::ok,
                    "the same document must load as host state");
            expectWithinAbsoluteError (plainValue (host.getValueTreeState(), "filter1_cutoff"),
                                       3200.0f, 0.5f);
        }
    }
};

CompatibilityTests compatibilityTests;

} // namespace
