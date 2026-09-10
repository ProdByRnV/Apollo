#pragma once

/*
    Apollo's MIDI control-mapping model.

    This is the "MIDI Learn" table: the record of which physical control on
    whichever controller happens to be plugged in drives which Apollo parameter
    (CLAUDE.md §16.2, ROADMAP Phase 6).

    Two properties shape everything here.

    **It is JUCE-free.** The table, the conflict policy and the value scaling are
    plain data and plain arithmetic, so the whole of the interesting behaviour —
    what happens when a controller is learned twice, what happens when the table
    fills, what a controller value becomes after scaling — is testable without a
    plugin host, a MIDI device or a message loop.

    **It is read from the audio thread.** A MIDI controller message arrives in
    processBlock, and the lookup that answers "does anything listen to this?"
    happens there. So the table is a fixed-capacity array with no allocation, no
    locking and no unbounded search, and it reaches the audio thread through
    MappingChannel rather than by being shared (CLAUDE.md §7.1).

    Apollo deliberately hard-codes nothing about any particular controller: a
    mapping is a controller *number*, learned from whatever the user moved
    (CLAUDE.md §16.3, §46). Controller profiles, when they arrive, are a way of
    filling this table quickly — never a requirement for it to work.
*/

#include "Parameters/ParameterDefinitions.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace apollo::midi
{

/** Maximum number of simultaneous controller mappings.

    Fixed so the table can live in preallocated storage and be copied to the
    audio thread without allocating. Sixty-four is comfortably more physical
    controls than any single controller surface exposes.
*/
inline constexpr int maxMappings = 64;

/** MIDI control-change numbers run 0-127. */
inline constexpr int controllerCount = 128;

/** Sentinel channel meaning "any channel".

    The default for a learned mapping. Most players have one controller, and a
    mapping silently locked to the channel the controller happened to be sending
    on when it was learned is a support problem rather than a feature. A specific
    channel can still be requested, for the multi-controller case.
*/
inline constexpr int omniChannel = 0;

/** MIDI channels are 1-16 in JUCE's numbering, which this layer shares. */
inline constexpr int firstChannel = 1;
inline constexpr int lastChannel = 16;

[[nodiscard]] constexpr bool isValidController (int controller) noexcept
{
    return controller >= 0 && controller < controllerCount;
}

[[nodiscard]] constexpr bool isValidChannel (int channel) noexcept
{
    return channel == omniChannel || (channel >= firstChannel && channel <= lastChannel);
}

/** @returns true if this controller number has a fixed meaning Apollo will not
    let a mapping take over.

    The set is deliberately small, because every entry in it is a control the
    user cannot map and will not be told why by any amount of staring at the UI:

      - **CC 64, sustain.** Apollo acts on it directly, and a sustain pedal that
        stopped sustaining because it was once waved at a learn button would be
        a genuinely bewildering failure.
      - **CC 120-127, the channel-mode messages** — all-sound-off, reset-all-
        controllers, all-notes-off, omni and mono/poly mode. These are commands
        rather than controls; they carry no continuous value to scale.

    CC 1 is deliberately *not* reserved. The mod wheel is a modulation source in
    the matrix rather than a mapping (CLAUDE.md §15), and a user who explicitly
    learns it to a parameter gets both behaviours, which is what they asked for.
*/
[[nodiscard]] constexpr bool isReservedController (int controller) noexcept
{
    return controller == 64 || (controller >= 120 && controller <= 127);
}

//==============================================================================

/** The MIDI control a mapping listens to. */
struct ControlAddress
{
    /** Control-change number, 0-127. Negative means "unset". */
    int controller = -1;

    /** 1-16, or omniChannel for any. */
    int channel = omniChannel;

    [[nodiscard]] constexpr bool isValid() const noexcept
    {
        return isValidController (controller) && isValidChannel (channel);
    }

    /** @returns true if an incoming message should drive this address.

        An omni mapping matches every channel; a channel-specific one matches
        only its own. Which of two overlapping mappings wins is MappingTable's
        decision, not this one's.
    */
    [[nodiscard]] constexpr bool matches (int messageChannel, int messageController) const noexcept
    {
        return controller == messageController
               && (channel == omniChannel || channel == messageChannel);
    }

    [[nodiscard]] constexpr bool operator== (const ControlAddress& other) const noexcept
    {
        return controller == other.controller && channel == other.channel;
    }

    [[nodiscard]] constexpr bool operator!= (const ControlAddress& other) const noexcept
    {
        return ! (*this == other);
    }
};

//==============================================================================

/** One controller-to-parameter mapping. */
struct Mapping
{
    ControlAddress address {};

    /** Index into params::parameterDefinitions. Negative means "unset".

        An index rather than an ID string: the audio thread does this lookup, and
        comparing integers is bounded work where comparing strings is not. The ID
        is what gets serialized, because indices move when the registry grows and
        IDs are permanent (Docs/PARAMETER-CONVENTIONS.md §1).
    */
    int parameterIndex = -1;

    /** The normalised parameter range the controller's 0-127 travel spans.

        Both ends are stored rather than a range plus an "invert" flag, because
        `minimum > maximum` *is* inversion and needs no separate concept: a
        controller mapped 1.0 to 0.0 simply runs backwards.
    */
    float minimum = 0.0f;
    float maximum = 1.0f;

    [[nodiscard]] constexpr bool isValid() const noexcept
    {
        return address.isValid()
               && parameterIndex >= 0
               && parameterIndex < static_cast<int> (params::parameterCount());
    }

    /** @returns the normalised parameter value for a 0-127 controller value.

        Audio thread: two multiplies and a clamp.
    */
    [[nodiscard]] constexpr float normalisedFor (int controllerValue) const noexcept
    {
        const auto clamped = controllerValue < 0 ? 0
                                                 : (controllerValue > 127 ? 127 : controllerValue);

        const auto travel = static_cast<float> (clamped) / 127.0f;
        const auto value = minimum + (maximum - minimum) * travel;

        return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    }
};

//==============================================================================

/** What happened when a mapping was assigned.

    Replacement is reported rather than performed silently: the user moved one
    control and two mappings changed, and an interface that does not say so
    leaves them to discover the loss later (CLAUDE.md §33).
*/
enum class AssignResult
{
    added,                      ///< Stored; nothing else changed.
    replacedParameterMapping,   ///< The parameter had a different controller; it was released.
    replacedControllerMapping,  ///< The controller drove a different parameter; it was released.
    replacedBoth,               ///< Both of the above, for two different mappings.
    unchanged,                  ///< This exact mapping was already present.
    rejectedInvalidAddress,     ///< Controller or channel out of range.
    rejectedReservedController, ///< isReservedController.
    rejectedInvalidParameter,   ///< No such parameter.
    rejectedInvalidRange,       ///< A non-finite or out-of-range scaling end.
    rejectedTableFull           ///< maxMappings already in use.
};

/** @returns true if the table now contains the requested mapping. */
[[nodiscard]] constexpr bool succeeded (AssignResult result) noexcept
{
    return result == AssignResult::added
           || result == AssignResult::replacedParameterMapping
           || result == AssignResult::replacedControllerMapping
           || result == AssignResult::replacedBoth
           || result == AssignResult::unchanged;
}

/** @returns the stable wire token, e.g. "REPLACED_CONTROLLER_MAPPING".

    Part of the bridge contract: the frontend may branch on these.
*/
[[nodiscard]] const char* toToken (AssignResult result) noexcept;

/** @returns a human-readable description, safe to display.

    Never contains a value, a path or any internal detail (UI_BINDINGS.md §13).
*/
[[nodiscard]] const char* describe (AssignResult result) noexcept;

//==============================================================================

/** The mapping table.

    **The conflict policy is a bijection**: one control address drives at most
    one parameter, and one parameter is driven by at most one control address.
    Learning a control a parameter already has replaces it; learning a control
    another parameter already uses takes it away from that parameter. Both are
    reported through AssignResult.

    The alternative — letting one controller drive several parameters — was
    rejected because Apollo already has a better answer to it. A control that
    should move many things at once is a macro, and a macro is a modulation
    source with sixteen routing slots and bipolar depths behind it (CLAUDE.md
    §15). Duplicating a weaker version of that in the MIDI layer would give two
    places to look when a parameter moves unexpectedly (ADR-0041).

    Trivially copyable by construction, because copying is how it reaches the
    audio thread.
*/
class MappingTable
{
public:
    MappingTable() = default;

    /** Stores a mapping, enforcing the bijection above. */
    AssignResult assign (const Mapping& mapping) noexcept;

    /** Removes whatever drives this parameter. @returns true if there was one. */
    bool removeParameter (int parameterIndex) noexcept;

    /** Removes the mapping on this exact address. @returns true if there was one. */
    bool removeAddress (const ControlAddress& address) noexcept;

    /** Empties the table. */
    void clear() noexcept;

    [[nodiscard]] int size() const noexcept { return count; }
    [[nodiscard]] bool isEmpty() const noexcept { return count == 0; }
    [[nodiscard]] bool isFull() const noexcept { return count >= maxMappings; }

    /** @returns entry @p index, which must be in [0, size()). */
    [[nodiscard]] const Mapping& at (int index) const noexcept;

    /** @returns the mapping an incoming message should drive, or null.

        AUDIO THREAD. Bounded by maxMappings and free of allocation.

        A channel-specific mapping beats an omni one on the same controller
        number, so a per-channel exception can be added over a general mapping
        without having to remove it first.
    */
    [[nodiscard]] const Mapping* findForMessage (int channel, int controller) const noexcept;

    /** @returns the mapping driving this parameter, or null. */
    [[nodiscard]] const Mapping* findForParameter (int parameterIndex) const noexcept;

    /** @returns the mapping on this exact address, or null. */
    [[nodiscard]] const Mapping* findForAddress (const ControlAddress& address) const noexcept;

private:
    void eraseAt (int index) noexcept;

    std::array<Mapping, static_cast<std::size_t> (maxMappings)> entries {};
    int count = 0;
};

//==============================================================================

/** Publishes mapping tables from the message thread to the audio thread.

    A single-producer, single-consumer ring of whole tables. The message thread
    pushes a complete table whenever the user changes one; the audio thread pops
    everything pending at the top of a block and keeps the newest.

    Whole tables rather than incremental edits, because a table is about 1.3 KB
    and edits happen at human speed: the copy costs nothing at the rate it
    actually occurs, and it removes any question of the audio thread observing a
    half-applied change. A ring rather than a shared buffer with a flag, because
    a ring has no window in which the producer can overwrite what the consumer is
    reading — the producer refuses to publish when the ring is full, and the
    caller republishes on its next tick.
*/
class MappingChannel
{
public:
    MappingChannel() = default;

    /** MESSAGE THREAD. @returns false if the ring is full and the caller should
        try again later.
    */
    bool publish (const MappingTable& table) noexcept;

    /** AUDIO THREAD. Copies the newest published table into @p destination.

        @returns true if @p destination was updated. Copies at most `slotCount`
        tables, so the work is bounded whatever the producer has been doing.
    */
    bool fetch (MappingTable& destination) noexcept;

    /** Number of tables that may be in flight at once. */
    static constexpr int slotCount = 4;

private:
    std::array<MappingTable, static_cast<std::size_t> (slotCount)> slots {};

    /** Free-running counters, not indices: the slot is the counter modulo
        slotCount, so the full/empty distinction needs no spare slot.
    */
    std::atomic<unsigned int> writeCounter { 0 };
    std::atomic<unsigned int> readCounter { 0 };
};

static_assert (std::is_trivially_copyable_v<Mapping>,
               "Mapping is copied into the audio thread's private table.");
static_assert (std::is_trivially_copyable_v<MappingTable>,
               "MappingTable is copied on the audio thread and must not allocate.");

} // namespace apollo::midi
