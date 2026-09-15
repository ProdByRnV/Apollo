#include "UI/ParameterBridge.h"

#include "Parameters/ParameterLayout.h"
#include "Resources/PresetLibrary.h"
#include "State/PresetDocument.h"

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
    setPresetLibrary (nullptr);

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

void ParameterBridge::setPresetLibrary (resources::PresetLibrary* libraryToUse)
{
    // The previous library's handler is released first, for the same reason the
    // MIDI manager's is: the library outlives the bridge, and a callback left
    // pointing at a destroyed one is a use-after-free on the next scan.
    if (presetLibrary != nullptr)
        presetLibrary->onIndexUpdated = {};

    presetLibrary = libraryToUse;

    if (presetLibrary != nullptr)
    {
        // A scan finishes on its own schedule, with nothing sent from the page,
        // so the new index is pushed rather than waited for. The page asked for
        // a scan; what it gets back later is the answer.
        presetLibrary->onIndexUpdated = [this]
        {
            if (! outboundHandler)
                return;

            outboundHandler (createPresetIndex());

            // AND THE STATUS AFTER IT, because which preset is loaded is
            // expressed as an id *into that index*, and the id was not knowable
            // until the scan finished. Saving a preset is the case that shows
            // it: the file is written, the index is stale by definition, and
            // the status sent alongside the save could only say 0. Sending the
            // status again here is what turns the row the user just created
            // into the row the browser highlights.
            outboundHandler (createPresetStatus ("CURRENT", {}));
        };

        // Started here rather than left to the first request, so that opening
        // the editor is what costs the scan and the browser has something in it
        // by the time anybody looks. Asynchronous: this returns immediately.
        presetLibrary->rescan();
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

        case BridgeCommandType::requestPresets:
        case BridgeCommandType::rescanPresets:
        case BridgeCommandType::loadPreset:
        case BridgeCommandType::savePreset:
            return applyPresetCommand (command);

        case BridgeCommandType::toggleFullscreen:
            // The bridge knows nothing about windows and should not: it owns
            // parameters. The editor registers what to do here, and if none has
            // — a headless test, or an instance with no editor open — the
            // command is accepted and does nothing rather than being an error,
            // because "fill the display" is meaningless without a display.
            if (onToggleFullscreen)
                onToggleFullscreen();

            return {};

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
        case BridgeCommandType::toggleFullscreen:
        case BridgeCommandType::requestPresets:
        case BridgeCommandType::rescanPresets:
        case BridgeCommandType::loadPreset:
        case BridgeCommandType::savePreset:
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

//==============================================================================
// Presets.

int ParameterBridge::idOfLoadedPreset() const
{
    if (presetLibrary == nullptr || loadedPresetKey.isEmpty())
        return 0;

    const auto index = presetLibrary->getIndex();

    for (const auto& entry : index.entries)
        if (entry.key() == loadedPresetKey)
            return entry.id;

    // Loaded from a file the library no longer lists: deleted, moved, or saved
    // somewhere outside the two roots. The sound is still correct; it is simply
    // no longer a row in the browser, and 0 is the honest answer.
    return 0;
}

juce::String ParameterBridge::createPresetIndex() const
{
    if (presetLibrary == nullptr)
        return makeErrorMessage (BridgeErrorCode::unknownMessageType);

    return makePresetIndexMessage (presetLibrary->getIndex(), presetLibrary->isScanning());
}

juce::String ParameterBridge::createPresetStatus (const juce::String& statusToken,
                                                  const juce::String& statusMessage) const
{
    return makePresetStatusMessage (statusToken,
                                    statusMessage,
                                    presets::getMetadata (apvts),
                                    idOfLoadedPreset());
}

juce::String ParameterBridge::handleStateReload()
{
    if (! std::exchange (reloadWasSelfInitiated, false))
        loadedPresetKey.clear();

    return createPresetStatus ("STATE_RELOADED", {});
}

juce::String ParameterBridge::applyPresetCommand (const BridgeCommand& command)
{
    // Answered rather than ignored, exactly as the MIDI commands are: a page
    // whose browser cannot work must be told, not left with a list that never
    // fills (UI_BINDINGS.md §13).
    if (presetLibrary == nullptr)
        return makeErrorMessage (BridgeErrorCode::unknownMessageType);

    switch (command.type)
    {
        case BridgeCommandType::requestPresets:
            // Two messages, because the browser needs two facts and one reply
            // carries one of them. The list is the reply; what the current
            // sound is called is pushed alongside it, so a page opened onto an
            // instrument that already has a preset loaded shows its name rather
            // than waiting for something to change.
            if (outboundHandler)
                outboundHandler (createPresetStatus ("CURRENT", {}));

            return createPresetIndex();

        case BridgeCommandType::rescanPresets:
            presetLibrary->rescan();

            // Replies immediately with what is known now, `scanning` true. The
            // finished index arrives later through onIndexUpdated. A browser
            // that waited for the scan instead would go blank every time
            // anything was saved.
            return createPresetIndex();

        case BridgeCommandType::loadPreset:
        {
            const auto index = presetLibrary->getIndex();
            const auto* entry = resources::findPreset (index, command.presetId);

            // The whole safety argument for numbering presets is this line: a
            // command naming something the backend did not publish reaches no
            // file at all.
            if (entry == nullptr)
                return makeErrorMessage (BridgeErrorCode::unknownPreset);

            juce::String text;

            // One call for both kinds. A built-in is rendered from the table, a
            // user preset is read off the disk, and from here down the two are
            // indistinguishable: the same validator, the same migration, and
            // the same guarantee that a refusal leaves the sound alone.
            if (const auto opened = resources::readPreset (*entry, text);
                opened != state::StateLoadResult::ok)
                return createPresetStatus ("LOAD_FAILED",
                                           "Could not read that preset: "
                                               + state::describe (opened));

            if (const auto applied = presets::read (apvts, text);
                applied != state::StateLoadResult::ok)
                // The current sound is untouched, because presets::read applies
                // nothing unless every check passes. So this says what did not
                // happen rather than what was lost (CLAUDE.md §33).
                return createPresetStatus ("LOAD_FAILED",
                                           "That preset could not be loaded: "
                                               + state::describe (applied));

            loadedPresetKey = entry->key();
            reloadWasSelfInitiated = true;

            // The document replaced the whole tree. The processor rebuilds what
            // hangs off it and tells anything watching, which is the same work a
            // host state load does.
            if (onStateReplaced)
                onStateReplaced();

            // Every parameter may have moved at once, and a page inferring that
            // from individual echoes would be repainting for a second.
            markAllParametersDirty();

            return createPresetStatus ("LOADED", "Loaded " + entry->name + ".");
        }

        case BridgeCommandType::savePreset:
        {
            const auto locations = presetLibrary->getLocations();
            auto directory = locations.userDirectory;

            // SAVING ONLY EVER GOES TO THE USER ROOT, which is why there is no
            // command that chooses a destination: a preset is saved into the
            // library belonging to whoever is saving it, and the factory root
            // is not somewhere Apollo offers to write.
            if (command.presetBank.isNotEmpty())
            {
                const auto bank = resources::toSafeFileName (command.presetBank);

                if (bank.isEmpty())
                    return makeErrorMessage (BridgeErrorCode::invalidPresetName);

                // One level, and one that cannot be a route anywhere:
                // toSafeFileName has already turned every separator into a
                // space, so this names a folder inside the library or nothing.
                directory = directory.getChildFile (bank);

                if (! directory.isAChildOf (locations.userDirectory))
                    return makeErrorMessage (BridgeErrorCode::invalidPresetName);
            }

            const auto target = resources::presetFileFor (directory, command.presetMetadata.name);

            if (target == juce::File())
                return makeErrorMessage (BridgeErrorCode::invalidPresetName);

            // ASKED BEFORE ANYTHING IS DESTROYED. The page has to say in as
            // many words that it means to replace what is there, and the
            // default answer to a question nobody asked is no.
            if (target.existsAsFile() && ! command.overwriteExisting)
                return createPresetStatus ("ALREADY_EXISTS",
                                           "A preset called " + command.presetMetadata.name
                                               + " is already there.");

            const auto text = presets::write (apvts, command.presetMetadata);

            juce::File written;

            if (const auto result = resources::savePreset (directory,
                                                           command.presetMetadata.name,
                                                           text,
                                                           written);
                result != resources::PresetSaveResult::ok)
                return createPresetStatus ("SAVE_FAILED",
                                           "Could not save: " + resources::describe (result));

            // Saving a preset is also becoming it: the sound in front of the
            // user is now that preset, and the browser should say so rather
            // than still showing whatever it was called before.
            presets::setMetadata (apvts, command.presetMetadata);
            loadedPresetKey = resources::presetKeyForFile (written);

            // The library has changed on disk, so the index is stale the moment
            // this returns. The rescan result arrives through onIndexUpdated.
            presetLibrary->rescan();

            return createPresetStatus ("SAVED", "Saved " + command.presetMetadata.name + ".");
        }

        case BridgeCommandType::requestState:
        case BridgeCommandType::requestMetadata:
        case BridgeCommandType::setParameter:
        case BridgeCommandType::gestureBegin:
        case BridgeCommandType::gestureEnd:
        case BridgeCommandType::requestMidiMappings:
        case BridgeCommandType::midiLearnBegin:
        case BridgeCommandType::midiLearnCancel:
        case BridgeCommandType::midiMappingRemove:
        case BridgeCommandType::midiMappingClearAll:
        case BridgeCommandType::requestControllerProfiles:
        case BridgeCommandType::applyControllerProfile:
        case BridgeCommandType::toggleFullscreen:
        case BridgeCommandType::none:
        default:
            jassertfalse;
            return makeErrorMessage (BridgeErrorCode::unknownMessageType);
    }
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
