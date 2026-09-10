/*
    Controller profile tests.

    A profile is a shortcut, not a mode, and these tests are mostly about
    holding it to that: every entry goes through the same validation a learned
    mapping does, the bijection still applies, and nothing behaves differently
    afterwards. The interesting cases are the ones where a profile meets
    something already there, and where a profile names something Apollo does not
    have.

    The built-in profiles are checked against the registry as well, because a
    profile entry naming a parameter that does not exist is a silent hole: it
    would apply one assignment fewer than the user was shown, and nothing else
    would say so.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include "Audio/ApolloAudioProcessor.h"
#include "MIDI/ControllerProfile.h"
#include "Parameters/ParameterDefinitions.h"
#include "UI/ParameterBridge.h"

using namespace apollo;

namespace
{

/** Builds a `{"version":1,"type":...}` bridge message. */
[[nodiscard]] juce::String profileMessage (const juce::String& type,
                                           const juce::String& profileId = {},
                                           const juce::String& mode = {})
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("version", ui::protocolVersion);
    object->setProperty ("type", type);

    if (profileId.isNotEmpty())
        object->setProperty ("profile", profileId);

    if (mode.isNotEmpty())
        object->setProperty ("mode", mode);

    return juce::JSON::toString (juce::var (object));
}

class ControllerProfileTests final : public juce::UnitTest
{
public:
    ControllerProfileTests()
        : juce::UnitTest ("Controller profiles", "MIDI")
    {
    }

    void runTest() override
    {
        testBuiltInProfilesAreWellFormed();
        testApplyingFillsTheTable();
        testReplaceAndMerge();
        testUnknownParameterEntriesAreDropped();
        testProfilesAreNotAMode();
        testBridgeCommands();
    }

private:
    static void prepare (ApolloAudioProcessor& processor)
    {
        processor.setRateAndBufferSizeDetails (48000.0, 128);
        processor.prepareToPlay (48000.0, 128);
    }

    void testBuiltInProfilesAreWellFormed()
    {
        beginTest ("Every built-in profile names real parameters and assignable controls");

        expect (midi::controllerProfileCount() > 0);

        std::vector<juce::String> seenIds;

        for (const auto& profile : midi::controllerProfiles)
        {
            // Named `profileName` rather than `name`: juce::UnitTest has a member
            // of that name, and a local that hides it is a warning Apollo treats
            // as an error.
            const auto profileName = params::toJuceString (profile.id);

            expect (! profile.id.empty(), "a profile needs an identifier");
            expect (! profile.name.empty(), profileName + ": a profile needs a display name");
            expect (! profile.description.empty(), profileName + ": a profile needs a description");
            expect (! profile.entries.empty(), profileName + ": an empty profile does nothing");

            for (const auto& seen : seenIds)
                expect (seen != profileName, "profile identifiers must be unique: " + profileName);

            seenIds.push_back (profileName);

            // Within one profile, no controller and no parameter may appear
            // twice: the bijection would silently drop the earlier entry, and
            // the user would be told the profile applied more than it did.
            for (std::size_t i = 0; i < profile.entries.size(); ++i)
            {
                const auto& entry = profile.entries[i];
                const auto label = profileName + " entry " + juce::String (static_cast<int> (i));

                expect (midi::isValidController (entry.controller),
                        label + ": controller out of range");
                expect (! midi::isReservedController (entry.controller),
                        label + ": names a reserved controller, which can never be assigned");
                expect (midi::isValidChannel (entry.channel), label + ": channel out of range");

                expect (params::indexOfParameter (entry.parameterId) >= 0,
                        label + ": names a parameter the registry does not have");

                for (std::size_t j = i + 1; j < profile.entries.size(); ++j)
                {
                    expect (profile.entries[j].controller != entry.controller,
                            label + ": duplicate controller inside one profile");
                    expect (profile.entries[j].parameterId != entry.parameterId,
                            label + ": duplicate parameter inside one profile");
                }
            }
        }

        expect (midi::findControllerProfile ("sound_controllers") != nullptr);
        expect (midi::findControllerProfile ("no_such_profile") == nullptr);
    }

