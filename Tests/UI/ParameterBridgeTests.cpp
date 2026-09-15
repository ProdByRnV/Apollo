/*
    Parameter bridge tests.

    Covers the synchronisation requirements of UI_BINDINGS.md §20: that the
    initial snapshot matches APVTS, that UI commands reach APVTS, that changes
    originating anywhere else propagate back to the UI, and that gestures map
    onto the host's automation boundaries.

    flushPendingUpdates() is driven directly rather than waiting on the timer, so
    these tests are deterministic and need no running message loop.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

#include "Audio/ApolloAudioProcessor.h"
#include "Parameters/ParameterDefinitions.h"
#include "Parameters/ParameterLayout.h"
#include "Resources/PresetLibrary.h"
#include "UI/ParameterBridge.h"

using namespace apollo;

namespace
{

/** Collects everything the bridge sends, standing in for the WebView. */
struct OutboundRecorder
{
    std::vector<juce::String> messages;

    [[nodiscard]] ui::ParameterBridge::OutboundHandler handler()
    {
        return [this] (const juce::String& message) { messages.push_back (message); };
    }

    [[nodiscard]] int countOfType (const juce::String& type) const
    {
        int count = 0;

        for (const auto& message : messages)
        {
            juce::var parsed;

            if (juce::JSON::parse (message, parsed).wasOk())
                if (auto* object = parsed.getDynamicObject())
                    if (object->getProperty ("type").toString() == type)
                        ++count;
        }

        return count;
    }

    [[nodiscard]] bool containsParameterChange (const juce::String& id) const
    {
        for (const auto& message : messages)
        {
            juce::var parsed;

            if (juce::JSON::parse (message, parsed).wasOk())
                if (auto* object = parsed.getDynamicObject())
                    if (object->getProperty ("type").toString() == "parameterChanged"
                        && object->getProperty ("id").toString() == id)
                        return true;
        }

        return false;
    }
};

class ParameterBridgeTests final : public juce::UnitTest
{
public:
    ParameterBridgeTests()
        : juce::UnitTest ("Parameter bridge", "UI")
    {
    }

    void runTest() override
    {
        testSnapshotMatchesApvts();
        testRequestStateReturnsSnapshot();
        testRequestMetadataReturnsMetadata();
        testSetParameterReachesApvts();
        testInvalidCommandsReturnErrorsAndChangeNothing();
        testExternalChangesPropagateToUi();
        testUpdatesAreCoalesced();
        testDetachedHandlerIsSafe();
        testMarkAllParametersDirty();
        testMetadataDescribesTheRegistry();
        testPresetCommandsNeedALibrary();
        testSaveThenLoadRoundTripsThroughTheBrowser();
        testSaveWillNotSilentlyReplaceAPreset();
        testAPresetNameCannotLeaveTheLibrary();
        testAStalePresetIdReachesNothing();
        testAHostReloadForgetsTheBrowsersPreset();
    }

private:
    void testSnapshotMatchesApvts()
    {
        beginTest ("The initial snapshot matches APVTS exactly");

        ApolloAudioProcessor processor;
        ui::ParameterBridge bridge (processor.getValueTreeState());

        juce::var parsed;
        expect (juce::JSON::parse (bridge.createStateSnapshot(), parsed).wasOk());

        auto* snapshot = parsed.getDynamicObject();
        expect (snapshot != nullptr);

        if (snapshot == nullptr)
            return;

        auto* parameters = snapshot->getProperty ("parameters").getDynamicObject();
        expect (parameters != nullptr, "the snapshot must carry a parameters object");

        if (parameters == nullptr)
            return;

        for (const auto& definition : params::parameterDefinitions)
        {
            const auto id = params::toJuceString (definition.id);

            expect (parameters->hasProperty (id), id + ": missing from the snapshot");

            const auto* parameter = processor.getValueTreeState().getParameter (id);
            expect (parameter != nullptr);

            if (parameter == nullptr)
                continue;

            expectWithinAbsoluteError (
                static_cast<float> (static_cast<double> (parameters->getProperty (id))),
                parameter->getValue(), 1.0e-5f,
                id + ": snapshot disagrees with APVTS");
        }
    }

