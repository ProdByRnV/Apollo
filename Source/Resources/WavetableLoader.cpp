#include "Resources/WavetableLoader.h"

#include "DSP/Oscillators/WavetableBuilder.h"

namespace apollo::resources
{

WavetableLoader::WavetableLoader (dsp::WavetableLibrary& libraryToUse)
    : juce::Thread ("Apollo wavetable loader"),
      library (libraryToUse)
{
    startTimer (collectionIntervalMs);
}

WavetableLoader::~WavetableLoader()
{
    stopTimer();

    // Cancelled before the thread is stopped: a delivery that arrived after
    // this object began dying would call into a half-destroyed one.
    cancelPendingUpdate();

    stopThread (2000);
}

void WavetableLoader::load (int slot, const juce::File& file)
{
    if (slot < 0 || slot >= dsp::WavetableLibrary::numTables)
        return;

    // Supersedes rather than queues. The only thing that starts a load is
    // somebody choosing a file, and somebody who chooses twice wants the
    // second one.
    stopThread (2000);

    pendingSlot = slot;
    pendingFile = file;
    built.reset();
    outcome = WavetableLoadResult::ok;

    startThread();
}

void WavetableLoader::restoreBuiltIn (int slot)
{
    library.restoreBuiltIn (slot);
}

void WavetableLoader::run()
{
    WavetableFrames frames;

    auto result = readWavetableFile (pendingFile, frames);

    std::unique_ptr<dsp::Wavetable> table;

    if (result == WavetableLoadResult::ok)
    {
        if (threadShouldExit())
            return;

        // The two steps that cost the time, and the reason this is on a thread
        // at all: a transform per frame, then eleven band-limited renders per
        // frame. Both are the same code the built-in tables go through.
        const auto spectrum = dsp::analyseFrames (frames, dsp::Wavetable::topLevelHarmonics);

        if (threadShouldExit())
            return;

        table = std::make_unique<dsp::Wavetable>();

        dsp::buildWavetable (*table, spectrum);

        // A file that read cleanly but produced nothing playable is refused
        // here rather than published as silence.
        if (table->isEmpty())
        {
            table.reset();
            result = WavetableLoadResult::empty;
        }
    }

    if (threadShouldExit())
        return;

    built = std::move (table);
    outcome = result;

    // State, not just a message: the console test runner has no message loop,
    // so a delivery that existed only as a queued call could never be observed
    // (PresetLibrary does the same, for the same reason).
    pendingDelivery.store (true, std::memory_order_release);

    triggerAsyncUpdate();
}

void WavetableLoader::handleAsyncUpdate()
{
    flushPendingLoad();
}

void WavetableLoader::flushPendingLoad()
{
    if (! pendingDelivery.exchange (false, std::memory_order_acq_rel))
        return;

    const auto slot = pendingSlot;
    const auto result = outcome;
    const auto name = pendingFile.getFileName();

    if (result == WavetableLoadResult::ok && built != nullptr)
    {
        // Publishing is the message thread's job: it shares the retired list
        // with the sweep below, and two threads editing that would be a race
        // where a use-after-free is the symptom (WavetableLibrary.h).
        if (! library.publish (slot, std::move (built)))
        {
            // The only way this refuses a table that built cleanly is a slot
            // that went out of range, which cannot happen from `load`. Saying
            // so beats publishing nothing and reporting success.
            library.restoreBuiltIn (slot);
        }
    }

    built.reset();

    if (onLoadFinished)
        onLoadFinished (slot, result, name);
}

void WavetableLoader::timerCallback()
{
    // The only regular work: freeing tables the audio thread has moved past.
    // Costs nothing when there are none, which is almost always.
    (void) library.collectRetired();
}

void WavetableLoader::waitForLoad()
{
    while (isThreadRunning())
        juce::Thread::sleep (1);
}

} // namespace apollo::resources
