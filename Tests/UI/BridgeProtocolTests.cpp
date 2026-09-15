/*
    UI bridge protocol tests.

    The WebView is an untrusted input boundary (UI_BINDINGS.md §14), so this is
    the security surface of the whole UI layer. The tests are written from the
    attacker's side as much as the frontend's: malformed JSON, wrong types,
    hostile numbers, oversized payloads, unknown parameters, and message shapes
    that are valid JSON but not valid commands.

    The protocol layer has no WebView and no APVTS dependency, which is what
    makes testing like this possible at all.
*/

#include <juce_core/juce_core.h>

#include "UI/BridgeProtocol.h"

using namespace apollo;
using namespace apollo::ui;

namespace
{

class BridgeProtocolTests final : public juce::UnitTest
{
public:
    BridgeProtocolTests()
        : juce::UnitTest ("UI bridge protocol", "UI")
    {
    }

    void runTest() override
    {
        testValidRequests();
        testValidSetParameter();
        testValidGestures();
        testMalformedJson();
        testNonObjectPayloads();
        testProtocolVersioning();
        testUnknownMessageType();
        testFullscreenIsAccepted();
        testPresetRequestsAreAccepted();
        testLoadPresetNamesANumberAndNothingElse();
        testSavePresetValidatesItsMetadata();
        testPresetMetadataIsBounded();
        testPresetMessagesCarryNoPaths();
        testPresetStatusBoundsWhatItRepeats();
        testParameterIdValidation();
        testNumericValidation();
        testGestureStateValidation();
        testMessageSizeLimit();
        testOutboundMessagesAreWellFormed();
        testErrorsDoNotLeakDetail();
    }

private:
    static juce::String reject (const juce::String& json)
    {
        return "should have been rejected: " + json;
    }

    void testValidRequests()
    {
        beginTest ("Well-formed requests are accepted");

        const auto state = parseMessage (R"({"type":"requestState","version":1})");
        expect (state.ok, "valid requestState must be accepted");
        expect (state.command.type == BridgeCommandType::requestState);

        const auto metadata = parseMessage (R"({"type":"requestMetadata","version":1})");
        expect (metadata.ok, "valid requestMetadata must be accepted");
        expect (metadata.command.type == BridgeCommandType::requestMetadata);
    }

    void testValidSetParameter()
    {
        beginTest ("A well-formed setParameter is accepted");

        const auto result = parseMessage (
            R"({"type":"setParameter","version":1,"id":"filter1_cutoff","normalizedValue":0.73})");

        expect (result.ok, "valid setParameter must be accepted");
        expect (result.command.type == BridgeCommandType::setParameter);
        expectEquals (result.command.parameterId, juce::String ("filter1_cutoff"));
        expectWithinAbsoluteError (result.command.normalisedValue, 0.73f, 1.0e-6f);

        // The range boundaries are legal values, not edge cases to reject.
        for (const auto* json : { R"({"type":"setParameter","version":1,"id":"master_gain","normalizedValue":0})",
                                  R"({"type":"setParameter","version":1,"id":"master_gain","normalizedValue":1})" })
            expect (parseMessage (json).ok, juce::String ("boundary value must be accepted: ") + json);
    }

    void testValidGestures()
    {
        beginTest ("Gesture begin and end are accepted");

        const auto begin = parseMessage (
            R"({"type":"gesture","version":1,"id":"filter1_cutoff","state":"begin"})");
        expect (begin.ok);
        expect (begin.command.type == BridgeCommandType::gestureBegin);

        const auto end = parseMessage (
            R"({"type":"gesture","version":1,"id":"filter1_cutoff","state":"end"})");
        expect (end.ok);
        expect (end.command.type == BridgeCommandType::gestureEnd);
    }

    void testMalformedJson()
    {
        beginTest ("Malformed JSON is rejected");

        for (const auto* json : { "", "{", "}", "not json at all",
                                  R"({"type":)", R"({"type":"setParameter",})" })
            expect (! parseMessage (json).ok, reject (json));
    }

