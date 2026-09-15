#include "Resources/PresetLibrary.h"

#include "ApolloVersion.h"
#include "Parameters/ParameterLayout.h"

#include <algorithm>

namespace apollo::resources
{

namespace
{

/** The folder both roots sit inside, named for the product.

    Taken from the version header rather than typed, so a rename happens in one
    place and the preset folder follows the product.
*/
[[nodiscard]] juce::String productFolderName()
{
    return params::toJuceString (productName);
}

constexpr const char* presetsFolderName = "Presets";

/** Windows still reserves these, with or without an extension, in any case.

    A file called `CON.rnv` cannot be created there, and the attempt fails in
    ways that are confusing rather than informative. They are refused on every
    platform so that a library written on a Mac still opens on Windows.
*/
[[nodiscard]] bool isReservedDeviceName (const juce::String& name)
{
    static const char* reserved[] = {
        "CON", "PRN", "AUX", "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
    };

    const auto upper = name.toUpperCase();

    for (const auto* candidate : reserved)
        if (upper == candidate)
            return true;

    return false;
}

/** @returns @p file's folder relative to @p root, or an empty string.

    This is the bank: the path between the root and the file, with separators
    normalised so a bank reads the same whichever platform wrote it.
*/
[[nodiscard]] juce::String bankOf (const juce::File& file, const juce::File& root)
{
    const auto relative = file.getParentDirectory().getRelativePathFrom (root);

    if (relative == "." || relative.startsWith (".."))
        return {};

    return relative.replaceCharacter ('\\', '/');
}

/** Walks one root, adding what it can read to @p index.

    Iterative rather than recursive, with an explicit depth on every entry: a
    filesystem can present a cycle through symbolic links, and a recursion that
    trusted the tree to be a tree would follow it until the stack ran out.
*/
void scanRoot (const juce::File& root,
               bool factory,
               PresetIndex& index,
               const std::function<bool()>& shouldAbort)
{
    struct Pending
    {
        juce::File directory;
        int depth = 0;
    };

    std::vector<Pending> pending { { root, 0 } };

    while (! pending.empty())
    {
        if (shouldAbort && shouldAbort())
            return;

        const auto current = pending.back();
        pending.pop_back();

        if (current.depth > maximumScanDepth)
        {
            index.truncated = true;
            continue;
        }

        // Listed rather than trusted: a folder can disappear, or turn out to be
        // unreadable, between being found and being opened. JUCE answers with an
        // empty range rather than throwing, which is the behaviour wanted here —
        // one unreadable folder must not end the scan.
        for (const auto& item : juce::RangedDirectoryIterator (
                 current.directory,
                 /* isRecursive */ false,
                 "*",
                 juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles))
        {
            if (shouldAbort && shouldAbort())
                return;

            const auto file = item.getFile();

            if (item.isDirectory())
            {
                pending.push_back ({ file, current.depth + 1 });
                continue;
            }

            if (! file.hasFileExtension (presets::fileExtension))
                continue;

            if (static_cast<int> (index.entries.size()) >= maximumPresets)
            {
                index.truncated = true;
                return;
            }

            juce::String text;

            if (readPresetFile (file, text) != state::StateLoadResult::ok)
            {
                ++index.unreadable;
                continue;
            }

            presets::Metadata metadata;

            if (presets::readMetadata (text, metadata) != state::StateLoadResult::ok)
            {
                // The extension is not evidence (ADR-0053). Counted, so the
                // interface can say how many files in a folder are not presets
                // rather than silently listing fewer than are there.
                ++index.unreadable;
                continue;
            }

            PresetEntry entry;

            entry.file = file;
            entry.name = metadata.name.isNotEmpty() ? metadata.name
                                                    : file.getFileNameWithoutExtension();
            entry.author = metadata.author;
            entry.category = metadata.category;
            entry.bank = bankOf (file, root);
            entry.factory = factory;

            index.entries.push_back (std::move (entry));
        }
    }
}

} // namespace

PresetLocations defaultPresetLocations()
{
    PresetLocations locations;

    // JUCE knows where this is on each platform; Apollo only names the two
    // folders under it (CLAUDE.md §46).
    const auto userRoot = juce::File::getSpecialLocation (
        juce::File::userApplicationDataDirectory);

    locations.userDirectory = userRoot.getChildFile (productFolderName())
                                      .getChildFile (presetsFolderName);

    const auto sharedRoot = juce::File::getSpecialLocation (
        juce::File::commonApplicationDataDirectory);

    locations.factoryDirectory = sharedRoot.getChildFile (productFolderName())
                                           .getChildFile (presetsFolderName);

    return locations;
}

juce::String toSafeFileName (const juce::String& presetName)
{
    // Separators become spaces *before* JUCE's filter runs, because it deletes
    // them outright and "Pads/Wide" would come back as "PadsWide" - the word
    // boundary the user typed, lost for no reason.
    auto name = presetName.trim()
                    .replaceCharacter ('/', ' ')
                    .replaceCharacter ('\\', ' ');

    // JUCE removes the characters a filesystem refuses. What it does not do is
    // everything below, each of which has its own failure.
    name = juce::File::createLegalFileName (name);

    name = name.removeCharacters (":");

    // A name is not a path. The parent-directory token cannot survive in any
    // form - a preset called "../../autoexec" must become a preset, not a
    // location (CLAUDE.md 40).
    while (name.contains (".."))
        name = name.replace ("..", ".");

    // A leading dot is not cosmetic: on Unix it makes a hidden file, and the
    // scanner skips hidden files - so the preset would save successfully and
    // then be invisible to the library that saved it.
    while (name.isNotEmpty() && (name.startsWithChar ('.') || name.startsWithChar (' ')))
        name = name.substring (1);

    // Windows silently discards trailing dots and spaces, which would make
    // "Bell." and "Bell" the same file and a rename look like a deletion.
    while (name.isNotEmpty()
           && (name.getLastCharacter() == '.' || name.getLastCharacter() == ' '))
        name = name.dropLastCharacters (1);

    name = name.trim();

    // The device names are reserved whatever follows the dot, so the check is on
    // the stem: `CON`, `con.rnv` and `Con.txt` are all the same refusal.
    if (name.isEmpty() || isReservedDeviceName (name.upToFirstOccurrenceOf (".", false, false)))
        return {};

    // Long enough for any real name, short enough that the whole path stays
    // inside the limits a filesystem imposes once a bank folder is in front of
    // it.
    return name.substring (0, presets::maximumNameLength);
}

juce::String describe (PresetSaveResult result)
{
    switch (result)
    {
        case PresetSaveResult::ok:                   return "Preset saved.";
        case PresetSaveResult::invalidName:          return "That name cannot be used for a file.";
        case PresetSaveResult::outsideLibrary:       return "That name would save outside the preset folder.";
        case PresetSaveResult::isFactoryPreset:      return "Factory presets cannot be overwritten. Save a copy instead.";
        case PresetSaveResult::couldNotCreateFolder: return "The preset folder could not be created.";
        case PresetSaveResult::couldNotWrite:        return "The preset could not be written.";
    }

    return "The preset could not be written.";
}

juce::File presetFileFor (const juce::File& directory, const juce::String& presetName)
{
    const auto safe = toSafeFileName (presetName);

    // Spelled out rather than braced, for the reason `savePreset` records
    // below: juce::File takes a String as well as a File, so a braced empty
    // initialiser is ambiguous to GCC and Clang even where MSVC picks one.
    if (safe.isEmpty())
        return juce::File();

    const auto target = directory.getChildFile (safe + presets::fileExtension);

    // Checked after the path is built rather than before, because the check
    // that matters is where it *resolved to*, not what it looked like. A name
    // that escaped every filter above would still be caught here.
    if (! target.isAChildOf (directory))
        return juce::File();

    return target;
}

PresetSaveResult savePreset (const juce::File& directory,
                             const juce::String& presetName,
                             const juce::String& text,
                             juce::File& destination)
{
    // Spelled out rather than braced: juce::File takes a String as well as a
    // File, so `= {}` is ambiguous to GCC and Clang even though MSVC picks one.
    destination = juce::File();

    if (toSafeFileName (presetName).isEmpty())
        return PresetSaveResult::invalidName;

    const auto target = presetFileFor (directory, presetName);

    // An empty result here means the path resolved outside the folder it was
    // for, the name having already been established as usable.
    if (target == juce::File())
        return PresetSaveResult::outsideLibrary;

    if (! directory.exists() && ! directory.createDirectory().wasOk())
        return PresetSaveResult::couldNotCreateFolder;

    if (! directory.isDirectory())
        return PresetSaveResult::couldNotCreateFolder;

    // ATOMIC OR NOT AT ALL. The bytes go to a temporary file beside the target
    // and are moved into place once they are all written. Writing straight into
    // the destination means an interrupted save leaves a truncated file where a
    // working preset used to be, which is the one outcome worth engineering
    // against: losing the preset being saved is an inconvenience, and losing the
    // one that was already there is lost work.
    juce::TemporaryFile temporary (target);

    if (! temporary.getFile().replaceWithText (text, /* asUnicode */ false, /* writeBom */ false))
        return PresetSaveResult::couldNotWrite;

    if (! temporary.overwriteTargetFileWithTemporary())
        return PresetSaveResult::couldNotWrite;

    destination = target;

    return PresetSaveResult::ok;
}

state::StateLoadResult readPresetFile (const juce::File& file, juce::String& destination)
{
    destination.clear();

    if (! file.existsAsFile())
        return state::StateLoadResult::emptyData;

    // Asked before reading, so pointing Apollo at a video file costs a stat
    // rather than an attempt to pull it into memory.
    const auto size = file.getSize();

    if (size <= 0)
        return state::StateLoadResult::emptyData;

    if (size > presets::maximumDocumentBytes)
        return state::StateLoadResult::tooLarge;

    destination = file.loadFileAsString();

    // A file that existed a moment ago and reads as nothing now is a file that
    // was removed, truncated or locked between the two calls. Not a crash, and
    // not a preset either.
    if (destination.isEmpty())
        return state::StateLoadResult::emptyData;

    return state::StateLoadResult::ok;
}

PresetIndex scanPresets (const PresetLocations& locations, const std::function<bool()>& shouldAbort)
{
    PresetIndex index;

    index.factoryDirectoryMissing = ! locations.factoryDirectory.isDirectory();
    index.userDirectoryMissing = ! locations.userDirectory.isDirectory();

    // Factory first, so that when two presets share a name the user's is the
    // one later in the list — which is the order a browser will show, and the
    // order that makes a user's own work easier to find.
    if (! index.factoryDirectoryMissing)
        scanRoot (locations.factoryDirectory, /* factory */ true, index, shouldAbort);

    if (! index.userDirectoryMissing)
        scanRoot (locations.userDirectory, /* factory */ false, index, shouldAbort);

    // SORTED BEFORE IT IS NUMBERED, because directory iteration order is
    // whatever the filesystem feels like and two scans of an unchanged folder
    // may not agree. A browser built on that would reshuffle its own list every
    // time anything was saved, and an id would mean a different sound depending
    // on which scan produced it.
    //
    // Factory before user keeps the rule scanRoot's ordering already stated:
    // where the two libraries share a name, the user's own work is the one
    // further down, which is where somebody looks for it.
    std::sort (index.entries.begin(), index.entries.end(),
               [] (const PresetEntry& a, const PresetEntry& b)
               {
                   if (a.factory != b.factory)
                       return a.factory;

                   if (const auto bank = a.bank.compareIgnoreCase (b.bank); bank != 0)
                       return bank < 0;

                   if (const auto name = a.name.compareIgnoreCase (b.name); name != 0)
                       return name < 0;

                   // Two presets can legitimately present the same name in the
                   // same bank — the metadata name is free text, and nothing
                   // stops two files carrying it. Falling back to the filename
                   // keeps the order total, so the sort is deterministic rather
                   // than merely usually stable.
                   return a.file.getFileName().compareIgnoreCase (b.file.getFileName()) < 0;
               });

    auto nextId = 1;

    for (auto& entry : index.entries)
        entry.id = nextId++;

    return index;
}

const PresetEntry* findPreset (const PresetIndex& index, int id)
{
    // Zero is "no preset" by construction and is not searched for, so a page
    // that has not chosen anything cannot accidentally match the first entry.
    if (id <= 0)
        return nullptr;

    const auto found = std::find_if (index.entries.begin(), index.entries.end(),
                                     [id] (const PresetEntry& entry) { return entry.id == id; });

    return found != index.entries.end() ? &*found : nullptr;
}

//==============================================================================

/** Hands a finished index to the message thread.

    A `juce::AsyncUpdater` rather than a direct call, because `onIndexUpdated`
    reaches the interface and the interface belongs to the message thread
    (CLAUDE.md §8).
*/
class PresetLibrary::Delivery final : public juce::AsyncUpdater
{
public:
    explicit Delivery (PresetLibrary& ownerToUse) : owner (ownerToUse) {}

