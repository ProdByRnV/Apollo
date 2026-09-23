/*
    The binary readers, against binaries built by hand.

    The point of these is that they run everywhere. The Mach-O reader exists to
    check a macOS package, and this project has no Mac: written without these it
    would first be exercised on a CI runner, where a failure is twenty minutes
    away and says only that something did not match (ADR-0077).

    So the fixtures are assembled byte by byte here — a universal binary with
    two slices, a thin one, a dylib list, and several malformed files — and the
    reader is asked what it makes of them on whatever platform is running the
    suite.
*/

#include <juce_core/juce_core.h>

#include "Package/BinaryImage.h"

#include <vector>

using namespace apollo::package;

namespace
{

/** A little-endian byte assembler, which is how both formats store everything
    except Mach-O's fat header.
*/
struct Bytes
{
    std::vector<juce::uint8> data;

    void u8 (juce::uint8 value) { data.push_back (value); }

    void u32 (juce::uint32 value)
    {
        for (int i = 0; i < 4; ++i)
            data.push_back (static_cast<juce::uint8> ((value >> (8 * i)) & 0xff));
    }

    /** Big-endian, for Mach-O's fat header. */
    void u32be (juce::uint32 value)
    {
        for (int i = 3; i >= 0; --i)
            data.push_back (static_cast<juce::uint8> ((value >> (8 * i)) & 0xff));
    }

    void u64 (juce::uint64 value)
    {
        for (int i = 0; i < 8; ++i)
            data.push_back (static_cast<juce::uint8> ((value >> (8 * i)) & 0xff));
    }

    void text (const juce::String& value)
    {
        const auto* utf8 = value.toRawUTF8();

        for (int i = 0; utf8[i] != 0; ++i)
            data.push_back (static_cast<juce::uint8> (utf8[i]));

        data.push_back (0);
    }

    void padTo (std::size_t multiple)
    {
        while (data.size() % multiple != 0)
            data.push_back (0);
    }

