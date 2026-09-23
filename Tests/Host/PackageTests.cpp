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

#include <cstdint>
#include <cstring>
#include <set>
#include <vector>

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

/** The Windows DLLs a machine is entitled to already have.

    Everything on this list ships with Windows itself. Anything else in the
    import table is something the user must install before Apollo will load,
    and a plugin that silently fails to load is indistinguishable from one that
    was never installed — the failure a host reports is "not found".
*/
bool isWindowsSystemLibrary (const juce::String& name)
{
    static const std::set<juce::String> system {
        "kernel32.dll", "user32.dll", "gdi32.dll", "advapi32.dll", "shell32.dll",
        "ole32.dll", "oleaut32.dll", "shlwapi.dll", "comdlg32.dll", "version.dll",
        "winmm.dll", "ws2_32.dll", "imm32.dll", "wininet.dll", "dwmapi.dll",
        "uxtheme.dll", "rpcrt4.dll", "crypt32.dll", "bcrypt.dll", "ntdll.dll",
        "setupapi.dll", "msimg32.dll", "gdiplus.dll", "psapi.dll", "userenv.dll",
        "winspool.drv", "oleacc.dll", "dbghelp.dll", "wldap32.dll", "normaliz.dll",
        "comctl32.dll",
        // JUCE 8 draws through Direct2D, which brings DXGI, Direct3D 11 and
        // DirectComposition with it, and reads per-monitor scaling through the
        // shcore API set. All are Windows components rather than anything a
        // user installs — but they are what sets Apollo's floor at Windows 10
        // (ADR-0076), which is the same floor the WebView2 runtime implies.
        "d2d1.dll", "dxgi.dll", "d3d11.dll", "dcomp.dll",
        "api-ms-win-shcore-scaling-l1-1-1.dll",
        "api-ms-win-crt-runtime-l1-1-0.dll", "api-ms-win-crt-heap-l1-1-0.dll",
        "api-ms-win-crt-math-l1-1-0.dll", "api-ms-win-crt-stdio-l1-1-0.dll",
        "api-ms-win-crt-string-l1-1-0.dll", "api-ms-win-crt-convert-l1-1-0.dll",
        "api-ms-win-crt-locale-l1-1-0.dll", "api-ms-win-crt-time-l1-1-0.dll",
        "api-ms-win-crt-filesystem-l1-1-0.dll", "api-ms-win-crt-utility-l1-1-0.dll",
        "api-ms-win-crt-environment-l1-1-0.dll", "api-ms-win-crt-multibyte-l1-1-0.dll"
    };

    return system.find (name.toLowerCase()) != system.end();
}

/** What a PE file says about itself: what it runs on, and what it needs.

    Parsed here rather than read from `dumpbin`, so the check runs wherever the
    tests run and needs no Visual Studio installation.
*/
struct PortableExecutable
{
    bool valid = false;
    juce::String problem;
    juce::uint16 machine = 0;
    bool is64Bit = false;
    juce::StringArray imports;

    [[nodiscard]] juce::String machineName() const
    {
        switch (machine)
        {
            case 0x8664: return "x86-64";
            case 0xaa64: return "ARM64";
            case 0x014c: return "x86";
            default:     return "0x" + juce::String::toHexString (static_cast<int> (machine));
        }
    }
};

template <typename T>
T readAt (const juce::MemoryBlock& data, std::size_t offset, bool& ok)
{
    if (offset + sizeof (T) > data.getSize())
    {
        ok = false;
        return {};
    }

    T value {};
    std::memcpy (&value, static_cast<const char*> (data.getData()) + offset, sizeof (T));
    return value;
}

