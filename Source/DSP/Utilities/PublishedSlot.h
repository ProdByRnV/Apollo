#pragma once

/*
    One immutable object the audio thread reads, replaced from another thread
    without either side waiting (Phase 13g).

    The machinery WavetableLibrary uses for its slots (ADR-0066), for anything
    else that is built off the audio thread and played on it — a user's noise
    sample, so far. The three things that make a replacement safe are the same
    three, for the same reasons (CLAUDE.md §12.4):

      - the pointer is exchanged atomically, so the audio thread reads a whole
        pointer or the previous one;
      - the audio thread is told there is something new to take, by a
        generation counter it checks once a block, because whoever holds the
        old pointer goes on holding it until they are handed another;
      - the object replaced is retired rather than freed, and freed only once
        the audio thread has begun two blocks since.

    THREADS. `publish`, `clear` and `collectRetired` share the retired list, so
    they must all be called from one thread — the message thread, in Apollo.
    `get`, `beginBlock` and `takeGenerationChange` are the audio thread's, and
    none of them allocates, frees or waits.
*/

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace apollo::dsp
{

template <typename Object>
class PublishedSlot
{
public:
    /** Blocks a retired object is kept for: two, because a swap can land in the
        middle of a block that has already taken the old pointer. */
    static constexpr std::uint64_t retirementBlocks = 2;

    PublishedSlot() = default;
    PublishedSlot (const PublishedSlot&) = delete;
    PublishedSlot& operator= (const PublishedSlot&) = delete;

    /** AUDIO THREAD. The object, or null when the slot is empty. */
    [[nodiscard]] const Object* get() const noexcept
    {
        return current.load (std::memory_order_acquire);
    }

    /** AUDIO THREAD. One relaxed increment at the top of every block. */
    void beginBlock() noexcept
    {
        blockCounter.fetch_add (1, std::memory_order_relaxed);
    }

    /** AUDIO THREAD. True once after each publish or clear. */
    [[nodiscard]] bool takeGenerationChange() noexcept
    {
        const auto latest = generation.load (std::memory_order_acquire);

        if (latest == seenByAudio)
            return false;

        seenByAudio = latest;
        return true;
    }

    /** MESSAGE THREAD. Puts @p object in the slot and retires what was there.

        @returns false, doing nothing, for a null object.
    */
    bool publish (std::unique_ptr<Object> object)
    {
        if (object == nullptr)
            return false;

        auto previous = std::move (owned);

        current.store (object.get(), std::memory_order_release);
        owned = std::move (object);

        generation.fetch_add (1, std::memory_order_release);
        retire (std::move (previous));

        return true;
    }

    /** MESSAGE THREAD. Empties the slot, retiring what was there. */
    void clear()
    {
        if (owned == nullptr)
            return;

        auto previous = std::move (owned);

        current.store (nullptr, std::memory_order_release);
        generation.fetch_add (1, std::memory_order_release);

        retire (std::move (previous));
    }

    /** MESSAGE THREAD. Frees retired objects the audio thread can no longer be
        holding. @returns how many. */
    int collectRetired()
    {
        const auto now = blockCounter.load (std::memory_order_relaxed);
        auto freed = 0;

        for (auto entry = retired.begin(); entry != retired.end();)
        {
            if (now < entry->retiredAtBlock + retirementBlocks)
            {
                ++entry;
                continue;
            }

            entry = retired.erase (entry);
            ++freed;
        }

        return freed;
    }

    [[nodiscard]] int getRetiredCount() const noexcept { return static_cast<int> (retired.size()); }

    /** MESSAGE THREAD. True while the slot holds something. */
    [[nodiscard]] bool isLoaded() const noexcept { return owned != nullptr; }

private:
    struct Retired
    {
        std::unique_ptr<Object> object;
        std::uint64_t retiredAtBlock = 0;
    };

    void retire (std::unique_ptr<Object> previous)
    {
        if (previous != nullptr)
            retired.push_back ({ std::move (previous), blockCounter.load (std::memory_order_relaxed) });
    }

    std::atomic<const Object*> current { nullptr };
    std::unique_ptr<Object> owned;
    std::vector<Retired> retired;

    std::atomic<std::uint64_t> blockCounter { 0 };
    std::atomic<std::uint32_t> generation { 0 };

    /** Audio thread only. */
    std::uint32_t seenByAudio = 0;
};

} // namespace apollo::dsp
