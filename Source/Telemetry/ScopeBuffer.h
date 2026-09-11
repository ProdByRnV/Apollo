#pragma once

/*
    The visualisation tap: how audio gets from the audio thread to a picture.

    A scope has to show what the audio thread just produced, and the audio thread
    may not wait for anything (CLAUDE.md §7.1). So the transport is one-way and
    lock-free: the audio thread writes samples into a preallocated ring and moves
    an index; the message thread reads the most recent window *behind* that index
    and never tells the writer anything.

    THE SAMPLES ARE `std::atomic<float>`, which looks heavy-handed and is not. On
    every architecture Apollo targets a relaxed load or store of a four-byte
    atomic compiles to the same instruction a plain one would, so it costs
    nothing at run time; what it buys is that a reader observing a sample the
    writer is overwriting is *defined* to see one value or the other, rather than
    being a data race. A scope can survive a stale sample. It cannot survive
    undefined behaviour.

    READING BEHIND THE WRITER is what makes tearing rare rather than merely
    harmless. The reader takes the window ending a little before the write
    position, so the writer is working an entire ring-length away from what is
    being copied. At 48 kHz the ring holds about 170 ms and the interface reads
    every 33 ms, so the margin is large; if it were ever exhausted the result
    would be a visibly wrong frame and nothing worse.

    JUCE-free, so the whole transport can be tested with no host, no device and
    no message loop — which is the only way to test a thing whose entire purpose
    is to be read from another thread.
*/

#include <array>
#include <atomic>
#include <cstddef>

namespace apollo::telemetry
{

/** Samples held per source.

    Sized in powers of two so the wrap is a mask rather than a division, and
    large enough to hold several cycles of anything a scope is useful for: 8192
    samples is 170 ms at 48 kHz, or about eight cycles of a 50 Hz note.
*/
inline constexpr int scopeBufferSize = 8192;

static_assert ((scopeBufferSize & (scopeBufferSize - 1)) == 0,
               "scopeBufferSize must be a power of two for the wrap to be a mask");

/** Points in one frame sent to the interface.

    The window is decimated to this many before it leaves the native side. A
    scope is a few hundred pixels wide, so sending a sample per pixel is the most
    that can be drawn and far less than the window holds — and the difference is
    the difference between a few hundred bytes per frame and thirty kilobytes
    (UI_BINDINGS.md §12).
*/
inline constexpr int scopeFramePoints = 192;

/** Below this peak a frame is reported as silent.

    A source that has stopped must read as stopped rather than holding its last
    picture (CLAUDE.md §26.1). -100 dBFS is inaudible by any measure and well
    above the denormal floor, so nothing that is actually sounding trips it.
*/
inline constexpr float scopeSilenceThreshold = 1.0e-5f;

//==============================================================================

/** One source's capture ring. */
class ScopeBuffer
{
public:
    ScopeBuffer();

    /** AUDIO THREAD. Appends @p numSamples mono samples.

        Lock-free, allocation-free, and linear in the number of samples — the
        same work the block itself already costs, once more.
    */
    void write (const float* source, int numSamples) noexcept;

    /** AUDIO THREAD. Appends a block mixed down to mono.

        Channels are averaged rather than summed, so a centred signal reads at
        the level it actually plays at instead of twice it.
    */
    void writeMixedToMono (const float* const* channels, int numChannels, int startSample,
                           int numSamples) noexcept;

    /** AUDIO THREAD. Appends @p numSamples of silence.

        Called by a source that produced nothing this block, so that a source
        which stops is *seen* to stop rather than holding its last picture. A
        source that is simply not running writes nothing at all and reports as
        inactive instead.
    */
    void writeSilence (int numSamples) noexcept;

    /** MESSAGE THREAD. Copies the most recent window into @p destination.

        @param destination  at least `count` floats.
        @param count        samples wanted, clamped to the ring size.
        @returns            false if the source has never been written, in which
                            case @p destination is left alone.
    */
    [[nodiscard]] bool readWindow (float* destination, int count) const noexcept;

    /** True once anything has been written.

        What separates "this source is silent" from "this source does not exist
        in this build yet", which the interface must not draw the same way.
    */
    [[nodiscard]] bool isActive() const noexcept
    {
        return active.load (std::memory_order_relaxed);
    }

    /** Forgets everything written, and marks the source inactive again. */
    void reset() noexcept;

    /** How far behind the write position a read begins.

        One block at the largest size Apollo prepares for, which is what the
        writer can have advanced by since the reader looked. Small next to the
        ring, so it costs almost none of the visible window.
    */
    static constexpr int readMargin = 512;

private:
    std::array<std::atomic<float>, static_cast<std::size_t> (scopeBufferSize)> samples;

    /** Free-running count of samples written. The ring position is this modulo
        the size, which for a power of two is a mask.
    */
    std::atomic<unsigned int> writePosition { 0 };

    std::atomic<bool> active { false };
};

} // namespace apollo::telemetry
