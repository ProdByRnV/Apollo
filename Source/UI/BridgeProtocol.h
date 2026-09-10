#pragma once

/*
    The Apollo UI bridge protocol.

    The WebView is an untrusted input boundary (UI_BINDINGS.md §14). Everything
    arriving from it is parsed and validated here, before it can reach the
    parameter system, and this layer deliberately knows nothing about APVTS, the
    processor, or any WebView implementation.

    That independence is the point: the whole validation surface — malformed
    JSON, wrong protocol version, unknown parameters, hostile numeric values,
    oversized payloads — can be tested exhaustively without a browser, a plugin
    host or an audio device.

    This layer never executes anything. It converts text into one of a small,
    fixed set of commands, or into an error. There is no path from a bridge
    message to arbitrary native behaviour (UI_BINDINGS.md §22).
*/

#include <juce_core/juce_core.h>

#include "MIDI/ControllerProfile.h"
#include "MIDI/MidiMapping.h"

#include <utility>
#include <vector>

namespace apollo::ui
{

/** Bridge protocol version.

    Incremented for any incompatible change to message shape or semantics.
    Independent of both the product version and the state schema version
    (Docs/VERSIONING.md §4).
*/
inline constexpr int protocolVersion = 1;

/** Maximum accepted inbound message size, in bytes.

    UI_BINDINGS.md §14 requires array, object and string sizes to be bounded. A
    whole-message bound is the cheapest of those checks and is applied before the
    JSON parser sees the text, so a hostile payload cannot force unbounded
    parsing work.
*/
inline constexpr int maxMessageBytes = 8192;

//==============================================================================

/** Why an inbound message was rejected. */
enum class BridgeErrorCode
{
    none,
    messageTooLarge,
    malformedMessage,
    unknownMessageType,
    unsupportedProtocolVersion,
    unknownParameter,
    invalidParameterValue,
    invalidGestureState
};

/** @returns the stable wire token, e.g. "UNKNOWN_PARAMETER".

    Part of the protocol contract: the frontend may branch on these.
*/
[[nodiscard]] juce::String toToken (BridgeErrorCode code);

/** @returns a human-readable description, safe to display.

    Never contains the offending message, a parameter value, a filesystem path,
    a memory address or any internal detail (UI_BINDINGS.md §13).
*/
[[nodiscard]] juce::String describe (BridgeErrorCode code);

//==============================================================================

/** The commands the frontend is permitted to express. */
enum class BridgeCommandType
{
    none,
    requestState,   ///< Send the authoritative snapshot.
    requestMetadata,///< Send the parameter registry description.
    setParameter,   ///< Set one parameter from a normalised value.
    gestureBegin,   ///< Start of a user gesture, for host automation and undo.
    gestureEnd,     ///< End of a user gesture.

    // MIDI Learn (Phase 6). Each one names an intent, not an operation on the
    // mapping table: the frontend cannot construct a mapping, only ask for one
    // to be learned from whatever the user physically moves, or removed.
    requestMidiMappings, ///< Send the current mappings and learn state.
    midiLearnBegin,      ///< Arm learn for one parameter.
    midiLearnCancel,     ///< Disarm learn without assigning anything.
    midiMappingRemove,   ///< Release the control driving one parameter.
    midiMappingClearAll, ///< Release every control.

    // Controller profiles (Phase 6c).
    requestControllerProfiles, ///< Send the list of built-in profiles.
    applyControllerProfile     ///< Fill the mapping table from one.
};

/** A validated command.

    Reaching this type means the message was well-formed, the protocol version
    matched, the parameter (if any) exists in the registry, and the value (if
    any) is finite and within range.
*/
struct BridgeCommand
{
    BridgeCommandType type = BridgeCommandType::none;
    juce::String parameterId;
    float normalisedValue = 0.0f;

    /** Controller profile identifier, for applyControllerProfile. Validated
        against the built-in registry before the command is accepted, so it is
        never an arbitrary string by the time anything acts on it.
    */
    juce::String profileId;

    /** True when an applied profile should replace the existing mappings
        rather than merge into them.
    */
    bool replaceExisting = true;
};

/** The outcome of parsing one inbound message. */
struct BridgeParseResult
{
    bool ok = false;
    BridgeCommand command {};
    BridgeErrorCode error = BridgeErrorCode::none;

    [[nodiscard]] static BridgeParseResult success (BridgeCommand command);
    [[nodiscard]] static BridgeParseResult failure (BridgeErrorCode code);
};

/** Parses and fully validates one inbound message.

    Rejects rather than repairs: a value outside [0, 1] is a frontend defect, and
    silently clamping it would hide the defect while leaving the UI and the
    engine disagreeing about what was set. Clamping to the parameter's own range
    still happens downstream, where the definition is authoritative.
*/
[[nodiscard]] BridgeParseResult parseMessage (const juce::String& json);

//==============================================================================
// Outbound messages, built through JUCE's JSON writer so escaping is handled
// correctly rather than by string concatenation.

/** `{"type":"error", ...}` */
[[nodiscard]] juce::String makeErrorMessage (BridgeErrorCode code);

/** `{"type":"parameterChanged", ...}` */
[[nodiscard]] juce::String makeParameterChangedMessage (const juce::String& parameterId,
                                                        float normalisedValue);

/** `{"type":"stateSnapshot", ...}`

    @param normalisedValues  every registered parameter ID mapped to its current
                             normalised value, in registry order.
*/
[[nodiscard]] juce::String makeStateSnapshotMessage (
    const std::vector<std::pair<juce::String, float>>& normalisedValues);

/** `{"type":"parameterMetadata", ...}`

    The registry described for the frontend, so controls consume metadata rather
    than hard-coding ranges (UI_BINDINGS.md §16).
*/
[[nodiscard]] juce::String makeParameterMetadataMessage();

/** `{"type":"midiMappings", ...}`

    The whole MIDI Learn state in one message: every mapping, whether learn is
    armed and for what, and how the last assignment turned out. One message
    rather than several because the three are always displayed together, and a
    UI that received them separately could render a mapping list that disagreed
    with its own learn indicator.

    @param mappings       the current table.
    @param learningId     the parameter learn is armed for, or an empty string.
    @param statusToken    stable token for the most recent change, which the
                          frontend may branch on.
    @param statusMessage  its displayable form. A replacement is reported here
                          rather than left to be discovered later.
*/
[[nodiscard]] juce::String makeMidiMappingsMessage (const midi::MappingTable& mappings,
                                                    const juce::String& learningId,
                                                    const juce::String& statusToken,
                                                    const juce::String& statusMessage);

/** `{"type":"controllerProfiles", ...}`

    The built-in profiles, described for the frontend. Sent on request and never
    unprompted: the list is fixed at build time and cannot change while Apollo
    is running.
*/
[[nodiscard]] juce::String makeControllerProfilesMessage();

} // namespace apollo::ui
