#pragma once

/*
    Connects the validated bridge protocol to the APVTS parameter system.

    Threading is the whole design problem here.

    Host automation calls the APVTS listener from the *audio thread*. Serializing
    JSON or calling into a WebView there would allocate, block and violate the
    real-time contract outright (UI_BINDINGS.md §19, CLAUDE.md §7.1). So the
    audio thread does the cheapest possible thing — sets a lock-free flag — and a
    message-thread timer coalesces those flags into outbound messages at a
    display-appropriate rate (UI_BINDINGS.md §5, §12).

    That coalescing is not only a performance measure: a parameter swept by
    automation changes far faster than any display can show, and forwarding every
    change would flood the WebView with values no one can see.

    Native/APVTS state stays authoritative throughout. The frontend's values are
    a cache that this class keeps in sync (UI_BINDINGS.md §1).
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include "MIDI/MidiControlManager.h"
#include "State/LfoCurveState.h"
#include "Parameters/ParameterDefinitions.h"
#include "UI/BridgeProtocol.h"

#include <array>
#include <atomic>
#include <functional>
#include <utility>

namespace apollo::ui
{

class ParameterBridge final : private juce::AudioProcessorValueTreeState::Listener,
                              private juce::Timer
{
public:
    /** Sends one message to the frontend. Always invoked on the message thread. */
    using OutboundHandler = std::function<void (const juce::String&)>;

    explicit ParameterBridge (juce::AudioProcessorValueTreeState& stateToUse);
    ~ParameterBridge() override;

    /** Sets the sink for outbound messages. Pass {} to detach.

        Detaching is what makes editor teardown safe: the bridge outlives no
        WebView, and a closed editor simply stops receiving.
    */
    void setOutboundHandler (OutboundHandler handler);

    /** Handles one inbound message from the frontend.

        @returns the message to send in reply, or an empty string if there is
                 nothing to reply with. Errors are returned as structured error
                 messages rather than thrown.
    */
    [[nodiscard]] juce::String handleMessage (const juce::String& json);

    /** @returns the authoritative snapshot of every registered parameter. */
    [[nodiscard]] juce::String createStateSnapshot() const;

    /** @returns the parameter registry described for the frontend. */
    [[nodiscard]] juce::String createParameterMetadata() const;

    /** Attaches the MIDI Learn subsystem. Pass nullptr to detach.

        Optional, and nullable, because the bridge is also constructed in
        contexts where there is no MIDI at all — the protocol tests, and any
        future headless use. Without it the MIDI commands are answered with a
        structured error rather than crashing or silently doing nothing.

        The bridge does not own the manager: the processor does, because a
        mapping has to keep working with the editor closed.
    */
    void setMidiControl (midi::MidiControlManager* controlToUse);

    /** Attaches the preset library. Pass nullptr to detach.

        Nullable for the same reasons the MIDI manager is: the bridge is built
        in tests and in any headless use where there is no library at all, and
        the preset commands are then answered with a structured error rather
        than doing nothing quietly.

        The bridge does not own the library — the processor does, because a scan
        started from an open editor must survive that editor being closed.
    */
    void setPresetLibrary (resources::PresetLibrary* libraryToUse);

    /** Attaches the user's controller profiles (Phase 13h-3). Pass nullptr to
        detach; the built-in profiles are still listed and applied without it,
        and the commands that save, delete, export or import are refused. */
    void setProfileLibrary (resources::ProfileLibrary* libraryToUse);

    /** The page asked to export a profile: the editor opens the system's save
        dialog, suggesting @p suggestedName, and calls exportControllerProfileTo
        with what the user chose. MESSAGE THREAD. */
    std::function<void (const juce::String& profileId, const juce::String& suggestedName)> onExportControllerProfile;

    /** The page asked to import a profile: the editor opens the system's open
        dialog and calls importControllerProfileFrom with what the user chose.
        MESSAGE THREAD. */
    std::function<void()> onImportControllerProfile;

    /** Writes the profile with @p profileId to @p destination. @returns the
        controllerProfiles message saying whether it was written. */
    [[nodiscard]] juce::String exportControllerProfileTo (const juce::String& profileId, const juce::File& destination);

    /** Copies the profile in @p source into the user's profiles. @returns the
        controllerProfiles message saying what came of it. */
    [[nodiscard]] juce::String importControllerProfileFrom (const juce::File& source);

    /** @returns every profile, built-in and the user's, with a status. */
    [[nodiscard]] juce::String createControllerProfiles (const juce::String& statusToken,
                                                         const juce::String& statusMessage,
                                                         const juce::String& selected) const;

    /** What to do when a preset load has replaced the whole state tree.

        A hook rather than something the bridge does, because what has to happen
        next belongs to the processor: the MIDI mapping table is rebuilt from
        what actually arrived, and the reload counter is bumped so that anything
        watching resynchronises wholesale instead of inferring a preset load
        from a burst of individual parameter changes. That is exactly what a
        host state load does, and a preset load is the same event reached a
        different way.

        MESSAGE THREAD.
    */
    std::function<void()> onStateReplaced;

    /** @returns the library described for the frontend, paths omitted. */
    [[nodiscard]] juce::String createPresetIndex() const;

    /** @returns what the current sound is called and what last happened to it. */
    [[nodiscard]] juce::String createPresetStatus (const juce::String& statusToken,
                                                   const juce::String& statusMessage) const;

    /** Call when the processor reports that state was replaced wholesale.

        A reload this bridge did not cause came from the host, and the sound in
        front of the user is now the project's rather than the preset the
        browser last loaded. Forgetting the file is what stops the browser
        highlighting a row that is no longer what is playing.

        @returns the preset status to send.
    */
    [[nodiscard]] juce::String handleStateReload();

    /** What to do when the page asks to fill the display.

        A hook rather than something the bridge implements, because the bridge
        owns parameters and knows nothing about windows. The editor sets this
        when it attaches and clears it when it leaves; with nothing set, the
        command is accepted and does nothing, which is the right answer for an
        instance with no editor open.

        MESSAGE THREAD.
    */
    std::function<void()> onToggleFullscreen;

    /** @returns the whole MIDI Learn state described for the frontend, with the
        status of the most recent assignment.
    */
    /** Attaches the drawn LFO curves. Pass nullptr to detach. Nullable for the
        reasons the MIDI manager is; without it the curve commands are answered
        with a structured error. Borrowed: the processor owns the store,
        because a curve has to keep sounding with the editor closed.
    */
    void setLfoCurves (state::LfoCurveStore* storeToUse) noexcept { lfoCurves = storeToUse; }

    /** @returns every LFO's drawn curve, described for the frontend. */
    [[nodiscard]] juce::String createLfoCurves() const;

    /** Attaches the user's content and the library it is chosen from (Phase
        13g). Pass nullptrs to detach. Borrowed: the processor owns both,
        because content has to keep sounding, and a scan started from an open
        editor has to survive it closing. Without them the content commands are
        answered with a structured error. Attaching starts a scan. */
    void setUserContent (resources::UserContent* contentToUse, resources::ContentLibrary* libraryToUse);

    /** What to do when the page asks for a file dialog for a slot. A hook,
        because a dialog is the editor's: the bridge owns parameters and knows
        nothing about windows. With none set the command is accepted and does
        nothing, as Fullscreen is. MESSAGE THREAD. */
    std::function<void (resources::ContentSlot)> onChooseContentFile;

    /** What to do when the page asks to open a library folder. Defaults, when
        unset, to opening it in the system's file manager; tests set it so that
        a test run does not open windows. MESSAGE THREAD. */
    std::function<void (resources::ContentKind)> onRevealContentFolder;

    /** @returns the wavetable and noise libraries, described for the page. */
    [[nodiscard]] juce::String createContentIndex() const;

    /** @returns every content slot, with what just happened. */
    [[nodiscard]] juce::String createContentSlots (const juce::String& token,
                                                   const juce::String& slot,
                                                   const juce::String& message) const;

    [[nodiscard]] juce::String createMidiMappings() const;

    /** As above, but with an explicit status — used when what just happened was
        not a single assignment, such as a controller profile being applied.
    */
    [[nodiscard]] juce::String createMidiMappings (const juce::String& statusToken,
                                                   const juce::String& statusMessage) const;

    /** Emits a parameterChanged message for every parameter marked dirty since
        the last flush.

        Called by the timer. Exposed so tests can drive it deterministically
        instead of waiting on a real message loop.
    */
    void flushPendingUpdates();

    /** Marks every parameter dirty, so the next flush reports all of them.

        Used after a preset or project load, where many parameters change at once
        and the UI needs to be told about all of them.
    */
    void markAllParametersDirty();

    /** Update rate for coalesced parameter changes, in hertz.

        30 Hz is comfortably above the rate at which a control reads as smooth
        and well below the rate at which automation actually changes values.
    */
    static constexpr int updateRateHz = 30;

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void timerCallback() override;

    /** Applies a validated command. @returns the reply, or an empty string. */
    [[nodiscard]] juce::String applyCommand (const BridgeCommand& command);

    /** Applies a validated MIDI Learn command. @returns the reply. */
    [[nodiscard]] juce::String applyMidiCommand (const BridgeCommand& command);

    /** Applies a validated preset command. @returns the reply. */
    [[nodiscard]] juce::String applyPresetCommand (const BridgeCommand& command);

    /** Applies a validated content command. @returns the reply. */
    [[nodiscard]] juce::String applyContentCommand (const BridgeCommand& command);

    /** @returns the index id of the loaded preset, or 0 if it is not in the
        library — a sound that has never been saved, or one restored from a host
        project by a build that has not scanned yet.

        The file this bridge loaded or saved, when it knows one. Otherwise the
        preset the state itself names, if exactly one entry in the library
        carries its name, author and category — which is what makes the list
        agree with the masthead after a restart or a reopened window (ADR-0085).

        Resolved through the file each time rather than remembered as a number,
        because a scan renumbers the index: an id kept across one would point at
        whatever preset had inherited it.
    */
    [[nodiscard]] int idOfLoadedPreset() const;

    /** @returns a displayable summary of an applied controller profile. */
    [[nodiscard]] static juce::String describeProfileResult (
        const juce::String& profileName, const midi::ProfileApplyResult& result);

    /** Saves, deletes, exports and imports controller profiles (Phase 13h-3). */
    [[nodiscard]] juce::String applyProfileCommand (const BridgeCommand& command);

    /** Changes one assignment's controller, channel or range (Phase 13h-3). */
    [[nodiscard]] juce::String applyMappingEdit (const BridgeCommand& command);

    [[nodiscard]] static int indexOfParameter (const juce::String& parameterID);

    juce::AudioProcessorValueTreeState& apvts;

    /** Borrowed, not owned, and null when there is no MIDI subsystem. */
    midi::MidiControlManager* midiControl = nullptr;

    /** Borrowed, not owned, and null when there is no preset library. */
    resources::PresetLibrary* presetLibrary = nullptr;

    /** Borrowed, not owned, and null when there are no curves to edit. */
    state::LfoCurveStore* lfoCurves = nullptr;

    /** Borrowed, not owned, and null when there is no user content. */
    resources::UserContent* userContent = nullptr;
    resources::ProfileLibrary* profileLibrary = nullptr;
    resources::ContentLibrary* contentLibrary = nullptr;

    /** What the current sound was loaded from or saved to, if anything.

        A key rather than a file, because a preset need not be one: the factory
        content is compiled in and has no path (ADR-0065). Empty for a sound
        that did not come from the library at all.

        NEVER LEAVES THIS CLASS. It exists so that the browser can show which
        row is the sound currently loaded, and it does that by being turned into
        an index id on the way out. The page is told a number; the key, which
        contains a path for a user preset, stays here (UI_BINDINGS.md §13).
    */
    juce::String loadedPresetKey;

    /** True between this bridge loading a preset and the reload being observed.

        The processor bumps one counter for every wholesale state replacement,
        whoever caused it, so this is how a load the browser performed is told
        apart from a project the host opened.
    */
    bool reloadWasSelfInitiated = false;

    OutboundHandler outboundHandler;

    /** One flag per registered parameter, set from whichever thread changed the
        parameter and cleared by the message thread. Lock-free and fixed size: no
        allocation, and nothing for the audio thread to contend on.
    */
    std::array<std::atomic<bool>, params::parameterCount()> dirtyFlags {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterBridge)
};

} // namespace apollo::ui