    void testApplyingFillsTheTable()
    {
        beginTest ("Applying a profile assigns every one of its entries");

        ApolloAudioProcessor processor;
        prepare (processor);

        auto& control = processor.getMidiControl();
        const auto* profile = midi::findControllerProfile ("sound_controllers");

        expect (profile != nullptr);

        if (profile == nullptr)
            return;

        const auto result = control.applyProfile (*profile, midi::ProfileMode::replace);
        const auto expected = static_cast<int> (profile->entries.size());

        expectEquals (result.applied, expected);
        expectEquals (result.unknownParameter, 0);
        expectEquals (result.rejected, 0);
        expectEquals (result.total(), expected);

        const auto mappings = control.getMappings();
        expectEquals (mappings.size(), expected);

        // Every entry is findable by both of the things a mapping is addressed
        // by, and carries the controller the profile named.
        for (const auto& entry : profile->entries)
        {
            const auto index = params::indexOfParameter (entry.parameterId);
            const auto* mapping = mappings.findForParameter (index);

            expect (mapping != nullptr,
                    juce::String ("no mapping for ") + juce::String (entry.parameterId.data(),
                                                                     entry.parameterId.size()));

            if (mapping != nullptr)
                expectEquals (mapping->address.controller, entry.controller);
        }

        expectEquals (control.getLastAppliedProfile(), juce::String ("sound_controllers"));
    }

    void testReplaceAndMerge()
    {
        beginTest ("Replace clears what was there; merge keeps it");

        const auto* first = midi::findControllerProfile ("sound_controllers");
        const auto* second = midi::findControllerProfile ("general_purpose");

        expect (first != nullptr && second != nullptr);

        if (first == nullptr || second == nullptr)
            return;

        {
            ApolloAudioProcessor processor;
            prepare (processor);
            auto& control = processor.getMidiControl();

            (void) control.applyProfile (*first, midi::ProfileMode::replace);
            const auto afterFirst = control.getMappings().size();

            (void) control.applyProfile (*second, midi::ProfileMode::replace);

            expectEquals (control.getMappings().size(), static_cast<int> (second->entries.size()),
                          "replace must leave the second profile and nothing else");
            expect (afterFirst > 0);

            // The first profile's controllers no longer drive anything.
            const auto mappings = control.getMappings();
            expect (mappings.findForMessage (1, first->entries[0].controller) == nullptr,
                    "a replaced profile's controller must have been released");
        }

        {
            ApolloAudioProcessor processor;
            prepare (processor);
            auto& control = processor.getMidiControl();

            (void) control.applyProfile (*first, midi::ProfileMode::replace);
            const auto result = control.applyProfile (*second, midi::ProfileMode::merge);

            const auto mappings = control.getMappings();

            // The two built-in profiles overlap on some parameters but on no
            // controller numbers, so a merge keeps both controller sets and the
            // overlapping parameters end up on the second profile's controls.
            expect (mappings.size() > static_cast<int> (second->entries.size()),
                    "merge must keep assignments the second profile did not touch");
            expectEquals (result.applied, static_cast<int> (second->entries.size()));

            for (const auto& entry : second->entries)
            {
                const auto* mapping = mappings.findForParameter (
                    params::indexOfParameter (entry.parameterId));

                expect (mapping != nullptr && mapping->address.controller == entry.controller,
                        "the merged profile must win where the two overlap");
            }

            expect (result.replaced > 0,
                    "the overlap must be reported rather than performed silently");
        }
    }

    void testUnknownParameterEntriesAreDropped()
    {
        beginTest ("An entry naming a parameter this build lacks is dropped, not fatal");

        ApolloAudioProcessor processor;
        prepare (processor);

        static constexpr std::array<midi::ProfileEntry, 3> entries { {
            { 20, "filter1_cutoff" },
            { 21, "osc9_does_not_exist" },
            { 64, "filter1_resonance" }, // CC 64 is reserved: refused, not dropped.
        } };

        const midi::ControllerProfile profile { "test_partial", "Partial", "For the test.",
                                                entries };

        const auto result = processor.getMidiControl().applyProfile (
            profile, midi::ProfileMode::replace);

        expectEquals (result.applied, 1);
        expectEquals (result.unknownParameter, 1);
        expectEquals (result.rejected, 1);
        expectEquals (result.total(), 3);

        expectEquals (processor.getMidiControl().getMappings().size(), 1,
                      "the entry that could be applied still was");
    }

