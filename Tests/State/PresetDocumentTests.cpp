/*
    Preset document tests.

    A `.rnv` file is a sound somebody made, sitting on a disk that Apollo does
    not control. It can be renamed, truncated, edited by hand, synced by
    something that mangles line endings, or not be a preset at all. So the
    failures tested here are weighted the way they are for host state: the worst
    outcome is not "the preset did not load" but "loading a bad preset destroyed
    the sound that was already there" (CLAUDE.md §33, ADR-0053).

    The other thing this file holds the format to is that a preset and host
    state are the *same document* read two ways. Every rejection the host path
    makes, the preset path must make identically — that is why several tests
    below assert the same reason code for the same defect through both entry
    points rather than testing one and assuming the other.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include "Audio/ApolloAudioProcessor.h"
#include "MIDI/MidiControlManager.h"
#include "Parameters/ParameterDefinitions.h"
#include "Parameters/ParameterLayout.h"
#include "State/PresetDocument.h"
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

float getPlain (juce::AudioProcessorValueTreeState& apvts, const char* id)
{
    if (const auto* parameter = apvts.getParameter (id))
        return parameter->convertFrom0to1 (parameter->getValue());

    return -1.0f;
}

void setPlain (juce::AudioProcessorValueTreeState& apvts, const char* id, float plain)
{
    if (auto* parameter = apvts.getParameter (id))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
}

/** Metadata with something in every field, and nothing exotic in any of them. */
[[nodiscard]] presets::Metadata sampleMetadata()
{
    presets::Metadata metadata;

    metadata.name = "Glass Bell";
    metadata.author = "ProdByRnV";
    metadata.category = "Keys";
    metadata.comment = "Mod wheel opens the filter.";

    return metadata;
}

class PresetDocumentTests final : public juce::UnitTest
{
public:
    PresetDocumentTests()
        : juce::UnitTest ("Preset document", "State")
    {
    }

    void runTest() override
    {
        testRoundTrip();
        testDocumentIsReadableText();
        testMetadataCanBeReadWithoutLoading();
        testMetadataIsOptional();
        testMetadataIsBounded();
        testAwkwardTextSurvives();
        testEveryRejectionPreservesTheCurrentSound();
        testTheTwoReadersAgree();
        testAnOlderPresetMigrates();
        testOversizedDocumentsAreNotParsed();
        testMetadataTravelsIntoHostState();
        testAPresetCarriesNoController();
        testLoadingAPresetLeavesTheControllerAlone();
    }

private:
    void testRoundTrip()
    {
        beginTest ("a preset restores the sound and the metadata that were saved");

        juce::String document;

        {
            ApolloAudioProcessor processor;
            auto& apvts = processor.getValueTreeState();

            setNormalised (apvts, "osc1_position", 0.63f);
            setNormalised (apvts, "filter1_cutoff", 0.42f);
            setNormalised (apvts, "fx_slot1", 1.0f);

            document = presets::write (apvts, sampleMetadata());
        }

        expect (document.isNotEmpty(), "a preset must have been written");

        ApolloAudioProcessor restored;
        auto& apvts = restored.getValueTreeState();

        expectEquals (static_cast<int> (presets::read (apvts, document)),
                      static_cast<int> (state::StateLoadResult::ok));

        expectWithinAbsoluteError (getNormalised (apvts, "osc1_position"), 0.63f, 0.0001f);
        expectWithinAbsoluteError (getNormalised (apvts, "filter1_cutoff"), 0.42f, 0.0001f);
        expectWithinAbsoluteError (getNormalised (apvts, "fx_slot1"), 1.0f, 0.0001f);

        expect (presets::getMetadata (apvts) == sampleMetadata(),
                "the metadata must come back exactly as it was saved");
    }

    void testDocumentIsReadableText()
    {
        beginTest ("a preset is text a person can read, not an opaque blob");

        // ADR-0053 chose text over binary so that a preset is inspectable,
        // diffable and survivable by hand. That is a property of the format
        // rather than a preference, so it is asserted rather than assumed: a
        // change to a binary container would fail here.
        ApolloAudioProcessor processor;

        const auto document = presets::write (processor.getValueTreeState(), sampleMetadata());

        expect (document.startsWith ("<?xml"), "a preset must begin as XML text");
        expect (document.contains (params::stateTreeType),
                "and carry the state root by name");
        expect (document.contains ("Glass Bell"),
                "and its name must be legible in the file");
        expect (document.contains (state::schemaVersionProperty),
                "and its schema version must be visible without a parser");

        logMessage ("  document: " + juce::String (document.length()) + " characters");
    }