    [[nodiscard]] std::size_t size() const { return data.size(); }
};

/** One LC_LOAD_DYLIB command naming a library. */
Bytes dylibCommand (const juce::String& path)
{
    Bytes command;
    Bytes name;
    name.text (path);
    name.padTo (8);

    // cmd, cmdsize, name offset, timestamp, current version, compatible
    // version — 24 bytes before the name.
    const auto commandSize = static_cast<juce::uint32> (24 + name.size());

    command.u32 (0x0c);           // LC_LOAD_DYLIB
    command.u32 (commandSize);
    command.u32 (24);             // the name begins after the fixed part
    command.u32 (0);
    command.u32 (0x00010000);
    command.u32 (0x00010000);

    for (const auto byte : name.data)
        command.data.push_back (byte);

    return command;
}

/** A thin 64-bit Mach-O image for one processor, needing the given libraries. */
Bytes machOSlice (juce::uint32 cpuType, const juce::StringArray& dependencies)
{
    Bytes commands;

    for (const auto& dependency : dependencies)
        for (const auto byte : dylibCommand (dependency).data)
            commands.data.push_back (byte);

    Bytes image;
    image.u32 (0xfeedfacf);       // MH_MAGIC_64
    image.u32 (cpuType);
    image.u32 (0);                // cpusubtype
    image.u32 (6);                // MH_DYLIB
    image.u32 (static_cast<juce::uint32> (dependencies.size()));
    image.u32 (static_cast<juce::uint32> (commands.size()));
    image.u32 (0);                // flags
    image.u32 (0);                // reserved

    for (const auto byte : commands.data)
        image.data.push_back (byte);

    return image;
}

/** A universal binary wrapping the given slices. */
Bytes universalBinary (const std::vector<Bytes>& slices, const std::vector<juce::uint32>& cpuTypes)
{
    Bytes file;
    file.u32be (0xcafebabe);
    file.u32be (static_cast<juce::uint32> (slices.size()));

    // Each slice is placed on a 4 KB boundary, as the real linker does.
    auto offset = static_cast<juce::uint32> (8 + slices.size() * 20);
    offset = (offset + 0xfff) & ~0xfffu;

    std::vector<juce::uint32> offsets;

    for (std::size_t i = 0; i < slices.size(); ++i)
    {
        offsets.push_back (offset);

        file.u32be (cpuTypes[i]);
        file.u32be (0);                                          // cpusubtype
        file.u32be (offset);
        file.u32be (static_cast<juce::uint32> (slices[i].size()));
        file.u32be (12);                                         // align, 2^12

        offset = static_cast<juce::uint32> ((offset + slices[i].size() + 0xfff) & ~0xfffu);
    }

    for (std::size_t i = 0; i < slices.size(); ++i)
    {
        while (file.size() < offsets[i])
            file.u8 (0);

        for (const auto byte : slices[i].data)
            file.data.push_back (byte);
    }

    return file;
}

/** A 64-bit little-endian ELF shared object needing the given libraries.

    Laid out so that virtual addresses and file offsets are the same number —
    one PT_LOAD covering the whole file at address zero — which is legitimate
    and keeps the fixture readable. The reader is not told this and still has
    to resolve the string table's address through the segment table.
*/
Bytes elfSharedObject (juce::uint16 machine, const juce::StringArray& dependencies)
{
    // The string table, and where each name begins inside it. Index 0 is the
    // empty string, as an ELF string table always starts.
    Bytes strings;
    strings.u8 (0);

    std::vector<juce::uint32> nameOffsets;

    for (const auto& dependency : dependencies)
    {
        nameOffsets.push_back (static_cast<juce::uint32> (strings.size()));
        strings.text (dependency);
    }

    constexpr std::size_t headerSize = 64;
    constexpr std::size_t programHeaderSize = 56;
    constexpr std::size_t programHeaderCount = 2;
    const std::size_t dynamicOffset = headerSize + programHeaderSize * programHeaderCount;

    // One DT_NEEDED each, then DT_STRTAB, DT_STRSZ and DT_NULL.
    const std::size_t dynamicEntries = dependencies.size() + 3;
    const std::size_t dynamicSize = dynamicEntries * 16;
    const std::size_t stringsOffset = dynamicOffset + dynamicSize;
    const std::size_t totalSize = stringsOffset + strings.size();

    Bytes file;

    // ELF header.
    file.u32 (0x464c457f);           // magic
    file.u8 (2);                     // 64-bit
    file.u8 (1);                     // little-endian
    file.u8 (1);                     // version
    file.u8 (0);                     // System V ABI

    for (int i = 0; i < 8; ++i)
        file.u8 (0);                 // padding to 16 bytes

    file.u32 (3 | (machine << 16));  // e_type = ET_DYN, e_machine
    file.u32 (1);                    // e_version
    file.u64 (0);                    // e_entry
    file.u64 (headerSize);           // e_phoff
    file.u64 (0);                    // e_shoff
    file.u32 (0);                    // e_flags
    file.u32 (static_cast<juce::uint32> (headerSize) | (programHeaderSize << 16));
    file.u32 (programHeaderCount | (0u << 16));  // e_phnum, e_shentsize
    file.u32 (0);                    // e_shnum, e_shstrndx

    // PT_LOAD over the whole file, at address zero.
    file.u32 (1);                    // p_type
    file.u32 (5);                    // p_flags
    file.u64 (0);                    // p_offset
    file.u64 (0);                    // p_vaddr
    file.u64 (0);                    // p_paddr
    file.u64 (totalSize);            // p_filesz
    file.u64 (totalSize);            // p_memsz
    file.u64 (0x1000);               // p_align

    // PT_DYNAMIC over the dynamic array.
    file.u32 (2);
    file.u32 (6);
    file.u64 (dynamicOffset);
    file.u64 (dynamicOffset);
    file.u64 (dynamicOffset);
    file.u64 (dynamicSize);
    file.u64 (dynamicSize);
    file.u64 (8);

    for (const auto offset : nameOffsets)
    {
        file.u64 (1);                // DT_NEEDED
        file.u64 (offset);
    }

    file.u64 (5);                    // DT_STRTAB
    file.u64 (stringsOffset);        // as a virtual address, which here is the offset
    file.u64 (10);                   // DT_STRSZ
    file.u64 (strings.size());
    file.u64 (0);                    // DT_NULL
    file.u64 (0);

    for (const auto byte : strings.data)
        file.data.push_back (byte);

    return file;
}

juce::File writeTemporary (const Bytes& bytes, const juce::String& name)
{
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("apollo-binary-image-tests")
                          .getChildFile (name);

    file.getParentDirectory().createDirectory();
    file.replaceWithData (bytes.data.data(), bytes.data.size());
    return file;
}

//==============================================================================
class BinaryImageTests final : public juce::UnitTest
{
public:
    BinaryImageTests() : juce::UnitTest ("Binary images", "Package") {}

