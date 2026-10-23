#pragma once

/*
    Where the user keeps wavetables and noise samples, and what is in there
    (Phase 13g, PRD §33).

    TWO KINDS, TWO ROOTS EACH. Wavetables and noise samples live in folders of
    their own, beside the preset folder:

        <user data>/Apollo/Wavetables      yours; Save to library writes here
        <user data>/Apollo/Noise
        <shared data>/Apollo/Wavetables    factory content an installer placed
        <shared data>/Apollo/Noise

    Either may be missing, and Apollo runs perfectly well with none of them:
    its own tables are compiled in (CLAUDE.md §29.3). A BANK IS A FOLDER, as it
    is for presets (ADR-0053): organising a library is moving files around in
    the file manager the user already knows, and Apollo opens that folder for
    them rather than reinventing it.

    THE PAGE NAMES A NUMBER, NEVER A PATH (ADR-0063). A scan numbers what it
    found; the browser asks for a number; the number is resolved against the
    index the backend built. Nothing sent to the page contains a location.

    A SCAN READS HEADERS, NOT AUDIO. Each file is opened far enough to learn
    how long it is, so the browser can show a table's frames and a sample's
    length, and say before anyone clicks that a file is not a whole number of
    frames or is too long to be a noise sample. The audio itself is read when
    the file is chosen, through the same readers as everything else.

    NOT THE AUDIO THREAD, AND NOT THE MESSAGE THREAD FOR LONG. Scanning is on a
    thread of its own; the finished index is delivered on the message thread.
*/

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include "Resources/UserContent.h"

namespace apollo::resources
{

enum class ContentKind
{
    wavetable,
    noise
};

/** "wavetable" or "noise". */
[[nodiscard]] juce::String toToken (ContentKind kind);
[[nodiscard]] bool kindFromToken (const juce::String& token, ContentKind& kind);

[[nodiscard]] constexpr ContentKind kindOf (ContentSlot slot) noexcept
{
    return slot == ContentSlot::noise ? ContentKind::noise : ContentKind::wavetable;
}

/** The file dialog's filter for @p kind. */
[[nodiscard]] juce::String wildcardFor (ContentKind kind);

struct ContentLocations
{
    juce::File userWavetables;
    juce::File userNoise;
    juce::File factoryWavetables;
    juce::File factoryNoise;

    [[nodiscard]] juce::File userRoot (ContentKind kind) const { return kind == ContentKind::noise ? userNoise : userWavetables; }
    [[nodiscard]] juce::File factoryRoot (ContentKind kind) const { return kind == ContentKind::noise ? factoryNoise : factoryWavetables; }

    [[nodiscard]] bool operator== (const ContentLocations&) const = default;
};

/** The platform's answers, from JUCE's special locations (CLAUDE.md §46). */
[[nodiscard]] ContentLocations defaultContentLocations();

struct ContentEntry
{
    /** What the page asks for. From 1; renumbered by every scan. */
    int id = 0;

    ContentKind kind = ContentKind::wavetable;

    /** Never sent to the page. */
    juce::File file;

    /** The file's name without its extension. */
    juce::String name;

    /** The folder it sits in, relative to its root, '/'-separated; empty at the
        top. */
    juce::String bank;

    /** Its path relative to its root, '/'-separated: what a patch remembers to
        find it again. */
    juce::String libraryPath;

    bool factory = false;

    /** A wavetable's frames, or a sample's length, from the header. */
    int frames = 0;
    double seconds = 0.0;

    /** Why the header says it will not load, or empty. */
    juce::String problem;
};

struct ContentIndex
{
    std::vector<ContentEntry> entries;

    /** True when a scan stopped at `maximumContentEntries` or the depth bound. */
    bool truncated = false;
};

inline constexpr int maximumContentEntries = 10000;
inline constexpr int maximumContentScanDepth = 8;

/** Scans every root in @p locations. BACKGROUND THREAD: opens every header. */
[[nodiscard]] ContentIndex scanContent (const ContentLocations& locations,
                                        const std::function<bool()>& shouldAbort = {});

/** @returns the entry numbered @p id, or nullptr. */
[[nodiscard]] const ContentEntry* findContent (const ContentIndex& index, int id);

/** @returns the entry @p source came from, if it is still in the index. */
[[nodiscard]] const ContentEntry* findContent (const ContentIndex& index, ContentKind kind,
                                               const ContentSource& source);

/** How a file chosen in a dialog is described: library content if it sits
    under one of the roots for @p kind, a plain file otherwise. */
[[nodiscard]] ContentSource sourceFor (const ContentLocations& locations, ContentKind kind, const juce::File& file);

/** Why content could not be written into the library. */
enum class ContentSaveResult
{
    ok,
    nothingToSave,
    unreadable,
    couldNotCreateFolder,
    couldNotWrite
};

[[nodiscard]] juce::String describe (ContentSaveResult result);

/** Writes the audio a patch carries — @p audio, as UserContent stores it — into
    @p directory as a 24-bit WAV called after @p name, without replacing
    anything already there: a name that is taken gets a number. The file is
    written beside its destination and moved into place once complete.

    @param written  the file, on success.
*/
[[nodiscard]] ContentSaveResult saveContent (const juce::String& audio, ContentKind kind, const juce::String& name,
                                             const juce::File& directory, juce::File& written);

class ContentLibrary final : private juce::Thread,
                             private juce::AsyncUpdater
{
public:
    ContentLibrary();
    ~ContentLibrary() override;

    /** For tests, and for a library somewhere else one day. Does not scan. */
    void setLocations (const ContentLocations& newLocations);
    [[nodiscard]] ContentLocations getLocations() const;

    /** Starts a scan, cancelling one already running. Returns at once. */
    void rescan();

    [[nodiscard]] bool isScanning() const noexcept { return isThreadRunning(); }

    /** The newest finished index. */
    [[nodiscard]] ContentIndex getIndex() const;

    /** Called on the message thread when a scan finishes. */
    std::function<void()> onIndexUpdated;

    /** Opens the user's folder for @p kind in the system's file manager,
        creating it first. @returns false if it could not be created. */
    bool revealFolder (ContentKind kind) const;

    void waitForScan();
    void flushPendingNotification();

private:
    void run() override;
    void handleAsyncUpdate() override;

    mutable juce::CriticalSection lock;
    ContentLocations locations;
    ContentIndex index;
    std::atomic<bool> pendingDelivery { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ContentLibrary)
};

} // namespace apollo::resources
