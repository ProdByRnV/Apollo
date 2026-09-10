#include "UI/BridgeProtocol.h"

#include "Parameters/ParameterDefinitions.h"
#include "Parameters/ParameterId.h"

#include <cmath>
#include <string>

namespace apollo::ui
{

namespace
{

constexpr const char* typeProperty = "type";
constexpr const char* versionProperty = "version";
constexpr const char* idProperty = "id";
constexpr const char* normalisedValueProperty = "normalizedValue";
constexpr const char* stateProperty = "state";

juce::String toJuceString (std::string_view text)
{
    return juce::String (juce::CharPointer_UTF8 (text.data()),
                         juce::CharPointer_UTF8 (text.data() + text.size()));
}

/** @returns true if the var holds a JSON number, not a string that merely looks
    numeric. A frontend sending "0.5" instead of 0.5 is a defect worth
    surfacing, not silently coercing.
*/
bool isNumber (const juce::var& value)
{
    return value.isDouble() || value.isInt() || value.isInt64();
}

} // namespace

//==============================================================================

juce::String toToken (BridgeErrorCode code)
{
    switch (code)
    {
        case BridgeErrorCode::none:                       return "NONE";
        case BridgeErrorCode::messageTooLarge:            return "MESSAGE_TOO_LARGE";
        case BridgeErrorCode::malformedMessage:           return "MALFORMED_MESSAGE";
        case BridgeErrorCode::unknownMessageType:         return "UNKNOWN_MESSAGE_TYPE";
        case BridgeErrorCode::unsupportedProtocolVersion: return "UNSUPPORTED_PROTOCOL_VERSION";
        case BridgeErrorCode::unknownParameter:           return "UNKNOWN_PARAMETER";
        case BridgeErrorCode::invalidParameterValue:      return "INVALID_PARAMETER_VALUE";
        case BridgeErrorCode::invalidGestureState:        return "INVALID_GESTURE_STATE";
    }

    return "MALFORMED_MESSAGE";
}

juce::String describe (BridgeErrorCode code)
{
    switch (code)
    {
        case BridgeErrorCode::none:                       return "No error.";
        case BridgeErrorCode::messageTooLarge:            return "The message exceeded the permitted size.";
        case BridgeErrorCode::malformedMessage:           return "The message could not be read.";
        case BridgeErrorCode::unknownMessageType:         return "The message type is not supported.";
        case BridgeErrorCode::unsupportedProtocolVersion: return "The message used an unsupported protocol version.";
        case BridgeErrorCode::unknownParameter:           return "The requested parameter does not exist.";
        case BridgeErrorCode::invalidParameterValue:      return "Parameter value was outside the permitted range.";
        case BridgeErrorCode::invalidGestureState:        return "The gesture state is not recognised.";
    }

    return "The message could not be read.";
}

//==============================================================================

BridgeParseResult BridgeParseResult::success (BridgeCommand command)
{
    BridgeParseResult result;
    result.ok = true;
    result.command = std::move (command);
    return result;
}

BridgeParseResult BridgeParseResult::failure (BridgeErrorCode code)
{
    BridgeParseResult result;
    result.ok = false;
    result.error = code;
    return result;
}

BridgeParseResult parseMessage (const juce::String& json)
{
    // Bound the payload before the parser sees it, so a hostile message cannot
    // force unbounded parsing work.
    if (json.getNumBytesAsUTF8() > static_cast<size_t> (maxMessageBytes))
        return BridgeParseResult::failure (BridgeErrorCode::messageTooLarge);

    juce::var parsed;

    if (juce::JSON::parse (json, parsed).failed())
        return BridgeParseResult::failure (BridgeErrorCode::malformedMessage);

    // Arrays, bare numbers, strings and null are all valid JSON but not valid
    // messages.
    const auto* object = parsed.getDynamicObject();

    if (object == nullptr)
        return BridgeParseResult::failure (BridgeErrorCode::malformedMessage);

    // Protocol version is validated before anything else is interpreted, so a
    // newer schema is never reinterpreted as an older one (UI_BINDINGS.md §15).
    if (! object->hasProperty (versionProperty))
        return BridgeParseResult::failure (BridgeErrorCode::unsupportedProtocolVersion);

    const auto versionValue = object->getProperty (versionProperty);

    if (! isNumber (versionValue))
        return BridgeParseResult::failure (BridgeErrorCode::malformedMessage);

    if (static_cast<int> (versionValue) != protocolVersion)
        return BridgeParseResult::failure (BridgeErrorCode::unsupportedProtocolVersion);

    if (! object->hasProperty (typeProperty))
        return BridgeParseResult::failure (BridgeErrorCode::malformedMessage);

    const auto typeValue = object->getProperty (typeProperty);

    if (! typeValue.isString())
        return BridgeParseResult::failure (BridgeErrorCode::malformedMessage);

    const auto messageType = typeValue.toString();

    //--------------------------------------------------------------------------
    if (messageType == "requestState")
    {
        BridgeCommand command;
        command.type = BridgeCommandType::requestState;
        return BridgeParseResult::success (std::move (command));
    }

    if (messageType == "requestMetadata")
    {
        BridgeCommand command;
        command.type = BridgeCommandType::requestMetadata;
        return BridgeParseResult::success (std::move (command));
    }

    if (messageType == "requestMidiMappings")
    {
        BridgeCommand command;
        command.type = BridgeCommandType::requestMidiMappings;
        return BridgeParseResult::success (std::move (command));
    }

    if (messageType == "midiLearnCancel")
    {
        BridgeCommand command;
        command.type = BridgeCommandType::midiLearnCancel;
        return BridgeParseResult::success (std::move (command));
    }

    if (messageType == "midiMappingClearAll")
    {
        BridgeCommand command;
        command.type = BridgeCommandType::midiMappingClearAll;
        return BridgeParseResult::success (std::move (command));
    }

    if (messageType == "requestControllerProfiles")
    {
        BridgeCommand command;
        command.type = BridgeCommandType::requestControllerProfiles;
        return BridgeParseResult::success (std::move (command));
    }

    if (messageType == "applyControllerProfile")
    {
        BridgeCommand command;
        command.type = BridgeCommandType::applyControllerProfile;

        if (! object->hasProperty ("profile"))
            return BridgeParseResult::failure (BridgeErrorCode::malformedMessage);

        const auto profileValue = object->getProperty ("profile");

        if (! profileValue.isString())
            return BridgeParseResult::failure (BridgeErrorCode::malformedMessage);

        const auto profileId = profileValue.toString();

        // Bounded before it is used as a lookup key, and resolved against the
        // built-in registry here rather than downstream — so nothing past this
        // point ever holds a profile identifier that does not exist
        // (UI_BINDINGS.md §14).
        if (profileId.getNumBytesAsUTF8() > params::maxParameterIdLength)
            return BridgeParseResult::failure (BridgeErrorCode::unknownParameter);

        if (midi::findControllerProfile (profileId.toStdString()) == nullptr)
            return BridgeParseResult::failure (BridgeErrorCode::unknownParameter);

        command.profileId = profileId;

        // Absent means replace, which is what "set my controller up" means when
        // it is said about a controller that was set up for something else.
        if (object->hasProperty ("mode"))
        {
            const auto modeValue = object->getProperty ("mode");

            if (! modeValue.isString())
                return BridgeParseResult::failure (BridgeErrorCode::malformedMessage);

            const auto mode = modeValue.toString();

            if (mode == "replace")
                command.replaceExisting = true;
            else if (mode == "merge")
                command.replaceExisting = false;
            else
                return BridgeParseResult::failure (BridgeErrorCode::malformedMessage);
        }

        return BridgeParseResult::success (std::move (command));
    }

    //--------------------------------------------------------------------------
    // Every remaining message type identifies a parameter, so the ID is
    // validated once, here, before any branch uses it.
    const auto readParameterId = [&object] (juce::String& outId) -> BridgeErrorCode
    {
        if (! object->hasProperty (idProperty))
            return BridgeErrorCode::malformedMessage;

        const auto idValue = object->getProperty (idProperty);

        if (! idValue.isString())
            return BridgeErrorCode::malformedMessage;

        const auto id = idValue.toString();

        // Bound the string before it is used as a lookup key, and reject
        // anything that could not be a valid ID in the first place, so an
        // oversized or malformed key never reaches the registry.
        if (id.getNumBytesAsUTF8() > params::maxParameterIdLength)
            return BridgeErrorCode::unknownParameter;

        const auto idUtf8 = id.toStdString();

        if (! params::isValidParameterId (idUtf8))
            return BridgeErrorCode::unknownParameter;

        if (params::findParameter (idUtf8) == nullptr)
            return BridgeErrorCode::unknownParameter;

        outId = id;
        return BridgeErrorCode::none;
    };

    //--------------------------------------------------------------------------
    if (messageType == "setParameter")
    {
        BridgeCommand command;
        command.type = BridgeCommandType::setParameter;

        if (const auto error = readParameterId (command.parameterId); error != BridgeErrorCode::none)
            return BridgeParseResult::failure (error);

        if (! object->hasProperty (normalisedValueProperty))
            return BridgeParseResult::failure (BridgeErrorCode::invalidParameterValue);

        const auto value = object->getProperty (normalisedValueProperty);

        if (! isNumber (value))
            return BridgeParseResult::failure (BridgeErrorCode::invalidParameterValue);

        const auto normalised = static_cast<double> (value);

        // NaN and infinity would propagate straight into the DSP.
        if (! std::isfinite (normalised))
            return BridgeParseResult::failure (BridgeErrorCode::invalidParameterValue);

        if (normalised < 0.0 || normalised > 1.0)
            return BridgeParseResult::failure (BridgeErrorCode::invalidParameterValue);

        command.normalisedValue = static_cast<float> (normalised);
        return BridgeParseResult::success (std::move (command));
    }

    //--------------------------------------------------------------------------
    if (messageType == "gesture")
    {
        BridgeCommand command;

        if (const auto error = readParameterId (command.parameterId); error != BridgeErrorCode::none)
            return BridgeParseResult::failure (error);

        if (! object->hasProperty (stateProperty))
            return BridgeParseResult::failure (BridgeErrorCode::invalidGestureState);

        const auto stateValue = object->getProperty (stateProperty);

        if (! stateValue.isString())
            return BridgeParseResult::failure (BridgeErrorCode::invalidGestureState);

        const auto gestureState = stateValue.toString();

        if (gestureState == "begin")
            command.type = BridgeCommandType::gestureBegin;
        else if (gestureState == "end")
            command.type = BridgeCommandType::gestureEnd;
        else
            return BridgeParseResult::failure (BridgeErrorCode::invalidGestureState);

        return BridgeParseResult::success (std::move (command));
    }

    //--------------------------------------------------------------------------
    if (messageType == "midiLearnBegin" || messageType == "midiMappingRemove")
    {
        BridgeCommand command;
        command.type = messageType == "midiLearnBegin" ? BridgeCommandType::midiLearnBegin
                                                       : BridgeCommandType::midiMappingRemove;

        if (const auto error = readParameterId (command.parameterId); error != BridgeErrorCode::none)
            return BridgeParseResult::failure (error);

        return BridgeParseResult::success (std::move (command));
    }

    return BridgeParseResult::failure (BridgeErrorCode::unknownMessageType);
}

//==============================================================================

juce::String makeErrorMessage (BridgeErrorCode code)
{
    auto* object = new juce::DynamicObject();
    object->setProperty (typeProperty, "error");
    object->setProperty (versionProperty, protocolVersion);
    object->setProperty ("code", toToken (code));
    object->setProperty ("message", describe (code));

    return juce::JSON::toString (juce::var (object));
}

juce::String makeParameterChangedMessage (const juce::String& parameterId, float normalisedValue)
{
    auto* object = new juce::DynamicObject();
    object->setProperty (typeProperty, "parameterChanged");
    object->setProperty (versionProperty, protocolVersion);
    object->setProperty (idProperty, parameterId);
    object->setProperty (normalisedValueProperty, static_cast<double> (normalisedValue));

    return juce::JSON::toString (juce::var (object));
}

juce::String makeStateSnapshotMessage (
    const std::vector<std::pair<juce::String, float>>& normalisedValues)
{
    auto* parameters = new juce::DynamicObject();

    for (const auto& [id, value] : normalisedValues)
        parameters->setProperty (id, static_cast<double> (value));

    auto* object = new juce::DynamicObject();
    object->setProperty (typeProperty, "stateSnapshot");
    object->setProperty (versionProperty, protocolVersion);
    object->setProperty ("parameters", juce::var (parameters));

    return juce::JSON::toString (juce::var (object));
}

juce::String makeParameterMetadataMessage()
{
    juce::Array<juce::var> entries;

    for (const auto& definition : params::parameterDefinitions)
    {
        auto* entry = new juce::DynamicObject();
        entry->setProperty (idProperty, toJuceString (definition.id));
        entry->setProperty ("name", toJuceString (definition.name));
        entry->setProperty ("type", toJuceString (params::toString (definition.type)));
        entry->setProperty ("unit", toJuceString (params::toString (definition.unit)));
        entry->setProperty ("min", static_cast<double> (definition.minimum));
        entry->setProperty ("max", static_cast<double> (definition.maximum));
        entry->setProperty ("default", static_cast<double> (definition.defaultValue));
        entry->setProperty ("skew", static_cast<double> (definition.skew));
        entry->setProperty ("step", static_cast<double> (definition.stepSize));
        entry->setProperty ("automatable", definition.automatable);
        entry->setProperty ("modulatable", definition.modulatable);
        entry->setProperty ("smoothed", definition.smoothed);

        entries.add (juce::var (entry));
    }

    auto* object = new juce::DynamicObject();
    object->setProperty (typeProperty, "parameterMetadata");
    object->setProperty (versionProperty, protocolVersion);
    object->setProperty ("parameters", entries);

    return juce::JSON::toString (juce::var (object));
}

juce::String makeMidiMappingsMessage (const midi::MappingTable& mappings,
                                      const juce::String& learningId,
                                      const juce::String& statusToken,
                                      const juce::String& statusMessage)
{
    juce::Array<juce::var> entries;

    for (int i = 0; i < mappings.size(); ++i)
    {
        const auto& mapping = mappings.at (i);
        const auto index = static_cast<std::size_t> (mapping.parameterIndex);

        if (index >= params::parameterCount())
            continue;

        auto* entry = new juce::DynamicObject();

        // The parameter ID, not its registry index: the frontend keys its
        // controls by ID, and an index is an internal detail that would become
        // wrong the moment the registry grows.
        entry->setProperty (idProperty, toJuceString (params::parameterDefinitions[index].id));
        entry->setProperty ("controller", mapping.address.controller);
        entry->setProperty ("channel", mapping.address.channel);
        entry->setProperty ("min", static_cast<double> (mapping.minimum));
        entry->setProperty ("max", static_cast<double> (mapping.maximum));

        entries.add (juce::var (entry));
    }

    auto* object = new juce::DynamicObject();
    object->setProperty (typeProperty, "midiMappings");
    object->setProperty (versionProperty, protocolVersion);
    object->setProperty ("mappings", entries);

    // An explicit empty string rather than an absent property: "not learning"
    // is a state the UI must render, and a missing key is easier to mishandle
    // than a present one.
    object->setProperty ("learning", learningId);
    object->setProperty ("status", statusToken);
    object->setProperty ("statusMessage", statusMessage);
    object->setProperty ("capacity", midi::maxMappings);

    return juce::JSON::toString (juce::var (object));
}

juce::String makeControllerProfilesMessage()
{
    juce::Array<juce::var> entries;

    for (const auto& profile : midi::controllerProfiles)
    {
        auto* entry = new juce::DynamicObject();

        entry->setProperty (idProperty, toJuceString (profile.id));
        entry->setProperty ("name", toJuceString (profile.name));
        entry->setProperty ("description", toJuceString (profile.description));
        entry->setProperty ("assignments", static_cast<int> (profile.entries.size()));

        entries.add (juce::var (entry));
    }

    auto* object = new juce::DynamicObject();
    object->setProperty (typeProperty, "controllerProfiles");
    object->setProperty (versionProperty, protocolVersion);
    object->setProperty ("profiles", entries);

    return juce::JSON::toString (juce::var (object));
}

} // namespace apollo::ui
