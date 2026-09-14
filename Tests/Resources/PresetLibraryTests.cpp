/*
    Preset library tests.

    Everything here is about a disk Apollo does not control. The document tests
    in `Tests/State/PresetDocumentTests.cpp` cover what is *inside* a preset;
    these cover the folder it sits in — which can be missing, unreadable,
    enormous, full of things that are not presets, or arranged in a loop.

    These tests do real file I/O, and they are the only ones in the suite that
    do. That is not incidental: a scanner tested against a mock filesystem is a
    scanner tested against the filesystem somebody imagined. Every test builds
    its own tree under the system temporary directory and removes it afterwards,
    and nothing here touches the real preset locations — a test that wrote into
    the developer's own library would be a test that could lose their work.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include "Audio/ApolloAudioProcessor.h"
#include "Resources/PresetLibrary.h"
#include "State/PresetDocument.h"

using namespace apollo;

namespace
{

/** A temporary directory that removes itself.

    `juce::TemporaryFile` is for a single file; a scan needs a tree. Named after
    the test using it so that a leftover folder, if one ever survives a crash,
    says where it came from.
*/
class ScratchTree
{
public:
    explicit ScratchTree (const juce::String& label)
        : root (juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("ApolloPresetTests")
                    .getChildFile (label + "-" + juce::String (juce::Random::getSystemRandom()
                                                                  .nextInt (1000000))))
    {
        root.createDirectory();
    }

    ~ScratchTree()
    {
        root.deleteRecursively();
    }

    [[nodiscard]] juce::File getRoot() const { return root; }

    [[nodiscard]] juce::File folder (const juce::String& relative) const
    {
        auto file = root.getChildFile (relative);
        file.createDirectory();

        return file;
    }

    /** Writes @p text to a file, creating whatever folders it needs. */
    juce::File write (const juce::String& relative, const juce::String& text) const
    {
        auto file = root.getChildFile (relative);

        file.getParentDirectory().createDirectory();
        file.replaceWithText (text, false, false);

        return file;
    }

private:
    juce::File root;

    JUCE_DECLARE_NON_COPYABLE (ScratchTree)
};

/** A valid preset document with the given metadata. */
[[nodiscard]] juce::String documentNamed (const juce::String& name,
                                          const juce::String& category = "Keys")
{
    ApolloAudioProcessor processor;

    presets::Metadata metadata;
    metadata.name = name;
    metadata.author = "ProdByRnV";
    metadata.category = category;

    return presets::write (processor.getValueTreeState(), metadata);
}

/** @returns the entry with this name, or nullptr. */
[[nodiscard]] const resources::PresetEntry* find (const resources::PresetIndex& index,
                                                  const juce::String& name)
{
    for (const auto& entry : index.entries)
        if (entry.name == name)
            return &entry;

    return nullptr;
}

class PresetLibraryTests final : public juce::UnitTest
{
public:
    PresetLibraryTests()
        : juce::UnitTest ("Preset library", "Resources")
    {
    }

    void runTest() override
    {
        testDefaultLocationsArePlatformOwned();
        testAnEmptyLibraryIsNotAnError();
        testScanFindsPresetsAndTheirBanks();
        testFilesThatAreNotPresetsAreCountedNotHidden();
        testFactoryAndUserAreDistinguished();
        testSafeFileNames();
        testSavingIsAtomic();
        testSavingRefusesToEscapeTheLibrary();
        testSaveAndScanRoundTrip();
        testReadingAMissingFileIsNotACrash();
        testDeepTreesAreBounded();
        testAsyncScanDeliversOnTheMessageThread();
    }

private:
    void testDefaultLocationsArePlatformOwned()
    {
        beginTest ("the default locations come from the platform, not from a hard-coded path");

        const auto locations = resources::defaultPresetLocations();

        expect (locations.userDirectory.getFullPathName().isNotEmpty(),
                "there must be a user preset location");
        expect (locations.factoryDirectory.getFullPathName().isNotEmpty(),
                "and a factory one");

        expect (locations.userDirectory != locations.factoryDirectory,
                "the two must be different folders");

        // Both must end in the product's own folder, which is the only part
        // Apollo chooses — everything above it is the platform's answer
        // (CLAUDE.md §46). Asserting the *shape* rather than the path keeps this
        // test true on Windows, macOS and Linux alike.
        for (const auto& directory : { locations.userDirectory, locations.factoryDirectory })
        {
            expectEquals (directory.getFileName(), juce::String ("Presets"));
            expectEquals (directory.getParentDirectory().getFileName(), juce::String ("Apollo"));

            expect (! directory.getFullPathName().containsIgnoreCase ("KIIT0001")
                        || directory.isAChildOf (juce::File::getSpecialLocation (
                               juce::File::userHomeDirectory)),
                    "a developer's name may appear only because the platform put it there");
        }

        logMessage ("  user:    " + locations.userDirectory.getFullPathName());
        logMessage ("  factory: " + locations.factoryDirectory.getFullPathName());
    }

