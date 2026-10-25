#pragma once

/*
    Notes played from the interface: the computer keyboard and the piano strip
    in the standalone (Phase 15, ADR-0111).

    The page cannot call into the engine, and the message thread may not touch
    a voice. So a note the page plays is written here by the message thread and
    read by the audio thread at the start of its next block, where it is handled
    exactly as a note from a MIDI keyboard is: the same handler, the same voice
    allocation, the same everything after.

    One writer and one reader, each owning one index, so neither ever waits for
    the other and nothing is allocated (CLAUDE.md §7.2). Fixed capacity, because
    a queue that grew would have to allocate: 255 events is several seconds of
    the fastest typing anyone can do, and the audio thread empties it every few
    milliseconds. A note that does not fit is refused, not blocked on.
*/

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace apollo::midi
{

class PageNoteQueue
{
public:
    /** One event. A velocity of 0 is a note-off, as on the MIDI wire. */
    struct Event
    {
        std::uint8_t note = 0;
        std::uint8_t velocity = 0;
    };

    /** Slots in the ring; one is always left empty to tell full from empty. */
    static constexpr std::size_t capacity = 256;

    /** Queues a note-on (velocity 1 to 127) or a note-off (velocity 0).

        WRITER ONLY — the message thread. @returns false, and queues nothing,
        for a note or velocity out of range or a full queue.
    */
    bool push (int note, int velocity) noexcept
    {
        if (note < 0 || note > 127 || velocity < 0 || velocity > 127)
            return false;

        const auto write = writeIndex.load (std::memory_order_relaxed);
        const auto next = (write + 1) % capacity;

        if (next == readIndex.load (std::memory_order_acquire))
            return false;

        events[write] = { static_cast<std::uint8_t> (note), static_cast<std::uint8_t> (velocity) };
        writeIndex.store (next, std::memory_order_release);
        return true;
    }

    /** Hands every queued event to @p handler, oldest first, as
        `handler (note, velocity)`, and empties the queue.

        READER ONLY — the audio thread. Bounded: at most capacity - 1 events.
    */
    template <typename Handler>
    void drain (Handler&& handler) noexcept
    {
        auto read = readIndex.load (std::memory_order_relaxed);
        const auto write = writeIndex.load (std::memory_order_acquire);

        while (read != write)
        {
            const auto event = events[read];
            handler (static_cast<int> (event.note), static_cast<int> (event.velocity));
            read = (read + 1) % capacity;
        }

        readIndex.store (read, std::memory_order_release);
    }

private:
    std::array<Event, capacity> events {};
    std::atomic<std::size_t> writeIndex { 0 };
    std::atomic<std::size_t> readIndex { 0 };
};

} // namespace apollo::midi
