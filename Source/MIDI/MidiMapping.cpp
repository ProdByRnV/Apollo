#include "MIDI/MidiMapping.h"

#include <cmath>

namespace apollo::midi
{

const char* toToken (AssignResult result) noexcept
{
    switch (result)
    {
        case AssignResult::added:                      return "ADDED";
        case AssignResult::replacedParameterMapping:   return "REPLACED_PARAMETER_MAPPING";
        case AssignResult::replacedControllerMapping:  return "REPLACED_CONTROLLER_MAPPING";
        case AssignResult::replacedBoth:               return "REPLACED_BOTH";
        case AssignResult::unchanged:                  return "UNCHANGED";
        case AssignResult::rejectedInvalidAddress:     return "REJECTED_INVALID_ADDRESS";
        case AssignResult::rejectedReservedController: return "REJECTED_RESERVED_CONTROLLER";
        case AssignResult::rejectedInvalidParameter:   return "REJECTED_INVALID_PARAMETER";
        case AssignResult::rejectedInvalidRange:       return "REJECTED_INVALID_RANGE";
        case AssignResult::rejectedTableFull:          return "REJECTED_TABLE_FULL";
    }

    return "REJECTED_INVALID_ADDRESS";
}

const char* describe (AssignResult result) noexcept
{
    switch (result)
    {
        case AssignResult::added:
            return "Control assigned.";
        case AssignResult::replacedParameterMapping:
            return "Control assigned; this parameter's previous control was released.";
        case AssignResult::replacedControllerMapping:
            return "Control assigned; it no longer drives the parameter it did before.";
        case AssignResult::replacedBoth:
            return "Control assigned; two earlier assignments were released.";
        case AssignResult::unchanged:
            return "That control was already assigned to this parameter.";
        case AssignResult::rejectedInvalidAddress:
            return "That is not a MIDI control Apollo can assign.";
        case AssignResult::rejectedReservedController:
            return "That control has a fixed function and cannot be reassigned.";
        case AssignResult::rejectedInvalidParameter:
            return "The requested parameter does not exist.";
        case AssignResult::rejectedInvalidRange:
            return "The assignment range was outside the permitted values.";
        case AssignResult::rejectedTableFull:
            return "No assignment slots remain. Remove one before adding another.";
    }

    return "That is not a MIDI control Apollo can assign.";
}

//==============================================================================

const Mapping& MappingTable::at (int index) const noexcept
{
    // A caller asking for an out-of-range entry has a bug, but returning a
    // reference to arbitrary memory would turn that bug into undefined
    // behaviour. Entry zero of a default-constructed array is invalid by its
    // own isValid(), which is what a caller checking its result will see.
    if (index < 0 || index >= count)
        return entries[0];

    return entries[static_cast<std::size_t> (index)];
}

const Mapping* MappingTable::findForMessage (int channel, int controller) const noexcept
{
    // AUDIO THREAD.
    //
    // Two passes rather than one so that a channel-specific mapping beats an
    // omni mapping on the same controller number regardless of the order the
    // two were learned in. Both passes are bounded by maxMappings, and the
    // table is small enough that a linear scan beats any index that would have
    // to be kept in step with it.
    const Mapping* omniMatch = nullptr;

    for (int i = 0; i < count; ++i)
    {
        const auto& entry = entries[static_cast<std::size_t> (i)];

        if (entry.address.controller != controller)
            continue;

        if (entry.address.channel == channel)
            return &entry;

        if (entry.address.channel == omniChannel)
            omniMatch = &entry;
    }

    return omniMatch;
}

const Mapping* MappingTable::findForParameter (int parameterIndex) const noexcept
{
    if (parameterIndex < 0)
        return nullptr;

    for (int i = 0; i < count; ++i)
        if (entries[static_cast<std::size_t> (i)].parameterIndex == parameterIndex)
            return &entries[static_cast<std::size_t> (i)];

    return nullptr;
}

const Mapping* MappingTable::findForAddress (const ControlAddress& address) const noexcept
{
    for (int i = 0; i < count; ++i)
        if (entries[static_cast<std::size_t> (i)].address == address)
            return &entries[static_cast<std::size_t> (i)];

    return nullptr;
}

void MappingTable::eraseAt (int index) noexcept
{
    if (index < 0 || index >= count)
        return;

    // Order is not part of the contract, so the last entry fills the hole. That
    // keeps removal constant-time and leaves no gap for a scan to skip.
    entries[static_cast<std::size_t> (index)] = entries[static_cast<std::size_t> (count - 1)];
    entries[static_cast<std::size_t> (count - 1)] = Mapping {};
    --count;
}

bool MappingTable::removeParameter (int parameterIndex) noexcept
{
    if (parameterIndex < 0)
        return false;

    for (int i = 0; i < count; ++i)
    {
        if (entries[static_cast<std::size_t> (i)].parameterIndex == parameterIndex)
        {
            eraseAt (i);
            return true;
        }
    }

    return false;
}

bool MappingTable::removeAddress (const ControlAddress& address) noexcept
{
    for (int i = 0; i < count; ++i)
    {
        if (entries[static_cast<std::size_t> (i)].address == address)
        {
            eraseAt (i);
            return true;
        }
    }

    return false;
}

void MappingTable::clear() noexcept
{
    entries.fill (Mapping {});
    count = 0;
}

AssignResult MappingTable::assign (const Mapping& mapping) noexcept
{
    if (! mapping.address.isValid())
        return AssignResult::rejectedInvalidAddress;

    if (isReservedController (mapping.address.controller))
        return AssignResult::rejectedReservedController;

    if (mapping.parameterIndex < 0
        || mapping.parameterIndex >= static_cast<int> (params::parameterCount()))
        return AssignResult::rejectedInvalidParameter;

    // The scaling ends are the one part of a mapping that can arrive from the
    // frontend as arbitrary numbers, so they are validated rather than clamped:
    // a non-finite end would propagate into a parameter value, and a silently
    // repaired range would leave the UI and the engine disagreeing about what
    // was set (UI_BINDINGS.md §14).
    if (! std::isfinite (mapping.minimum) || ! std::isfinite (mapping.maximum))
        return AssignResult::rejectedInvalidRange;

    if (mapping.minimum < 0.0f || mapping.minimum > 1.0f
        || mapping.maximum < 0.0f || mapping.maximum > 1.0f)
        return AssignResult::rejectedInvalidRange;

    // Nothing to do, and reported as such: the UI should not show a replacement
    // warning for an assignment that changed nothing.
    if (const auto* existing = findForAddress (mapping.address))
        if (existing->parameterIndex == mapping.parameterIndex
            && existing->minimum == mapping.minimum
            && existing->maximum == mapping.maximum)
            return AssignResult::unchanged;

    // The bijection is enforced by removing both sides before inserting, so the
    // table can never hold a state that violates it, not even transiently.
    const bool releasedParameter = removeParameter (mapping.parameterIndex);
    const bool releasedAddress = removeAddress (mapping.address);

    if (isFull())
    {
        // Unreachable in practice — two removals just ran — but the insert
        // below must not be able to run past the end of the array on any path.
        return AssignResult::rejectedTableFull;
    }

    entries[static_cast<std::size_t> (count)] = mapping;
    ++count;

    if (releasedParameter && releasedAddress)
        return AssignResult::replacedBoth;

    if (releasedParameter)
        return AssignResult::replacedParameterMapping;

    if (releasedAddress)
        return AssignResult::replacedControllerMapping;

    return AssignResult::added;
}

//==============================================================================

bool MappingChannel::publish (const MappingTable& table) noexcept
{
    const auto write = writeCounter.load (std::memory_order_relaxed);
    const auto read = readCounter.load (std::memory_order_acquire);

    // Unsigned subtraction, so the comparison stays correct when the counters
    // wrap.
    if (write - read >= static_cast<unsigned int> (slotCount))
        return false;

    slots[write % static_cast<unsigned int> (slotCount)] = table;

    // Release: the consumer that observes this counter must also observe the
    // slot contents written above it.
    writeCounter.store (write + 1, std::memory_order_release);
    return true;
}

bool MappingChannel::fetch (MappingTable& destination) noexcept
{
    // AUDIO THREAD.
    const auto write = writeCounter.load (std::memory_order_acquire);
    auto read = readCounter.load (std::memory_order_relaxed);

    if (read == write)
        return false;

    // Every pending table is copied, and the last one wins. Copying rather than
    // skipping to the newest is what guarantees the producer is never writing
    // into the slot being read: the slots in flight are exactly [read, write),
    // and the producer will not reach `read` again until this store below moves
    // it on.
    while (read != write)
    {
        destination = slots[read % static_cast<unsigned int> (slotCount)];
        ++read;
    }

    readCounter.store (read, std::memory_order_release);
    return true;
}

} // namespace apollo::midi