    void testAnEmptyLibraryIsNotAnError()
    {
        beginTest ("a library with no folders at all scans cleanly and says so");

        // The normal state of a fresh install: the user has never saved a
        // preset and no factory content has been placed. Neither is a failure,
        // and the difference between "empty" and "missing" is worth reporting
        // because only one of them means something is wrong.
        resources::PresetLocations locations;

        locations.userDirectory = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                      .getChildFile ("ApolloPresetTests-does-not-exist");
        locations.factoryDirectory = locations.userDirectory.getChildFile ("nor-this");

        const auto index = resources::scanPresets (locations);

        expect (index.entries.empty(), "nothing can have been found");
        expectEquals (index.unreadable, 0);
        expect (index.userDirectoryMissing, "and the missing user folder must be reported");
        expect (index.factoryDirectoryMissing, "and the missing factory folder");
        expect (! index.truncated);
    }

    void testScanFindsPresetsAndTheirBanks()
    {
        beginTest ("a scan finds presets, names them, and reports the folder each sits in");

        ScratchTree tree ("banks");

        tree.write ("Bell.rnv", documentNamed ("Glass Bell"));
        tree.write ("Pads/Wide.rnv", documentNamed ("Wide Pad", "Pads"));
        tree.write ("Pads/Deep/Deeper.rnv", documentNamed ("Deeper Pad", "Pads"));

        resources::PresetLocations locations;
        locations.userDirectory = tree.getRoot();

        const auto index = resources::scanPresets (locations);

        expectEquals (static_cast<int> (index.entries.size()), 3);
        expectEquals (index.unreadable, 0);
        expect (! index.truncated);

        const auto* bell = find (index, "Glass Bell");
        const auto* wide = find (index, "Wide Pad");
        const auto* deeper = find (index, "Deeper Pad");

        expect (bell != nullptr && wide != nullptr && deeper != nullptr,
                "every preset must have been found and named from its metadata");

        if (bell == nullptr || wide == nullptr || deeper == nullptr)
            return;

        // A bank is a folder (ADR-0053), and nesting is preserved rather than
        // flattened: the tree the user arranged is the tree they see.
        expectEquals (bell->bank, juce::String(), "a preset at the top is in no bank");
        expectEquals (wide->bank, juce::String ("Pads"));
        expectEquals (deeper->bank, juce::String ("Pads/Deep"),
                      "a nested bank keeps its whole path, with forward slashes on every platform");

        expectEquals (bell->author, juce::String ("ProdByRnV"));
        expectEquals (wide->category, juce::String ("Pads"));
    }

    void testFilesThatAreNotPresetsAreCountedNotHidden()
    {
        beginTest ("a folder full of things that are not presets is counted, not silently shortened");

        ScratchTree tree ("mixed");

        tree.write ("Real.rnv", documentNamed ("A Real Preset"));

        // Four ways a `.rnv` can fail to be a preset. The extension is not
        // evidence (ADR-0053).
        tree.write ("NotXml.rnv", "this is just some text");
        tree.write ("Truncated.rnv", documentNamed ("Half").substring (0, 40));
        tree.write ("Foreign.rnv", "<?xml version=\"1.0\"?>\n<RECIPE servings=\"4\"/>");
        tree.write ("Empty.rnv", "");

        // And things that are simply not presets at all, which must be ignored
        // rather than counted as failures — a user's folder has a readme in it.
        tree.write ("notes.txt", "remember to finish the bass patch");
        tree.write ("cover.png", "not really a png");

        resources::PresetLocations locations;
        locations.userDirectory = tree.getRoot();

        const auto index = resources::scanPresets (locations);

        expectEquals (static_cast<int> (index.entries.size()), 1,
                      "only the real preset may be listed");
        expectEquals (index.unreadable, 4,
                      "and every file wearing the extension that is not one must be counted");

        expect (find (index, "A Real Preset") != nullptr);

        logMessage ("  1 preset, " + juce::String (index.unreadable) + " unreadable");
    }

