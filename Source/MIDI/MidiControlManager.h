#pragma once

/*
    MIDI Learn: the live half of Apollo's controller mapping.

    MidiMapping.h owns the *model* — what a mapping is, which ones may coexist,
    what a controller value scales to. This class owns everything that model
    cannot express without JUCE and without threads:

      - the learn state machine, armed from the UI and completed by whatever the
        user physically moves;
      - getting controller values from the audio thread into APVTS, which must
        not be written from the audio thread;
      - getting the table from the message thread to the audio thread;
      - persistence, so a mapping survives a project save (ROADMAP Phase 6).

    THREADING, which is the whole design problem again (ADR-0042):

        audio thread                     message thread
        ------------                     --------------
        handleControllerMessage()  -->   flushPendingChanges()  --> APVTS
          - matches the private table      - applies coalesced values
          - stores a value + a flag        - completes an armed learn
          - captures a learn, if armed     - publishes an edited table
        refreshMappings()          <--   publish()

    The audio thread never touches APVTS, never allocates and never blocks. It
    writes one float and one flag per changed parameter, which coalesces a
    controller sweep to its newest value for free — exactly the right behaviour,
    since applying every intermediate step of a knob move is neither audible nor
    wanted in an automation lane.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include "MIDI/MidiMapping.h"
#include "Parameters/ParameterDefinitions.h"

#include <array>
#include <atomic>
#include <functional>

namespace apollo::midi
{

class MidiControlManager final : private juce::Timer
{
public:
    /** @param stateToUse  the APVTS whose parameters may be mapped, and whose
                           state tree carries the saved mappings.
    */
    explicit MidiControlManager (juce::AudioProcessorValueTreeState& stateToUse);
    ~MidiControlManager() override;

    //==========================================================================
    // Audio thread

    /** Picks up any table published since the last call.

        AUDIO THREAD. Call once at the top of a block, before any message is
        handled, so that a block sees one consistent table throughout.
    */
    void refreshMappings() noexcept;

    /** Offers one MIDI control-change message to the mapping layer.

        AUDIO THREAD. Lock-free, allocation-free and bounded.

        @returns true if the message drove a mapped parameter or completed a
                 learn. The caller acts on its own fixed-function controllers
                 regardless of the answer: a message consumed here is not
                 swallowed, because CC 1 is both a modulation source and a
                 legitimate mapping target.
    */
    bool handleControllerMessage (int messageChannel, int controller, int value) noexcept;

    //==========================================================================
    // Message thread

    /** Arms learn for a parameter. The next non-reserved controller moved is
        assigned to it.

        Arming a second time simply retargets; there is no need to cancel first.
    */
    void beginLearn (int parameterIndex);

    /** Arms learn by parameter ID. @returns false if the ID is not registered. */
    bool beginLearn (const juce::String& parameterId);

    /** Disarms learn without assigning anything. */
    void cancelLearn();

    [[nodiscard]] bool isLearning() const noexcept
    {
        return learnTarget.load (std::memory_order_relaxed) >= 0;
    }

    /** @returns the parameter learn is armed for, or -1. */
    [[nodiscard]] int getLearnParameterIndex() const noexcept
    {
        return learnTarget.load (std::memory_order_relaxed);
    }

    /** Stores a mapping directly, without learning it.

        This is the path a controller profile and the frontend's own editing
        controls use.
    */
    AssignResult assign (const Mapping& mapping);

    /** Removes whatever drives this parameter. @returns true if there was one. */
    bool removeParameterMapping (int parameterIndex);

    /** Removes every mapping. */
    void clearAllMappings();

    /** @returns a copy of the current table. Message thread. */
    [[nodiscard]] MappingTable getMappings() const { return table; }

    /** Outcome of the most recent assignment, learned or explicit.

        Kept so the frontend can be told that its learn replaced something,
        rather than discovering it later (CLAUDE.md §33).
    */
    [[nodiscard]] AssignResult getLastAssignResult() const noexcept { return lastAssignResult; }

    /** Applies queued controller values to APVTS and completes an armed learn.

        Driven by the timer. Exposed so tests can drive it deterministically
        instead of waiting on a real message loop.
    */
    void flushPendingChanges();

    //==========================================================================
    // Persistence

    /** Mirrors the table into the APVTS state tree.

        Called after every edit, so that whenever a host asks for state — which
        it may do at any moment, without warning — the tree already carries the
        current mappings (ROADMAP Phase 6, "mapping persistence").
    */
    void writeToState();

    /** Rebuilds the table from the APVTS state tree.

        Called after a project or preset load. A mapping naming a parameter this
        build does not have is dropped rather than rejecting the whole document:
        state written by a future Apollo must still load, minus what cannot be
        represented (CLAUDE.md §33).

        @returns the number of mappings restored.
    */
    int restoreFromState();

    //==========================================================================

    /** Notified whenever the mapping set or the learn state changes.

        Always invoked on the message thread, from flushPendingChanges, and
        never from the audio thread or from inside a state load — the handler's
        eventual destination is the WebView, which neither of those may touch.
        Pass {} to detach.
    */
    using ChangeHandler = std::function<void()>;
    void setChangeHandler (ChangeHandler handler);

    /** How often queued controller values are applied, in hertz.

        Twice the UI's own refresh rate, so a mapped move reaches the parameter
        before the interface would have drawn it. The resulting worst-case delay
        between a knob move and the parameter changing is one tick — under 17 ms,
        which is inside what a control gesture can resolve and far inside what
        the alternative would cost: writing APVTS from the audio thread
        (ADR-0042).
    */
    static constexpr int updateRateHz = 60;

    /** Ticks a mapped parameter must go quiet for before its automation gesture
        is closed. Three ticks is 50 ms — long enough that a knob paused mid-turn
        stays one gesture, short enough that a completed move is committed
        promptly.
    */
    static constexpr int gestureIdleTicks = 3;

    //==========================================================================
    // The serialized contract. Changing any of these invalidates saved mappings.

    static constexpr const char* mappingsTreeType = "MIDIMAP";
    static constexpr const char* mappingTreeType = "MAP";
    static constexpr const char* parameterProperty = "param";
    static constexpr const char* controllerProperty = "cc";
    static constexpr const char* channelProperty = "channel";
    static constexpr const char* minimumProperty = "min";
    static constexpr const char* maximumProperty = "max";