    void testMetadataCanBeReadWithoutLoading()
    {
        beginTest ("metadata can be read out of a preset without loading the sound");

        // What an index is built from. The browser reads a folder of these and
        // must not change the instrument to do it.
        ApolloAudioProcessor source;
        setNormalised (source.getValueTreeState(), "osc1_position", 0.77f);

        const auto document = presets::write (source.getValueTreeState(), sampleMetadata());

        ApolloAudioProcessor untouched;
        const auto before = getNormalised (untouched.getValueTreeState(), "osc1_position");

        presets::Metadata metadata;

        expectEquals (static_cast<int> (presets::readMetadata (document, metadata)),
                      static_cast<int> (state::StateLoadResult::ok));

        expect (metadata == sampleMetadata(), "the metadata must be complete");

        expectWithinAbsoluteError (getNormalised (untouched.getValueTreeState(), "osc1_position"),
                                   before, 0.0f,
                                   "reading metadata must not touch any instrument");
    }

    void testMetadataIsOptional()
    {
        beginTest ("a preset with no metadata is a valid preset");

        // Two documents look like this: one written before metadata existed, and
        // a host project that has never had a preset loaded into it. Neither is
        // an error — the browser names such a file after the file.
        ApolloAudioProcessor source;
        setNormalised (source.getValueTreeState(), "osc2_level", 0.5f);

        const auto document = presets::write (source.getValueTreeState(), {});

        presets::Metadata metadata;

        expectEquals (static_cast<int> (presets::readMetadata (document, metadata)),
                      static_cast<int> (state::StateLoadResult::ok));

        expect (metadata.name.isEmpty(), "an unnamed preset must report an empty name");

        ApolloAudioProcessor restored;

        expectEquals (static_cast<int> (presets::read (restored.getValueTreeState(), document)),
                      static_cast<int> (state::StateLoadResult::ok),
                      "and must still load");

        expectWithinAbsoluteError (getNormalised (restored.getValueTreeState(), "osc2_level"),
                                   0.5f, 0.0001f);
    }

    void testMetadataIsBounded()
    {
        beginTest ("an absurdly long field is cut rather than trusted or refused");

        presets::Metadata huge;
        huge.name = juce::String::repeatedString ("A", presets::maximumNameLength * 4);
        huge.comment = juce::String::repeatedString ("B", presets::maximumCommentLength * 4);

        ApolloAudioProcessor processor;

        const auto document = presets::write (processor.getValueTreeState(), huge);

        presets::Metadata read;

        expectEquals (static_cast<int> (presets::readMetadata (document, read)),
                      static_cast<int> (state::StateLoadResult::ok),
                      "a long name is not a reason to refuse a sound");

        expectEquals (read.name.length(), presets::maximumNameLength,
                      "the name must be cut to the documented bound");
        expectEquals (read.comment.length(), presets::maximumCommentLength,
                      "and so must the comment");

        // Bounded on the way out as well as in, so Apollo cannot write a file it
        // would then read back differently.
        expect (document.contains (juce::String::repeatedString ("A", presets::maximumNameLength)),
                "the cut name must be what was written");
        expect (! document.contains (
                    juce::String::repeatedString ("A", presets::maximumNameLength + 1)),
                "and nothing longer than it");
    }

    void testAwkwardTextSurvives()
    {
        beginTest ("quotes, angle brackets, newlines and non-Latin text survive a round trip");

        // A name is free text typed by a user, and XML has opinions about some
        // of these characters. Getting the escaping wrong produces a file that
        // either fails to parse or silently loses half a name.
        presets::Metadata awkward;
        awkward.name = "\"Bell\" & <Glass>";
        // Split so the compiler cannot read "\xa9e" as one hex escape, which is
        // out of range and is an error rather than the accented name intended.
        awkward.author = "Ren\xc3\xa9" "e";
        awkward.category = "Keys/Pads";
        awkward.comment = "Line one\nLine two\ttabbed";

        ApolloAudioProcessor processor;

        const auto document = presets::write (processor.getValueTreeState(), awkward);

        presets::Metadata read;

        expectEquals (static_cast<int> (presets::readMetadata (document, read)),
                      static_cast<int> (state::StateLoadResult::ok));

        expectEquals (read.name, awkward.name, "the name must survive its punctuation");
        expectEquals (read.author, awkward.author, "and the author its accents");
        expectEquals (read.category, awkward.category);
        expectEquals (read.comment, awkward.comment, "and the comment its line breaks");
    }

