#pragma once

/*
    Apollo state serialization.

    Serialized state is a long-lived external contract: it lives inside every
    saved DAW project and every preset. The rules it must obey (CLAUDE.md §33,
    ARCHITECTURE.md §6.3-§6.4, Docs/VERSIONING.md §3):

      - carry an explicit schema version, independent of the product version;
      - validate before applying, and never partially apply;
      - preserve the current state when incoming state is rejected, because a
        corrupt file must not also destroy the sound the user already had;
      - migrate older versions explicitly rather than reinterpreting fields.

    All of this runs on the message thread. None of it may run on the audio
    thread (CLAUDE.md §7.1).
*/

#include <juce_audio_processors/juce_audio_processors.h>

namespace apollo::state
{

/** Current state schema version.

    Incremented only when the serialized layout changes in a way an older reader
    would misinterpret. Adding an optional field with a safe default is not such
    a change.
*/
inline constexpr int currentSchemaVersion = 2;

/** The oldest schema version this build can still load, directly or by
    migration. Anything older is rejected rather than guessed at.
*/
inline constexpr int minimumSupportedSchemaVersion = 1;

/** Property names on the state root. Part of the serialized contract. */
inline constexpr const char* schemaVersionProperty = "schemaVersion";
inline constexpr const char* productProperty = "product";
inline constexpr const char* productVersionProperty = "productVersion";

/** Why a state document was rejected. */
enum class StateLoadResult
{
    ok,
    emptyData,          ///< No data supplied.
    malformed,          ///< Not parseable as Apollo state.
    wrongProduct,       ///< Valid XML, but not Apollo's state.
    unsupportedVersion, ///< Newer than this build understands, or too old.
    migrationFailed     ///< Recognised, but could not be brought up to date.
};

/** @returns a short, non-sensitive description, safe to show a user.

    Never contains a file path, a memory address, or document contents
    (UI_BINDINGS.md §13).
*/
[[nodiscard]] juce::String describe (StateLoadResult result);

/** Serializes APVTS state, stamped with schema and product metadata.

    Takes a non-const reference because APVTS::copyState acquires the state
    lock, which it cannot do through a const handle.
*/
void writeState (juce::AudioProcessorValueTreeState& apvts, juce::MemoryBlock& destination);

/** Validates, migrates and applies incoming state.

    The current state is left untouched unless the incoming document is
    recognised, supported and successfully migrated.
*/
[[nodiscard]] StateLoadResult readState (juce::AudioProcessorValueTreeState& apvts,
                                         const void* data,
                                         int sizeInBytes);

/** Brings a state tree up to currentSchemaVersion in place.

    Exposed for testing so each migration step can be exercised directly against
    a captured document from the version it upgrades.

    @returns true if the tree is at currentSchemaVersion afterwards.
*/
[[nodiscard]] bool migrate (juce::ValueTree& state, int fromVersion);

} // namespace apollo::state