private:
    void timerCallback() override;

    /** Pushes the current table to the audio thread, retrying on the next tick
        if the ring is momentarily full.
    */
    void publish();

    /** writeToState + publish + notify, in the one order every edit needs. */
    void commitEdit();

    void notifyChanged();

    /** Closes any automation gesture this class opened. */
    void endAllGestures();

    juce::AudioProcessorValueTreeState& apvts;

    /** Authoritative, message thread only. */
    MappingTable table;

    /** The audio thread's private copy. Written only by refreshMappings. */
    MappingTable audioTable;

    MappingChannel channel;

    /** Set when publish() found the ring full. */
    bool republishNeeded = false;

    AssignResult lastAssignResult = AssignResult::added;

    /** Parameter index learn is armed for, or -1. Message thread writes, audio
        thread reads.
    */
    std::atomic<int> learnTarget { -1 };

    /** The control the audio thread saw while learn was armed, packed as
        `channel * controllerCount + controller`, or -1 for nothing captured.

        One slot rather than a queue: if two controls move inside one block, the
        later one is what the user is holding, and the earlier one is exactly
        what a learn should not latch onto.
    */
    std::atomic<int> capturedControl { -1 };

    /** Newest value seen for each parameter, and whether it is unapplied.

        One slot per parameter rather than a queue of events, which is what makes
        a fast controller sweep collapse to its final value instead of filling a
        queue the message thread then has to drain in order.
    */
    std::array<std::atomic<float>, params::parameterCount()> pendingValues {};
    std::array<std::atomic<bool>, params::parameterCount()> pendingFlags {};

    /** Ticks since each parameter last moved, or -1 if no gesture is open. */
    std::array<int, params::parameterCount()> gestureIdle {};

    /** Registry index to APVTS parameter, resolved once at construction.

        The flush runs sixty times a second and would otherwise look every
        changed parameter up by string. It is also the only place that can
        notice the registry and the APVTS layout disagreeing, which would be a
        build-time defect rather than a runtime one.
    */
    std::array<juce::RangedAudioParameter*, params::parameterCount()> parameters {};

    ChangeHandler changeHandler;

    /** Set by notifyChanged, consumed by the flush. Atomic because a state load
        may arrive on whichever thread the host chose.
    */
    std::atomic<bool> changePending { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiControlManager)
};

} // namespace apollo::midi
