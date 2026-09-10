#include "UI/ParameterBridge.h"

#include "Parameters/ParameterLayout.h"

namespace apollo::ui
{

ParameterBridge::ParameterBridge (juce::AudioProcessorValueTreeState& stateToUse)
    : apvts (stateToUse)
{
    for (const auto& definition : params::parameterDefinitions)
        apvts.addParameterListener (params::toJuceString (definition.id), this);

    startTimerHz (updateRateHz);
}

ParameterBridge::~ParameterBridge()
{
    stopTimer();

    // Same reasoning as the listener below: the MIDI control manager outlives
    // the bridge, and a change handler that survives its object would be called
    // on a destroyed one.
    setMidiControl (nullptr);

    // Removed explicitly rather than left to teardown order: APVTS outlives the
    // bridge, and a listener that survives its object is a use-after-free that
    // the audio thread would be the one to trigger.
    for (const auto& definition : params::parameterDefinitions)
        apvts.removeParameterListener (params::toJuceString (definition.id), this);
}

void ParameterBridge::setOutboundHandler (OutboundHandler handler)
{
    outboundHandler = std::move (handler);
}

void ParameterBridge::setMidiControl (midi::MidiControlManager* controlToUse)
{
    // The previous manager's handler is released first: the bridge may be
    // attached to a different manager, or to none, and a callback left pointing
    // at a destroyed bridge is exactly the use-after-free this class already
    // takes care to avoid with its APVTS listener.
    if (midiControl != nullptr)
        midiControl->setChangeHandler ({});

    midiControl = controlToUse;

    if (midiControl != nullptr)
    {
        // A mapping can change without the frontend having asked for anything —
        // MIDI Learn completes when the user moves a control, not when the UI
        // sends a message — so the UI is pushed the new state rather than left
        // to poll for it.
        midiControl->setChangeHandler ([this]
        {
            if (outboundHandler)
                outboundHandler (createMidiMappings());
        });
    }
}

int ParameterBridge::indexOfParameter (const juce::String& parameterID)
{
    return params::indexOfParameter (parameterID.toStdString());
}

//==============================================================================

juce::String ParameterBridge::handleMessage (const juce::String& json)
{
    const auto parsed = parseMessage (json);

    if (! parsed.ok)
        return makeErrorMessage (parsed.error);

    return applyCommand (parsed.command);
}

juce::String ParameterBridge::applyCommand (const BridgeCommand& command)
{
    switch (command.type)
    {
        case BridgeCommandType::requestState:
            return createStateSnapshot();

        case BridgeCommandType::requestMetadata:
            return createParameterMetadata();

        case BridgeCommandType::requestControllerProfiles:
            // Answered without needing the MIDI subsystem: the profile list is
            // fixed at build time and describes what Apollo *could* be set to,
            // which is meaningful even where nothing can apply it.
            return makeControllerProfilesMessage();

        case BridgeCommandType::requestMidiMappings:
        case BridgeCommandType::midiLearnBegin:
        case BridgeCommandType::midiLearnCancel:
        case BridgeCommandType::midiMappingRemove:
        case BridgeCommandType::midiMappingClearAll:
        case BridgeCommandType::applyControllerProfile:
            return applyMidiCommand (command);

        case BridgeCommandType::setParameter:
        case BridgeCommandType::gestureBegin:
        case BridgeCommandType::gestureEnd:
            break;

        case BridgeCommandType::none:
        default:
            return makeErrorMessage (BridgeErrorCode::unknownMessageType);
    }

    // parseMessage has already established that the parameter exists in the
    // registry; this guards the case where the registry and the APVTS layout
    // have been allowed to disagree, which would be a build-time defect.
    auto* parameter = apvts.getParameter (command.parameterId);

    if (parameter == nullptr)
    {
        jassertfalse;
        return makeErrorMessage (BridgeErrorCode::unknownParameter);
    }

    switch (command.type)
    {
        case BridgeCommandType::gestureBegin:
            // Gesture boundaries are what let a host record automation as a
            // single edit and group it into one undo step (UI_BINDINGS.md §8).
            parameter->beginChangeGesture();
            return {};

        case BridgeCommandType::gestureEnd:
            parameter->endChangeGesture();
            return {};

        case BridgeCommandType::setParameter:
            // The resulting listener callback marks the parameter dirty, and the
            // next flush echoes the authoritative value back. The UI is updated
            // by that echo, not by assuming its own value was accepted
            // (UI_BINDINGS.md §6, §9).
            parameter->setValueNotifyingHost (command.normalisedValue);
            return {};

        case BridgeCommandType::requestState:
        case BridgeCommandType::requestMetadata:
        case BridgeCommandType::none:
        default:
            break;
    }

    return {};
}

juce::String ParameterBridge::applyMidiCommand (const BridgeCommand& command)
{
    // Answered rather than ignored: a frontend that asks for MIDI Learn in a
    // build that has none must be told so, not left with a button that appears
    // to work (UI_BINDINGS.md §13).
    if (midiControl == nullptr)
        return makeErrorMessage (BridgeErrorCode::unknownMessageType);

    switch (command.type)
    {
        case BridgeCommandType::requestMidiMappings:
            break;

        case BridgeCommandType::midiLearnBegin:
            // parseMessage has already established that the parameter exists.
            (void) midiControl->beginLearn (command.parameterId);
            break;

        case BridgeCommandType::midiLearnCancel:
            midiControl->cancelLearn();
            break;

        case BridgeCommandType::midiMappingRemove:
            (void) midiControl->removeParameterMapping (
                params::indexOfParameter (command.parameterId.toStdString()));
            break;

        case BridgeCommandType::midiMappingClearAll:
            midiControl->clearAllMappings();
            break;

        case BridgeCommandType::applyControllerProfile:
        {
            // parseMessage has already established that the profile exists.
            const auto result = midiControl->applyProfile (
                command.profileId,
                command.replaceExisting ? midi::ProfileMode::replace
                                        : midi::ProfileMode::merge);

            return createMidiMappings (
                "PROFILE_APPLIED",
                describeProfileResult (command.profileId, result));
        }

        case BridgeCommandType::requestControllerProfiles:
        case BridgeCommandType::requestState:
        case BridgeCommandType::requestMetadata:
        case BridgeCommandType::setParameter:
        case BridgeCommandType::gestureBegin:
        case BridgeCommandType::gestureEnd:
        case BridgeCommandType::none:
        default:
            jassertfalse;
            return makeErrorMessage (BridgeErrorCode::unknownMessageType);
    }

    // Every one of these replies with the whole state, including the ones that
    // changed nothing. The frontend then has one code path for "the mapping
    // state is now this", rather than one per command, and cannot end up
    // rendering a mapping list its own request invalidated.
    return createMidiMappings();
}

juce::String ParameterBridge::describeProfileResult (const juce::String& profileId,
                                                     const midi::ProfileApplyResult& result)
{
    const auto* profile = midi::findControllerProfile (profileId.toStdString());
    const auto name = profile != nullptr ? params::toJuceString (profile->name) : profileId;

    // Counted rather than merely "done": "eight of eight" and "five of eight,
    // three taken from something else" are very different outcomes, and the
    // user is entitled to know which one they got.
    juce::String text = "Applied " + juce::String (result.applied) + " of "
                      + juce::String (result.total()) + " assignments from " + name + ".";

    if (result.replaced > 0)
        text += " " + juce::String (result.replaced) + " replaced an existing control.";

    if (result.unknownParameter > 0)
        text += " " + juce::String (result.unknownParameter)
              + " named a parameter this version does not have.";

    if (result.rejected > 0)
        text += " " + juce::String (result.rejected) + " could not be assigned.";

    return text;
}

juce::String ParameterBridge::createMidiMappings() const
{
    if (midiControl == nullptr)
        return createMidiMappings ({}, {});

    return createMidiMappings (midi::toToken (midiControl->getLastAssignResult()),
                               midi::describe (midiControl->getLastAssignResult()));
}

juce::String ParameterBridge::createMidiMappings (const juce::String& statusToken,
                                                  const juce::String& statusMessage) const
{
    if (midiControl == nullptr)
        return makeMidiMappingsMessage ({}, {}, statusToken, statusMessage);

    const auto learningIndex = midiControl->getLearnParameterIndex();

    const auto learningId = learningIndex >= 0
                                && learningIndex < static_cast<int> (params::parameterCount())
                              ? params::toJuceString (
                                    params::parameterDefinitions[static_cast<std::size_t> (
                                        learningIndex)].id)
                              : juce::String();

    return makeMidiMappingsMessage (midiControl->getMappings(),
                                    learningId,
                                    statusToken,
                                    statusMessage);
}

juce::String ParameterBridge::createStateSnapshot() const
{
    std::vector<std::pair<juce::String, float>> values;
    values.reserve (params::parameterDefinitions.size());

    for (const auto& definition : params::parameterDefinitions)
    {
        const auto id = params::toJuceString (definition.id);

        if (const auto* parameter = apvts.getParameter (id))
            values.emplace_back (id, parameter->getValue());
    }

    return makeStateSnapshotMessage (values);
}

juce::String ParameterBridge::createParameterMetadata() const
{
    return makeParameterMetadataMessage();
}

//==============================================================================

void ParameterBridge::parameterChanged (const juce::String& parameterID, float newValue)
{
    juce::ignoreUnused (newValue);

    // REAL-TIME CONTEXT: host automation delivers this on the audio thread.
    // Everything here must stay lock-free and allocation-free. Marking a flag is
    // the entire job; the message thread does the rest.
    const auto index = indexOfParameter (parameterID);

    if (index >= 0)
        dirtyFlags[static_cast<std::size_t> (index)].store (true, std::memory_order_relaxed);
}

void ParameterBridge::timerCallback()
{
    flushPendingUpdates();
}

void ParameterBridge::markAllParametersDirty()
{
    for (auto& flag : dirtyFlags)
        flag.store (true, std::memory_order_relaxed);
}

void ParameterBridge::flushPendingUpdates()
{
    // Message thread only.
    if (! outboundHandler)
    {
        // With no sink attached there is nothing to send, but the flags are
        // still cleared: a snapshot is sent when the editor next attaches, so
        // holding stale flags would only produce redundant messages.
        for (auto& flag : dirtyFlags)
            flag.store (false, std::memory_order_relaxed);

        return;
    }

    for (std::size_t i = 0; i < params::parameterDefinitions.size(); ++i)
    {
        if (! dirtyFlags[i].exchange (false, std::memory_order_relaxed))
            continue;

        const auto id = params::toJuceString (params::parameterDefinitions[i].id);

        if (const auto* parameter = apvts.getParameter (id))
            outboundHandler (makeParameterChangedMessage (id, parameter->getValue()));
    }
}

} // namespace apollo::ui
