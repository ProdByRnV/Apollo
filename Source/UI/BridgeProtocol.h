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
#include "Resources/PresetLibrary.h"
#include "State/PresetDocument.h"
#include "Telemetry/InstrumentFrame.h"
#include "Telemetry/ScopeFrame.h"
#include "Telemetry/TelemetryHub.h"

#include <array>
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
    invalidGestureState,

    /** The page named a preset the current index does not contain.

        Its own category rather than `unknownParameter`, because it is the one
        error here a user can cause without anything being wrong: a preset
        deleted or renamed in a file manager while the browser was open is still
        listed on a page that has not been told yet.
    */
    unknownPreset,

    /** Nothing usable was left of a preset name, or a metadata field was longer
        than a metadata field may be.
    */
    invalidPresetName
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
    applyControllerProfile,    ///< Fill the mapping table from one.

    /** Fill the display, or go back to the size before that.

        The one command here that asks for something about the *window* rather
        than about the instrument, and it has to be a command rather than
        something the page does for itself: a WebView inside a plugin cannot
        resize the window it is hosted in, and the browser's own fullscreen would
        make the page fill a window that had not changed size. Only the editor
        can ask, so the page asks the editor.
    */
    toggleFullscreen,

    // Presets (Phase 9c). Like the MIDI commands, each one names an intent
    // rather than an operation on the filesystem: the page can ask for the
    // library, ask for a preset it has been shown, and ask for the current
    // sound to be saved under a name. It cannot express a path, and there is no
    // command here that deletes anything.
    requestPresets, ///< Send the index as it stands, without scanning.
    rescanPresets,  ///< Look at the disk again; the result arrives when it does.
    loadPreset,     ///< Load one preset named by its index id.
    savePreset      ///< Write the current sound into the user library.
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

    /** Which preset to load, as an id from the index the backend published.

        Never a path, and never anything the page invented: the id is resolved
        against the live index, and one that is not in it is refused.
    */
    int presetId = 0;

    /** What a saved preset should say about itself.

        Free text from a text field, trimmed and bounded before it gets here, so
        nothing downstream has to defend against a name a megabyte long.
    */
    presets::Metadata presetMetadata;

    /** The bank to save into, or empty for the top of the user library.

        A bank is a folder (ADR-0053), so this is turned into one safe path
        segment before anything opens it — it can name a folder, but it cannot
        name a route out of the library.
    */
    juce::String presetBank;

    /** True when the page has already asked the user about replacing a preset
        that is there.

        Absent means no, and that asymmetry is the point: a save that would
        destroy somebody else's sound has to be asked for in as many words,
        and the default answer to a question nobody asked is "don't".
    */
    bool overwriteExisting = false;
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

/** `{"type":"presetIndex", ...}`

    The library as the browser draws it: every preset the last scan could read,
    each with the id the page uses to ask for it, plus what the scan could *not*
    read. The failures travel with the list rather than being logged somewhere
    the user will never look — "three files in this folder are not presets" is
    something they can act on (CLAUDE.md §33).

    No paths. The interface is shown a name, an author, a category and a bank,
    which is everything a browser needs and nothing that could be turned back
    into a filesystem location (UI_BINDINGS.md §13).

    @param scanning  true while a scan is running, so the browser can say the
                     list is still filling rather than appearing to be complete
                     and wrong.
*/
[[nodiscard]] juce::String makePresetIndexMessage (const resources::PresetIndex& index,
                                                   bool scanning);

/** `{"type":"presetStatus", ...}`

    Two things at once, and deliberately: what the instrument is currently
    called, and what just happened to it. They belong together because every
    event that changes one is capable of changing the other — a load renames the
    sound, a save renames it, and a refusal leaves both exactly as they were,
    which is itself worth saying.

    @param token    a stable token the page may branch on, e.g. "ALREADY_EXISTS".
    @param message  its displayable form, naming no path (UI_BINDINGS.md §13).
    @param metadata what the loaded sound now says about itself.
    @param loadedId the index id of the preset currently loaded, or 0 if the
                    sound did not come from the library — a modified patch, or
                    one restored from a host project.
*/
[[nodiscard]] juce::String makePresetStatusMessage (const juce::String& token,
                                                    const juce::String& message,
                                                    const presets::Metadata& metadata,
                                                    int loadedId);

/** `{"type":"scopeFrames", ...}`

    One message carrying every active scope, rather than one message per source.
    They are drawn in the same repaint, and a frontend that received them
    separately could render a frame from one source beside a staler frame from
    another — which on six traces of the same note would look like a bug in the
    synthesiser rather than in the transport.

    Inactive sources are omitted entirely. A source this build does not capture
    is not the same as one that is quiet, and the interface must not draw them
    alike (PRD §30.1).

    @param frames  one entry per source, indexed by telemetry::ScopeSource.
*/
[[nodiscard]] juce::String makeScopeFramesMessage (
    const std::array<telemetry::ScopeFrame, telemetry::scopeSourceCount>& frames);

/** Serializes one instrument frame: modulator traces, meter, voices, wavetables.

    A second broadcast beside the scopes rather than more of the first, because
    the two want different rates. A scope is a moving picture and reads as a
    slideshow below about twenty-five frames a second; a meter needle and an
    envelope trace are perfectly legible at half that, and halving the larger of
    the two messages is worth more than the tidiness of having one
    (UI_BINDINGS.md §10.5).

    A modulator nothing has traced is omitted, exactly as an uncaptured scope
    source is, and for the same reason.
*/
[[nodiscard]] juce::String makeInstrumentFrameMessage (const telemetry::InstrumentFrame& frame);

} // namespace apollo::ui
