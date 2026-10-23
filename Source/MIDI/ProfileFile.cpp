#include "MIDI/ProfileFile.h"

#include "Parameters/ParameterId.h"
#include "Parameters/ParameterLayout.h"

#include <algorithm>
#include <cmath>

namespace apollo::midi
{

namespace
{

[[nodiscard]] bool isWholeNumberIn (const juce::var& value, int low, int high)
{
    if (! (value.isInt() || value.isInt64() || value.isDouble()))
        return false;

    const auto number = static_cast<double> (value);

    return std::isfinite (number) && number == std::floor (number)
           && number >= static_cast<double> (low) && number <= static_cast<double> (high);
}

[[nodiscard]] bool isShare (const juce::var& value)
{
    if (! (value.isInt() || value.isInt64() || value.isDouble()))
        return false;

    const auto number = static_cast<double> (value);

    return std::isfinite (number) && number >= 0.0 && number <= 1.0;
}

[[nodiscard]] double fourPlaces (float value)
{
    return std::round (static_cast<double> (value) * 10000.0) / 10000.0;
}

} // namespace

juce::String describe (ProfileReadResult result)
{
    switch (result)
    {
        case ProfileReadResult::ok:                 return "The profile was read.";
        case ProfileReadResult::empty:              return "The file is empty.";
        case ProfileReadResult::tooLarge:           return "The file is far too large to be a controller profile.";
        case ProfileReadResult::notJson:            return "The file is not a controller profile.";
        case ProfileReadResult::notAProfile:        return "The file is not a controller profile.";
        case ProfileReadResult::newerVersion:       return "The profile was saved by a newer version of Apollo.";
        case ProfileReadResult::invalidName:        return "The profile has no usable name.";
        case ProfileReadResult::tooManyAssignments: return "The profile has more assignments than Apollo can hold.";
        case ProfileReadResult::invalidAssignment:  return "One of the profile's assignments is not valid.";
    }

    return "The profile could not be read.";
}

juce::String cleanProfileName (const juce::String& text)
{
    const auto trimmed = text.trim();

    return trimmed.length() <= maximumProfileNameLength ? trimmed : juce::String();
}

ProfileData toProfileData (const ControllerProfile& profile)
{
    ProfileData data;
    data.name = params::toJuceString (profile.name);
    data.description = params::toJuceString (profile.description);

    for (const auto& entry : profile.entries)
        data.assignments.push_back ({ entry.controller, entry.channel,
                                      params::toJuceString (entry.parameterId),
                                      entry.minimum, entry.maximum });

    return data;
}

ProfileData captureProfile (const MappingTable& table,
                            const juce::String& name,
                            const juce::String& description)
{
    ProfileData data;
    data.name = name;
    data.description = description;

    for (int i = 0; i < table.size(); ++i)
    {
        const auto& mapping = table.at (i);
        const auto index = static_cast<std::size_t> (mapping.parameterIndex);

        if (index >= params::parameterCount())
            continue;

        data.assignments.push_back ({ mapping.address.controller, mapping.address.channel,
                                      params::toJuceString (params::parameterDefinitions[index].id),
                                      mapping.minimum, mapping.maximum });
    }

    // In controller order, so a file reads down the controller the way the
    // hardware is laid out rather than in the order things were learned.
    std::stable_sort (data.assignments.begin(), data.assignments.end(),
                      [] (const ProfileAssignment& a, const ProfileAssignment& b)
                      {
                          return a.controller != b.controller ? a.controller < b.controller
                                                              : a.channel < b.channel;
                      });

    return data;
}

juce::String writeProfile (const ProfileData& profile)
{
    juce::Array<juce::var> assignments;

    for (const auto& assignment : profile.assignments)
    {
        auto* entry = new juce::DynamicObject();
        entry->setProperty ("controller", assignment.controller);
        entry->setProperty ("channel", assignment.channel);
        entry->setProperty ("parameter", assignment.parameterId);
        // To four places, which is far finer than a controller's 128 steps:
        // the float's own digits, 0.600000023841858, are noise to anyone
        // reading the file.
        entry->setProperty ("min", fourPlaces (assignment.minimum));
        entry->setProperty ("max", fourPlaces (assignment.maximum));
        assignments.add (juce::var (entry));
    }

    auto* object = new juce::DynamicObject();
    object->setProperty ("format", profileFormatName);
    object->setProperty ("version", profileFormatVersion);
    object->setProperty ("name", profile.name);
    object->setProperty ("description", profile.description);
    object->setProperty ("assignments", assignments);

    // Indented, because a person may open it.
    return juce::JSON::toString (juce::var (object), false);
}

ProfileReadResult readProfile (const juce::String& text, ProfileData& destination)
{
    if (text.trim().isEmpty())
        return ProfileReadResult::empty;

    if (static_cast<int> (text.getNumBytesAsUTF8()) > maximumProfileBytes)
        return ProfileReadResult::tooLarge;

    juce::var parsed;

    if (juce::JSON::parse (text, parsed).failed())
        return ProfileReadResult::notJson;

    const auto* object = parsed.getDynamicObject();

    if (object == nullptr || object->getProperty ("format").toString() != profileFormatName)
        return ProfileReadResult::notAProfile;

    const auto version = object->getProperty ("version");

    if (! isWholeNumberIn (version, 1, 1000000))
        return ProfileReadResult::notAProfile;

    if (static_cast<int> (version) > profileFormatVersion)
        return ProfileReadResult::newerVersion;

    const auto nameValue = object->getProperty ("name");

    if (! nameValue.isString())
        return ProfileReadResult::invalidName;

    ProfileData data;
    data.name = cleanProfileName (nameValue.toString());

    if (data.name.isEmpty())
        return ProfileReadResult::invalidName;

    // Optional, and cut rather than refused: a long description is still a
    // perfectly good profile.
    if (const auto description = object->getProperty ("description"); description.isString())
        data.description = description.toString().trim().substring (0, maximumProfileDescriptionLength);
    else if (! description.isVoid())
        return ProfileReadResult::invalidAssignment;

    const auto* assignments = object->getProperty ("assignments").getArray();

    if (assignments == nullptr)
        return ProfileReadResult::notAProfile;

    if (assignments->size() > maxMappings)
        return ProfileReadResult::tooManyAssignments;

    for (const auto& item : *assignments)
    {
        const auto* entry = item.getDynamicObject();

        if (entry == nullptr)
            return ProfileReadResult::invalidAssignment;

        const auto controller = entry->getProperty ("controller");
        const auto channel = entry->getProperty ("channel");
        const auto parameter = entry->getProperty ("parameter");
        const auto minimum = entry->getProperty ("min");
        const auto maximum = entry->getProperty ("max");

        if (! isWholeNumberIn (controller, 0, controllerCount - 1)
            || ! isWholeNumberIn (channel, omniChannel, lastChannel)
            || ! parameter.isString()
            || ! isShare (minimum) || ! isShare (maximum))
            return ProfileReadResult::invalidAssignment;

        // A well-formed identifier, which is not the same as one this build
        // has: a later Apollo's parameter is dropped when applied, not here.
        const auto parameterId = parameter.toString();

        if (! params::isValidParameterId (parameterId.toStdString()))
            return ProfileReadResult::invalidAssignment;

        data.assignments.push_back ({ static_cast<int> (controller), static_cast<int> (channel), parameterId,
                                      static_cast<float> (static_cast<double> (minimum)),
                                      static_cast<float> (static_cast<double> (maximum)) });
    }

    destination = std::move (data);
    return ProfileReadResult::ok;
}

ProfileReadResult readProfileFile (const juce::File& file, ProfileData& destination)
{
    if (! file.existsAsFile())
        return ProfileReadResult::empty;

    if (file.getSize() > maximumProfileBytes)
        return ProfileReadResult::tooLarge;

    return readProfile (file.loadFileAsString(), destination);
}

} // namespace apollo::midi