    ~Delivery() override { cancelPendingUpdate(); }

    /** Marks a delivery due and asks the message loop to make it.

        The flag is the state and the async update is only the transport. Two
        reasons for that split: a host that is tearing down may never run the
        loop again, and the test runner is a console program that has no loop at
        all — in both cases the notification is still *owed*, and `deliverNow`
        can pay it.
    */
    void schedule()
    {
        pending.store (true, std::memory_order_release);
        triggerAsyncUpdate();
    }

    /** Runs the callback if one is owed, exactly once. */
    void deliverNow()
    {
        if (! pending.exchange (false, std::memory_order_acq_rel))
            return;

        if (owner.onIndexUpdated)
            owner.onIndexUpdated();
    }

    void handleAsyncUpdate() override { deliverNow(); }

private:
    PresetLibrary& owner;
    std::atomic<bool> pending { false };
};

PresetLibrary::PresetLibrary()
    : juce::Thread ("Apollo preset scan"),
      locations (defaultPresetLocations()),
      delivery (std::make_unique<Delivery> (*this))
{
}

PresetLibrary::~PresetLibrary()
{
    // Order matters. The delivery is cancelled first so a scan finishing during
    // teardown cannot call into a half-destroyed object, and the thread is then
    // given a bounded time to notice it should stop.
    delivery->cancelPendingUpdate();

    stopThread (2000);
}

void PresetLibrary::setLocations (const PresetLocations& newLocations)
{
    const juce::ScopedLock scoped (lock);

    locations = newLocations;
}

PresetLocations PresetLibrary::getLocations() const
{
    const juce::ScopedLock scoped (lock);

    return locations;
}

PresetIndex PresetLibrary::getIndex() const
{
    const juce::ScopedLock scoped (lock);

    return index;
}

void PresetLibrary::rescan()
{
    // A scan already running is answering a question that has just been
    // superseded, so it is stopped rather than allowed to finish and overwrite
    // the newer one.
    stopThread (2000);

    startThread();
}

void PresetLibrary::waitForScan()
{
    while (isThreadRunning())
        juce::Thread::sleep (5);
}

void PresetLibrary::flushPendingNotification()
{
    delivery->deliverNow();
}

void PresetLibrary::run()
{
    const auto snapshot = getLocations();

    auto scanned = scanPresets (snapshot, [this] { return threadShouldExit(); });

    if (threadShouldExit())
        return;

    publish (std::move (scanned));
}

void PresetLibrary::publish (PresetIndex&& scanned)
{
    {
        const juce::ScopedLock scoped (lock);

        index = std::move (scanned);
    }

    delivery->schedule();
}

} // namespace apollo::resources
