/*
    State serialization tests.

    Serialized state lives inside the user's saved projects. The failure that
    matters most is not "state did not load" but "loading bad state destroyed the
    sound that was already there", so preservation is tested as carefully as
    round-tripping (CLAUDE.md §33).
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include "Audio/ApolloAudioProcessor.h"
#include "Parameters/ParameterDefinitions.h"
#include "Parameters/ParameterLayout.h"
#include "State/StateSerialization.h"

using namespace apollo;

namespace
{

float getNormalised (juce::AudioProcessorValueTreeState& apvts, const char* id)
{
    if (const auto* parameter = apvts.getParameter (id))
        return parameter->getValue();

    return -1.0f;
}

void setNormalised (juce::AudioProcessorValueTreeState& apvts, const char* id, float value)
{
    if (auto* parameter = apvts.getParameter (id))
        parameter->setValueNotifyingHost (value);
}

class StateSerializationTests final : public juce::UnitTest
{
public:
    StateSerializationTests()
        : juce::UnitTest ("State serialization", "State")
    {
    }

    void runTest() override
    {
        testRoundTrip();
        testSchemaVersionIsStamped();
        testEmptyDataIsRejected();
        testMalformedDataIsRejected();
        testForeignStateIsRejected();
        testUnsupportedVersionsAreRejected();
        testRejectionPreservesCurrentState();
        testMigrationBoundaries();
        testFilterParameterMigration();
        testAllParametersSurviveRoundTrip();
        testReloadCounterTracksSuccessfulLoads();
    }

private:
    void testRoundTrip()
    {
        beginTest ("State round-trips through save and load");

        ApolloAudioProcessor source;
        setNormalised (source.getValueTreeState(), "filter1_cutoff", 0.25f);
        setNormalised (source.getValueTreeState(), "master_gain", 0.75f);

        juce::MemoryBlock saved;
        source.getStateInformation (saved);

        expect (saved.getSize() > 0, "saving must produce data");

        ApolloAudioProcessor destination;
        destination.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));

        expect (destination.getLastStateLoadResult() == state::StateLoadResult::ok,
                "a document written by this build must load");

        expectWithinAbsoluteError (getNormalised (destination.getValueTreeState(), "filter1_cutoff"),
                                   0.25f, 1.0e-4f);
        expectWithinAbsoluteError (getNormalised (destination.getValueTreeState(), "master_gain"),
                                   0.75f, 1.0e-4f);
    }

    void testSchemaVersionIsStamped()
    {
        beginTest ("Saved state carries schema and product metadata");

        ApolloAudioProcessor processor;

        juce::MemoryBlock saved;
        processor.getStateInformation (saved);

        const auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(),
                                                                 static_cast<int> (saved.getSize()));

        expect (xml != nullptr, "saved state must be readable XML");

        if (xml == nullptr)
            return;

        expect (xml->hasTagName (params::stateTreeType), "wrong state root tag");
        expectEquals (xml->getIntAttribute (state::schemaVersionProperty),
                      state::currentSchemaVersion);
        expectEquals (xml->getStringAttribute (state::productProperty), juce::String ("Apollo"));
        expect (xml->getStringAttribute (state::productVersionProperty).isNotEmpty(),
                "product version must be recorded");
    }

    void testEmptyDataIsRejected()
    {
        beginTest ("Empty state data is rejected");

        ApolloAudioProcessor processor;
        const char dummy = 0;

        processor.setStateInformation (nullptr, 0);
        expect (processor.getLastStateLoadResult() == state::StateLoadResult::emptyData);

        processor.setStateInformation (&dummy, 0);
        expect (processor.getLastStateLoadResult() == state::StateLoadResult::emptyData);

        processor.setStateInformation (&dummy, -1);
        expect (processor.getLastStateLoadResult() == state::StateLoadResult::emptyData);
    }

    void testMalformedDataIsRejected()
    {
        beginTest ("Malformed state data is rejected without crashing");

        ApolloAudioProcessor processor;

        // Text that is not the expected binary envelope, then a truncated
        // document. A host can hand over either after a partial write.
        const juce::String garbage ("this is definitely not apollo state");
        processor.setStateInformation (garbage.toRawUTF8(),
                                       static_cast<int> (garbage.getNumBytesAsUTF8()));
        expect (processor.getLastStateLoadResult() != state::StateLoadResult::ok,
                "garbage must not load");

        juce::MemoryBlock valid;
        processor.getStateInformation (valid);

        processor.setStateInformation (valid.getData(), static_cast<int> (valid.getSize() / 2));
        expect (processor.getLastStateLoadResult() != state::StateLoadResult::ok,
                "truncated state must not load");
    }

    void testForeignStateIsRejected()
    {
        beginTest ("State from another product is rejected");

        juce::ValueTree foreign ("SomeOtherSynthState");
        foreign.setProperty (state::schemaVersionProperty, 1, nullptr);

        juce::MemoryBlock block;

        if (const auto xml = foreign.createXml())
            juce::AudioProcessor::copyXmlToBinary (*xml, block);

        ApolloAudioProcessor processor;
        processor.setStateInformation (block.getData(), static_cast<int> (block.getSize()));

        expect (processor.getLastStateLoadResult() == state::StateLoadResult::wrongProduct);
    }

    void testUnsupportedVersionsAreRejected()
    {
        beginTest ("Unsupported schema versions are rejected, never guessed at");

        const auto makeDocument = [] (int version, bool includeVersion)
        {
            juce::ValueTree tree (params::stateTreeType);

            if (includeVersion)
                tree.setProperty (state::schemaVersionProperty, version, nullptr);

            juce::MemoryBlock block;

            if (const auto xml = tree.createXml())
                juce::AudioProcessor::copyXmlToBinary (*xml, block);

            return block;
        };

        ApolloAudioProcessor processor;

        auto future = makeDocument (state::currentSchemaVersion + 1, true);
        processor.setStateInformation (future.getData(), static_cast<int> (future.getSize()));
        expect (processor.getLastStateLoadResult() == state::StateLoadResult::unsupportedVersion,
                "a future schema must be refused, not reinterpreted");

        auto ancient = makeDocument (state::minimumSupportedSchemaVersion - 1, true);
        processor.setStateInformation (ancient.getData(), static_cast<int> (ancient.getSize()));
        expect (processor.getLastStateLoadResult() == state::StateLoadResult::unsupportedVersion);

        auto unversioned = makeDocument (0, false);
        processor.setStateInformation (unversioned.getData(), static_cast<int> (unversioned.getSize()));
        expect (processor.getLastStateLoadResult() == state::StateLoadResult::unsupportedVersion,
                "a document with no version must not be assumed current");
    }

    /** The property that matters most: a bad file must not take the user's
        current sound down with it.
    */
    void testRejectionPreservesCurrentState()
    {
        beginTest ("Rejected state leaves the current state untouched");

        ApolloAudioProcessor processor;
        auto& apvts = processor.getValueTreeState();

        setNormalised (apvts, "filter1_cutoff", 0.33f);
        setNormalised (apvts, "master_gain", 0.66f);
        setNormalised (apvts, "osc1_detune", 0.9f);

        const auto cutoffBefore = getNormalised (apvts, "filter1_cutoff");
        const auto gainBefore = getNormalised (apvts, "master_gain");
        const auto detuneBefore = getNormalised (apvts, "osc1_detune");

        const juce::String garbage ("<not-apollo/>");
        processor.setStateInformation (garbage.toRawUTF8(),
                                       static_cast<int> (garbage.getNumBytesAsUTF8()));

        expect (processor.getLastStateLoadResult() != state::StateLoadResult::ok);

        expectWithinAbsoluteError (getNormalised (apvts, "filter1_cutoff"), cutoffBefore, 1.0e-6f);
        expectWithinAbsoluteError (getNormalised (apvts, "master_gain"), gainBefore, 1.0e-6f);
        expectWithinAbsoluteError (getNormalised (apvts, "osc1_detune"), detuneBefore, 1.0e-6f);
    }

    void testMigrationBoundaries()
    {
        beginTest ("Migration accepts the current version and refuses the rest");

        juce::ValueTree tree (params::stateTreeType);

        expect (state::migrate (tree, state::currentSchemaVersion),
                "the current version needs no migration and must succeed");

        expect (! state::migrate (tree, state::currentSchemaVersion + 1),
                "a future version must not be migrated backwards");

        expect (! state::migrate (tree, state::minimumSupportedSchemaVersion - 1),
                "a version below the supported floor must be refused");
    }

    /** The first real schema migration, and therefore the first exercise of a
        code path that existed unused since Phase 2.
    */
    void testFilterParameterMigration()
    {
        beginTest ("A version 1 state migrates its un-indexed filter parameters");

        // A version 1 document, as Apollo would have written it before Phase 5b
        // indexed the filters.
        juce::ValueTree tree (params::stateTreeType);
        tree.setProperty (state::schemaVersionProperty, 1, nullptr);

        const auto addParameter = [&tree] (const char* id, float value)
        {
            juce::ValueTree parameter (params::parameterTreeType);
            parameter.setProperty (params::parameterIdProperty, id, nullptr);
            parameter.setProperty ("value", value, nullptr);
            tree.appendChild (parameter, nullptr);
        };

        addParameter ("filter_cutoff", 1234.0f);
        addParameter ("filter_resonance", 3.5f);
        addParameter ("filter_drive", 0.25f);
        addParameter ("master_gain", -6.0f);

        expect (state::migrate (tree, 1), "a version 1 document must migrate");

        expectEquals (static_cast<int> (tree.getProperty (state::schemaVersionProperty)),
                      state::currentSchemaVersion,
                      "migration must stamp the new version");

        const auto findValue = [&tree] (const juce::String& id) -> float
        {
            for (auto child : tree)
                if (child.getProperty (params::parameterIdProperty).toString() == id)
                    return static_cast<float> (child.getProperty ("value"));

            return -1.0f;
        };

        // Renamed, and — the point of a migration rather than a reset — carrying
        // the values the user actually saved.
        expectWithinAbsoluteError (findValue ("filter1_cutoff"), 1234.0f, 1.0e-6f,
                                   "the cutoff must survive the rename with its value");
        expectWithinAbsoluteError (findValue ("filter1_resonance"), 3.5f, 1.0e-6f);
        expectWithinAbsoluteError (findValue ("filter1_drive"), 0.25f, 1.0e-6f);

        expect (findValue ("filter_cutoff") < 0.0f, "the old identifier must be gone");

        // Untouched parameters must be left alone.
        expectWithinAbsoluteError (findValue ("master_gain"), -6.0f, 1.0e-6f);

        // A document that never had the old parameters is not an error: partial
        // documents are legal and the loader fills the rest from defaults.
        juce::ValueTree sparse (params::stateTreeType);
        sparse.setProperty (state::schemaVersionProperty, 1, nullptr);

        expect (state::migrate (sparse, 1),
                "a version 1 document without filter parameters must still migrate");
    }

    /** Every registered parameter, not just a sample. A parameter that silently
        fails to serialize is invisible until a user reopens a project and finds
        one control reset.
    */
    void testAllParametersSurviveRoundTrip()
    {
        beginTest ("Every registered parameter survives a round trip");

        ApolloAudioProcessor source;
        auto& sourceState = source.getValueTreeState();

        // A distinct, reproducible value per parameter, so a mix-up between two
        // of them cannot pass unnoticed.
        float value = 0.05f;

        for (const auto& definition : params::parameterDefinitions)
        {
            if (auto* parameter = sourceState.getParameter (params::toJuceString (definition.id)))
                parameter->setValueNotifyingHost (value);

            value += 0.1f;

            if (value > 0.95f)
                value = 0.05f;
        }

        juce::MemoryBlock saved;
        source.getStateInformation (saved);

        ApolloAudioProcessor destination;
        destination.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));

        expect (destination.getLastStateLoadResult() == state::StateLoadResult::ok);

        for (const auto& definition : params::parameterDefinitions)
        {
            const auto id = params::toJuceString (definition.id);

            const auto* before = sourceState.getParameter (id);
            const auto* after = destination.getValueTreeState().getParameter (id);

            expect (before != nullptr && after != nullptr, id + ": missing after reload");

            if (before == nullptr || after == nullptr)
                continue;

            expectWithinAbsoluteError (after->getValue(), before->getValue(), 1.0e-4f,
                                       id + ": value did not survive the round trip");
        }
    }

    /** The editor uses this counter to tell a preset load apart from ordinary
        parameter movement, so it must advance only on a successful load.
    */
    void testReloadCounterTracksSuccessfulLoads()
    {
        beginTest ("The state reload counter advances only on success");

        ApolloAudioProcessor source;
        juce::MemoryBlock saved;
        source.getStateInformation (saved);

        ApolloAudioProcessor processor;
        const auto before = processor.getStateReloadCounter();

        processor.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        expectEquals (processor.getStateReloadCounter(), before + 1,
                      "a successful load must advance the counter");

        const juce::String garbage ("<not-apollo/>");
        processor.setStateInformation (garbage.toRawUTF8(),
                                       static_cast<int> (garbage.getNumBytesAsUTF8()));
        expectEquals (processor.getStateReloadCounter(), before + 1,
                      "a rejected load must not advance the counter");
    }
};

StateSerializationTests stateSerializationTests;

} // namespace