    void testRequestStateReturnsSnapshot()
    {
        beginTest ("requestState is answered with a snapshot");

        ApolloAudioProcessor processor;
        ui::ParameterBridge bridge (processor.getValueTreeState());

        juce::var parsed;
        expect (juce::JSON::parse (bridge.handleMessage (R"({"type":"requestState","version":1})"),
                                   parsed).wasOk());
        expectEquals (parsed.getDynamicObject()->getProperty ("type").toString(),
                      juce::String ("stateSnapshot"));
    }

    void testRequestMetadataReturnsMetadata()
    {
        beginTest ("requestMetadata is answered with the registry description");

        ApolloAudioProcessor processor;
        ui::ParameterBridge bridge (processor.getValueTreeState());

        juce::var parsed;
        expect (juce::JSON::parse (bridge.handleMessage (R"({"type":"requestMetadata","version":1})"),
                                   parsed).wasOk());
        expectEquals (parsed.getDynamicObject()->getProperty ("type").toString(),
                      juce::String ("parameterMetadata"));
    }

    void testSetParameterReachesApvts()
    {
        beginTest ("A validated setParameter reaches APVTS");

        ApolloAudioProcessor processor;
        auto& apvts = processor.getValueTreeState();
        ui::ParameterBridge bridge (apvts);

        const auto reply = bridge.handleMessage (
            R"({"type":"setParameter","version":1,"id":"filter1_cutoff","normalizedValue":0.4})");

        expect (reply.isEmpty(), "a successful set needs no immediate reply");
        expectWithinAbsoluteError (apvts.getParameter ("filter1_cutoff")->getValue(), 0.4f, 1.0e-5f);
    }

    void testInvalidCommandsReturnErrorsAndChangeNothing()
    {
        beginTest ("Invalid commands produce errors and leave APVTS untouched");

        ApolloAudioProcessor processor;
        auto& apvts = processor.getValueTreeState();
        ui::ParameterBridge bridge (apvts);

        const auto before = apvts.getParameter ("filter1_cutoff")->getValue();

        for (const auto* json : {
                 R"({"type":"setParameter","version":1,"id":"filter1_cutoff","normalizedValue":7})",
                 R"({"type":"setParameter","version":2,"id":"filter1_cutoff","normalizedValue":0.1})",
                 R"({"type":"setParameter","version":1,"id":"nope_nope","normalizedValue":0.1})",
                 R"({"type":"evaluate","version":1})",
                 "garbage" })
        {
            juce::var parsed;
            expect (juce::JSON::parse (bridge.handleMessage (json), parsed).wasOk(),
                    juce::String ("error reply must be valid JSON for: ") + json);
            expectEquals (parsed.getDynamicObject()->getProperty ("type").toString(),
                          juce::String ("error"),
                          juce::String ("expected an error reply for: ") + json);
        }

        expectWithinAbsoluteError (apvts.getParameter ("filter1_cutoff")->getValue(), before, 1.0e-6f,
                                   "a rejected command must not change any parameter");
    }

    /** Host automation, preset recall and MIDI all change parameters without the
        UI knowing. Those changes must flow back out (UI_BINDINGS.md §9).
    */
    void testExternalChangesPropagateToUi()
    {
        beginTest ("Changes made outside the UI propagate back to the UI");

        ApolloAudioProcessor processor;
        auto& apvts = processor.getValueTreeState();
        ui::ParameterBridge bridge (apvts);

        OutboundRecorder recorder;
        bridge.setOutboundHandler (recorder.handler());

        // Simulates the host automating a parameter.
        apvts.getParameter ("master_gain")->setValueNotifyingHost (0.2f);

        bridge.flushPendingUpdates();

        expect (recorder.containsParameterChange ("master_gain"),
                "an externally changed parameter must be reported to the UI");
    }

