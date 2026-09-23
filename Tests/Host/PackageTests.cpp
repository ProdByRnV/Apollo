/*
    The artefact as it ships.

    Every other test asks whether Apollo works. These ask whether what comes out
    of the build is something a user could be given: the right shape for the
    format, describing itself correctly, carrying nothing it should not, and
    depending on nothing the machine that receives it will not have (ADR-0076).

    They run against whatever bundle the harness was pointed at, so the same
    tests cover the build tree and the staged package — and the staged package
    is the one that matters, because that is what gets copied.
*/


#include "Host/HostedApollo.h"
#include "Package/BinaryImage.h"

#include <set>

namespace apollo::host
{

namespace
{

/** @returns the directory inside the bundle that holds the binary, per the
    VST3 bundle layout for this platform and architecture.
*/
juce::String platformBinaryDirectory()
{
   #if JUCE_WINDOWS
    #if defined (_M_ARM64)
     return "arm64-win";
    #else
     return "x86_64-win";
    #endif
   #elif JUCE_MAC
    return "MacOS";
   #else
    #if defined (__aarch64__)
     return "aarch64-linux";
    #else
     return "x86_64-linux";
    #endif
   #endif
}

} // namespace


class PackageTests final : public juce::UnitTest
{
public:
    PackageTests() : juce::UnitTest ("Package", "Host") {}

    void runTest() override
    {
        testBundleLayout();
        testModuleInfo();
        testNoDebugArtefacts();
        testNoDeveloperPaths();
        testBundleDescribesItselfToTheSystem();
        testRuntimeDependencies();
        testEveryArchitectureTheBuildAskedFor();
    }

private:
    juce::File bundle() const { return getPluginBundle(); }

    juce::File binaryInBundle() const
    {
        const auto directory = bundle().getChildFile ("Contents").getChildFile (platformBinaryDirectory());

        juce::Array<juce::File> files;
        directory.findChildFiles (files, juce::File::findFiles, false);

        return files.isEmpty() ? juce::File {} : files.getFirst();
    }

    //==========================================================================
    void testBundleLayout()
    {
        beginTest ("The VST3 artefact is a bundle of the shape the format requires");

        // A host locates the binary by this layout alone. Anything else — a
        // bare DLL, a differently named architecture folder — is a plugin that
        // simply does not appear, with no error anywhere.
        expect (bundle().isDirectory(), "The artefact is a directory: " + bundle().getFullPathName());
        expectEquals (bundle().getFileExtension(), juce::String (".vst3"), "It is named .vst3");
        expectEquals (bundle().getFileNameWithoutExtension(), juce::String ("Apollo"));

        const auto contents = bundle().getChildFile ("Contents");
        expect (contents.isDirectory(), "It has a Contents directory");

        const auto binaryDirectory = contents.getChildFile (platformBinaryDirectory());
        expect (binaryDirectory.isDirectory(),
                "It has a " + platformBinaryDirectory() + " directory");

        const auto binary = binaryInBundle();
        expect (binary.existsAsFile(), "The binary is there");

        if (binary.existsAsFile())
        {
            // Large enough to be the instrument rather than a stub: the four
            // wavetable spectra, the factory presets and the whole interface
            // are compiled in (ADR-0065, ADR-0066).
            const auto megabytes = static_cast<double> (binary.getSize()) / (1024.0 * 1024.0);
            expect (megabytes > 1.0, "The binary is " + juce::String (megabytes, 1) + " MB");
            logMessage ("    " + binary.getFileName() + ": " + juce::String (megabytes, 1) + " MB");
        }
    }

