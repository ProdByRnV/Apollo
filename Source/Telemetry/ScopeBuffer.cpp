#include "Telemetry/ScopeBuffer.h"

namespace apollo::telemetry
{

namespace
{
constexpr unsigned int wrapMask = static_cast<unsigned int> (scopeBufferSize) - 1u;
}

ScopeBuffer::ScopeBuffer()
{
    reset();
}

void ScopeBuffer::reset() noexcept
{
    for (auto& sample : samples)
        sample.store (0.0f, std::memory_order_relaxed);

    writePosition.store (0, std::memory_order_release);
    active.store (false, std::memory_order_release);
}

void ScopeBuffer::write (const float* source, int numSamples) noexcept
{
    // AUDIO THREAD.
    if (source == nullptr || numSamples <= 0)
        return;

    auto position = writePosition.load (std::memory_order_relaxed);

    for (int i = 0; i < numSamples; ++i)
    {
        samples[position & wrapMask].store (source[i], std::memory_order_relaxed);
        ++position;
    }

    // Release, and published last: a reader that observes this position is
    // guaranteed to observe the samples written before it.
    writePosition.store (position, std::memory_order_release);
    active.store (true, std::memory_order_relaxed);
}

void ScopeBuffer::writeMixedToMono (const float* const* channels, int numChannels,
                                    int startSample, int numSamples) noexcept
{
    // AUDIO THREAD.
    if (channels == nullptr || numChannels <= 0 || numSamples <= 0)
        return;

    const auto scale = 1.0f / static_cast<float> (numChannels);

    auto position = writePosition.load (std::memory_order_relaxed);

    for (int i = 0; i < numSamples; ++i)
    {
        float sum = 0.0f;

        for (int channel = 0; channel < numChannels; ++channel)
            if (channels[channel] != nullptr)
                sum += channels[channel][startSample + i];

        samples[position & wrapMask].store (sum * scale, std::memory_order_relaxed);
        ++position;
    }

    writePosition.store (position, std::memory_order_release);
    active.store (true, std::memory_order_relaxed);
}

void ScopeBuffer::writeSilence (int numSamples) noexcept
{
    // AUDIO THREAD.
    if (numSamples <= 0)
        return;

    auto position = writePosition.load (std::memory_order_relaxed);

    for (int i = 0; i < numSamples; ++i)
    {
        samples[position & wrapMask].store (0.0f, std::memory_order_relaxed);
        ++position;
    }

    writePosition.store (position, std::memory_order_release);
    active.store (true, std::memory_order_relaxed);
}

bool ScopeBuffer::readWindow (float* destination, int count) const noexcept
{
    // MESSAGE THREAD.
    if (destination == nullptr || count <= 0)
        return false;

    if (! active.load (std::memory_order_acquire))
        return false;

    if (count > scopeBufferSize)
        count = scopeBufferSize;

    // Acquire, to pair with the writer's release: everything written before the
    // position it published is visible here.
    const auto published = writePosition.load (std::memory_order_acquire);

    // The window ends a margin behind the writer, so the writer is a whole ring
    // away from the samples being copied rather than just behind them.
    const auto end = published - static_cast<unsigned int> (readMargin);
    auto position = end - static_cast<unsigned int> (count);

    for (int i = 0; i < count; ++i)
    {
        destination[i] = samples[position & wrapMask].load (std::memory_order_relaxed);
        ++position;
    }

    return true;
}

} // namespace apollo::telemetry
