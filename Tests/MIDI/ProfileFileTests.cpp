/*
    Controller profiles as files (Phase 13h-3): the format, the user's library
    of them, and the bridge commands that save, delete, export, import and
    edit.

    Against real folders in a scratch directory, for the reason the preset
    library's tests use real ones.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include "Audio/ApolloAudioProcessor.h"
#include "MIDI/ProfileFile.h"
#include "Parameters/ParameterId.h"
#include "Resources/ProfileLibrary.h"
#include "UI/ParameterBridge.h"

using namespace apollo;

namespace
{

class Scratch
{
public:
    Scratch()
        : root (juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("ApolloProfileTests")
                    .getChildFile (juce::String (juce::Random::getSystemRandom().nextInt (1000000))))
    {
        root.createDirectory();
    }

    ~Scratch() { root.getParentDirectory().deleteRecursively(); }

    [[nodiscard]] juce::File child (const juce::String& path) const { return root.getChildFile (path); }

private:
    juce::File root;
};

[[nodiscard]] juce::var parse (const juce::String& json)
{
    juce::var parsed;
    (void) juce::JSON::parse (json, parsed);
    return parsed;
}

[[nodiscard]] juce::String field (const juce::String& json, const char* name)
{
    // Held, because the object belongs to the var: a pointer taken from a
    // temporary would outlive it.
    const auto parsed = parse (json);
    const auto* object = parsed.getDynamicObject();
    return object != nullptr ? object->getProperty (name).toString() : juce::String();
}

/** A bridge message: the type, the version, and @p fields. */
[[nodiscard]] juce::String message (const char* type, std::initializer_list<std::pair<const char*, juce::var>> fields = {})
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("type", type);
    object->setProperty ("version", 1);

    for (const auto& [name, value] : fields)
        object->setProperty (name, value);

    return juce::JSON::toString (juce::var (object));
}

/** The text of a profile file with @p assignment as its one assignment. */
[[nodiscard]] juce::String withAssignment (const juce::String& assignment)
{
    return R"({"format":"apollo-controller-profile","version":1,"name":"One","assignments":[)" + assignment + "]}";
}

class ProfileFileTests final : public juce::UnitTest
{
public:
    ProfileFileTests()
        : juce::UnitTest ("Controller profile files", "MIDI")
    {
    }

    void runTest() override
    {
        testRoundTrip();
        testRefusals();
        testRandomDamage();
        testTheLibrary();
        testImportAndExport();
        testTheBridge();
        testEditingAnAssignment();
    }

private:
    void testRoundTrip()
    {
        beginTest ("A profile written and read back is the same profile");

        for (const auto& builtIn : midi::controllerProfiles)
        {
            const auto data = midi::toProfileData (builtIn);
            midi::ProfileData read;

            expect (midi::readProfile (midi::writeProfile (data), read) == midi::ProfileReadResult::ok);
            expect (read == data, data.name);
        }

        beginTest ("The current assignments become a profile in controller order, ranges and channels kept");

        midi::MappingTable table;

        midi::Mapping cutoff;
        cutoff.address = { 74, 3 };
        cutoff.parameterIndex = params::indexOfParameter ("filter1_cutoff");
        cutoff.minimum = 0.9f;
        cutoff.maximum = 0.1f;

        midi::Mapping level;
        level.address = { 20, midi::omniChannel };
        level.parameterIndex = params::indexOfParameter ("osc1_level");

        (void) table.assign (cutoff);
        (void) table.assign (level);

        const auto captured = midi::captureProfile (table, "Desk", "Two knobs");

        expectEquals (static_cast<int> (captured.assignments.size()), 2);
        expectEquals (captured.assignments[0].controller, 20, "in controller order");
        expectEquals (captured.assignments[1].parameterId, juce::String ("filter1_cutoff"));
        expectEquals (captured.assignments[1].channel, 3);
        expectEquals (captured.assignments[1].minimum, 0.9f, "a reversed range stays reversed");

        // Written as a person would write it, and read back as the same float.
        const auto written = midi::writeProfile (captured);
        expect (written.contains ("0.9") && ! written.contains ("0.899999") && ! written.contains ("0.90000"), written);

        midi::ProfileData reread;
        expect (midi::readProfile (written, reread) == midi::ProfileReadResult::ok);
        expect (reread == captured, "four places keep a captured range exactly");

        beginTest ("A parameter this build lacks is read, then dropped when applied, and counted");

        midi::ProfileData later;
        expect (midi::readProfile (withAssignment (R"({"controller":20,"channel":0,"parameter":"osc9_warp_depth","min":0,"max":1},)"
                                                   R"({"controller":21,"channel":0,"parameter":"osc1_level","min":0,"max":1})"),
                                   later) == midi::ProfileReadResult::ok);

        ApolloAudioProcessor processor;
        const auto result = processor.getMidiControl().applyProfile (later, midi::ProfileMode::replace, "user-1");

        expectEquals (result.applied, 1);
        expectEquals (result.unknownParameter, 1);
        expectEquals (processor.getMidiControl().getLastAppliedProfile(), juce::String ("user-1"));
    }