    /** Valid JSON that is not a command object: arrays, bare scalars, null. */
    void testNonObjectPayloads()
    {
        beginTest ("Valid JSON that is not a command object is rejected");

        for (const auto* json : { "[]",
                                  R"([{"type":"requestState","version":1}])",
                                  "42",
                                  R"("requestState")",
                                  "null",
                                  "true" })
        {
            const auto result = parseMessage (json);
            expect (! result.ok, reject (json));
            expect (result.error == BridgeErrorCode::malformedMessage,
                    juce::String ("wrong error for: ") + json);
        }
    }

    void testProtocolVersioning()
    {
        beginTest ("Unsupported protocol versions are rejected, never reinterpreted");

        // A newer schema must never be read as if it were the current one.
        for (const auto* json : { R"({"type":"requestState","version":2})",
                                  R"({"type":"requestState","version":0})",
                                  R"({"type":"requestState","version":-1})",
                                  R"({"type":"requestState"})" })
        {
            const auto result = parseMessage (json);
            expect (! result.ok, reject (json));
            expect (result.error == BridgeErrorCode::unsupportedProtocolVersion,
                    juce::String ("wrong error for: ") + json);
        }

        // A version of the wrong type is malformed rather than unsupported.
        expect (parseMessage (R"({"type":"requestState","version":"1"})").error
                    == BridgeErrorCode::malformedMessage);
    }

    void testUnknownMessageType()
    {
        beginTest ("Unknown message types are rejected");

        for (const auto* json : { R"({"type":"loadFile","version":1})",
                                  R"({"type":"eval","version":1})",
                                  R"({"type":"","version":1})",
                                  R"({"type":"SETPARAMETER","version":1})" })
        {
            const auto result = parseMessage (json);
            expect (! result.ok, reject (json));
            expect (result.error == BridgeErrorCode::unknownMessageType,
                    juce::String ("wrong error for: ") + json);
        }

        expect (parseMessage (R"({"type":7,"version":1})").error
                    == BridgeErrorCode::malformedMessage);
    }

    void testFullscreenIsAccepted()
    {
        beginTest ("A fullscreen request is accepted and carries nothing else");

        // The one command that asks about the window rather than the
        // instrument. It has no payload at all, which is the point: the page
        // cannot be allowed to name a size, only to ask for the display's.
        const auto result = parseMessage (R"({"type":"toggleFullscreen","version":1})");

        expect (result.ok, "a well-formed fullscreen request must be accepted");
        expect (result.command.type == BridgeCommandType::toggleFullscreen);
        expect (result.command.parameterId.isEmpty(),
                "it must not carry a parameter");
        expect (result.command.profileId.isEmpty(),
                "nor a profile");

        // And it obeys the same version rule everything else does, rather than
        // being a back door that skips it.
        expect (! parseMessage (R"({"type":"toggleFullscreen","version":99})").ok,
                "an unsupported protocol version must be refused here too");
    }

    void testParameterIdValidation()
    {
        beginTest ("Parameter IDs are validated against the registry");

        // Unknown, but conventionally shaped.
        expect (parseMessage (
                    R"({"type":"setParameter","version":1,"id":"unknown_parameter","normalizedValue":0.5})")
                        .error == BridgeErrorCode::unknownParameter);

        // Malformed IDs are rejected before any registry lookup happens.
        for (const auto* id : { "", "Filter_Cutoff", "filter__cutoff", "filter cutoff", "cutoff" })
        {
            const auto json = juce::String (R"({"type":"setParameter","version":1,"id":")")
                            + id + R"(","normalizedValue":0.5})";
            const auto result = parseMessage (json);
            expect (! result.ok, reject (json));
            expect (result.error == BridgeErrorCode::unknownParameter,
                    juce::String ("wrong error for id: ") + id);
        }