    /** A swept parameter changes far faster than any display can show. The
        bridge must coalesce, not forward every intermediate value.
    */
    void testUpdatesAreCoalesced()
    {
        beginTest ("Rapid changes are coalesced into one update per flush");

        ApolloAudioProcessor processor;
        auto& apvts = processor.getValueTreeState();
        ui::ParameterBridge bridge (apvts);

        OutboundRecorder recorder;
        bridge.setOutboundHandler (recorder.handler());

        auto* parameter = apvts.getParameter ("filter1_cutoff");

        for (int i = 0; i < 200; ++i)
            parameter->setValueNotifyingHost (static_cast<float> (i) / 200.0f);

        bridge.flushPendingUpdates();

        expectEquals (recorder.countOfType ("parameterChanged"), 1,
                      "200 changes between flushes must coalesce into one message");

        // And the value reported is the latest, not the first.
        juce::var parsed;
        juce::JSON::parse (recorder.messages.front(), parsed);
        expectWithinAbsoluteError (
            static_cast<float> (static_cast<double> (
                parsed.getDynamicObject()->getProperty ("normalizedValue"))),
            parameter->getValue(), 1.0e-5f,
            "the coalesced update must carry the current value");

        // Nothing further is sent until something changes again.
        recorder.messages.clear();
        bridge.flushPendingUpdates();
        expectEquals (recorder.countOfType ("parameterChanged"), 0,
                      "a flush with no changes must send nothing");
    }

    /** The editor can close at any time; the bridge outlives it. */
    void testDetachedHandlerIsSafe()
    {
        beginTest ("Flushing with no handler attached is safe");

        ApolloAudioProcessor processor;
        auto& apvts = processor.getValueTreeState();
        ui::ParameterBridge bridge (apvts);

        OutboundRecorder recorder;
        bridge.setOutboundHandler (recorder.handler());
        bridge.setOutboundHandler ({});

        apvts.getParameter ("master_gain")->setValueNotifyingHost (0.4f);
        bridge.flushPendingUpdates();

        expect (recorder.messages.empty(), "a detached handler must receive nothing");

        // Reattaching must not replay stale changes as if they were new.
        bridge.setOutboundHandler (recorder.handler());
        bridge.flushPendingUpdates();
        expect (recorder.messages.empty(), "stale flags must not be replayed on reattach");
    }

    void testMarkAllParametersDirty()
    {
        beginTest ("Marking everything dirty reports every parameter once");

        ApolloAudioProcessor processor;
        ui::ParameterBridge bridge (processor.getValueTreeState());

        OutboundRecorder recorder;
        bridge.setOutboundHandler (recorder.handler());

        bridge.markAllParametersDirty();
        bridge.flushPendingUpdates();

        expectEquals (recorder.countOfType ("parameterChanged"),
                      static_cast<int> (params::parameterCount()),
                      "every parameter must be reported exactly once");
    }

    //==========================================================================
    // Presets (Phase 9c).
    //
    // These drive the bridge against a real library in a temporary folder,
    // because the interesting failures are all failures of the seam: an id that
    // no longer resolves, a save that would replace somebody's work, a name
    // that tries to leave the folder it was given. None of them can be seen in
    // the protocol layer alone, and none of them can be seen in the library
    // alone either.

    /** A preset library in a folder that is deleted afterwards. */
    struct TemporaryLibrary
    {
        juce::File root { juce::File::createTempFile ("ApolloPresetBridge") };
        resources::PresetLibrary library;

        TemporaryLibrary()
        {
            root.deleteFile();
            root.createDirectory();

            resources::PresetLocations locations;
            locations.userDirectory = root.getChildFile ("User");
            locations.factoryDirectory = root.getChildFile ("Factory");

            locations.userDirectory.createDirectory();

            library.setLocations (locations);
        }

        ~TemporaryLibrary()
        {
            library.waitForScan();
            root.deleteRecursively();
        }