    void testEveryRejectionPreservesTheCurrentSound()
    {
        beginTest ("every way a preset can be refused leaves the loaded sound untouched");

        // The assertion that matters most in this file. A preset arrives from a
        // disk Apollo does not control, and the user is one click away from it
        // while holding a sound they have not saved.
        struct Case { const char* what; juce::String text; state::StateLoadResult expected; };

        ApolloAudioProcessor source;
        const auto valid = presets::write (source.getValueTreeState(), sampleMetadata());

        const std::vector<Case> cases {
            { "nothing at all", {}, state::StateLoadResult::emptyData },
            { "plain text", "this is not a preset", state::StateLoadResult::malformed },
            { "a truncated document", valid.substring (0, valid.length() / 2),
              state::StateLoadResult::malformed },
            { "well-formed XML that is not Apollo",
              "<?xml version=\"1.0\"?>\n<RECIPE servings=\"4\"/>",
              state::StateLoadResult::wrongProduct },
            { "Apollo state with no schema version",
              juce::String ("<?xml version=\"1.0\"?>\n<") + params::stateTreeType + "/>",
              state::StateLoadResult::unsupportedVersion },
            { "a version from the future",
              juce::String ("<?xml version=\"1.0\"?>\n<") + params::stateTreeType
                  + " " + state::schemaVersionProperty + "=\"999\"/>",
              state::StateLoadResult::unsupportedVersion },
            { "a version that is not a number",
              juce::String ("<?xml version=\"1.0\"?>\n<") + params::stateTreeType
                  + " " + state::schemaVersionProperty + "=\"two\"/>",
              state::StateLoadResult::malformed },
        };

        for (const auto& testCase : cases)
        {
            ApolloAudioProcessor processor;
            auto& apvts = processor.getValueTreeState();

            // A sound worth losing: three parameters away from the defaults, so
            // a partial apply would show up as one of them moving.
            setNormalised (apvts, "osc1_position", 0.31f);
            setNormalised (apvts, "filter1_cutoff", 0.58f);
            setNormalised (apvts, "env1_release", 0.74f);

            presets::setMetadata (apvts, sampleMetadata());

            const auto result = presets::read (apvts, testCase.text);

            expectEquals (static_cast<int> (result), static_cast<int> (testCase.expected),
                          juce::String (testCase.what) + " was refused for the wrong reason");

            expectWithinAbsoluteError (getNormalised (apvts, "osc1_position"), 0.31f, 0.0001f,
                                       juce::String (testCase.what) + " moved a parameter");
            expectWithinAbsoluteError (getNormalised (apvts, "filter1_cutoff"), 0.58f, 0.0001f,
                                       juce::String (testCase.what) + " moved a parameter");
            expectWithinAbsoluteError (getNormalised (apvts, "env1_release"), 0.74f, 0.0001f,
                                       juce::String (testCase.what) + " moved a parameter");

            expect (presets::getMetadata (apvts) == sampleMetadata(),
                    juce::String (testCase.what) + " replaced the loaded preset's name");

            // And the reason is safe to put in front of a user: no path, no
            // memory address, no quoted contents (UI_BINDINGS.md §13).
            const auto message = state::describe (result);

            expect (message.isNotEmpty(), "every refusal must have something to say");
            expect (! message.contains (testCase.text.substring (0, 12))
                        || testCase.text.isEmpty(),
                    "and must not quote the document back");
        }
    }

    void testTheTwoReadersAgree()
    {
        beginTest ("a preset and host state are refused on the same terms");

        // The reason `readStateXml` is shared rather than reimplemented. If
        // these ever diverge, a document a host would refuse becomes loadable
        // from a file, which is the difference between one validated entry point
        // and two.
        const std::vector<juce::String> documents {
            "<?xml version=\"1.0\"?>\n<RECIPE servings=\"4\"/>",
            juce::String ("<?xml version=\"1.0\"?>\n<") + params::stateTreeType + "/>",
            juce::String ("<?xml version=\"1.0\"?>\n<") + params::stateTreeType
                + " " + state::schemaVersionProperty + "=\"999\"/>",
        };

        for (const auto& text : documents)
        {
            ApolloAudioProcessor viaPreset;
            const auto presetResult = presets::read (viaPreset.getValueTreeState(), text);

            // The same document offered as host state, through the binary
            // wrapper a host would hand over.
            const auto xml = juce::parseXML (text);
            expect (xml != nullptr, "the test document must at least parse");

            if (xml == nullptr)
                continue;

            juce::MemoryBlock binary;
            juce::AudioProcessor::copyXmlToBinary (*xml, binary);

            ApolloAudioProcessor viaHost;
            const auto hostResult = state::readState (viaHost.getValueTreeState(),
                                                      binary.getData(),
                                                      static_cast<int> (binary.getSize()));

            expectEquals (static_cast<int> (presetResult), static_cast<int> (hostResult),
                          "the two readers disagreed about the same document");
        }
    }