        // An oversized ID is bounded before it is used as a lookup key.
        const auto huge = juce::String ("a_") + juce::String::repeatedString ("x", 5000);
        expect (! parseMessage (juce::String (R"({"type":"setParameter","version":1,"id":")")
                                + huge + R"(","normalizedValue":0.5})").ok,
                "an oversized id must be rejected");

        // A missing or wrongly typed id is malformed.
        expect (parseMessage (R"({"type":"setParameter","version":1,"normalizedValue":0.5})").error
                    == BridgeErrorCode::malformedMessage);
        expect (parseMessage (R"({"type":"setParameter","version":1,"id":5,"normalizedValue":0.5})").error
                    == BridgeErrorCode::malformedMessage);
    }

    /** NaN and infinity must never reach the parameter system: they propagate
        through the DSP and silence or destroy the output.
    */
    void testNumericValidation()
    {
        beginTest ("Hostile numeric values are rejected");

        const auto attempt = [this] (const juce::String& value)
        {
            const auto json = juce::String (R"({"type":"setParameter","version":1,)")
                            + R"("id":"filter1_cutoff","normalizedValue":)" + value + "}";
            const auto result = parseMessage (json);
            expect (! result.ok, reject (json));
            expect (result.error == BridgeErrorCode::invalidParameterValue,
                    "wrong error for value: " + value);
        };

        attempt ("1.5");
        attempt ("-0.5");
        attempt ("1000000");
        attempt ("-1e30");
        attempt ("\"0.5\"");   // a numeric-looking string is not a number
        attempt ("null");
        attempt ("true");

        // A missing value is an invalid value, not a malformed message.
        expect (parseMessage (R"({"type":"setParameter","version":1,"id":"filter1_cutoff"})").error
                    == BridgeErrorCode::invalidParameterValue);
    }

    void testGestureStateValidation()
    {
        beginTest ("Gesture states are validated");

        for (const auto* gestureState : { "start", "finish", "BEGIN", "" })
        {
            const auto json = juce::String (R"({"type":"gesture","version":1,"id":"filter1_cutoff","state":")")
                            + gestureState + R"("})";
            const auto result = parseMessage (json);
            expect (! result.ok, reject (json));
            expect (result.error == BridgeErrorCode::invalidGestureState,
                    juce::String ("wrong error for gesture state: ") + gestureState);
        }

        expect (parseMessage (R"({"type":"gesture","version":1,"id":"filter1_cutoff"})").error
                    == BridgeErrorCode::invalidGestureState);
        expect (parseMessage (R"({"type":"gesture","version":1,"id":"filter1_cutoff","state":3})").error
                    == BridgeErrorCode::invalidGestureState);
    }

    void testMessageSizeLimit()
    {
        beginTest ("Oversized messages are rejected before parsing");

        const auto padding = juce::String::repeatedString ("x", maxMessageBytes + 1000);
        const auto result = parseMessage (
            juce::String (R"({"type":"requestState","version":1,"pad":")") + padding + R"("})");

        expect (! result.ok, "an oversized message must be rejected");
        expect (result.error == BridgeErrorCode::messageTooLarge);

        // A message under the limit is parsed normally, extra properties and all.
        expect (parseMessage (juce::String (R"({"type":"requestState","version":1,"pad":")")
                              + juce::String::repeatedString ("x", 100) + R"("})").ok,
                "unknown extra properties must be tolerated");
    }

    /** The preset commands, which are the only ones that reach a filesystem.

        Everything here is about what the page may *express*. It may name a
        number the backend published and a name a user typed; it may not name a
        path, a root, or anything that resolves to one. The check that a valid
        id still cannot escape the library lives with the library itself; this
        checks that nothing else gets past the parser at all.
    */
    void testPresetRequestsAreAccepted()
    {
        beginTest ("The preset requests that carry nothing are accepted");

        const auto list = parseMessage (R"({"type":"requestPresets","version":1})");
        expect (list.ok, "valid requestPresets must be accepted");
        expect (list.command.type == BridgeCommandType::requestPresets);

        const auto rescan = parseMessage (R"({"type":"rescanPresets","version":1})");
        expect (rescan.ok, "valid rescanPresets must be accepted");
        expect (rescan.command.type == BridgeCommandType::rescanPresets);

        // The version rule applies here as everywhere else.
        expect (! parseMessage (R"({"type":"requestPresets","version":2})").ok);
        expect (! parseMessage (R"({"type":"rescanPresets","version":2})").ok);
    }

    void testLoadPresetNamesANumberAndNothingElse()
    {
        beginTest ("A preset is loaded by id, and only by an id that could exist");

        const auto result = parseMessage (R"({"type":"loadPreset","version":1,"preset":7})");

        expect (result.ok, "a well-formed loadPreset must be accepted");
        expect (result.command.type == BridgeCommandType::loadPreset);
        expectEquals (result.command.presetId, 7);

        // A path is not a preset id, and nothing about the message shape lets
        // one be smuggled in as one.
        expect (! parseMessage (
                    R"({"type":"loadPreset","version":1,"preset":"C:/Windows/system.ini"})").ok,
                reject ("a string where the id belongs"));

        expect (! parseMessage (R"({"type":"loadPreset","version":1})").ok,
                reject ("loadPreset with no id at all"));

        // Zero is "no preset" by construction, and a negative id is not an id.
        expect (! parseMessage (R"({"type":"loadPreset","version":1,"preset":0})").ok,
                reject ("preset 0"));
        expect (! parseMessage (R"({"type":"loadPreset","version":1,"preset":-1})").ok,
                reject ("a negative id"));

        // JSON has one numeric type, so "1e30" is a well-formed way of saying
        // something that is not an index. Refused rather than narrowed, which
        // would be implementation-defined.
        expect (! parseMessage (R"({"type":"loadPreset","version":1,"preset":1e30})").ok,
                reject ("an id beyond any library"));

        expect (! parseMessage (
                    R"({"type":"loadPreset","version":1,"preset":99999999})").ok,
                reject ("an id past the scan bound"));
    }

    void testSavePresetValidatesItsMetadata()
    {
        beginTest ("A save carries a usable name and bounded metadata");

        const auto result = parseMessage (
            R"({"type":"savePreset","version":1,"name":"Glass Bell","author":"RnV",)"
            R"("category":"Keys","comment":"soft","bank":"Mine","overwrite":true})");

        expect (result.ok, "a well-formed savePreset must be accepted");
        expect (result.command.type == BridgeCommandType::savePreset);
        expectEquals (result.command.presetMetadata.name, juce::String ("Glass Bell"));
        expectEquals (result.command.presetMetadata.author, juce::String ("RnV"));
        expectEquals (result.command.presetMetadata.category, juce::String ("Keys"));
        expectEquals (result.command.presetMetadata.comment, juce::String ("soft"));
        expectEquals (result.command.presetBank, juce::String ("Mine"));
        expect (result.command.overwriteExisting);

        // Everything but the name is optional: a preset with no author and no
        // category is still a sound.
        const auto bare = parseMessage (R"({"type":"savePreset","version":1,"name":"Bare"})");

        expect (bare.ok, "a save with only a name must be accepted");
        expect (bare.command.presetMetadata.author.isEmpty());
        expect (bare.command.presetBank.isEmpty());

        // ABSENT MEANS NO. A save that would destroy what is already there has
        // to say so, and a message that says nothing must not be read as
        // permission.
        expect (! bare.command.overwriteExisting,
                "an absent overwrite flag must default to refusing");

        expect (! parseMessage (R"({"type":"savePreset","version":1})").ok,
                reject ("a save with no name"));

        expect (! parseMessage (R"({"type":"savePreset","version":1,"name":"   "})").ok,
                reject ("a name that is nothing but spaces"));

        expect (! parseMessage (R"({"type":"savePreset","version":1,"name":""})").ok,
                reject ("an empty name"));

        expect (! parseMessage (R"({"type":"savePreset","version":1,"name":42})").ok,
                reject ("a number where the name belongs"));

        expect (! parseMessage (
                    R"({"type":"savePreset","version":1,"name":"A","overwrite":"yes"})").ok,
                reject ("a string where the overwrite flag belongs"));
    }

    void testPresetMetadataIsBounded()
    {
        beginTest ("Oversized preset metadata is refused rather than truncated");

        const auto withName = [] (const juce::String& presetName)
        {
            auto* object = new juce::DynamicObject();
            object->setProperty ("type", "savePreset");
            object->setProperty ("version", 1);
            object->setProperty ("name", presetName);

            return juce::JSON::toString (juce::var (object));
        };

        // At the bound, not over it: the longest legal name must still work,
        // or the bound is one character tighter than it claims to be.
        const auto atLimit = juce::String::repeatedString ("n", presets::maximumNameLength);
        expect (parseMessage (withName (atLimit)).ok,
                "a name of exactly the maximum length must be accepted");

        const auto overLimit = juce::String::repeatedString ("n", presets::maximumNameLength + 1);
        const auto refused = parseMessage (withName (overLimit));

        expect (! refused.ok, reject ("a name one character too long"));
        expect (refused.error == BridgeErrorCode::invalidPresetName,
                "an oversized name is a name problem, not a parse failure");

        // Every field is bounded, not just the name, and a comment has its own
        // larger allowance because a comment is a sentence rather than a label.
        auto* object = new juce::DynamicObject();
        object->setProperty ("type", "savePreset");
        object->setProperty ("version", 1);
        object->setProperty ("name", "Fine");
        object->setProperty ("comment",
                             juce::String::repeatedString ("c", presets::maximumCommentLength + 1));

        expect (! parseMessage (juce::JSON::toString (juce::var (object))).ok,
                reject ("an oversized comment"));

        // And the whole-message bound still applies on top, so a hostile
        // payload cannot force unbounded parsing before any of this runs.
        expect (! parseMessage (withName (juce::String::repeatedString ("n", maxMessageBytes))).ok,
                reject ("a message past the size limit"));
    }

    void testPresetMessagesCarryNoPaths()
    {
        beginTest ("Nothing the backend sends about presets contains a path");

        resources::PresetIndex index;

        resources::PresetEntry entry;
        entry.id = 1;
        entry.file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("Secret Folder")
                         .getChildFile ("Bell.rnv");
        entry.name = "Bell";
        entry.author = "RnV";
        entry.category = "Keys";
        entry.bank = "Mine";
        entry.factory = false;

        index.entries.push_back (entry);
        index.unreadable = 3;

        const auto message = makePresetIndexMessage (index, /* scanning */ false);

        expect (message.contains ("Bell"), "the name is what the browser draws");
        expect (! message.contains ("Secret Folder"),
                "a path must never reach the page (UI_BINDINGS.md §13)");
        expect (! message.contains (".rnv"),
                "not even the filename: the page asks by id");

        juce::var parsed;
        expect (juce::JSON::parse (message, parsed).wasOk());

        auto* object = parsed.getDynamicObject();
        expect (object != nullptr);

        if (object == nullptr)
            return;

        expectEquals (static_cast<int> (object->getProperty ("unreadable")), 3,
                      "what could not be read travels with the list, not to a log");

        const auto* entries = object->getProperty ("presets").getArray();
        expect (entries != nullptr);

        if (entries == nullptr)
            return;

        expectEquals (entries->size(), 1);

        auto* fields = entries->getFirst().getDynamicObject();
        expect (fields != nullptr);

        if (fields == nullptr)
            return;

        for (const auto* required : { "id", "name", "author", "category", "bank", "factory" })
            expect (fields->hasProperty (required),
                    juce::String ("a preset entry is missing '") + required + "'");

        expect (! fields->hasProperty ("file") && ! fields->hasProperty ("path"),
                "an entry must not carry a location of any kind");
    }

    void testPresetStatusBoundsWhatItRepeats()
    {
        beginTest ("A preset status bounds metadata that came off a disk");

        // The name here is what a hostile or corrupt `.rnv` could contain. It
        // is bounded on the way *out* as well as on the way in, because a bound
        // applied only on the inbound path is not a bound on the path that
        // reaches the interface.
        presets::Metadata metadata;
        metadata.name = juce::String::repeatedString ("x", presets::maximumNameLength * 4);

        const auto message = makePresetStatusMessage ("LOADED", "Loaded it.", metadata, 4);

        juce::var parsed;
        expect (juce::JSON::parse (message, parsed).wasOk());

        auto* object = parsed.getDynamicObject();
        expect (object != nullptr);

        if (object == nullptr)
            return;

        expectEquals (object->getProperty ("name").toString().length(),
                      presets::maximumNameLength,
                      "an oversized name must be cut to the bound before it is sent");

        expectEquals (static_cast<int> (object->getProperty ("loaded")), 4);
        expectEquals (object->getProperty ("status").toString(), juce::String ("LOADED"));
    }

    void testOutboundMessagesAreWellFormed()
    {
        beginTest ("Outbound messages are valid JSON carrying the protocol version");

        const auto check = [this] (const juce::String& message, const juce::String& expectedType)
        {
            juce::var parsed;
            expect (juce::JSON::parse (message, parsed).wasOk(),
                    expectedType + ": outbound message must be valid JSON");

            auto* object = parsed.getDynamicObject();
            expect (object != nullptr, expectedType + ": must be a JSON object");

            if (object == nullptr)
                return;

            expectEquals (object->getProperty ("type").toString(), expectedType);
            expectEquals (static_cast<int> (object->getProperty ("version")), protocolVersion);
        };

        check (makeErrorMessage (BridgeErrorCode::unknownParameter), "error");
        check (makeParameterChangedMessage ("filter1_cutoff", 0.5f), "parameterChanged");
        check (makeStateSnapshotMessage ({ { "filter1_cutoff", 1.0f }, { "master_gain", 0.9f } }),
               "stateSnapshot");
        check (makeParameterMetadataMessage(), "parameterMetadata");

        // Error messages carry the stable machine-readable token.
        juce::var parsedError;
        juce::JSON::parse (makeErrorMessage (BridgeErrorCode::invalidParameterValue), parsedError);
        expectEquals (parsedError.getDynamicObject()->getProperty ("code").toString(),
                      juce::String ("INVALID_PARAMETER_VALUE"));
    }

    /** UI_BINDINGS.md §13: errors must not expose internal detail. */
    void testErrorsDoNotLeakDetail()
    {
        beginTest ("Error text carries no internal detail");

        for (const auto code : { BridgeErrorCode::messageTooLarge,
                                 BridgeErrorCode::malformedMessage,
                                 BridgeErrorCode::unknownMessageType,
                                 BridgeErrorCode::unsupportedProtocolVersion,
                                 BridgeErrorCode::unknownParameter,
                                 BridgeErrorCode::invalidParameterValue,
                                 BridgeErrorCode::invalidGestureState,
                                 BridgeErrorCode::unknownPreset,
                                 BridgeErrorCode::invalidPresetName })
        {
            const auto text = describe (code);

            expect (text.isNotEmpty(), "every error needs a description");
            expect (! text.containsChar ('\\') && ! text.contains ("/"),
                    "error text must not contain anything path-like");
            expect (! text.contains ("0x"), "error text must not contain addresses");
            expect (toToken (code).isNotEmpty(), "every error needs a wire token");
        }
    }
};

BridgeProtocolTests bridgeProtocolTests;

} // namespace