    void runTest() override
    {
        testUniversalBinary();
        testThinBinary();
        testElfSharedObject();
        testFormatIsDetectedByContent();
        testMalformedFilesAreRefused();
        testSystemLibraryRules();
        testLinuxDependencyClasses();

        juce::File::getSpecialLocation (juce::File::tempDirectory)
            .getChildFile ("apollo-binary-image-tests")
            .deleteRecursively();
    }

private:
    void testUniversalBinary()
    {
        beginTest ("A universal binary reports both processors and every library it needs");

        // What a macOS release has to be: one file that runs on an Apple
        // Silicon Mac and on an Intel one. A package with only the builder's
        // architecture in it works perfectly on the machine that made it.
        const juce::StringArray dependencies {
            "/usr/lib/libc++.1.dylib",
            "/usr/lib/libSystem.B.dylib",
            "/System/Library/Frameworks/WebKit.framework/Versions/A/WebKit"
        };

        const auto file = writeTemporary (universalBinary ({ machOSlice (0x0100000c, dependencies),
                                                             machOSlice (0x01000007, dependencies) },
                                                           { 0x0100000c, 0x01000007 }),
                                          "universal.dylib");

        const auto image = readMachO (file);

        expect (image.valid, image.problem);
        expect (image.is64Bit);
        expectEquals (image.architectures.size(), 2);
        expect (image.architectures.contains ("arm64"), image.architectures.joinIntoString (", "));
        expect (image.architectures.contains ("x86_64"), image.architectures.joinIntoString (", "));

        // Named once each, not once per slice: the question is what the loader
        // must find, and both slices need the same things.
        expectEquals (image.dependencies.size(), 3);
        expect (image.dependencies.contains ("/usr/lib/libc++.1.dylib"));
        expect (image.dependencies.contains ("/System/Library/Frameworks/WebKit.framework/Versions/A/WebKit"));
    }

    void testThinBinary()
    {
        beginTest ("A single-architecture binary reads as itself");

        const auto file = writeTemporary (machOSlice (0x0100000c, { "/usr/lib/libSystem.B.dylib" }),
                                          "thin.dylib");
        const auto image = readMachO (file);

        expect (image.valid, image.problem);
        expectEquals (image.architectures.size(), 1);
        expectEquals (image.architectures[0], juce::String ("arm64"));
        expectEquals (image.dependencies.size(), 1);
    }