PortableExecutable readPortableExecutable (const juce::File& file)
{
    PortableExecutable result;
    juce::MemoryBlock data;

    if (! file.loadFileAsData (data))
    {
        result.problem = "could not be read";
        return result;
    }

    bool ok = true;

    if (readAt<juce::uint16> (data, 0, ok) != 0x5a4d || ! ok)
    {
        result.problem = "is not a PE file";
        return result;
    }

    const auto headerOffset = static_cast<std::size_t> (readAt<juce::uint32> (data, 0x3c, ok));

    if (! ok || readAt<juce::uint32> (data, headerOffset, ok) != 0x00004550)
    {
        result.problem = "has no PE signature";
        return result;
    }

    result.machine = readAt<juce::uint16> (data, headerOffset + 4, ok);

    const auto sectionCount = readAt<juce::uint16> (data, headerOffset + 6, ok);
    const auto optionalHeaderSize = readAt<juce::uint16> (data, headerOffset + 20, ok);
    const auto optionalHeader = headerOffset + 24;
    const auto magic = readAt<juce::uint16> (data, optionalHeader, ok);

    if (! ok || (magic != 0x10b && magic != 0x20b))
    {
        result.problem = "has no recognisable optional header";
        return result;
    }

    result.is64Bit = (magic == 0x20b);

    // The import directory is entry 1 of the data directory, which follows the
    // optional header's fixed part — at a different offset for PE32 and PE32+.
    const auto dataDirectory = optionalHeader + (result.is64Bit ? 112 : 96);
    const auto importRva = readAt<juce::uint32> (data, dataDirectory + 8, ok);

    if (! ok)
    {
        result.problem = "has a truncated data directory";
        return result;
    }

    // Section headers follow the optional header, and are what turns a virtual
    // address into a file offset.
    struct Section { juce::uint32 virtualAddress, virtualSize, rawAddress, rawSize; };
    std::vector<Section> sections;

    for (int i = 0; i < sectionCount; ++i)
    {
        const auto header = optionalHeader + optionalHeaderSize + static_cast<std::size_t> (i) * 40;

        sections.push_back ({ readAt<juce::uint32> (data, header + 12, ok),
                              readAt<juce::uint32> (data, header + 8, ok),
                              readAt<juce::uint32> (data, header + 20, ok),
                              readAt<juce::uint32> (data, header + 16, ok) });
    }

    const auto toFileOffset = [&sections] (juce::uint32 rva) -> std::size_t
    {
        for (const auto& section : sections)
            if (rva >= section.virtualAddress && rva < section.virtualAddress + juce::jmax (section.virtualSize, section.rawSize))
                return static_cast<std::size_t> (section.rawAddress + (rva - section.virtualAddress));

        return 0;
    };

    if (importRva == 0)
    {
        // A binary that imports nothing at all would be remarkable, but it is
        // not malformed.
        result.valid = ok;
        return result;
    }

    auto descriptor = toFileOffset (importRva);

    if (descriptor == 0)
    {
        result.problem = "has an import directory outside its sections";
        return result;
    }

    // Each descriptor is 20 bytes and the list ends with a zeroed one. The
    // name is at offset 12, as a virtual address of a NUL-terminated string.
    for (int i = 0; i < 4096; ++i, descriptor += 20)
    {
        const auto nameRva = readAt<juce::uint32> (data, descriptor + 12, ok);
        const auto firstThunk = readAt<juce::uint32> (data, descriptor + 16, ok);

        if (! ok || (nameRva == 0 && firstThunk == 0))
            break;

        const auto nameOffset = toFileOffset (nameRva);

        if (nameOffset == 0 || nameOffset >= data.getSize())
            continue;

        const auto* start = static_cast<const char*> (data.getData()) + nameOffset;
        const auto available = data.getSize() - nameOffset;
        const auto length = strnlen (start, available);

        result.imports.addIfNotAlreadyThere (juce::String (start, length));
    }

    result.valid = ok;
    return result;
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
        testRuntimeDependencies();
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

    void testRuntimeDependencies()
    {
        beginTest ("The plugin depends only on what the user's machine already has");

        const auto binary = binaryInBundle();

        if (! binary.existsAsFile())
        {
            expect (false, "No binary to inspect");
            return;
        }

       #if JUCE_WINDOWS
        const auto pe = readPortableExecutable (binary);
        expect (pe.valid, "The binary parses as a PE image: " + pe.problem);

        if (! pe.valid)
            return;

        expect (pe.is64Bit, "It is a 64-bit image");
        logMessage ("    machine: " + pe.machineName() + ", imports " + juce::String (pe.imports.size()) + " libraries");

        juce::StringArray notOnTheMachine;

        for (const auto& import : pe.imports)
        {
            logMessage ("      " + import);

            if (! isWindowsSystemLibrary (import))
                notOnTheMachine.add (import);
        }

        // The two that would actually happen: the Visual C++ runtime, which
        // needs a redistributable installed, and WebView2Loader.dll, which is
        // linked statically precisely so it does not have to travel beside the
        // plugin (Source/Plugin/CMakeLists.txt).
        expectEquals (notOnTheMachine.joinIntoString (", "), juce::String(),
                      "Every imported library ships with Windows");

        // The standalone travels in the same package and is installed by the
        // same copy, so it has to be as self-contained as the plugin. Present
        // only when the harness is pointed at a staged package.
        const auto standalone = bundle().getParentDirectory().getParentDirectory()
                                        .getChildFile ("Standalone").getChildFile ("Apollo.exe");

        if (standalone.existsAsFile())
        {
            const auto application = readPortableExecutable (standalone);
            expect (application.valid, "The standalone parses as a PE image: " + application.problem);

            juce::StringArray applicationNeeds;

            for (const auto& import : application.imports)
                if (! isWindowsSystemLibrary (import))
                    applicationNeeds.add (import);

            expectEquals (applicationNeeds.joinIntoString (", "), juce::String(),
                          "The standalone imports only Windows libraries too");
            logMessage ("    standalone: " + application.machineName() + ", imports "
                        + juce::String (application.imports.size()) + " libraries");
        }
        else
        {
            logMessage ("    no staged standalone beside this bundle; plugin only");
        }
       #else
        logMessage ("    dependency inspection is implemented for Windows only so far (Phase 11a)");
        expect (binary.getSize() > 0);
       #endif
    }
};

static PackageTests packageTests;

} // namespace apollo::host