    void testProfilesAreNotAMode()
    {
        beginTest ("A profile is a shortcut: what it produces is ordinary mappings");

        ApolloAudioProcessor processor;
        prepare (processor);

        auto& control = processor.getMidiControl();
        (void) control.applyProfile ("sound_controllers", midi::ProfileMode::replace);

        const auto before = control.getMappings().size();

        // Learned over the top, removed, cleared — every ordinary operation
        // works on a profile's mappings exactly as on a learned one.
        expect (control.beginLearn ("master_gain"));

        juce::MidiBuffer midiBuffer;
        midiBuffer.addEvent (juce::MidiMessage::controllerEvent (1, 74, 100), 0);

        juce::AudioBuffer<float> buffer (2, 128);
        buffer.clear();
        processor.processBlock (buffer, midiBuffer);
        control.flushPendingChanges();

        const auto mappings = control.getMappings();
        expectEquals (mappings.size(), before,
                      "learning CC 74 onto another parameter must move it, not add one");

        const auto* moved = mappings.findForParameter (params::indexOfParameter ("master_gain"));
        expect (moved != nullptr && moved->address.controller == 74,
                "the profile's CC 74 must now drive what it was learned to");
        expect (mappings.findForParameter (params::indexOfParameter ("filter1_cutoff")) == nullptr,
                "and must have been taken from the parameter the profile gave it to");

        control.clearAllMappings();
        expectEquals (control.getMappings().size(), 0);

        // Applying by ID, and an ID that does not exist.
        const auto ok = control.applyProfile ("general_purpose", midi::ProfileMode::replace);
        expect (ok.applied > 0);

        const auto missing = control.applyProfile ("no_such_profile", midi::ProfileMode::replace);
        expectEquals (missing.total(), 0, "an unknown profile applies nothing");
        expect (control.getMappings().size() > 0,
                "and leaves what was already there alone");
    }

    void testBridgeCommands()
    {
        beginTest ("The bridge lists profiles and applies them by name");

        ApolloAudioProcessor processor;
        prepare (processor);

        ui::ParameterBridge bridge (processor.getValueTreeState());

        const auto parse = [] (const juce::String& json)
        {
            juce::var parsed;
            juce::JSON::parse (json, parsed);
            return parsed;
        };

        // The list is answered with no MIDI subsystem attached: it describes
        // what Apollo could be set to, which is true regardless.
        {
            const auto reply = bridge.handleMessage (profileMessage ("requestControllerProfiles"));
            const auto parsed = parse (reply);
            auto* object = parsed.getDynamicObject();

            expect (object != nullptr);

            if (object == nullptr)
                return;

            expectEquals (object->getProperty ("type").toString(),
                          juce::String ("controllerProfiles"));

            const auto* profiles = object->getProperty ("profiles").getArray();
            expect (profiles != nullptr);
            expectEquals (profiles != nullptr ? profiles->size() : -1,
                          static_cast<int> (midi::controllerProfileCount()));

            if (profiles != nullptr)
            {
                for (const auto& entry : *profiles)
                {
                    auto* fields = entry.getDynamicObject();
                    expect (fields != nullptr);

                    if (fields == nullptr)
                        continue;

                    for (const auto* required : { "id", "name", "description", "assignments" })
                        expect (fields->hasProperty (required),
                                juce::String ("profile entry missing '") + required + "'");
                }
            }
        }

        bridge.setMidiControl (&processor.getMidiControl());

        // An unknown profile is refused by the protocol layer, before anything
        // can act on it.
        expect (bridge.handleMessage (profileMessage ("applyControllerProfile", "no_such_profile"))
                    .contains ("UNKNOWN_PARAMETER"));
        expectEquals (processor.getMidiControl().getMappings().size(), 0);

        // As is a mode that is neither of the two.
        expect (bridge.handleMessage (
                    profileMessage ("applyControllerProfile", "sound_controllers", "sideways"))
                    .contains ("MALFORMED_MESSAGE"));
        expectEquals (processor.getMidiControl().getMappings().size(), 0);

        {
            const auto reply = bridge.handleMessage (
                profileMessage ("applyControllerProfile", "sound_controllers"));

            const auto parsed = parse (reply);
            auto* object = parsed.getDynamicObject();

            expect (object != nullptr);

            if (object == nullptr)
                return;

            expectEquals (object->getProperty ("type").toString(), juce::String ("midiMappings"));
            expectEquals (object->getProperty ("status").toString(),
                          juce::String ("PROFILE_APPLIED"));
            expect (object->getProperty ("statusMessage").toString().contains ("Applied"),
                    "the reply must say how much of the profile applied");

            const auto* mappings = object->getProperty ("mappings").getArray();
            expect (mappings != nullptr);
            expectEquals (mappings != nullptr ? mappings->size() : -1,
                          processor.getMidiControl().getMappings().size());
        }

        // Merging through the bridge, with the mode named explicitly.
        {
            const auto reply = bridge.handleMessage (
                profileMessage ("applyControllerProfile", "general_purpose", "merge"));

            expect (reply.contains ("PROFILE_APPLIED"));
            expect (processor.getMidiControl().getMappings().size()
                        > static_cast<int> (midi::findControllerProfile ("general_purpose")
                                                ->entries.size()),
                    "a merge must have kept what the first profile assigned");
        }
    }
};

ControllerProfileTests controllerProfileTests;

} // namespace
