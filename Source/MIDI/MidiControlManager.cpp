#include "MIDI/MidiControlManager.h"

#include "Parameters/ParameterLayout.h"

#include <cmath>
#include <utility>

namespace apollo::midi
{

namespace
{

/** Packs a control address into one int, so the audio thread can hand a learn
    capture to the message thread through a single atomic store.
*/
[[nodiscard]] int packAddress (int channel, int controller) noexcept
{
    return channel * controllerCount + controller;
}

[[nodiscard]] ControlAddress unpackAddress (int packed) noexcept
{
    ControlAddress address;
    address.controller = packed % controllerCount;
    address.channel = packed / controllerCount;
    return address;
}

/** Reads a property that must be a number, with a default for absent or
    unusable values.

    State arriving from a file is untrusted in exactly the way a bridge message
    is: it may have been hand-edited, truncated, or written by a build that
    spelled a property differently.
*/
[[nodiscard]] int readInt (const juce::ValueTree& tree, const char* property, int fallback)
{
    const auto value = tree.getProperty (property);

    if (value.isVoid())
        return fallback;

    const auto text = value.toString().trim();

    if (text.isEmpty() || ! text.containsOnly ("+-0123456789"))
        return fallback;

    return text.getIntValue();
}

[[nodiscard]] float readFloat (const juce::ValueTree& tree, const char* property, float fallback)
{
    const auto value = tree.getProperty (property);

    if (value.isVoid())
        return fallback;

    const auto asDouble = static_cast<double> (value);

    if (! std::isfinite (asDouble))
        return fallback;

    return static_cast<float> (asDouble);
}

} // namespace

//==============================================================================

MidiControlManager::MidiControlManager (juce::AudioProcessorValueTreeState& stateToUse)
    : apvts (stateToUse)
{
    for (std::size_t i = 0; i < params::parameterCount(); ++i)
    {
        parameters[i] = apvts.getParameter (params::toJuceString (params::parameterDefinitions[i].id));

        // The registry is authoritative and the APVTS layout is generated from
        // it, so a null here means the two have been allowed to disagree.
        jassert (parameters[i] != nullptr);
    }

    gestureIdle.fill (-1);

    for (auto& value : pendingValues)
        value.store (0.0f, std::memory_order_relaxed);

    for (auto& flag : pendingFlags)
        flag.store (false, std::memory_order_relaxed);

    // An empty table is published immediately, so the audio thread starts from
    // a known state rather than from whatever a default-constructed copy is.
    publish();

    startTimerHz (updateRateHz);
}

MidiControlManager::~MidiControlManager()
{
    stopTimer();

    // A gesture left open would leave the host recording an automation edit that
    // never ends.
    endAllGestures();
}

void MidiControlManager::setChangeHandler (ChangeHandler handler)
{
    changeHandler = std::move (handler);
}

void MidiControlManager::notifyChanged()
{
    // Deliberately not a direct call. Two of the callers cannot promise which
    // thread they are on: a learn completes from inside the flush, and
    // restoreFromState runs wherever the host chose to call
    // setStateInformation. The handler ends in a WebView call, which is message
    // thread only (CLAUDE.md §7.1), so the notification is deferred to the
    // timer that is already running there.
    changePending.store (true, std::memory_order_release);
}

//==============================================================================
// Audio thread

void MidiControlManager::refreshMappings() noexcept
{
    channel.fetch (audioTable);
}

bool MidiControlManager::handleControllerMessage (int messageChannel, int controller, int value) noexcept
{
    // AUDIO THREAD. No allocation, no locks, no unbounded work.
    if (! isValidController (controller))
        return false;

    // Learn takes priority over an existing mapping. A control being learned is
    // being pointed at, not played, and driving its old destination on the way
    // would move a parameter the user is in the middle of reassigning.
    //
    // A reserved controller is captured too, rather than ignored: the message
    // thread refuses it and says why, which is far better than a learn that
    // sits armed while the user presses a pedal at it.
    if (learnTarget.load (std::memory_order_relaxed) >= 0)
    {
        capturedControl.store (packAddress (messageChannel, controller), std::memory_order_release);
        return true;
    }

    const auto* mapping = audioTable.findForMessage (messageChannel, controller);

    if (mapping == nullptr)
        return false;

    const auto index = static_cast<std::size_t> (mapping->parameterIndex);

    if (index >= pendingValues.size())
        return false;

    pendingValues[index].store (mapping->normalisedFor (value), std::memory_order_relaxed);
    pendingFlags[index].store (true, std::memory_order_release);

    return true;
}

//==============================================================================
// Message thread

void MidiControlManager::beginLearn (int parameterIndex)
{
    if (parameterIndex < 0 || parameterIndex >= static_cast<int> (params::parameterCount()))
        return;

    // Cleared first: a capture left over from a previous, cancelled learn must
    // not complete this one.
    capturedControl.store (-1, std::memory_order_relaxed);
    learnTarget.store (parameterIndex, std::memory_order_release);

    notifyChanged();
}

bool MidiControlManager::beginLearn (const juce::String& parameterId)
{
    const auto index = params::indexOfParameter (parameterId.toStdString());

    if (index < 0)
        return false;

    beginLearn (index);
    return true;
}

void MidiControlManager::cancelLearn()
{
    if (! isLearning())
        return;

    learnTarget.store (-1, std::memory_order_release);
    capturedControl.store (-1, std::memory_order_relaxed);

    notifyChanged();
}

AssignResult MidiControlManager::assign (const Mapping& mapping)
{
    lastAssignResult = table.assign (mapping);

    if (succeeded (lastAssignResult) && lastAssignResult != AssignResult::unchanged)
        commitEdit();

    return lastAssignResult;
}

bool MidiControlManager::removeParameterMapping (int parameterIndex)
{
    if (! table.removeParameter (parameterIndex))
        return false;

    commitEdit();
    return true;
}

void MidiControlManager::clearAllMappings()
{
    if (table.isEmpty())
        return;

    table.clear();
    commitEdit();
}

void MidiControlManager::commitEdit()
{
    writeToState();
    publish();
    notifyChanged();
}

void MidiControlManager::publish()
{
    republishNeeded = ! channel.publish (table);
}

//==============================================================================

void MidiControlManager::timerCallback()
{
    flushPendingChanges();
}

void MidiControlManager::flushPendingChanges()
{
    // MESSAGE THREAD.
    //
    // The deferred change notification is delivered at the end, once every edge
    // this tick could produce has been applied, so the frontend is never told
    // about a mapping the table has not finished storing.

    // A learn completes before values are applied, so the control that finished
    // the learn drives its new destination from the next message rather than
    // this one — which is what stops a knob that was already at 0 from slamming
    // the parameter it was just assigned to.
    const auto captured = capturedControl.exchange (-1, std::memory_order_acquire);

    if (captured >= 0)
    {
        const auto targetParameter = learnTarget.load (std::memory_order_relaxed);

        if (targetParameter >= 0)
        {
            Mapping mapping;
            mapping.address = unpackAddress (captured);
            mapping.parameterIndex = targetParameter;

            // A learned mapping is always omni, whatever channel the message
            // arrived on. The channel a controller happens to be transmitting on
            // is not something most players know or think about, and a mapping
            // that silently stopped working after they changed it — or plugged
            // the same controller into a different port — would be a bug from
            // where they are standing. A channel-specific mapping is still
            // reachable through assign(), which is the path a controller profile
            // uses; it is just not what pointing at a knob means.
            mapping.address.channel = omniChannel;

            lastAssignResult = table.assign (mapping);

            if (succeeded (lastAssignResult))
            {
                learnTarget.store (-1, std::memory_order_release);
                commitEdit();
            }
            else
            {
                // A refusal leaves learn armed, so the user can simply move
                // something else instead of re-entering learn mode.
                notifyChanged();
            }
        }
    }

    if (republishNeeded)
        publish();

    for (std::size_t i = 0; i < pendingFlags.size(); ++i)
    {
        auto* parameter = parameters[i];

        if (parameter == nullptr)
            continue;

        if (pendingFlags[i].exchange (false, std::memory_order_acquire))
        {
            // Gestures are opened on the first change and closed once the
            // control goes quiet, so a controller sweep records in a host as one
            // automation edit and one undo step rather than as several hundred
            // (UI_BINDINGS.md §8).
            if (gestureIdle[i] < 0)
                parameter->beginChangeGesture();

            gestureIdle[i] = 0;
            parameter->setValueNotifyingHost (pendingValues[i].load (std::memory_order_relaxed));
        }
        else if (gestureIdle[i] >= 0)
        {
            ++gestureIdle[i];

            if (gestureIdle[i] > gestureIdleTicks)
            {
                parameter->endChangeGesture();
                gestureIdle[i] = -1;
            }
        }
    }

    if (changePending.exchange (false, std::memory_order_acquire) && changeHandler)
        changeHandler();
}

void MidiControlManager::endAllGestures()
{
    for (std::size_t i = 0; i < gestureIdle.size(); ++i)
    {
        if (gestureIdle[i] >= 0 && parameters[i] != nullptr)
            parameters[i]->endChangeGesture();

        gestureIdle[i] = -1;
    }
}

//==============================================================================
// Persistence

void MidiControlManager::writeToState()
{
    // MESSAGE THREAD. The mappings live as a child of the APVTS state root, so
    // that AudioProcessorValueTreeState::copyState carries them along with the
    // parameters and Apollo's state serialization needs to know nothing about
    // MIDI. A reader that predates this child simply does not see it, and one
    // that expects it and does not find it gets an empty table — which is why
    // this addition needed no state schema bump (ADR-0043).
    auto& root = apvts.state;

    root.removeChild (root.getChildWithName (mappingsTreeType), nullptr);

    if (table.isEmpty())
        return;

    juce::ValueTree mappings (mappingsTreeType);

    for (int i = 0; i < table.size(); ++i)
    {
        const auto& mapping = table.at (i);
        const auto index = static_cast<std::size_t> (mapping.parameterIndex);

        if (index >= params::parameterCount())
            continue;

        juce::ValueTree entry (mappingTreeType);

        // The parameter ID, not its index: indices move whenever the registry
        // grows, and a mapping that silently pointed at a different parameter
        // after an update would be worse than one that was dropped.
        entry.setProperty (parameterProperty,
                           params::toJuceString (params::parameterDefinitions[index].id),
                           nullptr);
        entry.setProperty (controllerProperty, mapping.address.controller, nullptr);
        entry.setProperty (channelProperty, mapping.address.channel, nullptr);
        entry.setProperty (minimumProperty, static_cast<double> (mapping.minimum), nullptr);
        entry.setProperty (maximumProperty, static_cast<double> (mapping.maximum), nullptr);

        mappings.appendChild (entry, nullptr);
    }

    root.appendChild (mappings, nullptr);
}

int MidiControlManager::restoreFromState()
{
    table.clear();

    const auto mappings = apvts.state.getChildWithName (mappingsTreeType);

    if (mappings.isValid())
    {
        for (const auto entry : mappings)
        {
            if (! entry.hasType (mappingTreeType))
                continue;

            const auto id = entry.getProperty (parameterProperty).toString();
            const auto parameterIndex = params::indexOfParameter (id.toStdString());

            if (parameterIndex < 0)
                continue; // A parameter this build does not have.

            Mapping mapping;
            mapping.parameterIndex = parameterIndex;
            mapping.address.controller = readInt (entry, controllerProperty, -1);
            mapping.address.channel = readInt (entry, channelProperty, omniChannel);
            mapping.minimum = readFloat (entry, minimumProperty, 0.0f);
            mapping.maximum = readFloat (entry, maximumProperty, 1.0f);

            // Every restored mapping goes through the same validation and the
            // same conflict policy as a learned one, so a hand-edited document
            // cannot put the table into a state the learn path could not.
            (void) table.assign (mapping);
        }
    }

    // Learn cannot survive a state load: the parameter it was armed for may not
    // even be the same one now.
    learnTarget.store (-1, std::memory_order_release);
    capturedControl.store (-1, std::memory_order_relaxed);

    // Rewritten rather than left as loaded, so the stored form always matches
    // what the table actually accepted.
    writeToState();
    publish();
    notifyChanged();

    return table.size();
}

} // namespace apollo::midi
