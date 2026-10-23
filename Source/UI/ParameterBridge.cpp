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
            if (! outboundHandler)
                return;

            // A learn's outcome is news; any other change answers a command
            // that has already been replied to, and repeating the last
            // assignment's outcome then would say something untrue.
            outboundHandler (midiControl->takeLearnOutcome() ? createMidiMappings()
                                                             : createMidiMappings ({}, {}));
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

void ParameterBridge::setUserContent (resources::UserContent* contentToUse, resources::ContentLibrary* libraryToUse)
{
    // Released first, as the library's handler is: both outlive the bridge, and
    // a callback left pointing at a destroyed one is a use-after-free.
    if (userContent != nullptr)
        userContent->onChanged = {};

    if (contentLibrary != nullptr)
        contentLibrary->onIndexUpdated = {};

    userContent = contentToUse;
    contentLibrary = libraryToUse;

    if (userContent != nullptr)
    {
        // A load finishes on its own schedule, so what happened is pushed.
        userContent->onChanged = [this] (const resources::ContentEvent& event)
        {
            if (outboundHandler)
                outboundHandler (createContentSlots (event.token, resources::toToken (event.slot), event.message));
        };
    }

    if (contentLibrary != nullptr)
    {
        // The index, and the slots after it: which entry a slot came from is
        // an id into the index, and was not knowable until the scan finished.
        contentLibrary->onIndexUpdated = [this]
        {
            if (! outboundHandler)
                return;

            outboundHandler (createContentIndex());
            outboundHandler (createContentSlots ("CURRENT", {}, {}));
        };

        contentLibrary->rescan();
    }
}

juce::String ParameterBridge::createContentIndex() const
{
    if (contentLibrary == nullptr)
        return makeContentIndexMessage ({}, false);

    return makeContentIndexMessage (contentLibrary->getIndex(), contentLibrary->isScanning());
}

juce::String ParameterBridge::createContentSlots (const juce::String& token,
                                                  const juce::String& slot,
                                                  const juce::String& message) const
{
    std::vector<ContentSlotDescription> slots;

    if (userContent != nullptr)
    {
        const auto index = contentLibrary != nullptr ? contentLibrary->getIndex() : resources::ContentIndex {};

        for (const auto which : { resources::ContentSlot::osc1, resources::ContentSlot::osc2, resources::ContentSlot::noise })
        {
            ContentSlotDescription description;
            description.slot = which;
            description.info = userContent->getInfo (which);

            if (description.info.loaded)
                if (const auto* entry = resources::findContent (index, resources::kindOf (which), description.info.source))
                    description.entry = entry->id;

            slots.push_back (std::move (description));
        }
    }

    return makeContentSlotsMessage (slots, token, slot, message);
}

