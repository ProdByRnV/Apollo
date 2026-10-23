#include "Resources/ProfileLibrary.h"

#include "ApolloVersion.h"
#include "Parameters/ParameterLayout.h"
#include "Resources/PresetLibrary.h"

#include <algorithm>

namespace apollo::resources
{

namespace
{

constexpr const char* userIdPrefix = "user-";

/** Writes @p text to @p file through a temporary file beside it, so a failed
    write leaves whatever was there. */
[[nodiscard]] bool writeAtomically (const juce::File& file, const juce::String& text)
{
    if (! file.getParentDirectory().createDirectory())
        return false;

    juce::TemporaryFile temporary (file);

    if (! temporary.getFile().replaceWithText (text, false, false, "\n"))
        return false;

    return temporary.overwriteTargetFileWithTemporary();
}

} // namespace

juce::String describe (ProfileSaveResult result)
{
    switch (result)
    {
        case ProfileSaveResult::ok:            return "Saved.";
        case ProfileSaveResult::invalidName:   return "That name cannot be used for a profile.";
        case ProfileSaveResult::alreadyExists: return "A profile of that name is already there.";
        case ProfileSaveResult::couldNotWrite: return "The profile could not be written to disk.";
    }

    return "The profile was not saved.";
}

juce::File defaultProfileDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile (params::toJuceString (productName))
        .getChildFile ("Profiles");
}

ProfileLibrary::ProfileLibrary()
    : directory (defaultProfileDirectory())
{
}

void ProfileLibrary::setDirectory (const juce::File& newDirectory)
{
    directory = newDirectory;
}

void ProfileLibrary::rescan()
{
    profiles.clear();
    unreadable = 0;

    for (const auto& profile : midi::controllerProfiles)
        profiles.push_back ({ params::toJuceString (profile.id), midi::toProfileData (profile), false, {} });

    if (! directory.isDirectory())
        return;

    std::vector<ProfileListing> found;

    for (const auto& entry : juce::RangedDirectoryIterator (directory, false,
                                                            juce::String ("*") + midi::profileFileExtension,
                                                            juce::File::findFiles))
    {
        if (static_cast<int> (found.size()) >= maximumUserProfiles)
            break;

        ProfileListing listing;
        listing.user = true;
        listing.file = entry.getFile();

        if (midi::readProfileFile (listing.file, listing.data) != midi::ProfileReadResult::ok)
        {
            ++unreadable;
            continue;
        }

        found.push_back (std::move (listing));
    }

    std::sort (found.begin(), found.end(), [] (const ProfileListing& a, const ProfileListing& b)
    {
        const auto byName = a.data.name.compareNatural (b.data.name);
        return byName != 0 ? byName < 0 : a.file.getFileName() < b.file.getFileName();
    });

    for (std::size_t i = 0; i < found.size(); ++i)
    {
        found[i].id = userIdPrefix + juce::String (static_cast<int> (i) + 1);
        profiles.push_back (std::move (found[i]));
    }
}

const ProfileListing* ProfileLibrary::find (const juce::String& id) const
{
    if (id.isEmpty())
        return nullptr;

    for (const auto& listing : profiles)
        if (listing.id == id)
            return &listing;

    return nullptr;
}

juce::File ProfileLibrary::fileFor (const juce::String& name) const
{
    const auto safe = toSafeFileName (name);

    if (safe.isEmpty())
        return {};

    const auto file = directory.getChildFile (safe + midi::profileFileExtension);

    // toSafeFileName leaves no separators, so this can only fail on a name
    // that was nothing but dots; refused either way.
    return file.getParentDirectory() == directory ? file : juce::File();
}

ProfileSaveResult ProfileLibrary::save (const midi::ProfileData& profile,
                                        bool overwrite,
                                        juce::String& savedId)
{
    auto clean = profile;
    clean.name = midi::cleanProfileName (profile.name);
    clean.description = profile.description.trim().substring (0, midi::maximumProfileDescriptionLength);

    const auto target = fileFor (clean.name);

    if (clean.name.isEmpty() || target == juce::File())
        return ProfileSaveResult::invalidName;

    if (target.existsAsFile() && ! overwrite)
        return ProfileSaveResult::alreadyExists;

    if (! writeAtomically (target, midi::writeProfile (clean)))
        return ProfileSaveResult::couldNotWrite;

    rescan();

    for (const auto& listing : profiles)
        if (listing.user && listing.file == target)
            savedId = listing.id;

    return ProfileSaveResult::ok;
}

bool ProfileLibrary::remove (const juce::String& id)
{
    const auto* listing = find (id);

    if (listing == nullptr || ! listing->user)
        return false;

    const auto deleted = listing->file.deleteFile();

    rescan();
    return deleted;
}

midi::ProfileReadResult ProfileLibrary::importFile (const juce::File& source,
                                                    juce::String& importedId,
                                                    ProfileSaveResult& saveResult)
{
    midi::ProfileData data;

    if (const auto read = midi::readProfileFile (source, data); read != midi::ProfileReadResult::ok)
        return read;

    // Never over a profile already there: an import adds, and a user who
    // wanted to replace one can delete it first.
    const auto base = data.name;

    for (int copy = 2; fileFor (data.name).existsAsFile() && copy < 1000; ++copy)
    {
        const auto suffix = " (" + juce::String (copy) + ")";
        data.name = base.substring (0, midi::maximumProfileNameLength - suffix.length()) + suffix;
    }

    saveResult = save (data, false, importedId);
    return midi::ProfileReadResult::ok;
}

bool ProfileLibrary::exportTo (const juce::String& id, const juce::File& destination) const
{
    const auto* listing = find (id);

    if (listing == nullptr || destination == juce::File())
        return false;

    return writeAtomically (destination, midi::writeProfile (listing->data));
}

} // namespace apollo::resources