    void testElfSharedObject()
    {
        beginTest ("An ELF shared object reports its processor and the sonames it needs");

        // The Linux plugin is a .so, and what it needs is the DT_NEEDED list:
        // the names the dynamic loader will look for. Resolving them means
        // finding the string table by its virtual address, through the
        // segment table — which is the part worth testing.
        const juce::StringArray dependencies {
            "libwebkit2gtk-4.1.so.0",
            "libgtk-3.so.0",
            "libasound.so.2",
            "libstdc++.so.6",
            "libc.so.6"
        };

        const auto file = writeTemporary (elfSharedObject (0x3e, dependencies), "plugin.so");
        const auto image = readElf (file);

        expect (image.valid, image.problem);
        expect (image.is64Bit);
        expectEquals (image.architectures.size(), 1);
        expectEquals (image.architectures[0], juce::String ("x86_64"));
        expectEquals (image.dependencies.size(), dependencies.size());

        for (const auto& dependency : dependencies)
            expect (image.dependencies.contains (dependency),
                    dependency + " is listed: " + image.dependencies.joinIntoString (", "));

        // And on the other architecture Apollo may one day ship for.
        const auto arm = readElf (writeTemporary (elfSharedObject (0xb7, { "libc.so.6" }), "arm.so"));
        expect (arm.valid, arm.problem);
        expectEquals (arm.architectures[0], juce::String ("arm64"));
    }

    void testFormatIsDetectedByContent()
    {
        beginTest ("The format is decided by what is in the file, not by the platform reading it");

        // This is what lets a macOS bundle be inspected from a Windows machine,
        // which is the only way this reader could be written at all here.
        const auto machO = writeTemporary (machOSlice (0x01000007, { "/usr/lib/libSystem.B.dylib" }),
                                           "detect.dylib");

        const auto detected = readBinaryImage (machO);
        expect (detected.valid, detected.problem);
        expectEquals (detected.architectures[0], juce::String ("x86_64"));

        Bytes notABinary;
        notABinary.text ("#!/bin/sh\necho hello\n");

        const auto script = readBinaryImage (writeTemporary (notABinary, "script.sh"));
        expect (! script.valid, "A shell script is not a binary image");
        expect (script.problem.isNotEmpty() && ! script.problem.contains ("/"),
                "The refusal names no path: " + script.problem);
    }

    void testMalformedFilesAreRefused()
    {
        beginTest ("Truncated and lying files are refused rather than read past their end");

        // A parser that walks a table whose length the file itself supplies is
        // exactly where a malformed file becomes a crash. Every case here is
        // one the reader could plausibly meet on a half-copied package.
        Bytes empty;
        expect (! readBinaryImage (writeTemporary (empty, "empty")).valid, "An empty file");

        Bytes shortHeader;
        shortHeader.u32 (0xfeedfacf);
        shortHeader.u32 (0x0100000c);
        expect (! readMachO (writeTemporary (shortHeader, "short")).valid, "A header that stops early");

        // A fat header claiming more slices than the file could hold.
        Bytes lyingFat;
        lyingFat.u32be (0xcafebabe);
        lyingFat.u32be (9999);
        expect (! readMachO (writeTemporary (lyingFat, "lying-fat")).valid,
                "A fat header claiming thousands of slices");

        // A slice offset past the end of the file.
        Bytes badOffset;
        badOffset.u32be (0xcafebabe);
        badOffset.u32be (1);
        badOffset.u32be (0x0100000c);
        badOffset.u32be (0);
        badOffset.u32be (0x00f00000);  // offset far past the end
        badOffset.u32be (64);
        badOffset.u32be (12);
        expect (! readMachO (writeTemporary (badOffset, "bad-offset")).valid,
                "A slice offset past the end of the file");

        // A load command whose size would walk backwards forever.
        Bytes zeroSizedCommand;
        zeroSizedCommand.u32 (0xfeedfacf);
        zeroSizedCommand.u32 (0x0100000c);
        zeroSizedCommand.u32 (0);
        zeroSizedCommand.u32 (6);
        zeroSizedCommand.u32 (4);      // four commands
        zeroSizedCommand.u32 (32);
        zeroSizedCommand.u32 (0);
        zeroSizedCommand.u32 (0);
        zeroSizedCommand.u32 (0x0c);
        zeroSizedCommand.u32 (0);      // cmdsize of zero
        zeroSizedCommand.padTo (32);

        expect (! readMachO (writeTemporary (zeroSizedCommand, "zero-command")).valid,
                "A load command with a size of zero");

        // And a PE that stops immediately after its DOS magic.
        Bytes shortPe;
        shortPe.u8 ('M');
        shortPe.u8 ('Z');
        expect (! readPortableExecutable (writeTemporary (shortPe, "short.dll")).valid,
                "A PE file that stops after MZ");

        expect (! readBinaryImage (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                       .getChildFile ("apollo-nothing-here.bin")).valid,
                "A file that does not exist");
    }