    void testAnOlderPresetMigrates()
    {
        beginTest ("a preset written by an older schema is migrated, not refused");

        // Presets inherit the migration path rather than having one of their
        // own (ADR-0053). A version 1 document names the filter without an
        // index; Phase 5b renamed it, and a preset from before then must still
        // open with its cutoff intact (ADR-0032).
        // Built as a tree and written out, rather than assembled as a string:
        // the point is a document Apollo itself would have produced before
        // Phase 5b, and hand-written XML is one typo away from testing the
        // parser instead of the migration.
        juce::ValueTree tree (params::stateTreeType);
        tree.setProperty (state::schemaVersionProperty, 1, nullptr);

        juce::ValueTree cutoff { juce::Identifier (params::parameterTreeType) };
        cutoff.setProperty (params::parameterIdProperty, "filter_cutoff", nullptr);
        cutoff.setProperty ("value", 640.0f, nullptr);
        tree.appendChild (cutoff, nullptr);

        // Braced rather than parenthesised: `juce::ValueTree x (juce::Identifier (y))`
        // is a function declaration, not a variable.
        juce::ValueTree saved { juce::Identifier (presets::presetTreeType) };
        saved.setProperty (presets::nameProperty, "Old Sound", nullptr);
        tree.appendChild (saved, nullptr);

        const auto xml = tree.createXml();
        expect (xml != nullptr, "the test document must be writable");

        const auto document = xml != nullptr ? xml->toString() : juce::String();

        ApolloAudioProcessor processor;
        auto& apvts = processor.getValueTreeState();

        expectEquals (static_cast<int> (presets::read (apvts, document)),
                      static_cast<int> (state::StateLoadResult::ok),
                      "an older preset must migrate rather than be refused");

        if (const auto* parameter = apvts.getParameter ("filter1_cutoff"))
            expectWithinAbsoluteError (parameter->convertFrom0to1 (parameter->getValue()),
                                       640.0f, 1.0f,
                                       "the cutoff must arrive under its new name");

        expectEquals (presets::getMetadata (apvts).name, juce::String ("Old Sound"),
                      "and the metadata must survive the migration");
    }

    void testAPresetCarriesNoController()
    {
        beginTest ("a preset contains no MIDI mappings and no expression settings");

        // A `.rnv` is made to be shared. Sending somebody a sound must not send
        // them your keyboard's setup with it.
        ApolloAudioProcessor processor;
        auto& apvts = processor.getValueTreeState();

        // A controller configuration worth not leaking: a learned mapping, an
        // MPE zone and a non-default bend range.
        auto& midi = processor.getMidiControl();

        midi::Mapping mapping;
        mapping.parameterIndex = params::indexOfParameter ("osc1_position");
        mapping.address.controller = 74;
        mapping.address.channel = midi::omniChannel;

        expect (midi::succeeded (midi.assign (mapping)), "the test mapping must have been learned");

        setPlain (apvts, "midi_bend_range", 7.0f);
        setPlain (apvts, "mpe_zone", 1.0f);

        const auto document = presets::write (apvts, sampleMetadata());

        expect (! document.contains (midi::MidiControlManager::mappingsTreeType),
                "a preset must not carry the learned MIDI mappings");

        for (const char* id : { "midi_bend_range", "mpe_zone", "mpe_members", "mpe_bend_range" })
            expect (! document.contains (juce::String ("\"") + id + "\""),
                    juce::String (id) + " describes the controller and must not be in a preset");

        // And the rule is the registry's, not a list kept in the document layer.
        expect (presets::isExcludedFromPresets ("mpe_zone"));
        expect (presets::isExcludedFromPresets ("midi_bend_range"));
        expect (! presets::isExcludedFromPresets ("osc1_position"),
                "an ordinary patch parameter must be in the preset");
        expect (! presets::isExcludedFromPresets ("no_such_parameter"),
                "an unknown id is not excluded, it is simply unknown");
    }