        /** Scans and delivers, standing in for the message loop the test runner
            does not have. */
        void scan()
        {
            library.rescan();
            library.waitForScan();
            library.flushPendingNotification();
        }

        [[nodiscard]] juce::File userFile (const juce::String& presetName) const
        {
            return library.getLocations().userDirectory.getChildFile (presetName + ".rnv");
        }
    };

    /** @returns the value of one field of the last message of @p type. */
    static juce::String fieldOf (const juce::String& message, const juce::String& field)
    {
        juce::var parsed;

        if (! juce::JSON::parse (message, parsed).wasOk())
            return {};

        auto* object = parsed.getDynamicObject();

        return object != nullptr ? object->getProperty (field).toString() : juce::String();
    }

    void testPresetCommandsNeedALibrary()
    {
        beginTest ("Without a library the preset commands are refused, not ignored");

        ApolloAudioProcessor processor;
        ui::ParameterBridge bridge (processor.getValueTreeState());

        // A page whose browser cannot work must be told so. The failure mode
        // being guarded against is a list that silently never fills, which is
        // indistinguishable from an empty library (UI_BINDINGS.md §13).
        const auto reply = bridge.handleMessage (R"({"type":"requestPresets","version":1})");

        expectEquals (fieldOf (reply, "type"), juce::String ("error"));
    }

    void testSaveThenLoadRoundTripsThroughTheBrowser()
    {
        beginTest ("A sound saved through the bridge is listed, and loads back");

        TemporaryLibrary temporary;

        ApolloAudioProcessor processor;
        auto& apvts = processor.getValueTreeState();

        ui::ParameterBridge bridge (apvts);

        OutboundRecorder recorder;
        bridge.setOutboundHandler (recorder.handler());
        bridge.setPresetLibrary (&temporary.library);

        temporary.library.waitForScan();

        auto* cutoff = apvts.getParameter ("filter1_cutoff");
        expect (cutoff != nullptr);

        if (cutoff == nullptr)
            return;

        // A value to recognise the sound by on the way back.
        cutoff->setValueNotifyingHost (0.31f);

        const auto saved = bridge.handleMessage (
            R"({"type":"savePreset","version":1,"name":"Glass Bell","author":"RnV",)"
            R"("category":"Keys"})");

        expectEquals (fieldOf (saved, "status"), juce::String ("SAVED"));
        expect (temporary.userFile ("Glass Bell").existsAsFile(),
                "the save must have produced a file with the sanitised name");

        // Saving is also becoming: the instrument is now that preset, and the
        // name travels in the state tree so a host project reopens showing it.
        expectEquals (fieldOf (saved, "name"), juce::String ("Glass Bell"));

        temporary.scan();

        const auto listed = bridge.handleMessage (R"({"type":"requestPresets","version":1})");

        juce::var parsed;
        expect (juce::JSON::parse (listed, parsed).wasOk());

        auto* object = parsed.getDynamicObject();
        expect (object != nullptr);

        if (object == nullptr)
            return;

        const auto* entries = object->getProperty ("presets").getArray();
        expect (entries != nullptr, "the index must carry an array");

        // Found by name rather than by position: the index also carries the
        // compiled-in factory content, which sorts before anything the user has
        // saved (ADR-0065).
        juce::DynamicObject* savedEntry = nullptr;

        for (const auto& value : *entries)
            if (auto* fields = value.getDynamicObject())
                if (fields->getProperty ("name").toString() == "Glass Bell")
                    savedEntry = fields;

        expect (savedEntry != nullptr, "the saved preset must appear in the index");

        if (savedEntry == nullptr)
            return;

        expectEquals (savedEntry->getProperty ("author").toString(), juce::String ("RnV"));
        expect (! static_cast<bool> (savedEntry->getProperty ("factory")),
                "a preset in the user root is not factory content");

        const auto id = static_cast<int> (savedEntry->getProperty ("id"));
        expect (id > 0, "every listed preset must carry an id the page can ask for");

        // Move the sound somewhere else, then ask for the preset back.
        cutoff->setValueNotifyingHost (0.88f);

        const auto loaded = bridge.handleMessage (
            juce::String (R"({"type":"loadPreset","version":1,"preset":)") + juce::String (id)
                + "}");

        expectEquals (fieldOf (loaded, "status"), juce::String ("LOADED"));
        expectWithinAbsoluteError (cutoff->getValue(), 0.31f, 1.0e-4f,
                                   "loading the preset must restore the sound it saved");

        expectEquals (fieldOf (loaded, "loaded"), juce::String (id),
                      "the browser is told which row is now playing");

        bridge.setPresetLibrary (nullptr);
    }

