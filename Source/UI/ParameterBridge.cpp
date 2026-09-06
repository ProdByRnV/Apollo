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

int ParameterBridge::indexOfParameter (const juce::String& parameterID)
{
    const auto id = parameterID.toStdString();

    for (std::size_t i = 0; i < params::parameterDefinitions.size(); ++i)
        if (params::parameterDefinitions[i].id == id)
            return static_cast<int> (i);

    return -1;
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