    void testLoadingAPresetLeavesTheControllerAlone()
    {
        beginTest ("loading a preset keeps this user's MIDI mappings and expression settings");

        // The other half, and the one that would hurt: auditioning a sound must
        // not silently unlearn the mappings the user set up, nor reset their MPE
        // zone to whatever the preset's author had — nor to the defaults, which
        // is what would happen if the omission were simply left alone.
        ApolloAudioProcessor author;
        setNormalised (author.getValueTreeState(), "osc1_position", 0.81f);

        const auto document = presets::write (author.getValueTreeState(), sampleMetadata());

        ApolloAudioProcessor user;
        auto& apvts = user.getValueTreeState();
        auto& midi = user.getMidiControl();

        midi::Mapping mapping;
        mapping.parameterIndex = params::indexOfParameter ("filter1_cutoff");
        mapping.address.controller = 21;
        mapping.address.channel = midi::omniChannel;

        expect (midi::succeeded (midi.assign (mapping)), "the user's mapping must have been learned");
        midi.writeToState();

        setPlain (apvts, "midi_bend_range", 12.0f);
        setPlain (apvts, "mpe_zone", 2.0f);

        expectEquals (static_cast<int> (presets::read (apvts, document)),
                      static_cast<int> (state::StateLoadResult::ok));

        expectWithinAbsoluteError (getNormalised (apvts, "osc1_position"), 0.81f, 0.0001f,
                                   "the sound must have loaded");

        expectWithinAbsoluteError (getPlain (apvts, "midi_bend_range"), 12.0f, 0.01f,
                                   "the user's bend range must survive a preset load");
        expectWithinAbsoluteError (getPlain (apvts, "mpe_zone"), 2.0f, 0.01f,
                                   "and so must their MPE zone");

        const auto mappings = apvts.state.getChildWithName (
            juce::Identifier (midi::MidiControlManager::mappingsTreeType));

        expect (mappings.isValid() && mappings.getNumChildren() == 1,
                "the user's learned mapping must still be in the state tree");

        // And it must still be in the table that actually drives audio, not only
        // in the document — the two are kept in step by the manager.
        expectEquals (user.getMidiControl().getMappings().size(), 1,
                      "and must still be live");
    }

    void testOversizedDocumentsAreNotParsed()
    {
        beginTest ("something far too large to be a preset is refused on its size alone");

        // Pointing Apollo at a video file should cost a length check, not an
        // attempt to build a DOM from it.
        const auto huge = juce::String::repeatedString ("<A/>", presets::maximumDocumentBytes / 2);

        expect (static_cast<int> (huge.getNumBytesAsUTF8()) > presets::maximumDocumentBytes,
                "the test document must actually exceed the bound");

        ApolloAudioProcessor processor;
        auto& apvts = processor.getValueTreeState();

        setNormalised (apvts, "osc1_position", 0.19f);

        expectEquals (static_cast<int> (presets::read (apvts, huge)),
                      static_cast<int> (state::StateLoadResult::tooLarge));

        expectWithinAbsoluteError (getNormalised (apvts, "osc1_position"), 0.19f, 0.0001f,
                                   "and the sound must survive it");

        presets::Metadata metadata;

        expectEquals (static_cast<int> (presets::readMetadata (huge, metadata)),
                      static_cast<int> (state::StateLoadResult::tooLarge),
                      "the index path must refuse it on the same terms");
    }

    void testMetadataTravelsIntoHostState()
    {
        beginTest ("the name of the loaded preset is saved into the host project");

        // Metadata is a child of the state tree rather than a wrapper around it,
        // so it rides along into a project save for free. That is what lets a
        // session reopen showing the sound it was on rather than "Init".
        juce::MemoryBlock hostState;

        {
            ApolloAudioProcessor processor;
            auto& apvts = processor.getValueTreeState();

            const auto preset = presets::write (apvts, sampleMetadata());

            expectEquals (static_cast<int> (presets::read (apvts, preset)),
                          static_cast<int> (state::StateLoadResult::ok));

            processor.getStateInformation (hostState);
        }

        ApolloAudioProcessor reopened;
        reopened.setStateInformation (hostState.getData(), static_cast<int> (hostState.getSize()));

        expect (presets::getMetadata (reopened.getValueTreeState()) == sampleMetadata(),
                "a reopened project must remember which preset it was on");
    }
};

PresetDocumentTests presetDocumentTests;

} // namespace
