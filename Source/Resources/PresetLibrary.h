#pragma once

/*
    Presets on disk: where they live, how they are found, and how one is saved
    without risking the one already there.

    Phase 9a built the document — text in, validated state out, and no file I/O
    at all. This is the half that touches the filesystem, which means it is the
    half that has to survive a disk it does not control: a folder that does not
    exist, a folder it may not read, a file that vanishes between being listed
    and being opened, a `.rnv` that is a photograph, a symbolic link pointing at
    its own parent, and a library with more files in it than anyone should have.
    None of those may crash, and none may stop the rest of the library appearing
    (CLAUDE.md §30, §33).

    A BANK IS A FOLDER. ADR-0053 chose one file per sound and no container
    format, which leaves the filesystem to do what it already does well: a
    folder groups presets, and copying, renaming and sharing a bank are things
    the user already knows how to do. The index mirrors the tree rather than
    flattening it.

    NOTHING HERE RUNS ON THE AUDIO THREAD, and nothing here runs on the message
    thread for long. Scanning opens and parses every file it finds; on a library
    of a few thousand that is seconds, not milliseconds, so `PresetLibrary` does
    it on a thread of its own and publishes a finished index (CLAUDE.md §7.3).

    A SAVE IS ATOMIC OR IT DOES NOT HAPPEN. Writing straight into the
    destination means a crash, a full disk or a pulled cable leaves a truncated
    file where a working preset used to be. Every save goes to a temporary file
    beside the target and is moved into place once it is complete, so the
    failure mode is "the new preset was not saved" rather than "the old one is
    gone".
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>
#include <vector>

#include "State/PresetDocument.h"
#include "State/StateSerialization.h"

namespace apollo::resources
{

/** Where Apollo looks for presets.

    Two roots, both optional at run time. The user's is writable and is where
    saving goes; the factory's is read-only and may legitimately not exist —
    Apollo runs perfectly well with no factory content, and will until Phase 9d
    supplies some.
*/
struct PresetLocations
{
    juce::File userDirectory;
    juce::File factoryDirectory;

    [[nodiscard]] bool operator== (const PresetLocations&) const = default;
};

/** @returns the platform's own answers.

    Nothing here is spelled out per operating system: JUCE's special-location
    lookup is what knows that this is `%APPDATA%` on Windows,
    `~/Library/Application Support` on macOS and `~/.config` on Linux
    (CLAUDE.md §46). Apollo's contribution is the two folder names under it.
*/
[[nodiscard]] PresetLocations defaultPresetLocations();

/** One preset found on disk. */
struct PresetEntry
{
    juce::File file;

    /** What to call it: the metadata name if the file has one, otherwise the
        filename without its extension. A preset is never nameless in a list. */
    juce::String name;

    juce::String author;
    juce::String category;

    /** The folder it sits in, relative to its root, or empty at the top. This
        is what a bank is (ADR-0053). */
    juce::String bank;

    /** True for anything under the factory root. Origin is a location, not a
        format: a factory preset is read by the same reader as any other, and
        the only thing this changes is that saving over it is refused. */
    bool factory = false;

    [[nodiscard]] bool operator== (const PresetEntry&) const = default;
};

/** What a scan found, including what it could not read.

    The failures are counted rather than dropped silently. "Four files in this
    folder are not presets" is something a user can act on; a library that is
    quietly four shorter than the folder is not (CLAUDE.md §33).
*/
struct PresetIndex
{
    std::vector<PresetEntry> entries;

    /** Files with the right extension that could not be read as Apollo
        presets — corrupt, truncated, from a newer build, or never presets. */
    int unreadable = 0;

    /** True when the root was not there at all. Not an error: a user who has
        never saved a preset has no user folder, and a build with no factory
        content has no factory folder. */
    bool userDirectoryMissing = false;
    bool factoryDirectoryMissing = false;

    /** True when the scan stopped because it hit `maximumPresets` or
        `maximumScanDepth`. The entries found so far are still valid; the user
        is told the list is not everything. */
    bool truncated = false;
};

/** Most presets one scan will index.

    A bound rather than a belief: a user preset folder is whatever someone
    points Apollo at, and "scan until finished" is not a plan when that might be
    a network share or a home directory. Ten thousand is far beyond any real
    library and still finishes quickly.
*/
inline constexpr int maximumPresets = 10000;

/** How far down a preset tree is followed.

    Banks are folders, and folders nest, but not like this. The bound also ends
    the one case a filesystem can pose that no amount of care avoids: a
    symbolic link that points at one of its own parents.
*/
inline constexpr int maximumScanDepth = 8;

