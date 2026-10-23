#pragma once

/*
    The user's own content in a patch: a wavetable for each oscillator and a
    noise sample (Phase 13g, ADR-0104).

    THREE SLOTS. Each oscillator has a table of its own, played when its
    wavetable choice is USER, and the noise generator a sample, played when its
    type is SAMPLE (WavetableLibrary.h, NoiseSource.h). An empty slot sounds:
    an oscillator plays the first built-in table, the noise plays white.

    THE AUDIO TRAVELS WITH THE PATCH. Whatever is loaded is stored in the state
    tree as audio (EmbeddedAudio.h), beside the parameters, as `<CONTENT>`:

        <CONTENT>
          <ITEM slot="osc1" name="Growl" origin="library" path="Basses/Growl.wav"
                frames="64" format="1" audio="...24-bit FLAC, base64..."/>
        </CONTENT>

    So a host project and a preset both carry it, a preset load replaces it,
    and a document without it empties every slot — a preset is a complete
    sound. Where it came from is kept beside the audio for the browser to show,
    and is never used to open anything: `path` is relative to a library and
    only ever compared with one.

    LOADING NEVER WAITS. Reading a file, analysing and rendering a table, or
    building a sample's mipmap takes tens to hundreds of milliseconds, so every
    load is a job for one worker thread (CLAUDE.md §7.3, §12.4). A newer request
    for a slot supersedes an older one, by a serial number per slot, so a stale
    result cannot land. Everything that touches the engine's slots or the state
    tree happens in one delivery, on the message thread.

    A FAILURE LEAVES THE INSTRUMENT PLAYING (CLAUDE.md §30, §33). A file that
    cannot be used changes nothing, and is reported by name. Audio in a document
    that cannot be read empties its slot — which still sounds — and is reported
    too.

    THREADS. `loadFile`, `clear` and the delivery run on the message thread.
    `restoreFromState` may be called from whatever thread a host loads state
    on: it only reads the tree and queues work. The audio thread never sees
    this class.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "DSP/Noise/TransientLibrary.h"
#include "DSP/Oscillators/WavetableLibrary.h"
#include "DSP/Utilities/PublishedSlot.h"

namespace apollo::resources
{

/** Where user content can go. */
enum class ContentSlot
{
    osc1 = 0,
    osc2,
    noise
};

inline constexpr int numContentSlots = 3;

[[nodiscard]] constexpr bool isWavetableSlot (ContentSlot slot) noexcept { return slot != ContentSlot::noise; }

/** "osc1", "osc2", "noise": the slot's name on the wire and in the document. */
[[nodiscard]] juce::String toToken (ContentSlot slot);

/** @returns false, leaving @p slot alone, for anything that is not a slot's token. */
[[nodiscard]] bool slotFromToken (const juce::String& token, ContentSlot& slot);

/** "Oscillator 1", "Oscillator 2", "Noise": the slot in a sentence. */
[[nodiscard]] juce::String describe (ContentSlot slot);

/** Where content came from. Shown, and used to find it in a library again. */
enum class ContentOrigin
{
    library,        ///< The user's library folder.
    factoryLibrary, ///< The shared factory folder an installer fills.
    file            ///< Anywhere else, chosen through a file dialog.
};

[[nodiscard]] juce::String toToken (ContentOrigin origin);

struct ContentSource
{
    /** The file's name without its extension. */
    juce::String name;

    ContentOrigin origin = ContentOrigin::file;

    /** For library content, its path inside the library, '/'-separated. Empty
        otherwise. Never opened: only compared with a library's own entries. */
    juce::String libraryPath;

    [[nodiscard]] bool operator== (const ContentSource&) const = default;
};

/** What a slot is playing, and whether something else is on its way. */
struct ContentSlotInfo
{
    bool loaded = false;
    ContentSource source;

    /** A wavetable's frames, or a sample's length. */
    int frames = 0;
    double seconds = 0.0;

    /** True while a load or a restore for this slot is running. */
    bool loading = false;
    juce::String loadingName;
};

/** Something the interface should be told. */
struct ContentEvent
{
    ContentSlot slot = ContentSlot::osc1;

    /** LOADED, LOAD_FAILED, RESTORE_FAILED, CLEARED or LOADING. */
    juce::String token;

    /** A sentence, naming no path (UI_BINDINGS.md §13). */
    juce::String message;
};

