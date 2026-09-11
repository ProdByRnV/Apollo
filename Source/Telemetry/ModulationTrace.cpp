#include "Telemetry/ModulationTrace.h"

namespace apollo::telemetry
{

namespace
{
constexpr unsigned int wrapMask = static_cast<unsigned int> (modulationTracePoints) - 1u;
}

ModulationTrace::ModulationTrace()
{
    reset();
}

void ModulationTrace::reset() noexcept
{
    for (auto& value : values)
        value.store (0.0f, std::memory_order_relaxed);

    writePosition.store (0, std::memory_order_release);
    active.store (false, std::memory_order_release);
}

void ModulationTrace::write (float value) noexcept
{
    // AUDIO THREAD.
    const auto position = writePosition.load (std::memory_order_relaxed);

    values[position & wrapMask].store (value, std::memory_order_relaxed);

    // Released last, so a reader that observes this position observes the value
    // written before it.
    writePosition.store (position + 1u, std::memory_order_release);
    active.store (true, std::memory_order_relaxed);
}

bool ModulationTrace::read (float* destination) const noexcept
{
    // MESSAGE THREAD.
    if (destination == nullptr)
        return false;

    if (! active.load (std::memory_order_acquire))
        return false;

    const auto published = writePosition.load (std::memory_order_acquire);

    // The whole ring, oldest first: the next entry the writer will overwrite is
    // the oldest one still present.
    auto position = published;

    for (int i = 0; i < modulationTracePoints; ++i)
    {
        destination[i] = values[position & wrapMask].load (std::memory_order_relaxed);
        ++position;
    }

    return true;
}

} // namespace apollo::telemetry