/** Scans both roots and returns everything it could read.

    BACKGROUND THREAD. Opens and parses every file it finds.

    @param locations   the two roots; either may be absent.
    @param shouldAbort polled between files. A scan that is no longer wanted —
                       because the user changed folders, or the plugin is
                       closing — stops rather than finishing out of politeness.
*/
[[nodiscard]] PresetIndex scanPresets (const PresetLocations& locations,
                                       const std::function<bool()>& shouldAbort = {});

/** @returns a filename, without extension, that is safe on every platform.

    A preset name is free text a user typed; a filename is not. This strips path
    separators so a name can never escape the folder it is being saved into,
    refuses the device names Windows still reserves, and trims the trailing dots
    and spaces Windows silently discards — which would otherwise make "Bell."
    and "Bell" the same file (CLAUDE.md §40).

    @returns an empty string if nothing usable is left, which the caller must
             treat as a refusal rather than as a filename.
*/
[[nodiscard]] juce::String toSafeFileName (const juce::String& presetName);

/** Why a save did not happen. */
enum class PresetSaveResult
{
    ok,
    invalidName,          ///< Nothing usable was left of the name.
    outsideLibrary,       ///< The path resolved outside the folder it was for.
    isFactoryPreset,      ///< Factory content is read-only.
    couldNotCreateFolder,
    couldNotWrite         ///< The disk refused it; anything already there survives.
};

[[nodiscard]] juce::String describe (PresetSaveResult result);

/** Writes @p text as a preset called @p presetName inside @p directory.

    Atomic: the bytes go to a temporary file beside the target and are moved
    into place only once they are all written, so an interrupted save leaves the
    preset that was already there intact.

    @param destination receives the file that was written, on success.
*/
[[nodiscard]] PresetSaveResult savePreset (const juce::File& directory,
                                           const juce::String& presetName,
                                           const juce::String& text,
                                           juce::File& destination);

/** Reads a preset file into @p destination.

    Checks the size before reading rather than after, so pointing Apollo at
    something enormous costs a stat call. Every other failure — missing,
    unreadable, a directory, empty — comes back as one of the same reasons a
    malformed document does, because to the user they are the same event: the
    preset did not load and the sound did not change.
*/
[[nodiscard]] state::StateLoadResult readPresetFile (const juce::File& file,
                                                     juce::String& destination);

/** Owns the index and keeps it off the message thread.

    Scanning is started, not waited for. `onIndexUpdated` is called on the
    message thread when a scan finishes; a scan that is superseded or cancelled
    calls nothing, because the answer it was computing is no longer the answer
    to anything.
*/
class PresetLibrary final : private juce::Thread
{
public:
    PresetLibrary();
    ~PresetLibrary() override;

    /** Points the library at somewhere other than the platform defaults.

        Exists for tests, and for the day a user wants their library on an
        external disk. Setting locations does not start a scan.
    */
    void setLocations (const PresetLocations& locations);

    [[nodiscard]] PresetLocations getLocations() const;

    /** Starts a scan, cancelling one already running. Returns immediately. */
    void rescan();

    /** Blocks until any scan in progress has finished. Tests only. */
    void waitForScan();

    /** Delivers a pending notification now, instead of when the message loop
        next runs.

        Exposed for testing, the way `state::migrate` is. In an application the
        message loop does this; the test runner is a console program that has no
        loop at all, so without it the one thing worth asserting about the
        delivery — that a finished scan reaches `onIndexUpdated` — could not be
        observed.
    */
    void flushPendingNotification();

    /** The most recent finished index. Message thread. */
    [[nodiscard]] PresetIndex getIndex() const;

    [[nodiscard]] bool isScanning() const noexcept { return isThreadRunning(); }

    /** Called on the message thread after a scan finishes. */
    std::function<void()> onIndexUpdated;

private:
    void run() override;

    /** Delivers a finished index to the message thread. */
    void publish (PresetIndex&& index);

    PresetLocations locations;
    PresetIndex index;

    /** Guards `locations` and `index`, both of which are written by the scan
        thread and read by the message thread. Held only to copy a small struct
        in or out — never while scanning, and never on the audio thread, which
        never sees this class at all.
    */
    mutable juce::CriticalSection lock;

    /** Message-thread delivery, so `onIndexUpdated` is never called from the
        scan thread. Cancelled in the destructor before the thread stops.
    */
    class Delivery;
    std::unique_ptr<Delivery> delivery;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetLibrary)
};

} // namespace apollo::resources