    void testFactoryAndUserAreDistinguished()
    {
        beginTest ("factory and user presets are read the same way and told apart by where they are");

        ScratchTree tree ("origins");

        const auto factory = tree.folder ("Factory");
        const auto user = tree.folder ("User");

        factory.getChildFile ("Init.rnv").replaceWithText (documentNamed ("Init"), false, false);
        user.getChildFile ("Mine.rnv").replaceWithText (documentNamed ("Mine"), false, false);

        resources::PresetLocations locations;
        locations.factoryDirectory = factory;
        locations.userDirectory = user;

        const auto index = resources::scanPresets (locations);

        expectEquals (static_cast<int> (index.entries.size()), 2);
        expect (! index.userDirectoryMissing && ! index.factoryDirectoryMissing);

        const auto* init = find (index, "Init");
        const auto* mine = find (index, "Mine");

        expect (init != nullptr && mine != nullptr);

        if (init == nullptr || mine == nullptr)
            return;

        expect (init->factory, "a preset under the factory root is factory content");
        expect (! mine->factory, "and one under the user root is not");

        // Origin is a location, not a format: both went through the same reader
        // and both produced the same kind of entry.
        expectEquals (init->author, mine->author);
    }

    void testSafeFileNames()
    {
        beginTest ("a preset name is turned into a filename that cannot escape or collide");

        struct Case { const char* name; const char* expected; const char* why; };

        const Case cases[] = {
            { "Glass Bell", "Glass Bell", "an ordinary name is left alone" },
            { "  Padded  ", "Padded", "surrounding space is trimmed" },
            { "../../autoexec", "autoexec", "a path is not a name" },
            { "Pads/Wide", "Pads Wide", "a separator cannot create a folder" },
            { "Pads\\Wide", "Pads Wide", "on either platform's spelling" },
            { "C:evil", "Cevil", "nor a drive" },
            { "Bell...", "Bell", "trailing dots, which Windows discards silently" },
            { "Bell   ", "Bell", "and trailing spaces, likewise" },
            { "CON", "", "a Windows device name is refused outright" },
            { "con.rnv", "", "in any case, with or without an extension" },
            { "", "", "and an empty name is a refusal, not a file called nothing" },
            { "   ", "", "as is a name that is only space" },
            { "...", "", "or only dots" },
        };

        for (const auto& testCase : cases)
            expectEquals (resources::toSafeFileName (testCase.name),
                          juce::String (testCase.expected),
                          juce::String (testCase.why));

        // Whatever comes out, it is one path component and it is not a
        // traversal. Asserted as a property over every case rather than only the
        // ones thought of.
        for (const auto& testCase : cases)
        {
            const auto safe = resources::toSafeFileName (testCase.name);

            expect (! safe.contains ("/") && ! safe.contains ("\\"),
                    "a safe name must never contain a separator");
            expect (! safe.contains (".."),
                    "nor the parent-directory token");
            expect (safe.length() <= presets::maximumNameLength,
                    "and must stay inside the documented bound");
        }
    }

