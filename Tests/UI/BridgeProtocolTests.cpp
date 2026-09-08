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
                                 BridgeErrorCode::invalidGestureState })
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
