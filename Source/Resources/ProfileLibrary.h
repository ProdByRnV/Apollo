#pragma once

/*
    The controller profiles the user keeps (Phase 13h-3).

    A folder of `.rnvprofile` files beside the presets — on Windows
    `%APPDATA%/Apollo/Profiles` — listed after the built-in profiles, which are
    compiled in and cannot be changed or removed. A profile is edited by
    applying it, changing the assignments, and saving it again under its name.

    SCANNED ON THE CALLER'S THREAD, unlike the preset and content libraries.
    Those read thousands of files that can be megabytes each; this reads at
    most `maximumUserProfiles`, each at most 64 kB and in practice a kilobyte or
    two, from a folder only Apollo writes to. A thread would cost more
    machinery than the reading does. MESSAGE THREAD.

    THE PAGE NAMES A PROFILE BY ID, never by path (ADR-0063): a built-in by its
    permanent id, a user profile by `user-N`, numbered by the last scan. A stale
    number is refused rather than resolved to whatever now has it.
*/

#include <juce_core/juce_core.h>

#include <vector>

#include "MIDI/ProfileFile.h"

namespace apollo::resources
{

/** One profile the picker can offer. */
struct ProfileListing
{
    /** What the page sends back. A built-in's permanent id, or `user-N`. */
    juce::String id;

    midi::ProfileData data;

    /** True for one of the user's, which can be overwritten and removed. */
    bool user = false;

    /** Where a user profile lives. Never sent to the page. */
    juce::File file;
};

/** The most user profiles one scan lists. */
inline constexpr int maximumUserProfiles = 500;

/** Why a profile was not saved. */
enum class ProfileSaveResult
{
    ok,
    invalidName,   ///< Nothing usable was left of the name.
    alreadyExists, ///< A profile of that name is there, and replacing it was not asked for.
    couldNotWrite  ///< The disk refused; anything already there survives.
};

[[nodiscard]] juce::String describe (ProfileSaveResult result);

/** `Apollo/Profiles` in the user's application data folder. */
[[nodiscard]] juce::File defaultProfileDirectory();

class ProfileLibrary
{
public:
    ProfileLibrary();

    /** Points the library somewhere else. Tests use it. Does not scan. */
    void setDirectory (const juce::File& directory);

    [[nodiscard]] juce::File getDirectory() const { return directory; }

    /** Lists the built-ins, then reads the folder. Missing is not an error. */
    void rescan();

    /** Built-ins first, in their own order, then the user's by name. */
    [[nodiscard]] const std::vector<ProfileListing>& getProfiles() const noexcept { return profiles; }

    /** Files in the folder that could not be read as profiles. */
    [[nodiscard]] int getUnreadable() const noexcept { return unreadable; }

    /** @returns the listing with @p id, or nullptr. */
    [[nodiscard]] const ProfileListing* find (const juce::String& id) const;

    /** Writes @p profile into the folder under its name, atomically, and
        rescans. A profile of that name already there is replaced only when
        @p overwrite says so. @p savedId receives its new id. */
    [[nodiscard]] ProfileSaveResult save (const midi::ProfileData& profile,
                                          bool overwrite,
                                          juce::String& savedId);

    /** Deletes the user profile with @p id and rescans. A built-in, or an id
        that is not there, is refused. */
    [[nodiscard]] bool remove (const juce::String& id);

    /** Reads a profile file from anywhere and copies it into the folder. A
        name already taken becomes "Name (2)" rather than replacing anything.
        @returns why it could not be read, or ok; then @p saveResult says
        whether it was written and @p importedId what it is called now. */
    [[nodiscard]] midi::ProfileReadResult importFile (const juce::File& source,
                                                      juce::String& importedId,
                                                      ProfileSaveResult& saveResult);

    /** Writes the profile with @p id to @p destination, atomically. */
    [[nodiscard]] bool exportTo (const juce::String& id, const juce::File& destination) const;

private:
    /** The file a profile called @p name would occupy, or an invalid file if
        nothing usable is left of the name. */
    [[nodiscard]] juce::File fileFor (const juce::String& name) const;

    juce::File directory;
    std::vector<ProfileListing> profiles;
    int unreadable = 0;
};

} // namespace apollo::resources