    void testModuleInfo()
    {
        beginTest ("moduleinfo.json describes the plugin a host will find inside");

        // What a host reads before loading anything, and what lets it skip the
        // load entirely while building a menu. Wrong here means wrong in every
        // plugin list, whatever the binary says.
        const auto file = bundle().getChildFile ("Contents").getChildFile ("Resources")
                                  .getChildFile ("moduleinfo.json");

        expect (file.existsAsFile(), "moduleinfo.json is present");

        if (! file.existsAsFile())
            return;

        const auto parsed = juce::JSON::parse (file);
        expect (parsed.isObject(), "It is valid JSON");

        if (! parsed.isObject())
            return;

        expectEquals (parsed["Name"].toString(), juce::String ("Apollo"));
        expectEquals (parsed["Version"].toString(), juce::String (APOLLO_HOST_TEST_EXPECTED_VERSION));

        const auto factory = parsed["Factory Info"];
        expectEquals (factory["Vendor"].toString(), juce::String ("ProdByRnV"));

        const auto* classes = parsed["Classes"].getArray();
        expect (classes != nullptr && classes->size() >= 1, "It declares at least one class");

        if (classes == nullptr || classes->isEmpty())
            return;

        // The processor class: the one a host instantiates to make sound.
        auto describesTheInstrument = false;

        for (const auto& entry : *classes)
        {
            if (entry["Category"].toString() != "Audio Module Class")
                continue;

            // An array, which is what the format says and what a host reads;
            // the first version of this test asked it for a string and got an
            // empty one.
            juce::StringArray subCategories;

            if (const auto* listed = entry["Sub Categories"].getArray())
                for (const auto& subCategory : *listed)
                    subCategories.add (subCategory.toString());

            if (subCategories.contains ("Instrument") && subCategories.contains ("Synth"))
                describesTheInstrument = true;

            logMessage ("    " + entry["Name"].toString() + ": " + subCategories.joinIntoString ("|")
                        + ", " + entry["SDKVersion"].toString());
        }

        expect (describesTheInstrument, "A class is declared as an Instrument|Synth");
    }

    void testNoDebugArtefacts()
    {
        beginTest ("The bundle carries nothing but what it needs to run");

        // A .pdb is tens of megabytes, is useless to a user, and on Windows
        // contains the absolute path of the machine that built it. A .lib or
        // .exp is a link-time leftover. None belongs in something shipped.
        juce::StringArray unwanted;
        int fileCount = 0;

        for (const auto& entry : juce::RangedDirectoryIterator (bundle(), true, "*", juce::File::findFiles))
        {
            const auto file = entry.getFile();
            const auto extension = file.getFileExtension().toLowerCase();
            ++fileCount;

            if (extension == ".pdb" || extension == ".ilk" || extension == ".exp"
                || extension == ".lib" || extension == ".obj")
                unwanted.add (file.getRelativePathFrom (bundle()));
        }

        expect (fileCount > 0, "The bundle has files in it");
        expectEquals (unwanted.joinIntoString (", "), juce::String(), "No build leftovers");
        logMessage ("    " + juce::String (fileCount) + " files in the bundle");
    }

    void testNoDeveloperPaths()
    {
        beginTest ("Nothing shipped as text names the machine that built it");

        // CLAUDE.md §31.2: no hard-coded developer paths. The text files in the
        // bundle are the ones a user could open, and the ones most likely to
        // have been written by a script that knew where the build tree was.
        juce::StringArray offenders;

        for (const auto& entry : juce::RangedDirectoryIterator (bundle(), true, "*", juce::File::findFiles))
        {
            const auto file = entry.getFile();
            const auto extension = file.getFileExtension().toLowerCase();

            if (extension != ".json" && extension != ".txt" && extension != ".plist"
                && extension != ".md" && extension != ".xml")
                continue;

            const auto text = file.loadFileAsString();

            if (text.contains ("C:\\Users\\") || text.contains ("C:/Users/")
                || text.contains ("/Users/") || text.contains ("/home/")
                || text.contains ("build-strict") || text.contains ("Apollo_artefacts"))
                offenders.add (file.getRelativePathFrom (bundle()));
        }

        expectEquals (offenders.joinIntoString (", "), juce::String(),
                      "No file in the bundle contains a build-machine path");
    }


    void testBundleDescribesItselfToTheSystem()
    {
        beginTest ("The bundle's Info.plist identifies Apollo to the operating system");

        // macOS only: the plist is what identifies a bundle to Launch Services
        // and to a host's plugin scanner, and a wrong identifier means two
        // different plugins that the system believes are the same one.
        const auto plist = bundle().getChildFile ("Contents").getChildFile ("Info.plist");

       #if JUCE_MAC
        expect (plist.existsAsFile(), "Info.plist is present");
       #endif

        if (! plist.existsAsFile())
        {
            logMessage ("    no Info.plist in this bundle; not a macOS package");
            return;
        }

        const auto text = plist.loadFileAsString();

        expect (text.contains ("com.prodbyrnv.apollo"),
                "It carries Apollo's bundle identifier");
        expect (text.contains (APOLLO_HOST_TEST_EXPECTED_VERSION),
                "It carries the project version");

        // The plugin code, as the bundle's signature. It is permanent once
        // released, the same way a parameter ID is.
        //
        // The *manufacturer* code is deliberately not looked for here: a VST3
        // bundle's plist does not carry one — that is an Audio Unit
        // convention — and the vendor a VST3 host actually reads is in
        // moduleinfo.json's Factory Info, which testModuleInfo checks. The
        // first version of this test asserted it and failed on a package that
        // was correct.
        expect (text.contains ("Apol"), "The plugin code is there");
    }

