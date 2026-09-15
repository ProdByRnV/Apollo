#pragma once

/*
    Getting a wavetable off a disk and into the instrument without the audio
    thread ever waiting for it.

    THE WORK IS NOT SMALL. Reading a file, decoding it, taking a Fourier
    transform of every one of up to 256 frames and then rendering eleven
    band-limited mip levels of each is tens of milliseconds at best. None of it
    may happen on the audio thread, and none of it may block the message thread
    either, or the interface freezes while somebody picks a file (CLAUDE.md
    §7.3, §30).

    So the shape is the one `PresetLibrary` already uses, and deliberately the
    same one: a worker thread does the work, an async update delivers the result
    to the message thread, and the message thread is the only place that touches
    the library's slots. The audio thread is not involved at any point — it
    finds out that a table changed by reading an atomic counter, which is the
    library's own business (WavetableLibrary.h).

    A FAILURE IS NOT AN ERROR CONDITION, it is a normal outcome. A file can be
    missing, renamed, locked, truncated, not audio at all, or audio of the wrong
    shape. Every one of those leaves the slot playing the table it was playing
    before — Apollo's built-in — and reports a sentence saying what happened.
    The instrument never goes quiet because a file went away (CLAUDE.md §33).

    ONE LOAD AT A TIME. A second request supersedes the first rather than
    queueing behind it: the only thing that starts one is a person choosing a
    file, and a person who chooses twice wants the second one.
*/

#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <memory>

#include "DSP/Oscillators/WavetableLibrary.h"
#include "Resources/WavetableFile.h"

namespace apollo::resources
{

class WavetableLoader final : private juce::Thread,
                              private juce::AsyncUpdater,
                              private juce::Timer
{
public:
    /** @param libraryToUse  the slots to publish into. Must outlive this. */
    explicit WavetableLoader (dsp::WavetableLibrary& libraryToUse);
    ~WavetableLoader() override;

    /** Starts loading @p file into @p slot. Returns immediately.

        Supersedes any load already running. An out-of-range slot is refused
        without starting anything.
    */
    void load (int slot, const juce::File& file);

    /** Puts @p slot back to Apollo's own table, immediately.

        MESSAGE THREAD. There is nothing to load, so there is nothing to wait
        for.
    */
    void restoreBuiltIn (int slot);

    /** Called on the message thread when a load finishes, however it finished.

        @param slot    the slot it was for.
        @param result  `ok`, or why not.
        @param name    the file's name without its path, for saying which one it
                       was. A name rather than a path, because this reaches the
                       interface and a path must not (UI_BINDINGS.md §13).
    */
    std::function<void (int slot, WavetableLoadResult result, juce::String name)> onLoadFinished;

    [[nodiscard]] bool isLoading() const noexcept { return isThreadRunning(); }

    /** Blocks until the load in progress has finished. Tests only. */
    void waitForLoad();

    /** Delivers a finished load now rather than when the message loop next
        runs.

        Exposed for testing, as `PresetLibrary::flushPendingNotification` is and
        for the same reason: the test runner is a console program with no
        message loop, so without this the one thing worth asserting — that a
        finished load reaches the library — could not be observed.
    */
    void flushPendingLoad();

private:
    void run() override;
    void handleAsyncUpdate() override;
    void timerCallback() override;

    /** How often retired tables are swept up, in milliseconds.

        Rarely, because it is bookkeeping: a retired table is a couple of
        megabytes and nothing is waiting on it. Often enough that a session
        spent auditioning wavetables does not accumulate them.
    */
    static constexpr int collectionIntervalMs = 2000;

    dsp::WavetableLibrary& library;

    /** What the worker was asked for. Written before it starts, read by it. */
    juce::File pendingFile;
    int pendingSlot = -1;

    /** What the worker produced. Read by the message thread afterwards. */
    std::unique_ptr<dsp::Wavetable> built;
    WavetableLoadResult outcome = WavetableLoadResult::ok;

    /** True between a finished load and its delivery, so the delivery is state
        rather than only a queued message.
    */
    std::atomic<bool> pendingDelivery { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WavetableLoader)
};

} // namespace apollo::resources