class UserContent final : private juce::Thread,
                          private juce::AsyncUpdater,
                          private juce::Timer
{
public:
    UserContent (juce::AudioProcessorValueTreeState& stateToUse,
                 dsp::WavetableLibrary& tablesToUse,
                 dsp::PublishedSlot<dsp::TransientSample>& noiseSampleToUse);
    ~UserContent() override;

    /** Loads @p file into @p slot. Returns at once; the result is delivered on
        the message thread. On success the audio is stored in the patch and the
        slot's choice is selected — USER, or SAMPLE — as one gesture a host can
        record. MESSAGE THREAD. */
    void loadFile (ContentSlot slot, const juce::File& file, const ContentSource& source);

    /** Empties @p slot and removes it from the patch, at once. Anything still
        loading for it is abandoned. MESSAGE THREAD. */
    void clear (ContentSlot slot);

    /** Rebuilds every slot from the state tree, after a host or preset load
        replaced it: queues a restore for each slot the tree carries content
        for and an emptying for each it does not. Content identical to what a
        slot already plays is left alone, so a host that restores the same
        state again — some do, for undo — costs nothing. ANY THREAD. */
    void restoreFromState();

    [[nodiscard]] ContentSlotInfo getInfo (ContentSlot slot) const;

    /** The audio stored in the patch for @p slot, as encoded, or empty. */
    [[nodiscard]] juce::String getStoredAudio (ContentSlot slot) const;

    /** Called on the message thread for every load, failure and clearing. */
    std::function<void (const ContentEvent&)> onChanged;

    /** Blocks until the worker has nothing left to do. Tests only. */
    void waitUntilIdle();

    /** Delivers finished work now rather than when the message loop next runs.
        Tests only: the test runner has no message loop. */
    void flushDeliveries();

    /** Element and property names. Part of the serialized contract. */
    static constexpr const char* contentTreeType = "CONTENT";
    static constexpr const char* itemTreeType = "ITEM";
    static constexpr const char* slotProperty = "slot";
    static constexpr const char* nameProperty = "name";
    static constexpr const char* originProperty = "origin";
    static constexpr const char* pathProperty = "path";
    static constexpr const char* framesProperty = "frames";
    static constexpr const char* secondsProperty = "seconds";
    static constexpr const char* formatProperty = "format";
    static constexpr const char* audioProperty = "audio";

    /** Longest name or library path read from a document, in characters. */
    static constexpr int maximumNameLength = 128;
    static constexpr int maximumPathLength = 512;

private:
    struct Job
    {
        ContentSlot slot = ContentSlot::osc1;
        std::uint64_t serial = 0;
        bool fromDocument = false;
        juce::File file;
        juce::String audio;
        ContentSource source;
    };

    struct Result
    {
        enum class Kind { built, failed, cleared };

        ContentSlot slot = ContentSlot::osc1;
        std::uint64_t serial = 0;
        Kind kind = Kind::cleared;
        bool fromDocument = false;

        std::unique_ptr<dsp::Wavetable> table;
        std::unique_ptr<dsp::TransientSample> sample;
        juce::String audio;
        ContentSource source;
        int frames = 0;
        double seconds = 0.0;
        juce::String failure;
    };

    void run() override;
    void handleAsyncUpdate() override;
    void timerCallback() override;

    [[nodiscard]] Result process (const Job& job) const;
    void deliver (Result& result);

    /** Puts @p slot back on its fallback: the first built-in, or no sample. */
    void empty (ContentSlot slot);

    void writeItem (const Result& result);
    void removeItem (ContentSlot slot);
    void select (ContentSlot slot);

    void queueJob (Job job);
    void queueResult (Result result);

    [[nodiscard]] static std::size_t indexOf (ContentSlot slot) noexcept { return static_cast<std::size_t> (slot); }

    juce::AudioProcessorValueTreeState& apvts;
    dsp::WavetableLibrary& tables;
    dsp::PublishedSlot<dsp::TransientSample>& noiseSample;

    /** The newest request for each slot. A job or result with an older serial
        has been superseded and is dropped. */
    std::array<std::atomic<std::uint64_t>, numContentSlots> latest {};

    mutable std::mutex lock;
    std::deque<Job> jobs;
    std::vector<Result> results;
    bool working = false;
    std::array<ContentSlotInfo, numContentSlots> info {};

    /** The encoded audio each slot is playing, for telling a restore of the same
        content from new content. Shares the tree's string; costs no copy. */
    std::array<juce::String, numContentSlots> playing {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UserContent)
};

} // namespace apollo::resources
