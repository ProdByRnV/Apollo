#pragma once

/*
    A controller profile as data and as a file (Phase 13h-3).

    The built-in profiles (ControllerProfile.h) are constants compiled in. A
    profile the user saved, or one somebody sent them, is the same thing held at
    run time: a name, a line of description, and a list of assignments. This is
    that type, and the file it is written to and read from.

    THE FILE IS JSON, because a profile is short, worth reading, and something a
    person may reasonably want to edit by hand: change a controller number,
    swap two knobs, send it to a friend whose keyboard numbers them differently.

        {
          "format": "apollo-controller-profile",
          "version": 1,
          "name": "Studio Desk",
          "description": "The eight knobs on the left.",
          "assignments": [
            { "controller": 74, "channel": 0, "parameter": "filter1_cutoff",
              "min": 0.0, "max": 1.0 }
          ]
        }

    Channel 0 is any channel. `min` and `max` are the share of the parameter's
    travel the controller sweeps, and `min` above `max` runs it backwards, as a
    learned mapping can.

    A FILE IS UNTRUSTED, wherever it came from (CLAUDE.md §40). It is bounded in
    size before it is parsed, and refused whole if anything in it is malformed:
    a wrong type, a number out of range, more assignments than the table holds.
    A parameter this build does not have is the one thing not refused, because
    it is not malformed: a profile written by a later Apollo names parameters
    this one lacks, and those entries are dropped when it is applied, counted,
    and the rest applied (ADR-0046).
*/

#include <juce_core/juce_core.h>

#include <vector>

#include "MIDI/ControllerProfile.h"
#include "MIDI/MidiMapping.h"

namespace apollo::midi
{

/** One assignment in a profile held at run time. */
struct ProfileAssignment
{
    int controller = -1;
    int channel = omniChannel;
    juce::String parameterId;
    float minimum = 0.0f;
    float maximum = 1.0f;

    [[nodiscard]] bool operator== (const ProfileAssignment&) const = default;
};

/** A profile held at run time: a built-in converted, or one read from a file. */
struct ProfileData
{
    juce::String name;
    juce::String description;
    std::vector<ProfileAssignment> assignments;

    [[nodiscard]] bool operator== (const ProfileData&) const = default;
};

inline constexpr int maximumProfileNameLength = 64;
inline constexpr int maximumProfileDescriptionLength = 200;

/** Far beyond the largest profile the table can produce, which is about six
    kilobytes. A file bigger than this is not a profile. */
inline constexpr int maximumProfileBytes = 64 * 1024;

inline constexpr const char* profileFileExtension = ".rnvprofile";
inline constexpr const char* profileFormatName = "apollo-controller-profile";
inline constexpr int profileFormatVersion = 1;

/** Why a profile file was refused. */
enum class ProfileReadResult
{
    ok,
    empty,
    tooLarge,
    notJson,
    notAProfile,      ///< Valid JSON, but not this format.
    newerVersion,     ///< This format, from a later Apollo.
    invalidName,      ///< No name, or one too long.
    tooManyAssignments,
    invalidAssignment ///< A wrong type, or a number out of range.
};

/** @returns a sentence for the user. Names no path and quotes nothing from
    the file (UI_BINDINGS.md §13). */
[[nodiscard]] juce::String describe (ProfileReadResult result);

/** @returns @p profile as run-time data. */
[[nodiscard]] ProfileData toProfileData (const ControllerProfile& profile);

/** @returns the mappings in @p table as a profile called @p name. */
[[nodiscard]] ProfileData captureProfile (const MappingTable& table,
                                          const juce::String& name,
                                          const juce::String& description);

/** @returns @p profile as the text of a profile file. */
[[nodiscard]] juce::String writeProfile (const ProfileData& profile);

/** Reads the text of a profile file into @p destination, which is left alone
    unless the result is ok. */
[[nodiscard]] ProfileReadResult readProfile (const juce::String& text, ProfileData& destination);

/** Reads a profile file, checking its size before reading it. */
[[nodiscard]] ProfileReadResult readProfileFile (const juce::File& file, ProfileData& destination);

/** @returns @p text trimmed, or empty if it is too long to be a name. */
[[nodiscard]] juce::String cleanProfileName (const juce::String& text);

} // namespace apollo::midi
