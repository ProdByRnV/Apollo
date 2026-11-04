#include "Resources/UserContent.h"

#include "DSP/Noise/NoiseSource.h"
#include "DSP/Oscillators/ExtraTables.h"
#include "DSP/Oscillators/WavetableBuilder.h"
#include "Resources/EmbeddedAudio.h"
#include "Resources/NoiseSampleFile.h"
#include "Resources/WavetableFile.h"

namespace apollo::resources
{

juce::String toToken (ContentSlot slot)
{
    switch (slot)
    {
        case ContentSlot::osc1:  return "osc1";
        case ContentSlot::osc2:  return "osc2";
        case ContentSlot::noise: return "noise";
    }

    return "osc1";
}

bool slotFromToken (const juce::String& token, ContentSlot& slot)
{
    for (const auto candidate : { ContentSlot::osc1, ContentSlot::osc2, ContentSlot::noise })
    {
        if (token == toToken (candidate))
        {
            slot = candidate;
            return true;
        }
    }

    return false;
}

juce::String describe (ContentSlot slot)
{
    switch (slot)
    {
        case ContentSlot::osc1:  return "Oscillator 1";
        case ContentSlot::osc2:  return "Oscillator 2";
        case ContentSlot::noise: return "Noise";
    }

    return "Oscillator 1";
}

juce::String toToken (ContentOrigin origin)
{
    switch (origin)
    {
        case ContentOrigin::library:        return "library";
        case ContentOrigin::factoryLibrary: return "factory";
        case ContentOrigin::file:           return "file";
        case ContentOrigin::builtIn:        return "builtin";
    }

    return "file";
}

namespace
{

[[nodiscard]] ContentOrigin originFromToken (const juce::String& token)
{
    for (const auto origin : { ContentOrigin::library, ContentOrigin::factoryLibrary, ContentOrigin::file,
                               ContentOrigin::builtIn })
        if (token == toToken (origin))
            return origin;

    return ContentOrigin::file;
}

/** A name or library path from a document, made safe to show: bounded, and
    without anything that would let a path climb out of a library — although
    nothing opens it, a stored path should not even look like one that could. */
[[nodiscard]] juce::String cleanText (const juce::String& text, int maximumLength)
{
    auto cleaned = text.substring (0, maximumLength).removeCharacters ("\r\n\t").trim();

    if (cleaned.contains (".."))
        cleaned = cleaned.replace ("..", "");

    return cleaned;
}

/** One of Apollo's extra tables as the audio a patch carries: built, and its
    top mip level — every harmonic, normalised — written out frame by frame.

    Through audio rather than straight into a slot, so that loading one and
    reopening a patch that carries one are the same path, and play the same
    table. */
[[nodiscard]] juce::String renderExtraTable (int index)
{
    static_assert (dsp::Wavetable::samplesAtLevel (0) == wavetableFrameSamples,
                   "a frame of the table must be a frame of a wavetable file");

    dsp::Wavetable table;
    dsp::buildWavetable (table, dsp::makeExtraTable (index));

    if (table.isEmpty())
        return {};

    std::vector<double> joined;
    joined.reserve (static_cast<std::size_t> (table.getNumFrames() * wavetableFrameSamples));

    for (int frame = 0; frame < table.getNumFrames(); ++frame)
        if (const auto* samples = table.getReadPointer (0, frame))
            joined.insert (joined.end(), samples, samples + wavetableFrameSamples);

    return encodeAudio ({ joined }, 48000.0);
}

/** The name a file is shown as. */
[[nodiscard]] juce::String nameOf (const juce::File& file)
{
    return file.getFileNameWithoutExtension().substring (0, UserContent::maximumNameLength);
}

} // namespace

//==============================================================================

UserContent::UserContent (juce::AudioProcessorValueTreeState& stateToUse,
                          dsp::WavetableLibrary& tablesToUse,
                          dsp::PublishedSlot<dsp::TransientSample>& noiseSampleToUse)
    : juce::Thread ("Apollo content loader"),
      apvts (stateToUse),
      tables (tablesToUse),
      noiseSample (noiseSampleToUse)
{
    startThread();

    // Freeing what the audio thread has moved past. Bookkeeping: a retired
    // table is a couple of megabytes and nothing waits on it.
    startTimer (2000);
}

UserContent::~UserContent()
{
    stopTimer();

    // Cancelled before the thread is stopped: a delivery that arrived while
    // this object was dying would call into half of one.
    cancelPendingUpdate();

    signalThreadShouldExit();
    notify();
    stopThread (4000);
}

//==============================================================================
// Requests.

void UserContent::loadFile (ContentSlot slot, const juce::File& file, const ContentSource& source)
{
    Job job;
    job.slot = slot;
    job.serial = latest[indexOf (slot)].fetch_add (1) + 1;
    job.file = file;
    job.source = source;

    if (job.source.name.isEmpty())
        job.source.name = nameOf (file);

    {
        const std::lock_guard<std::mutex> guard (lock);
        info[indexOf (slot)].loading = true;
        info[indexOf (slot)].loadingName = job.source.name;
    }

    if (onChanged)
        onChanged ({ slot, "LOADING", "Loading " + job.source.name + "." });

    queueJob (std::move (job));
}

bool UserContent::loadExtraTable (ContentSlot slot, int table)
{
    if (! isWavetableSlot (slot) || table < 0 || table >= dsp::numExtraTables)
        return false;

    Job job;
    job.slot = slot;
    job.serial = latest[indexOf (slot)].fetch_add (1) + 1;
    job.extraTable = table;
    job.source.name = dsp::extraTableName (table);
    job.source.origin = ContentOrigin::builtIn;

    {
        const std::lock_guard<std::mutex> guard (lock);
        info[indexOf (slot)].loading = true;
        info[indexOf (slot)].loadingName = job.source.name;
    }

    if (onChanged)
        onChanged ({ slot, "LOADING", "Loading " + job.source.name + "." });

    queueJob (std::move (job));
    return true;
}

void UserContent::clear (ContentSlot slot)
{
    // Abandons anything on its way to this slot.
    latest[indexOf (slot)].fetch_add (1);

    empty (slot);
    removeItem (slot);

    if (onChanged)
        onChanged ({ slot, "CLEARED", describe (slot) + (isWavetableSlot (slot)
                                                         ? " is back on Apollo's own table."
                                                         : " has no sample now.") });
}

void UserContent::restoreFromState()
{
    const auto content = apvts.state.getChildWithName (juce::Identifier (contentTreeType));

    for (const auto slot : { ContentSlot::osc1, ContentSlot::osc2, ContentSlot::noise })
    {
        const auto serial = latest[indexOf (slot)].fetch_add (1) + 1;

        juce::ValueTree item;

        for (int i = 0; i < content.getNumChildren(); ++i)
        {
            const auto child = content.getChild (i);

            if (child.hasType (juce::Identifier (itemTreeType))
                && child.getProperty (juce::Identifier (slotProperty)).toString() == toToken (slot))
            {
                item = child;
                break;
            }
        }

        const auto audio = item.isValid() ? item.getProperty (juce::Identifier (audioProperty)).toString()
                                          : juce::String();

        if (audio.isEmpty())
        {
            Result cleared;
            cleared.slot = slot;
            cleared.serial = serial;
            cleared.kind = Result::Kind::cleared;
            cleared.fromDocument = true;
            queueResult (std::move (cleared));
            continue;
        }

        Job job;
        job.slot = slot;
        job.serial = serial;
        job.fromDocument = true;
        job.audio = audio;
        job.source.name = cleanText (item.getProperty (juce::Identifier (nameProperty)).toString(), maximumNameLength);
        job.source.origin = originFromToken (item.getProperty (juce::Identifier (originProperty)).toString());
        job.source.libraryPath = cleanText (item.getProperty (juce::Identifier (pathProperty)).toString(), maximumPathLength);

        if (job.source.origin == ContentOrigin::file || job.source.origin == ContentOrigin::builtIn)
            job.source.libraryPath = {};

        {
            const std::lock_guard<std::mutex> guard (lock);
            auto& slotInfo = info[indexOf (slot)];

            // The same audio as is playing: a host restoring the state it was
            // just given. Nothing to rebuild; the serial bump above has already
            // abandoned anything older that was on its way.
            if (slotInfo.loaded && playing[indexOf (slot)] == audio)
            {
                slotInfo.loading = false;
                slotInfo.source = job.source;
                continue;
            }

            slotInfo.loading = true;
            slotInfo.loadingName = job.source.name;
        }

        queueJob (std::move (job));
    }

    triggerAsyncUpdate();
}

ContentSlotInfo UserContent::getInfo (ContentSlot slot) const
{
    const std::lock_guard<std::mutex> guard (lock);
    return info[indexOf (slot)];
}

void UserContent::adoptSource (ContentSlot slot, const ContentSource& source)
{
    {
        const std::lock_guard<std::mutex> guard (lock);
        auto& slotInfo = info[indexOf (slot)];

        if (! slotInfo.loaded)
            return;

        slotInfo.source = source;
    }

    auto content = apvts.state.getChildWithName (juce::Identifier (contentTreeType));

    for (int i = 0; i < content.getNumChildren(); ++i)
    {
        auto item = content.getChild (i);

        if (item.getProperty (juce::Identifier (slotProperty)).toString() != toToken (slot))
            continue;

        item.setProperty (juce::Identifier (nameProperty), source.name, nullptr);
        item.setProperty (juce::Identifier (originProperty), toToken (source.origin), nullptr);

        if (source.libraryPath.isNotEmpty())
            item.setProperty (juce::Identifier (pathProperty), source.libraryPath, nullptr);
        else
            item.removeProperty (juce::Identifier (pathProperty), nullptr);
    }
}

juce::String UserContent::getStoredAudio (ContentSlot slot) const
{
    const std::lock_guard<std::mutex> guard (lock);
    return playing[indexOf (slot)];
}

void UserContent::queueJob (Job job)
{
    {
        const std::lock_guard<std::mutex> guard (lock);
        jobs.push_back (std::move (job));
    }

    notify();
}

void UserContent::queueResult (Result result)
{
    {
        const std::lock_guard<std::mutex> guard (lock);
        results.push_back (std::move (result));
    }

    triggerAsyncUpdate();
}

//==============================================================================
// The worker.

void UserContent::run()
{
    while (! threadShouldExit())
    {
        Job job;

        {
            const std::lock_guard<std::mutex> guard (lock);

            if (jobs.empty())
            {
                working = false;
            }
            else
            {
                job = std::move (jobs.front());
                jobs.pop_front();
                working = true;
            }
        }

        if (! working)
        {
            wait (-1);
            continue;
        }

        // Superseded before it started: somebody chose again, or the state
        // was replaced. Not worth the work.
        if (job.serial != latest[indexOf (job.slot)].load())
            continue;

        auto result = process (job);

        if (threadShouldExit())
            return;

        queueResult (std::move (result));
    }
}

UserContent::Result UserContent::process (const Job& job) const
{
    Result result;
    result.slot = job.slot;
    result.serial = job.serial;
    result.fromDocument = job.fromDocument;
    result.source = job.source;
    result.kind = Result::Kind::failed;

    const auto unreadableInDocument = [&job]
    {
        if (! isWavetableSlot (job.slot))
            return juce::String ("The noise sample stored in this patch could not be read, so the noise is "
                                 "playing white instead.");

        return "The wavetable stored in this patch for " + describe (job.slot).toLowerCase()
             + " could not be read, so it is playing Apollo's own table instead.";
    };

    // Audio to decode: what a document carried, or one of Apollo's extra
    // tables rendered as the audio a patch would carry. A file is read from
    // disk instead.
    auto audioText = job.audio;

    if (job.extraTable >= 0)
    {
        audioText = renderExtraTable (job.extraTable);

        if (audioText.isEmpty())
        {
            result.failure = job.source.name + ": the table could not be built.";
            return result;
        }
    }

    std::unique_ptr<juce::AudioFormatReader> embedded;

    if (audioText.isNotEmpty())
    {
        embedded = decodeAudio (audioText);

        if (embedded == nullptr)
        {
            result.failure = job.fromDocument ? unreadableInDocument()
                                              : job.source.name + ": the table could not be built.";
            return result;
        }
    }

    if (isWavetableSlot (job.slot))
    {
        WavetableFrames frames;

        const auto read = embedded != nullptr ? readWavetable (*embedded, frames)
                                              : readWavetableFile (job.file, frames);

        if (read != WavetableLoadResult::ok)
        {
            result.failure = job.fromDocument ? unreadableInDocument()
                                              : job.source.name + ": " + resources::describe (read);
            return result;
        }

        if (threadShouldExit())
            return result;

        auto table = std::make_unique<dsp::Wavetable>();
        dsp::buildWavetable (*table, dsp::analyseFrames (frames, dsp::Wavetable::topLevelHarmonics));

        if (table->isEmpty())
        {
            result.failure = job.fromDocument ? unreadableInDocument()
                                              : job.source.name + ": " + resources::describe (WavetableLoadResult::empty);
            return result;
        }

        if (embedded != nullptr)
        {
            result.audio = audioText;
        }
        else
        {
            // Stored as one channel of frames end to end: the file's own shape.
            std::vector<double> joined;
            joined.reserve (frames.size() * static_cast<std::size_t> (wavetableFrameSamples));

            for (const auto& frame : frames)
                joined.insert (joined.end(), frame.begin(), frame.end());

            result.audio = encodeAudio ({ joined }, 48000.0);

            if (result.audio.isEmpty())
            {
                result.failure = job.source.name + ": the wavetable could not be stored in the patch.";
                return result;
            }
        }

        result.frames = static_cast<int> (frames.size());
        result.table = std::move (table);
        result.kind = Result::Kind::built;
        return result;
    }

    NoiseSampleAudio audio;

    const auto read = embedded != nullptr ? readNoiseSample (*embedded, audio)
                                          : readNoiseSampleFile (job.file, audio);

    if (read != NoiseSampleLoadResult::ok)
    {
        result.failure = job.fromDocument ? unreadableInDocument()
                                          : job.source.name + ": " + resources::describe (read);
        return result;
    }

    if (threadShouldExit())
        return result;

    result.sample = std::make_unique<dsp::TransientSample> (dsp::makeTransient (audio.left, audio.right));

    if (embedded != nullptr)
    {
        result.audio = audioText;
    }
    else
    {
        result.audio = encodeAudio ({ audio.left, audio.right }, noiseSampleRate);

        if (result.audio.isEmpty())
        {
            result.failure = job.source.name + ": the sample could not be stored in the patch.";
            return result;
        }
    }

    result.seconds = audio.seconds();
    result.kind = Result::Kind::built;
    return result;
}

//==============================================================================
// Delivery, on the message thread.

void UserContent::handleAsyncUpdate()
{
    flushDeliveries();
}

void UserContent::flushDeliveries()
{
    std::vector<Result> ready;

    {
        const std::lock_guard<std::mutex> guard (lock);
        ready.swap (results);
    }

    for (auto& result : ready)
        deliver (result);
}

void UserContent::deliver (Result& result)
{
    // Superseded while it was being built: dropped, and the table or sample
    // it made with it.
    if (result.serial != latest[indexOf (result.slot)].load())
        return;

    const auto slot = result.slot;

    switch (result.kind)
    {
        case Result::Kind::cleared:
        {
            const auto wasLoaded = getInfo (slot).loaded;
            empty (slot);

            if (wasLoaded && onChanged)
                onChanged ({ slot, "CLEARED", describe (slot) + " has none of your content in this patch." });

            return;
        }

        case Result::Kind::failed:
        {
            if (result.fromDocument)
            {
                // The document's audio is unusable. The slot plays its
                // fallback and the patch stops carrying what cannot be played.
                empty (slot);
                removeItem (slot);
            }
            else
            {
                // A file that cannot be used changes nothing at all.
                const std::lock_guard<std::mutex> guard (lock);
                info[indexOf (slot)].loading = false;
            }

            if (! result.fromDocument && onLoadFinished)
                onLoadFinished (slot, false);

            if (onChanged)
                onChanged ({ slot, result.fromDocument ? "RESTORE_FAILED" : "LOAD_FAILED", result.failure });

            return;
        }

        case Result::Kind::built:
            break;
    }

    // The swap the audio thread will notice at its next block, and the
    // retirement that keeps whatever it held alive until it has.
    if (isWavetableSlot (slot))
        (void) tables.publish (dsp::WavetableLibrary::userSlot (slot == ContentSlot::osc2 ? 2 : 1),
                               std::move (result.table));
    else
        (void) noiseSample.publish (std::move (result.sample));

    {
        const std::lock_guard<std::mutex> guard (lock);
        auto& slotInfo = info[indexOf (slot)];

        slotInfo.loaded = true;
        slotInfo.loading = false;
        slotInfo.source = result.source;
        slotInfo.frames = result.frames;
        slotInfo.seconds = result.seconds;
        playing[indexOf (slot)] = result.audio;
    }

    // A restore says only that it finished, so a browser can show the name
    // of what the project brought with it.
    if (result.fromDocument)
    {
        if (onChanged)
            onChanged ({ slot, "RESTORED", {} });

        return;
    }

    writeItem (result);
    select (slot);

    if (onLoadFinished)
        onLoadFinished (slot, true);

    if (onChanged)
        onChanged ({ slot, "LOADED", isWavetableSlot (slot)
                                         ? result.source.name + " is playing on " + describe (slot).toLowerCase() + "."
                                         : result.source.name + " is the noise sample now." });
}

void UserContent::empty (ContentSlot slot)
{
    if (isWavetableSlot (slot))
        tables.restoreBuiltIn (dsp::WavetableLibrary::userSlot (slot == ContentSlot::osc2 ? 2 : 1));
    else
        noiseSample.clear();

    const std::lock_guard<std::mutex> guard (lock);
    info[indexOf (slot)] = {};
    playing[indexOf (slot)] = {};
}

void UserContent::writeItem (const Result& result)
{
    // Removed first: removing the last item takes the element away with it, so
    // the element has to be fetched after, or the item would be added to a
    // tree that is no longer in the state.
    removeItem (result.slot);

    auto content = apvts.state.getOrCreateChildWithName (juce::Identifier (contentTreeType), nullptr);

    juce::ValueTree item { juce::Identifier (itemTreeType) };
    item.setProperty (juce::Identifier (slotProperty), toToken (result.slot), nullptr);
    item.setProperty (juce::Identifier (nameProperty), result.source.name, nullptr);
    item.setProperty (juce::Identifier (originProperty), toToken (result.source.origin), nullptr);

    if (result.source.libraryPath.isNotEmpty())
        item.setProperty (juce::Identifier (pathProperty), result.source.libraryPath, nullptr);

    if (isWavetableSlot (result.slot))
        item.setProperty (juce::Identifier (framesProperty), result.frames, nullptr);
    else
        item.setProperty (juce::Identifier (secondsProperty), result.seconds, nullptr);

    item.setProperty (juce::Identifier (formatProperty), embeddedAudioFormatVersion, nullptr);
    item.setProperty (juce::Identifier (audioProperty), result.audio, nullptr);

    content.appendChild (item, nullptr);
}

void UserContent::removeItem (ContentSlot slot)
{
    auto content = apvts.state.getChildWithName (juce::Identifier (contentTreeType));

    if (! content.isValid())
        return;

    for (int i = content.getNumChildren(); --i >= 0;)
        if (content.getChild (i).getProperty (juce::Identifier (slotProperty)).toString() == toToken (slot))
            content.removeChild (i, nullptr);

    // An empty element is not written at all, so a patch with no content saves
    // the document it saved before content existed.
    if (content.getNumChildren() == 0)
        apvts.state.removeChild (content, nullptr);
}

void UserContent::select (ContentSlot slot)
{
    const auto* id = selectionParameterOf (slot);

    const auto plain = slot == ContentSlot::noise ? static_cast<float> (dsp::NoiseType::sample)
                                                  : static_cast<float> (dsp::WavetableLibrary::userChoice);

    if (auto* parameter = apvts.getParameter (id))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
        parameter->endChangeGesture();
    }
}

void UserContent::timerCallback()
{
    (void) tables.collectRetired();
    (void) noiseSample.collectRetired();
}

void UserContent::waitUntilIdle()
{
    for (;;)
    {
        {
            const std::lock_guard<std::mutex> guard (lock);

            if (jobs.empty() && ! working)
                return;
        }

        juce::Thread::sleep (1);
    }
}

} // namespace apollo::resources
