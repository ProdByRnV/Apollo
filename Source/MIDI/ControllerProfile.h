#pragma once

/*
    Controller profiles: a way to fill the MIDI mapping table quickly.

    A profile is a named list of "this controller number drives that parameter"
    entries. Applying one is exactly the same operation as learning each of them
    by hand — every entry goes through `MidiControlManager::assign`, is validated
    the same way, and obeys the same bijection (ADR-0041). A profile is
    therefore a *shortcut*, never a mode: nothing behaves differently because one
    was applied, and nothing needs one to work (CLAUDE.md §16.3).

    **Profiles are kept strictly separate from the parameter registry.** The
    registry describes what Apollo has; a profile describes what somebody's
    hardware sends. Putting a "which knob on which controller" field into
    ParameterDefinitions.h would tie the instrument's permanent contract to the
    accessories of the moment, so a profile refers to parameters by ID and is
    resolved at the point it is applied. An entry naming a parameter this build
    does not have is dropped and the rest are applied (CLAUDE.md §33).

    **The built-in profiles name no manufacturer and no product.** Apollo must
    not hard-code a specific controller (CLAUDE.md §46), and it does not need to:
    the MIDI specification already assigns meanings to two groups of controller
    numbers, and a controller that follows the specification is served by a
    profile built on it. That is the vendor-neutral answer, and it is a better
    one than a list of device names that would be out of date within a year.

    User-supplied profiles are a natural extension of this file and are
    deliberately not here yet: they are files, and file loading, user library
    paths and their failure modes belong with the resource work in Phase 9
    (ADR-0046).
*/

#include "MIDI/MidiMapping.h"

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace apollo::midi
{

/** One assignment inside a profile. */
struct ProfileEntry
{
    /** Control-change number, 0-127. */
    int controller = -1;

    /** Parameter identifier. Resolved when the profile is applied, so a profile
        naming something this build does not have costs one dropped entry rather
        than a refusal.
    */
    std::string_view parameterId;

    /** 1-16, or omniChannel. Omni for every built-in profile: a player who has
        not thought about MIDI channels should not have to.
    */
    int channel = omniChannel;

    /** The normalised parameter range the controller's travel spans. */
    float minimum = 0.0f;
    float maximum = 1.0f;
};

/** A named set of assignments. */
struct ControllerProfile
{
    /** Stable identifier, used on the bridge. Permanent once released. */
    std::string_view id;

    /** Display name. May change; the ID may not. */
    std::string_view name;

    /** One line saying what it assumes about the controller. */
    std::string_view description;

    std::span<const ProfileEntry> entries;
};

//==============================================================================

/** The MIDI specification's **Sound Controllers**, CC 70-79.

    Eight of the ten have agreed meanings, and they map onto a subtractive
    synthesiser almost exactly. A controller that labels a knob "Brightness" is
    sending CC 74, whoever made it.

    CC 74 is also MPE's timbre axis. There is no conflict: inside an active zone
    a member channel's CC 74 is per-note expression, and everywhere else — which
    includes every controller this profile is for — it is an ordinary control
    change (ADR-0044).
*/
inline constexpr std::array<ProfileEntry, 8> soundControllerEntries { {
    { 71, "filter1_resonance" }, // Timbre / Harmonic Intensity
    { 72, "env1_release" },      // Release Time
    { 73, "env1_attack" },       // Attack Time
    { 74, "filter1_cutoff" },    // Brightness
    { 75, "env1_decay" },        // Decay Time
    { 76, "lfo1_rate" },         // Vibrato Rate
    { 77, "osc1_detune" },       // Vibrato Depth — the nearest thing Apollo has
    { 78, "env1_delay" },        // Vibrato Delay
} };

/** The MIDI specification's **General Purpose Controllers**, CC 16-19 and
    80-83.

    Eight numbers the specification deliberately leaves undefined, which is
    precisely what makes them the right home for a generic bank of knobs: a
    controller sending them is asserting nothing about what they mean. The
    parameters chosen are the eight most worth having under a hand while playing.
*/
inline constexpr std::array<ProfileEntry, 8> generalPurposeEntries { {
    { 16, "filter1_cutoff" },
    { 17, "filter1_resonance" },
    { 18, "osc1_position" },
    { 19, "osc2_position" },
    { 80, "osc1_level" },
    { 81, "osc2_level" },
    { 82, "env1_attack" },
    { 83, "env1_release" },
} };

/** The registry of built-in profiles.

    IDs are permanent once released, for the same reason parameter IDs are: they
    appear in bridge messages the frontend keys on.
*/
inline constexpr std::array<ControllerProfile, 2> controllerProfiles { {
    { "sound_controllers",
      "Sound Controllers",
      "MIDI CC 70-79, whose meanings the specification already fixes.",
      soundControllerEntries },

    { "general_purpose",
      "General Purpose",
      "MIDI CC 16-19 and 80-83, the eight the specification leaves undefined.",
      generalPurposeEntries },
} };

[[nodiscard]] constexpr std::size_t controllerProfileCount() noexcept
{
    return controllerProfiles.size();
}

/** @returns the profile with this ID, or nullptr. */
[[nodiscard]] constexpr const ControllerProfile* findControllerProfile (std::string_view id) noexcept
{
    for (const auto& profile : controllerProfiles)
        if (profile.id == id)
            return &profile;

    return nullptr;
}

//==============================================================================

/** How an applied profile meets the mappings already there. */
enum class ProfileMode
{
    /** Every existing mapping is released first. What the user ends up with is
        the profile and nothing else — which is what "set my controller up" means
        when it is said about a controller that was previously set up for
        something else.
    */
    replace,

    /** The profile's entries are added to what is there. The bijection still
        applies, so an entry whose controller or parameter is already in use
        takes it over, and that is reported.
    */
    merge
};

/** What happened when a profile was applied.

    Counted rather than merely succeeded-or-not, because "nine of nine" and
    "three of nine, six taken from something else" are very different outcomes
    and the user is entitled to know which one they got.
*/
struct ProfileApplyResult
{
    /** Entries the table now holds. */
    int applied = 0;

    /** Entries dropped because this build has no such parameter. */
    int unknownParameter = 0;

    /** Entries the table refused — a reserved controller, or no room left. */
    int rejected = 0;

    /** Applied entries that released an existing mapping. */
    int replaced = 0;

    [[nodiscard]] constexpr int total() const noexcept
    {
        return applied + unknownParameter + rejected;
    }
};

} // namespace apollo::midi