    void testSavingIsAtomic()
    {
        beginTest ("a preset that is already there survives a save that goes wrong");

        ScratchTree tree ("atomic");

        const auto directory = tree.getRoot();

        juce::File written;

        expectEquals (static_cast<int> (resources::savePreset (directory, "Keeper",
                                                              documentNamed ("Version One"),
                                                              written)),
                      static_cast<int> (resources::PresetSaveResult::ok));

        expect (written.existsAsFile(), "the preset must be on disk");
        expectEquals (written.getFileName(), juce::String ("Keeper.rnv"));

        const auto firstContents = written.loadFileAsString();

        // Saving over it works, and the file that results is the new one in its
        // entirety rather than the old one with the new one written across it.
        expectEquals (static_cast<int> (resources::savePreset (directory, "Keeper",
                                                              documentNamed ("Version Two"),
                                                              written)),
                      static_cast<int> (resources::PresetSaveResult::ok));

        const auto secondContents = written.loadFileAsString();

        expect (secondContents != firstContents, "the second save must have replaced the first");
        expect (secondContents.contains ("Version Two"));
        expect (! secondContents.contains ("Version One"),
                "and must not have left any of the first behind");

        // Nothing temporary may be left lying about. A save that littered would
        // fill a user's library with debris and confuse the next scan.
        auto leftovers = 0;

        for (const auto& item : juce::RangedDirectoryIterator (directory, false, "*",
                                                               juce::File::findFiles))
            if (! item.getFile().hasFileExtension (presets::fileExtension))
                ++leftovers;

        expectEquals (leftovers, 0, "a finished save must leave no temporary files");
    }

    void testSavingRefusesToEscapeTheLibrary()
    {
        beginTest ("a save cannot be talked into writing outside the folder it was given");

        ScratchTree tree ("escape");

        const auto inside = tree.folder ("Library");
        const auto outside = tree.folder ("Elsewhere");

        const auto sentinel = outside.getChildFile ("autoexec.rnv");

        juce::File written;

        for (const char* hostile : { "../Elsewhere/autoexec", "..\\Elsewhere\\autoexec",
                                     "../../autoexec", "CON", "" })
        {
            const auto result = resources::savePreset (inside, hostile,
                                                       documentNamed ("Hostile"), written);

            // Either refused outright, or written somewhere that is still
            // inside the folder it was given. Both are acceptable; escaping is
            // not (CLAUDE.md §40).
            if (result == resources::PresetSaveResult::ok)
                expect (written.isAChildOf (inside),
                        juce::String (hostile) + " escaped the library");
            else
                expect (result == resources::PresetSaveResult::invalidName
                            || result == resources::PresetSaveResult::outsideLibrary,
                        juce::String (hostile) + " was refused for the wrong reason");

            expect (! sentinel.existsAsFile(),
                    juce::String (hostile) + " wrote outside the library");
        }
    }

    void testSaveAndScanRoundTrip()
    {
        beginTest ("a saved preset is found by the next scan and loads back");

        ScratchTree tree ("roundtrip");

        ApolloAudioProcessor source;

        if (auto* parameter = source.getValueTreeState().getParameter ("osc1_position"))
            parameter->setValueNotifyingHost (0.44f);

        presets::Metadata metadata;
        metadata.name = "Round Trip";
        metadata.category = "Bass";

        const auto text = presets::write (source.getValueTreeState(), metadata);

        juce::File written;

        expectEquals (static_cast<int> (resources::savePreset (tree.getRoot(), metadata.name,
                                                              text, written)),
                      static_cast<int> (resources::PresetSaveResult::ok));

        resources::PresetLocations locations;
        locations.userDirectory = tree.getRoot();

        const auto index = resources::scanPresets (locations);

        expectEquals (static_cast<int> (index.entries.size()), 1);

        const auto* entry = find (index, "Round Trip");
        expect (entry != nullptr, "the saved preset must be in the index");

        if (entry == nullptr)
            return;

        expectEquals (entry->category, juce::String ("Bass"));

        // And the whole way back: from the index, off the disk, into an
        // instrument.
        juce::String loaded;

        expectEquals (static_cast<int> (resources::readPresetFile (entry->file, loaded)),
                      static_cast<int> (state::StateLoadResult::ok));

        ApolloAudioProcessor destination;

        expectEquals (static_cast<int> (presets::read (destination.getValueTreeState(), loaded)),
                      static_cast<int> (state::StateLoadResult::ok));

        if (const auto* parameter = destination.getValueTreeState().getParameter ("osc1_position"))
            expectWithinAbsoluteError (parameter->getValue(), 0.44f, 0.0001f,
                                       "the sound must have made the whole trip");
    }