    void testSaveWillNotSilentlyReplaceAPreset()
    {
        beginTest ("A save over an existing preset is refused until it is asked for");

        TemporaryLibrary temporary;

        ApolloAudioProcessor processor;
        auto& apvts = processor.getValueTreeState();

        ui::ParameterBridge bridge (apvts);
        bridge.setPresetLibrary (&temporary.library);
        temporary.library.waitForScan();

        auto* cutoff = apvts.getParameter ("filter1_cutoff");
        expect (cutoff != nullptr);

        if (cutoff == nullptr)
            return;

        cutoff->setValueNotifyingHost (0.2f);

        expectEquals (fieldOf (bridge.handleMessage (
                          R"({"type":"savePreset","version":1,"name":"Bell"})"), "status"),
                      juce::String ("SAVED"));

        const auto original = temporary.userFile ("Bell").loadFileAsString();

        // The second save names the same preset with a different sound behind
        // it. It must not happen, and the file must be untouched afterwards —
        // "refused" and "refused but wrote anyway" look identical from the
        // status line alone.
        cutoff->setValueNotifyingHost (0.9f);

        const auto refused = bridge.handleMessage (
            R"({"type":"savePreset","version":1,"name":"Bell"})");

        expectEquals (fieldOf (refused, "status"), juce::String ("ALREADY_EXISTS"));
        expectEquals (temporary.userFile ("Bell").loadFileAsString(), original,
                      "a refused save must leave the preset that was there alone");

        // Asked for in as many words, it goes ahead.
        const auto replaced = bridge.handleMessage (
            R"({"type":"savePreset","version":1,"name":"Bell","overwrite":true})");

        expectEquals (fieldOf (replaced, "status"), juce::String ("SAVED"));
        expect (temporary.userFile ("Bell").loadFileAsString() != original,
                "an authorised replacement must actually replace it");

        bridge.setPresetLibrary (nullptr);
    }

    void testAPresetNameCannotLeaveTheLibrary()
    {
        beginTest ("A save cannot be talked into writing outside the user library");

        TemporaryLibrary temporary;

        ApolloAudioProcessor processor;
        ui::ParameterBridge bridge (processor.getValueTreeState());

        bridge.setPresetLibrary (&temporary.library);
        temporary.library.waitForScan();

        // Every one of these is a name a user could type and a name an attacker
        // would try. None may produce a file outside the user root — and the
        // ones that survive sanitising must land inside it, not be quietly
        // dropped.
        for (const auto* hostile : { R"(..\\..\\evil)",
                                     "../../evil",
                                     "C:/Windows/evil",
                                     "/etc/evil",
                                     "sub/dir/evil",
                                     "CON",
                                     "..",
                                     "." })
        {
            auto* object = new juce::DynamicObject();
            object->setProperty ("type", "savePreset");
            object->setProperty ("version", 1);
            object->setProperty ("name", hostile);
            object->setProperty ("overwrite", true);

            const auto reply = bridge.handleMessage (
                juce::JSON::toString (juce::var (object)));

            const auto status = fieldOf (reply, "status");
            const auto type = fieldOf (reply, "type");

            expect (type == "error" || status == "SAVED" || status == "SAVE_FAILED",
                    juce::String ("unexpected outcome for ") + hostile);
        }

        // The whole test in one line: whatever those names did, nothing was
        // written anywhere but inside the user folder.
        const auto outside = temporary.root.getNumberOfChildFiles (
            juce::File::findFilesAndDirectories);

        expectEquals (outside, 1, "nothing may be created beside the user folder");

        for (const auto& item : juce::RangedDirectoryIterator (
                 temporary.root, /* recursive */ true, "*",
                 juce::File::findFilesAndDirectories))
            expect (item.getFile().isAChildOf (temporary.root),
                    "every file created must be inside the library");

        bridge.setPresetLibrary (nullptr);
    }