    void testRuntimeDependencies()
    {
        beginTest ("The binaries depend only on what the user's machine already has");

        // The check that found Apollo's dependency on the Visual C++
        // Redistributable (ADR-0076). It reads the binary's own table of what
        // the loader must find, and asks whether each entry is part of the
        // operating system.
        const auto binary = binaryInBundle();

        if (! binary.existsAsFile())
        {
            expect (false, "No binary to inspect");
            return;
        }

       #if JUCE_LINUX
        // Reading an ELF binary's DT_NEEDED entries is Phase 11c, along with
        // deciding which libraries a Linux user is entitled to already have —
        // a question with a different answer on each distribution, and the
        // reason it is its own sub-phase rather than a line here.
        logMessage ("    ELF dependency reading arrives with 11c; the binary is only checked for existence");
        expect (binary.getSize() > 0, "The binary is not empty");
        return;
       #endif

        const auto check = [this] (const juce::File& file, const juce::String& what)
        {
            const auto image = package::readBinaryImage (file);

            expect (image.valid, what + " parses as a binary image: " + image.problem);

            if (! image.valid)
                return;

            expect (image.is64Bit, what + " is a 64-bit image");

            logMessage ("    " + what + ": " + image.architectures.joinIntoString ("+")
                        + ", needs " + juce::String (image.dependencies.size()) + " libraries");

            juce::StringArray notOnTheMachine;

            for (const auto& dependency : image.dependencies)
            {
                logMessage ("      " + dependency);

               #if JUCE_MAC
                if (! package::isMacOsSystemLibrary (dependency))
               #elif JUCE_WINDOWS
                if (! package::isWindowsSystemLibrary (dependency))
               #else
                // Linux is 11c. The dependencies are listed rather than judged
                // until the set that ships with a distribution is decided.
                if (false)
               #endif
                    notOnTheMachine.add (dependency);
            }

            expectEquals (notOnTheMachine.joinIntoString (", "), juce::String(),
                          "Everything " + what + " needs ships with the operating system");
        };

        check (binary, "the plugin");

        // The standalone travels in the same package and is installed by the
        // same copy, so it has to be as self-contained as the plugin. Present
        // only when the harness is pointed at a staged package.
        const auto standaloneDirectory = bundle().getParentDirectory().getParentDirectory()
                                                 .getChildFile ("Standalone");

        if (! standaloneDirectory.isDirectory())
        {
            logMessage ("    no staged standalone beside this bundle; plugin only");
            return;
        }

       #if JUCE_MAC
        const auto standalone = standaloneDirectory.getChildFile ("Apollo.app")
                                                   .getChildFile ("Contents")
                                                   .getChildFile ("MacOS")
                                                   .getChildFile ("Apollo");
       #elif JUCE_WINDOWS
        const auto standalone = standaloneDirectory.getChildFile ("Apollo.exe");
       #else
        const auto standalone = standaloneDirectory.getChildFile ("Apollo");
       #endif

        if (standalone.existsAsFile())
            check (standalone, "the standalone");
        else
            expect (false, "The staged package has a Standalone folder with no application in it");
    }

    void testEveryArchitectureTheBuildAskedFor()
    {
        beginTest ("The binaries contain the processors this build was asked to produce");

        // A macOS release has to run on Apple Silicon and on Intel, and a
        // package built on one of them contains only that one unless it was
        // asked for both. The build states what it asked for, so the package
        // can be checked against the intention rather than against a guess
        // (ADR-0077).
       #ifdef APOLLO_EXPECTED_ARCHITECTURES
        const juce::StringArray expected =
            juce::StringArray::fromTokens (juce::String (APOLLO_EXPECTED_ARCHITECTURES), ",", "");
       #else
        const juce::StringArray expected;
       #endif

        if (expected.isEmpty())
        {
            logMessage ("    the build named no architectures; whatever the toolchain produced is what ships");
            return;
        }

        const auto image = package::readBinaryImage (binaryInBundle());
        expect (image.valid, image.problem);

        if (! image.valid)
            return;

        for (const auto& architecture : expected)
            expect (image.architectures.contains (architecture),
                    "the plugin contains " + architecture + "; it has "
                        + image.architectures.joinIntoString ("+"));

        logMessage ("    asked for " + expected.joinIntoString ("+")
                    + ", built " + image.architectures.joinIntoString ("+"));
    }
};

static PackageTests packageTests;

} // namespace apollo::host