juce::String ParameterBridge::applyContentCommand (const BridgeCommand& command)
{
    if (userContent == nullptr || contentLibrary == nullptr)
        return makeErrorMessage (BridgeErrorCode::unknownMessageType);

    const auto slot = command.contentSlot;
    const auto kind = resources::kindOf (slot);

    switch (command.type)
    {
        case BridgeCommandType::requestContent:
            if (outboundHandler)
                outboundHandler (createContentIndex());

            return createContentSlots ("CURRENT", {}, {});

        case BridgeCommandType::rescanContent:
            // Answered with the index as it stands, marked as scanning; the
            // finished one arrives through onIndexUpdated.
            contentLibrary->rescan();
            return createContentIndex();

        case BridgeCommandType::loadContent:
        {
            const auto index = contentLibrary->getIndex();
            const auto* entry = resources::findContent (index, command.contentId);

            // A number the index does not have, or a sample offered to an
            // oscillator: refused, with nothing opened.
            if (entry == nullptr || entry->kind != kind)
                return makeErrorMessage (BridgeErrorCode::unknownContent);

            resources::ContentSource source;
            source.name = entry->name;
            source.origin = entry->factory ? resources::ContentOrigin::factoryLibrary : resources::ContentOrigin::library;
            source.libraryPath = entry->libraryPath;

            userContent->loadFile (slot, entry->file, source);
            return {};
        }

        case BridgeCommandType::chooseContentFile:
            if (onChooseContentFile)
                onChooseContentFile (slot);

            return {};

        case BridgeCommandType::clearContent:
            userContent->clear (slot);
            return {};

        case BridgeCommandType::saveContentToLibrary:
        {
            const auto info = userContent->getInfo (slot);
            const auto locations = contentLibrary->getLocations();

            juce::File written;
            const auto result = resources::saveContent (userContent->getStoredAudio (slot), kind,
                                                        info.source.name, locations.userRoot (kind), written);

            if (result != resources::ContentSaveResult::ok)
                return createContentSlots ("SAVE_FAILED", resources::toToken (slot), resources::describe (result));

            // The slot now comes from the library, which is what makes the
            // browser highlight the row the save created once the scan lands.
            userContent->adoptSource (slot, resources::sourceFor (locations, kind, written));
            contentLibrary->rescan();

            return createContentSlots ("SAVED", resources::toToken (slot),
                                       written.getFileNameWithoutExtension() + " is in your library now.");
        }

        case BridgeCommandType::revealContentFolder:
            if (onRevealContentFolder)
                onRevealContentFolder (command.contentKind);
            else
                (void) contentLibrary->revealFolder (command.contentKind);

            return {};

        default:
            return makeErrorMessage (BridgeErrorCode::unknownMessageType);
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
            // Answered without needing the MIDI subsystem: the profile list
            // describes what Apollo *could* be set to, which is meaningful even
            // where nothing can apply it. The user's folder is read again each
            // time, so a profile copied in by hand appears when the page asks.
            if (profileLibrary == nullptr)
                return makeControllerProfilesMessage();

            profileLibrary->rescan();
            return createControllerProfiles ({}, {}, {});

        case BridgeCommandType::saveControllerProfile:
        case BridgeCommandType::deleteControllerProfile:
        case BridgeCommandType::exportControllerProfile:
        case BridgeCommandType::importControllerProfile:
            return applyProfileCommand (command);

        case BridgeCommandType::midiMappingEdit:
            return applyMappingEdit (command);

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

        case BridgeCommandType::requestLfoCurves:
        case BridgeCommandType::setLfoCurve:
        {
            if (lfoCurves == nullptr)
                return makeErrorMessage (BridgeErrorCode::unknownMessageType);

            // Answered with all four either way. After a set, that is the echo
            // the page draws from: the store's copy is the one that counts.
            if (command.type == BridgeCommandType::setLfoCurve
                && ! lfoCurves->set (command.lfoIndex, command.lfoCurve))
                return makeErrorMessage (BridgeErrorCode::invalidLfoCurve);

            return createLfoCurves();
        }

        case BridgeCommandType::requestContent:
        case BridgeCommandType::rescanContent:
        case BridgeCommandType::loadContent:
        case BridgeCommandType::chooseContentFile:
        case BridgeCommandType::clearContent:
        case BridgeCommandType::saveContentToLibrary:
        case BridgeCommandType::revealContentFolder:
            return applyContentCommand (command);

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

        // A release and a clear say what they did. Both used to reply with the
        // last *assignment's* outcome, so after Clear all the footer went on
        // reading "Applied 8 of 8 assignments" beside a masthead reading MIDI 0
        // (12c-3).
        case BridgeCommandType::midiMappingRemove:
        {
            const auto index = params::indexOfParameter (command.parameterId.toStdString());

            if (! midiControl->removeParameterMapping (index))
                break;

            const auto& definition = params::parameterDefinitions[static_cast<std::size_t> (index)];

            return createMidiMappings ("RELEASED",
                                       "Released the MIDI control on "
                                           + params::toJuceString (definition.name) + ".");
        }

        case BridgeCommandType::midiMappingClearAll:
        {
            const auto released = midiControl->getMappings().size();

            midiControl->clearAllMappings();

            return createMidiMappings ("CLEARED",
                                       released == 1 ? juce::String ("Released 1 MIDI assignment.")
                                                     : "Released " + juce::String (released)
                                                           + " MIDI assignments.");
        }

        case BridgeCommandType::applyControllerProfile:
        {
            const auto mode = command.replaceExisting ? midi::ProfileMode::replace
                                                      : midi::ProfileMode::merge;

            // A built-in by its permanent id; anything else is one of the
            // user's, resolved against the list the page was last shown.
            if (const auto* builtIn = midi::findControllerProfile (command.profileId.toStdString()))
                return createMidiMappings ("PROFILE_APPLIED",
                                           describeProfileResult (params::toJuceString (builtIn->name),
                                                                  midiControl->applyProfile (*builtIn, mode)));

            const auto* listing = profileLibrary != nullptr ? profileLibrary->find (command.profileId) : nullptr;

            if (listing == nullptr)
                return makeErrorMessage (BridgeErrorCode::unknownProfile);

            const auto data = listing->data;

            return createMidiMappings ("PROFILE_APPLIED",
                                       describeProfileResult (data.name,
                                                              midiControl->applyProfile (data, mode, command.profileId)));
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

juce::String ParameterBridge::describeProfileResult (const juce::String& name,
                                                     const midi::ProfileApplyResult& result)
{
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
// Controller profiles (Phase 13h-3).

void ParameterBridge::setProfileLibrary (resources::ProfileLibrary* libraryToUse)
{
    profileLibrary = libraryToUse;
}

juce::String ParameterBridge::createControllerProfiles (const juce::String& statusToken,
                                                        const juce::String& statusMessage,
                                                        const juce::String& selected) const
{
    if (profileLibrary == nullptr)
        return makeControllerProfilesMessage();

    return makeControllerProfilesMessage (*profileLibrary, statusToken, statusMessage, selected);
}

juce::String ParameterBridge::applyProfileCommand (const BridgeCommand& command)
{
    // Refused rather than ignored, as a preset command is without a library.
    if (profileLibrary == nullptr)
        return makeErrorMessage (BridgeErrorCode::unknownMessageType);

    switch (command.type)
    {
        case BridgeCommandType::saveControllerProfile:
        {
            if (midiControl == nullptr)
                return makeErrorMessage (BridgeErrorCode::unknownMessageType);

            const auto mappings = midiControl->getMappings();

            // A profile of nothing would apply as "release everything", which
            // is what Clear all is for.
            if (mappings.isEmpty())
                return createControllerProfiles ("SAVE_FAILED",
                                                 "There are no assignments to save. Learn some, or apply a profile, first.",
                                                 {});

            juce::String savedId;
            const auto result = profileLibrary->save (midi::captureProfile (mappings,
                                                                            command.profileName,
                                                                            command.profileDescription),
                                                      command.overwriteExisting,
                                                      savedId);

            if (result == resources::ProfileSaveResult::alreadyExists)
                return createControllerProfiles ("ALREADY_EXISTS",
                                                 "A profile called " + command.profileName + " is already there.",
                                                 {});

            if (result != resources::ProfileSaveResult::ok)
                return createControllerProfiles ("SAVE_FAILED", "Could not save: " + resources::describe (result), {});

            return createControllerProfiles ("SAVED",
                                             "Saved " + juce::String (mappings.size()) + " assignments as "
                                                 + command.profileName + ".",
                                             savedId);
        }

        case BridgeCommandType::deleteControllerProfile:
        {
            const auto* listing = profileLibrary->find (command.profileId);

            if (listing == nullptr)
                return makeErrorMessage (BridgeErrorCode::unknownProfile);

            // The built-ins are compiled in; there is nothing to delete.
            if (! listing->user)
                return createControllerProfiles ("DELETE_FAILED", "Apollo's own profiles cannot be removed.", {});

            const auto name = listing->data.name;

            if (! profileLibrary->remove (command.profileId))
                return createControllerProfiles ("DELETE_FAILED", "Could not remove " + name + ".", {});

            // The assignments it made stay where they are: a profile is a
            // shortcut to them, not a mode they depend on (ADR-0046).
            return createControllerProfiles ("DELETED", "Removed the profile " + name + ".", {});
        }

        case BridgeCommandType::exportControllerProfile:
        {
            const auto* listing = profileLibrary->find (command.profileId);

            if (listing == nullptr)
                return makeErrorMessage (BridgeErrorCode::unknownProfile);

            // The dialog answers later; the editor sends what came of it.
            if (onExportControllerProfile)
                onExportControllerProfile (command.profileId, listing->data.name);

            return {};
        }

        case BridgeCommandType::importControllerProfile:
            if (onImportControllerProfile)
                onImportControllerProfile();

            return {};

        default:
            jassertfalse;
            return makeErrorMessage (BridgeErrorCode::unknownMessageType);
    }
}

juce::String ParameterBridge::exportControllerProfileTo (const juce::String& profileId, const juce::File& destination)
{
    if (profileLibrary == nullptr)
        return makeErrorMessage (BridgeErrorCode::unknownMessageType);

    const auto* listing = profileLibrary->find (profileId);

    if (listing == nullptr)
        return makeErrorMessage (BridgeErrorCode::unknownProfile);

    const auto name = listing->data.name;

    // The message names the profile, never where it went (UI_BINDINGS.md §13).
    if (! profileLibrary->exportTo (profileId, destination))
        return createControllerProfiles ("EXPORT_FAILED", "Could not write " + name + " there.", profileId);

    return createControllerProfiles ("EXPORTED", "Exported " + name + ".", profileId);
}

juce::String ParameterBridge::importControllerProfileFrom (const juce::File& source)
{
    if (profileLibrary == nullptr)
        return makeErrorMessage (BridgeErrorCode::unknownMessageType);

    juce::String importedId;
    auto saved = resources::ProfileSaveResult::couldNotWrite;

    if (const auto read = profileLibrary->importFile (source, importedId, saved); read != midi::ProfileReadResult::ok)
        return createControllerProfiles ("IMPORT_FAILED", "Could not import that file. " + midi::describe (read), {});

    if (saved != resources::ProfileSaveResult::ok)
        return createControllerProfiles ("IMPORT_FAILED", "Could not import that file. " + resources::describe (saved), {});

    const auto* listing = profileLibrary->find (importedId);
    const auto name = listing != nullptr ? listing->data.name : juce::String ("the profile");

    // Added to the list, not applied: what to do with it is the next choice,
    // and Replace and Merge are both one click away.
    return createControllerProfiles ("IMPORTED", "Imported " + name + ". Apply it with Replace or Merge.", importedId);
}

juce::String ParameterBridge::applyMappingEdit (const BridgeCommand& command)
{
    if (midiControl == nullptr)
        return makeErrorMessage (BridgeErrorCode::unknownMessageType);

    const auto index = params::indexOfParameter (command.parameterId.toStdString());
    const auto mappings = midiControl->getMappings();

    // An edit changes an assignment that exists; making one is what learn and
    // the profiles are for.
    if (mappings.findForParameter (index) == nullptr)
        return createMidiMappings ("EDIT_FAILED", "That control has no MIDI assignment to change.");

    midi::Mapping mapping;
    mapping.address.controller = command.mappingController;
    mapping.address.channel = command.mappingChannel;
    mapping.parameterIndex = index;
    mapping.minimum = command.mappingMinimum;
    mapping.maximum = command.mappingMaximum;

    // What sat on the new control, before the edit takes it.
    const auto* displaced = mappings.findForAddress (mapping.address);
    const auto displacedIndex = displaced != nullptr && displaced->parameterIndex != index ? displaced->parameterIndex : -1;

    const auto result = midiControl->assign (mapping);

    if (! midi::succeeded (result))
        return createMidiMappings ("EDIT_FAILED", midi::describe (result));

    const auto& definition = params::parameterDefinitions[static_cast<std::size_t> (index)];
    auto text = "Changed the MIDI control on " + params::toJuceString (definition.name) + ".";

    // The one thing an edit can do that the user did not ask for, so it is
    // said rather than discovered (CLAUDE.md §33).
    if (displacedIndex >= 0)
        text += " " + params::toJuceString (params::parameterDefinitions[static_cast<std::size_t> (displacedIndex)].name)
              + " no longer has one: that control now drives this.";

    return createMidiMappings ("EDITED", text);
}

//==============================================================================
// Presets.

int ParameterBridge::idOfLoadedPreset() const
{
    if (presetLibrary == nullptr)
        return 0;

    const auto index = presetLibrary->getIndex();

    if (loadedPresetKey.isNotEmpty())
    {
        for (const auto& entry : index.entries)
            if (entry.key() == loadedPresetKey)
                return entry.id;

        // Loaded from a file the library no longer lists: deleted, moved, or
        // saved somewhere outside the two roots. The sound is still correct; it
        // is simply no longer a row in the browser, and 0 is the honest answer.
        return 0;
    }

    // NO KEY, WHICH IS THE COMMON CASE RATHER THAN THE RARE ONE. The key lives
    // in this bridge and the bridge lives in the editor, so it is gone after a
    // restart, after a host project load, and every time a host closes the
    // plugin's window. The state still names its preset — that is what the
    // masthead shows — so the list used to say nothing while the masthead said
    // "Wire Pluck", and the two contradicted each other (ADR-0085).
    //
    // So the state's own claim is resolved against the library: the preset
    // whose name, author and category it carries, if exactly ONE does. Two
    // candidates — a user copy saved under the factory name — is a question
    // this cannot answer, and no highlight is better than the wrong one.
    const auto metadata = presets::getMetadata (apvts);

    if (metadata.name.isEmpty())
        return 0;

    int match = 0;

    for (const auto& entry : index.entries)
    {
        if (entry.name != metadata.name || entry.author != metadata.author
            || entry.category != metadata.category)
            continue;

        if (match != 0)
            return 0;

        match = entry.id;
    }

    return match;
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

            // The same call a MIDI program change makes (Phase 13h-2), so the
            // two cannot load a preset differently.
            auto failedToRead = false;

            if (const auto applied = resources::applyPreset (apvts, *entry, failedToRead);
                applied != state::StateLoadResult::ok)
                // The current sound is untouched, because nothing is applied
                // unless every check passes. So this says what did not happen
                // rather than what was lost (CLAUDE.md §33).
                return createPresetStatus ("LOAD_FAILED",
                                           (failedToRead ? "Could not read that preset: "
                                                         : "That preset could not be loaded: ")
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

juce::String ParameterBridge::createLfoCurves() const
{
    std::array<dsp::LfoCurve, 4> curves;
    static_assert (std::tuple_size_v<decltype (curves)> == static_cast<std::size_t> (state::numLfoCurves));

    for (std::size_t i = 0; i < curves.size(); ++i)
        curves[i] = lfoCurves != nullptr ? lfoCurves->get (static_cast<int> (i)) : dsp::defaultLfoCurve();

    return makeLfoCurvesMessage (curves);
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