    void testAStalePresetIdReachesNothing()
    {
        beginTest ("An id the index does not contain reaches no file at all");

        TemporaryLibrary temporary;

        ApolloAudioProcessor processor;
        ui::ParameterBridge bridge (processor.getValueTreeState());

        bridge.setPresetLibrary (&temporary.library);
        temporary.library.waitForScan();

        // Past the end of an index that holds only the factory content, which
        // is what a page holding a list from before a preset was deleted would
        // send. The id is still inside the range the protocol accepts, so this
        // is the library refusing it rather than the parser.
        const auto reply = bridge.handleMessage (
            R"({"type":"loadPreset","version":1,"preset":9000})");

        expectEquals (fieldOf (reply, "type"), juce::String ("error"));
        expectEquals (fieldOf (reply, "code"), juce::String ("UNKNOWN_PRESET"));

        bridge.setPresetLibrary (nullptr);
    }

    void testAHostReloadForgetsTheBrowsersPreset()
    {
        beginTest ("A project load stops the browser claiming the old preset is playing");

        TemporaryLibrary temporary;

        ApolloAudioProcessor processor;
        ui::ParameterBridge bridge (processor.getValueTreeState());

        bridge.setPresetLibrary (&temporary.library);
        temporary.library.waitForScan();

        expectEquals (fieldOf (bridge.handleMessage (
                          R"({"type":"savePreset","version":1,"name":"Bell"})"), "status"),
                      juce::String ("SAVED"));

        temporary.scan();

        const auto afterSave = bridge.createPresetStatus ("CURRENT", {});
        expect (fieldOf (afterSave, "loaded") != "0",
                "the saved preset is the sound in front of the user");

        // A reload the bridge did not cause came from the host: the sound is now
        // the project's, whatever the browser last loaded.
        const auto afterReload = bridge.handleStateReload();

        expectEquals (fieldOf (afterReload, "loaded"), juce::String ("0"),
                      "a host project load must clear the browser highlight");

        bridge.setPresetLibrary (nullptr);
    }

    void testMetadataDescribesTheRegistry()
    {
        beginTest ("Parameter metadata describes every registered parameter");

        ApolloAudioProcessor processor;
        ui::ParameterBridge bridge (processor.getValueTreeState());

        juce::var parsed;
        expect (juce::JSON::parse (bridge.createParameterMetadata(), parsed).wasOk());

        auto* object = parsed.getDynamicObject();
        expect (object != nullptr);

        if (object == nullptr)
            return;

        const auto* entries = object->getProperty ("parameters").getArray();
        expect (entries != nullptr, "metadata must carry an array of parameters");

        if (entries == nullptr)
            return;

        expectEquals (entries->size(), static_cast<int> (params::parameterCount()));

        // Each entry must carry everything a generic control needs, so the
        // frontend never hard-codes a range (UI_BINDINGS.md §16).
        for (const auto& entry : *entries)
        {
            auto* fields = entry.getDynamicObject();
            expect (fields != nullptr);

            if (fields == nullptr)
                continue;

            for (const auto* required : { "id", "name", "type", "unit", "min", "max",
                                          "default", "skew", "step", "automatable",
                                          "modulatable", "smoothed" })
                expect (fields->hasProperty (required),
                        juce::String ("metadata entry is missing '") + required + "'");
        }
    }
};

ParameterBridgeTests parameterBridgeTests;

} // namespace