    void testRefusals()
    {
        beginTest ("Everything that is not a valid profile is refused, and leaves the destination alone");

        const auto good = withAssignment (R"({"controller":74,"channel":0,"parameter":"filter1_cutoff","min":0,"max":1})");

        midi::ProfileData sentinel;
        sentinel.name = "untouched";

        const auto refused = [&] (const juce::String& text, midi::ProfileReadResult expected, const juce::String& why)
        {
            auto destination = sentinel;
            const auto result = midi::readProfile (text, destination);

            expect (result == expected, why + ": got " + midi::describe (result));
            expect (destination == sentinel, why + ": the destination changed");
        };

        expect (midi::readProfile (good, sentinel) == midi::ProfileReadResult::ok);
        sentinel = {};
        sentinel.name = "untouched";

        refused ({}, midi::ProfileReadResult::empty, "empty");
        refused ("   ", midi::ProfileReadResult::empty, "blank");
        refused (juce::String::repeatedString ("x", midi::maximumProfileBytes + 1), midi::ProfileReadResult::tooLarge, "too large");
        refused ("{ not json", midi::ProfileReadResult::notJson, "not JSON");
        refused (R"([1,2,3])", midi::ProfileReadResult::notAProfile, "an array");
        refused (R"({"format":"something-else","version":1,"name":"X","assignments":[]})",
                 midi::ProfileReadResult::notAProfile, "another format");
        refused (R"({"format":"apollo-controller-profile","name":"X","assignments":[]})",
                 midi::ProfileReadResult::notAProfile, "no version");
        refused (R"({"format":"apollo-controller-profile","version":2,"name":"X","assignments":[]})",
                 midi::ProfileReadResult::newerVersion, "a newer version");
        refused (R"({"format":"apollo-controller-profile","version":1,"name":"X"})",
                 midi::ProfileReadResult::notAProfile, "no assignments");
        refused (R"({"format":"apollo-controller-profile","version":1,"assignments":[]})",
                 midi::ProfileReadResult::invalidName, "no name");
        refused (R"({"format":"apollo-controller-profile","version":1,"name":"   ","assignments":[]})",
                 midi::ProfileReadResult::invalidName, "a blank name");
        refused (R"({"format":"apollo-controller-profile","version":1,"name":")" + juce::String::repeatedString ("n", 65)
                     + R"(","assignments":[]})",
                 midi::ProfileReadResult::invalidName, "a name too long");
        refused (R"({"format":"apollo-controller-profile","version":1,"name":"X","description":7,"assignments":[]})",
                 midi::ProfileReadResult::invalidAssignment, "a description that is not text");

        juce::StringArray many;
        for (int i = 0; i <= midi::maxMappings; ++i)
            many.add (R"({"controller":1,"channel":0,"parameter":"osc1_level","min":0,"max":1})");

        refused (withAssignment (many.joinIntoString (",")), midi::ProfileReadResult::tooManyAssignments, "65 assignments");

        for (const auto* bad : { R"({"controller":128,"channel":0,"parameter":"osc1_level","min":0,"max":1})",
                                 R"({"controller":-1,"channel":0,"parameter":"osc1_level","min":0,"max":1})",
                                 R"({"controller":1.5,"channel":0,"parameter":"osc1_level","min":0,"max":1})",
                                 R"({"controller":"74","channel":0,"parameter":"osc1_level","min":0,"max":1})",
                                 R"({"controller":74,"channel":17,"parameter":"osc1_level","min":0,"max":1})",
                                 R"({"controller":74,"channel":0,"parameter":"Filter Cutoff","min":0,"max":1})",
                                 R"({"controller":74,"channel":0,"parameter":12,"min":0,"max":1})",
                                 R"({"controller":74,"channel":0,"parameter":"osc1_level","min":1.5,"max":1})",
                                 R"({"controller":74,"channel":0,"parameter":"osc1_level","min":0,"max":-0.1})",
                                 R"({"controller":74,"channel":0,"parameter":"osc1_level","min":0})",
                                 R"("not an object")" })
            refused (withAssignment (bad), midi::ProfileReadResult::invalidAssignment, bad);
    }

    void testRandomDamage()
    {
        beginTest ("A profile damaged anywhere is refused, or read as a valid one");

        const auto good = midi::writeProfile (midi::toProfileData (midi::controllerProfiles[0]));
        juce::Random random (1313);
        auto refusedCount = 0, readCount = 0;

        for (int attempt = 0; attempt < 400; ++attempt)
        {
            auto damaged = good;

            for (int flips = 1 + random.nextInt (4); flips > 0; --flips)
            {
                const auto at = random.nextInt (damaged.length());
                damaged = damaged.replaceSection (at, 1, juce::String::charToString (
                                                             static_cast<juce::juce_wchar> (32 + random.nextInt (95))));
            }

            midi::ProfileData read;

            if (midi::readProfile (damaged, read) != midi::ProfileReadResult::ok)
            {
                ++refusedCount;
                continue;
            }

            ++readCount;
            expect (read.name.isNotEmpty() && read.name.length() <= midi::maximumProfileNameLength);
            expect (static_cast<int> (read.assignments.size()) <= midi::maxMappings);

            for (const auto& assignment : read.assignments)
            {
                expect (midi::isValidController (assignment.controller));
                expect (midi::isValidChannel (assignment.channel));
                expect (assignment.minimum >= 0.0f && assignment.minimum <= 1.0f);
                expect (assignment.maximum >= 0.0f && assignment.maximum <= 1.0f);
                expect (params::isValidParameterId (assignment.parameterId.toStdString()));
            }
        }

        logMessage ("  of 400 damaged profiles: " + juce::String (refusedCount) + " refused, "
                    + juce::String (readCount) + " read and checked");
        expect (refusedCount > 0);
    }

    void testTheLibrary()
    {
        beginTest ("The library lists the built-ins, then the user's by name; a missing folder is no error");

        Scratch scratch;
        resources::ProfileLibrary library;
        library.setDirectory (scratch.child ("Profiles"));
        library.rescan();

        expectEquals (static_cast<int> (library.getProfiles().size()), static_cast<int> (midi::controllerProfileCount()));
        expect (! library.getProfiles()[0].user);
        expectEquals (library.getProfiles()[0].id, juce::String ("sound_controllers"));

        beginTest ("A saved profile is the user's, and is never replaced unless that is asked for");

        auto desk = midi::toProfileData (midi::controllerProfiles[1]);
        desk.name = "Studio Desk";

        juce::String id;
        expect (library.save (desk, false, id) == resources::ProfileSaveResult::ok);
        expectEquals (id, juce::String ("user-1"));

        const auto* saved = library.find (id);
        expect (saved != nullptr && saved->user && saved->data == desk);
        expect (scratch.child ("Profiles/Studio Desk.rnvprofile").existsAsFile());

        auto changed = desk;
        changed.assignments.pop_back();

        juce::String again;
        expect (library.save (changed, false, again) == resources::ProfileSaveResult::alreadyExists);
        expect (library.find (id)->data == desk, "a refused save changed nothing");
        expect (library.save (changed, true, again) == resources::ProfileSaveResult::ok);
        expect (library.find (again)->data == changed);

        auto alpha = desk;
        alpha.name = "Alpha";
        expect (library.save (alpha, false, again) == resources::ProfileSaveResult::ok);
        expectEquals (library.find ("user-1")->data.name, juce::String ("Alpha"), "the user's are in name order");

        beginTest ("A name cannot climb out of the folder, or be nothing");

        auto climbing = desk;
        climbing.name = "../../Escape";
        expect (library.save (climbing, false, again) == resources::ProfileSaveResult::ok);
        expect (library.find (again)->file.isAChildOf (scratch.child ("Profiles")));

        auto nothing = desk;
        nothing.name = "...";
        expect (library.save (nothing, false, again) == resources::ProfileSaveResult::invalidName);

        beginTest ("Files that are not profiles are counted, not listed");

        expect (scratch.child ("Profiles/Broken.rnvprofile").replaceWithText ("{ nope"));
        expect (scratch.child ("Profiles/Notes.txt").replaceWithText ("not a profile"));
        library.rescan();

        expectEquals (library.getUnreadable(), 1);
        expectEquals (static_cast<int> (library.getProfiles().size()), static_cast<int> (midi::controllerProfileCount()) + 3);

        beginTest ("A user profile can be removed; a built-in cannot");

        const auto before = library.getProfiles().size();
        expect (! library.remove ("sound_controllers"));
        expect (! library.remove ("user-99"));
        expect (library.remove ("user-1"));
        expectEquals (library.getProfiles().size(), before - 1);
    }

    void testImportAndExport()
    {
        beginTest ("An export reads back as the profile; an import is a copy that never replaces one");

        Scratch scratch;
        resources::ProfileLibrary library;
        library.setDirectory (scratch.child ("Profiles"));
        library.rescan();

        const auto exported = scratch.child ("Elsewhere/Sound Controllers.rnvprofile");
        expect (library.exportTo ("sound_controllers", exported));

        midi::ProfileData read;
        expect (midi::readProfileFile (exported, read) == midi::ProfileReadResult::ok);
        expect (read == midi::toProfileData (midi::controllerProfiles[0]));
        expect (! library.exportTo ("user-7", scratch.child ("Elsewhere/Nothing.rnvprofile")));

        juce::String first, second;
        auto saved = resources::ProfileSaveResult::couldNotWrite;

        expect (library.importFile (exported, first, saved) == midi::ProfileReadResult::ok);
        expect (saved == resources::ProfileSaveResult::ok);
        expect (library.importFile (exported, second, saved) == midi::ProfileReadResult::ok);

        expectEquals (library.find (first)->data.name, juce::String ("Sound Controllers"));
        expectEquals (library.find (second)->data.name, juce::String ("Sound Controllers (2)"));

        const auto broken = scratch.child ("Elsewhere/Broken.rnvprofile");
        expect (broken.replaceWithText (R"({"format":"apollo-controller-profile","version":9})"));

        const auto count = library.getProfiles().size();
        juce::String none;
        expect (library.importFile (broken, none, saved) == midi::ProfileReadResult::newerVersion);
        expectEquals (library.getProfiles().size(), count, "nothing was written");
    }

    void testTheBridge()
    {
        Scratch scratch;
        ApolloAudioProcessor processor;
        auto& library = processor.getProfileLibrary();
        library.setDirectory (scratch.child ("Profiles"));

        ui::ParameterBridge bridge (processor.getValueTreeState());
        bridge.setMidiControl (&processor.getMidiControl());
        bridge.setProfileLibrary (&library);

        const auto folder = scratch.child ("Profiles").getFullPathName();

        beginTest ("The list says which profiles are the user's and never says where they are");

        const auto listed = bridge.handleMessage (message ("requestControllerProfiles"));
        expectEquals (field (listed, "type"), juce::String ("controllerProfiles"));
        expect (listed.contains (R"("user": false)") || listed.contains (R"("user":false)"));

        beginTest ("Saving the current assignments, refusing to replace, and replacing when asked");

        auto reply = bridge.handleMessage (message ("saveControllerProfile", { { "name", "Desk" } }));
        expectEquals (field (reply, "status"), juce::String ("SAVE_FAILED"), "nothing to save");

        (void) bridge.handleMessage (message ("applyControllerProfile", { { "profile", "sound_controllers" } }));

        reply = bridge.handleMessage (message ("saveControllerProfile", { { "name", "Desk" }, { "description", "Mine" } }));
        expectEquals (field (reply, "status"), juce::String ("SAVED"));
        expectEquals (field (reply, "selected"), juce::String ("user-1"));
        expect (! reply.contains (folder), "a path reached the page");

        reply = bridge.handleMessage (message ("saveControllerProfile", { { "name", "Desk" } }));
        expectEquals (field (reply, "status"), juce::String ("ALREADY_EXISTS"));

        reply = bridge.handleMessage (message ("saveControllerProfile", { { "name", "Desk" }, { "overwrite", true } }));
        expectEquals (field (reply, "status"), juce::String ("SAVED"));

        for (const auto* blank : { "", "   " })
            expect (bridge.handleMessage (message ("saveControllerProfile", { { "name", blank } })).contains ("INVALID_PROFILE_NAME"));

        expect (bridge.handleMessage (message ("saveControllerProfile", { { "name", juce::String::repeatedString ("n", 65) } }))
                    .contains ("INVALID_PROFILE_NAME"));

        beginTest ("A user profile applies as a built-in does, and a stale or malformed id is refused");

        (void) bridge.handleMessage (message ("midiMappingClearAll"));
        expectEquals (processor.getMidiControl().getMappings().size(), 0);

        reply = bridge.handleMessage (message ("applyControllerProfile", { { "profile", "user-1" } }));
        expectEquals (field (reply, "status"), juce::String ("PROFILE_APPLIED"));
        expect (field (reply, "statusMessage").contains ("Desk"));
        expectEquals (processor.getMidiControl().getMappings().size(),
                      static_cast<int> (midi::controllerProfiles[0].entries.size()));

        for (const auto* stale : { "user-99", "user-abc", "user-", "../user-1", "nothing" })
            expect (bridge.handleMessage (message ("applyControllerProfile", { { "profile", stale } })).contains ("UNKNOWN_PROFILE"),
                    stale);

        beginTest ("Export and import go through the editor's dialogs, and say what came of them");

        juce::String exportId, exportName;
        auto importAsked = false;

        bridge.onExportControllerProfile = [&] (const juce::String& id, const juce::String& profileName)
        {
            exportId = id;
            exportName = profileName;
        };
        bridge.onImportControllerProfile = [&importAsked] { importAsked = true; };

        expect (bridge.handleMessage (message ("exportControllerProfile", { { "profile", "user-1" } })).isEmpty(),
                "the answer comes when the dialog closes");
        expectEquals (exportId, juce::String ("user-1"));
        expectEquals (exportName, juce::String ("Desk"));

        const auto destination = scratch.child ("Elsewhere/Desk.rnvprofile");
        reply = bridge.exportControllerProfileTo (exportId, destination);
        expectEquals (field (reply, "status"), juce::String ("EXPORTED"));
        expect (destination.existsAsFile());
        expect (! reply.contains (destination.getFullPathName()));

        expect (bridge.handleMessage (message ("importControllerProfile")).isEmpty());
        expect (importAsked);

        reply = bridge.importControllerProfileFrom (destination);
        expectEquals (field (reply, "status"), juce::String ("IMPORTED"));
        expectEquals (library.find (field (reply, "selected"))->data.name, juce::String ("Desk (2)"));

        const auto junk = scratch.child ("Elsewhere/Junk.rnvprofile");
        expect (junk.replaceWithText ("{ junk"));
        expectEquals (field (bridge.importControllerProfileFrom (junk), "status"), juce::String ("IMPORT_FAILED"));

        beginTest ("A user profile can be removed and its assignments stay; a built-in cannot be");

        const auto assignments = processor.getMidiControl().getMappings().size();

        reply = bridge.handleMessage (message ("deleteControllerProfile", { { "profile", "sound_controllers" } }));
        expectEquals (field (reply, "status"), juce::String ("DELETE_FAILED"));

        reply = bridge.handleMessage (message ("deleteControllerProfile", { { "profile", "user-1" } }));
        expectEquals (field (reply, "status"), juce::String ("DELETED"));
        expectEquals (processor.getMidiControl().getMappings().size(), assignments,
                      "a profile is a shortcut, not something its assignments depend on");

        bridge.setProfileLibrary (nullptr);
        expect (bridge.handleMessage (message ("deleteControllerProfile", { { "profile", "user-1" } })).contains ("error"),
                "without a library the commands are refused");
        expectEquals (field (bridge.handleMessage (message ("requestControllerProfiles")), "type"),
                      juce::String ("controllerProfiles"), "the built-ins are still listed");
    }

    void testEditingAnAssignment()
    {
        beginTest ("An assignment's controller, channel and range can be edited, and a taken control is reported");

        ApolloAudioProcessor processor;
        ui::ParameterBridge bridge (processor.getValueTreeState());
        bridge.setMidiControl (&processor.getMidiControl());

        (void) bridge.handleMessage (message ("applyControllerProfile", { { "profile", "sound_controllers" } }));

        const auto cutoff = params::indexOfParameter ("filter1_cutoff");
        const auto resonance = params::indexOfParameter ("filter1_resonance");

        const auto edit = [&bridge] (const char* id, int controller, int channel, double minimum, double maximum)
        {
            return bridge.handleMessage (message ("midiMappingEdit", { { "id", id }, { "controller", controller },
                                                                       { "channel", channel }, { "min", minimum },
                                                                       { "max", maximum } }));
        };

        auto reply = edit ("filter1_cutoff", 20, 3, 0.2, 0.8);
        expectEquals (field (reply, "status"), juce::String ("EDITED"));

        // Held: getMappings returns a copy, and a pointer into a temporary one
        // would outlive it (the sanitizer job caught exactly that).
        const auto table = processor.getMidiControl().getMappings();
        const auto* edited = table.findForParameter (cutoff);
        expect (edited != nullptr);

        if (edited != nullptr)
        {
            expectEquals (edited->address.controller, 20);
            expectEquals (edited->address.channel, 3);
            expectEquals (edited->minimum, 0.2f);
            expectEquals (edited->maximum, 0.8f);
        }

        // Onto the control the resonance has: the resonance loses it, and is told.
        reply = edit ("filter1_cutoff", 71, 0, 1.0, 0.0);
        expectEquals (field (reply, "status"), juce::String ("EDITED"));
        expect (field (reply, "statusMessage").contains ("Resonance"), field (reply, "statusMessage"));
        expect (processor.getMidiControl().getMappings().findForParameter (resonance) == nullptr);

        const auto before = processor.getMidiControl().getMappings().size();

        expectEquals (field (edit ("filter1_cutoff", 64, 0, 0.0, 1.0), "status"), juce::String ("EDIT_FAILED"),
                      "the sustain pedal is not assignable");
        expectEquals (field (edit ("osc2_level", 30, 0, 0.0, 1.0), "status"), juce::String ("EDIT_FAILED"),
                      "an edit does not make an assignment");
        expectEquals (processor.getMidiControl().getMappings().size(), before);

        for (const auto& bad : { edit ("filter1_cutoff", 128, 0, 0.0, 1.0), edit ("filter1_cutoff", 20, 17, 0.0, 1.0),
                                 edit ("filter1_cutoff", 20, 0, 2.0, 1.0), edit ("filter1_cutoff", 20, 0, 0.0, -1.0) })
            expect (bad.contains ("INVALID_MAPPING"), bad);

        expect (bridge.handleMessage (message ("midiMappingEdit", { { "id", "filter1_cutoff" }, { "controller", 20.5 },
                                                                    { "channel", 0 }, { "min", 0 }, { "max", 1 } }))
                    .contains ("INVALID_MAPPING"));
        expect (edit ("no_such_thing", 20, 0, 0.0, 1.0).contains ("UNKNOWN_PARAMETER"));

        beginTest ("The update pushed after an edit says nothing more; the one after a learn says what it did");

        // Found driving 13h-3 by hand: each edit's reply was followed by the
        // change notification repeating the table's last assignment outcome,
        // "this parameter's previous control was released", over the reply.
        juce::StringArray pushed;
        bridge.setOutboundHandler ([&pushed] (const juce::String& json) { pushed.add (json); });

        (void) edit ("filter1_cutoff", 40, 0, 0.0, 1.0);
        processor.getMidiControl().flushPendingChanges();

        expectEquals (pushed.size(), 1);
        expectEquals (field (pushed[0], "status"), juce::String(), "an edit's push carries no outcome of its own");

        pushed.clear();
        (void) bridge.handleMessage (message ("midiLearnBegin", { { "id", "osc2_level" } }));
        expect (processor.getMidiControl().handleControllerMessage (1, 41, 64));
        processor.getMidiControl().flushPendingChanges();

        expect (pushed.size() >= 1);
        expectEquals (field (pushed[pushed.size() - 1], "status"), juce::String ("ADDED"), "a learn is news");

        bridge.setOutboundHandler ({});
    }
};

ProfileFileTests profileFileTests;

} // namespace