    void testReadingAMissingFileIsNotACrash()
    {
        beginTest ("reading something that is missing, empty, a folder or enormous fails gracefully");

        ScratchTree tree ("reads");

        juce::String text;

        const auto missing = tree.getRoot().getChildFile ("nope.rnv");

        expectEquals (static_cast<int> (resources::readPresetFile (missing, text)),
                      static_cast<int> (state::StateLoadResult::emptyData),
                      "a file that is not there is not a crash");

        const auto empty = tree.write ("empty.rnv", "");

        expectEquals (static_cast<int> (resources::readPresetFile (empty, text)),
                      static_cast<int> (state::StateLoadResult::emptyData));

        const auto directory = tree.folder ("Folder.rnv");

        expectEquals (static_cast<int> (resources::readPresetFile (directory, text)),
                      static_cast<int> (state::StateLoadResult::emptyData),
                      "a folder wearing the extension is not a preset");

        // Bigger than the document bound, and refused on its size rather than
        // pulled into memory first.
        const auto huge = tree.getRoot().getChildFile ("huge.rnv");

        {
            juce::FileOutputStream stream (huge);

            expect (stream.openedOk(), "the oversized test file must be writable");

            const juce::String chunk = juce::String::repeatedString ("x", 64 * 1024);

            for (int i = 0; i < (presets::maximumDocumentBytes / (64 * 1024)) + 2; ++i)
                stream.writeText (chunk, false, false, nullptr);
        }

        expect (huge.getSize() > presets::maximumDocumentBytes,
                "the test file must actually exceed the bound");

        expectEquals (static_cast<int> (resources::readPresetFile (huge, text)),
                      static_cast<int> (state::StateLoadResult::tooLarge));

        expect (text.isEmpty(), "and nothing of it may have been read");
    }

    void testDeepTreesAreBounded()
    {
        beginTest ("a tree deeper than any real library is cut off rather than followed for ever");

        ScratchTree tree ("deep");

        // Deeper than the bound, which stands in for the case no amount of care
        // avoids on a real filesystem: a symbolic link pointing at one of its
        // own parents.
        juce::String path;

        for (int depth = 0; depth < resources::maximumScanDepth + 4; ++depth)
        {
            path += juce::String (depth) + "/";
            tree.write (path + "Deep.rnv", documentNamed ("Deep " + juce::String (depth)));
        }

        resources::PresetLocations locations;
        locations.userDirectory = tree.getRoot();

        const auto index = resources::scanPresets (locations);

        expect (index.truncated, "the scan must report that it stopped short");

        expect (! index.entries.empty(), "and must still return what it did find");
        expect (static_cast<int> (index.entries.size()) <= resources::maximumScanDepth + 1,
                "nothing below the depth bound may be indexed");

        logMessage ("  indexed " + juce::String (static_cast<int> (index.entries.size()))
                    + " of " + juce::String (resources::maximumScanDepth + 4) + " levels");
    }

    void testAsyncScanDeliversOnTheMessageThread()
    {
        beginTest ("a scan runs off the message thread and delivers its index back onto it");

        ScratchTree tree ("async");

        tree.write ("One.rnv", documentNamed ("One"));
        tree.write ("Two.rnv", documentNamed ("Two"));

        resources::PresetLocations locations;
        locations.userDirectory = tree.getRoot();

        resources::PresetLibrary library;
        library.setLocations (locations);

        expect (library.getIndex().entries.empty(),
                "a library that has not scanned knows nothing");

        library.rescan();
        library.waitForScan();

        // The index is published by the scan thread and readable immediately;
        // the *callback* is the part that waits for the message thread, which is
        // why it is dispatched separately.
        expectEquals (static_cast<int> (library.getIndex().entries.size()), 2);

        auto delivered = false;
        library.onIndexUpdated = [&delivered] { delivered = true; };

        library.rescan();
        library.waitForScan();

        // The test runner is a console program with no message loop, so the
        // delivery that an application would get when the loop next ran is
        // released explicitly here.
        expect (! delivered, "nothing may have been delivered before the loop runs");

        library.flushPendingNotification();

        expect (delivered, "a finished scan must reach onIndexUpdated");
        expectEquals (static_cast<int> (library.getIndex().entries.size()), 2);

        // And a library destroyed while a scan is running must not take the
        // process with it — the case that matters when a plugin window closes.
        {
            resources::PresetLibrary transient;
            transient.setLocations (locations);
            transient.rescan();
        }

        expect (true, "destroying a library mid-scan must return cleanly");
    }
};

PresetLibraryTests presetLibraryTests;

} // namespace