    void testSystemLibraryRules()
    {
        beginTest ("What counts as a library the user already has");

        expect (isMacOsSystemLibrary ("/usr/lib/libSystem.B.dylib"));
        expect (isMacOsSystemLibrary ("/System/Library/Frameworks/WebKit.framework/Versions/A/WebKit"));
        expect (isMacOsSystemLibrary ("/usr/lib/libc++.1.dylib"));

        // The ones that would actually happen, and each is a plugin that loads
        // on the machine that built it and nowhere else.
        expect (! isMacOsSystemLibrary ("/opt/homebrew/lib/libfftw3.dylib"), "a Homebrew library");
        expect (! isMacOsSystemLibrary ("/usr/local/lib/libsndfile.dylib"), "a /usr/local library");
        expect (! isMacOsSystemLibrary ("@rpath/libSomething.dylib"),
                "an @rpath library, which means one travelling beside the binary");
        expect (! isMacOsSystemLibrary ("@loader_path/../Frameworks/Thing.framework/Thing"),
                "a framework expected beside the loader");
        expect (! isMacOsSystemLibrary ("/Users/someone/build/libLocal.dylib"), "a developer's own path");

        expect (isWindowsSystemLibrary ("KERNEL32.dll"), "matched without regard to case");
        expect (isWindowsSystemLibrary ("d2d1.dll"));
        expect (! isWindowsSystemLibrary ("MSVCP140.dll"), "the Visual C++ runtime is not a system library");
        expect (! isWindowsSystemLibrary ("WebView2Loader.dll"));
    }

    void testLinuxDependencyClasses()
    {
        beginTest ("What a Linux user already has, and what they may have to install");

        // Linux has no single answer, so Apollo sorts its dependencies into
        // what any desktop system has and what the installation notes must
        // name (ADR-0078). Getting this wrong in the generous direction is a
        // plugin that does not load and a user with nothing to go on.
        using Dependency = LinuxDependency;

        expect (classifyLinuxDependency ("libc.so.6") == Dependency::alwaysPresent);
        expect (classifyLinuxDependency ("libstdc++.so.6") == Dependency::alwaysPresent);
        expect (classifyLinuxDependency ("libgcc_s.so.1") == Dependency::alwaysPresent);

        // The ones a minimal or headless system will not have, and which the
        // package has to name.
        expect (classifyLinuxDependency ("libgtk-3.so.0") == Dependency::desktopPrerequisite);
        expect (classifyLinuxDependency ("libwebkit2gtk-4.1.so.0") == Dependency::desktopPrerequisite);
        expect (classifyLinuxDependency ("libwebkit2gtk-4.0.so.37") == Dependency::desktopPrerequisite,
                "either WebKitGTK generation, because distributions differ");
        expect (classifyLinuxDependency ("libasound.so.2") == Dependency::desktopPrerequisite);
        expect (classifyLinuxDependency ("libX11.so.6") == Dependency::desktopPrerequisite);
        expect (classifyLinuxDependency ("libfreetype.so.6") == Dependency::desktopPrerequisite);

        // And the ones that mean the package was built wrong: a library from
        // the builder's own tree, or one no distribution ships.
        expect (classifyLinuxDependency ("libfftw3.so.3") == Dependency::unexpected);
        expect (classifyLinuxDependency ("libApolloInternal.so") == Dependency::unexpected);
        expect (classifyLinuxDependency ("(runpath)") == Dependency::unexpected,
                "a runpath is recorded as a dependency, because it searches the builder's machine");
    }
};

static BinaryImageTests binaryImageTests;

} // namespace
