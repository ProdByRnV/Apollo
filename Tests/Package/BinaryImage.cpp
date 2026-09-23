#include "Package/BinaryImage.h"

#include <cstring>
#include <set>
#include <vector>

namespace apollo::package
{

namespace
{

/** Reads a little-endian value at an offset, refusing to read past the end. */
template <typename T>
T readAt (const juce::MemoryBlock& data, std::size_t offset, bool& ok)
{
    if (! ok || offset + sizeof (T) > data.getSize())
    {
        ok = false;
        return {};
    }

    T value {};
    std::memcpy (&value, static_cast<const char*> (data.getData()) + offset, sizeof (T));
    return value;
}

/** Mach-O stores its fat header big-endian whatever the machine reading it. */
juce::uint32 swap32 (juce::uint32 value) noexcept
{
    return ((value & 0x000000ffu) << 24) | ((value & 0x0000ff00u) << 8)
         | ((value & 0x00ff0000u) >> 8)  | ((value & 0xff000000u) >> 24);
}

/** A NUL-terminated string inside the file, bounded by the file's own end. */
juce::String stringAt (const juce::MemoryBlock& data, std::size_t offset, std::size_t limit)
{
    if (offset >= data.getSize())
        return {};

    const auto* start = static_cast<const char*> (data.getData()) + offset;
    const auto available = juce::jmin (limit, data.getSize() - offset);

    return juce::String (start, strnlen (start, available));
}

//==============================================================================
// Mach-O

constexpr juce::uint32 machO64Magic = 0xfeedfacf;
constexpr juce::uint32 machO64Cigam = 0xcffaedfe; ///< Byte-swapped: a big-endian 64-bit image.
constexpr juce::uint32 machO32Magic = 0xfeedface;
constexpr juce::uint32 fatMagic = 0xcafebabe;
constexpr juce::uint32 fatCigam = 0xbebafeca;

constexpr juce::uint32 lcLoadDylib = 0x0c;
constexpr juce::uint32 lcLoadWeakDylib = 0x80000018;
constexpr juce::uint32 lcReexportDylib = 0x8000001f;
constexpr juce::uint32 lcLoadUpwardDylib = 0x80000023;

/** The processors Apollo could plausibly be built for, named as a person would
    name them rather than as a pair of numbers.
*/
juce::String machOArchitecture (juce::uint32 cpuType, juce::uint32 cpuSubType)
{
    switch (cpuType)
    {
        case 0x0100000c: return "arm64";
        case 0x01000007: return "x86_64";
        case 0x0000000c: return "arm";
        case 0x00000007: return "x86";
        default: break;
    }

    return "cpu 0x" + juce::String::toHexString (static_cast<int> (cpuType))
         + "/0x" + juce::String::toHexString (static_cast<int> (cpuSubType));
}

/** Reads one thin image at @p base, adding what it finds to @p image. */
void readMachOSlice (const juce::MemoryBlock& data, std::size_t base, BinaryImage& image, bool& ok)
{
    const auto magic = readAt<juce::uint32> (data, base, ok);

    if (! ok)
    {
        image.problem = "is truncated before a slice header";
        return;
    }

    if (magic == machO32Magic)
    {
        // A 32-bit slice. Recorded rather than parsed: Apollo ships 64-bit
        // only, and the test that cares asserts exactly that.
        image.is64Bit = false;
        image.architectures.add (machOArchitecture (readAt<juce::uint32> (data, base + 4, ok),
                                                    readAt<juce::uint32> (data, base + 8, ok)));
        return;
    }

    if (magic != machO64Magic && magic != machO64Cigam)
    {
        ok = false;
        image.problem = "has a slice that is not a Mach-O image";
        return;
    }

    image.architectures.add (machOArchitecture (readAt<juce::uint32> (data, base + 4, ok),
                                                readAt<juce::uint32> (data, base + 8, ok)));

    const auto commandCount = readAt<juce::uint32> (data, base + 16, ok);

    // The 64-bit header is 32 bytes: magic, cputype, cpusubtype, filetype,
    // ncmds, sizeofcmds, flags, reserved.
    auto command = base + 32;

    for (juce::uint32 i = 0; i < commandCount && ok; ++i)
    {
        const auto kind = readAt<juce::uint32> (data, command, ok);
        const auto size = readAt<juce::uint32> (data, command + 4, ok);

        if (! ok || size < 8)
        {
            ok = false;
            image.problem = "has a load command with an impossible size";
            return;
        }

        if (kind == lcLoadDylib || kind == lcLoadWeakDylib
            || kind == lcReexportDylib || kind == lcLoadUpwardDylib)
        {
            // A dylib command holds an offset, from the start of the command,
            // to the library's path.
            const auto nameOffset = readAt<juce::uint32> (data, command + 8, ok);

            if (ok && nameOffset >= 8 && nameOffset < size)
                image.dependencies.addIfNotAlreadyThere (
                    stringAt (data, command + nameOffset, size - nameOffset));
        }

        command += size;
    }
}

} // namespace

//==============================================================================
BinaryImage readMachO (const juce::File& file)
{
    BinaryImage image;
    juce::MemoryBlock data;

    if (! file.loadFileAsData (data))
    {
        image.problem = "could not be read";
        return image;
    }

    bool ok = true;
    const auto magic = readAt<juce::uint32> (data, 0, ok);

    if (! ok)
    {
        image.problem = "is too small to be a binary";
        return image;
    }

    image.is64Bit = true;

    if (magic == fatMagic || magic == fatCigam)
    {
        // A universal binary: a big-endian table of slices, each naming its
        // processor and where in the file it begins.
        const auto sliceCount = swap32 (readAt<juce::uint32> (data, 4, ok));

        if (! ok || sliceCount == 0 || sliceCount > 32)
        {
            image.problem = "claims an implausible number of architectures";
            return image;
        }

        for (juce::uint32 i = 0; i < sliceCount && ok; ++i)
        {
            const auto entry = 8 + static_cast<std::size_t> (i) * 20;
            const auto offset = swap32 (readAt<juce::uint32> (data, entry + 8, ok));

            if (ok)
                readMachOSlice (data, offset, image, ok);
        }

        image.valid = ok;
        return image;
    }

    if (magic == machO64Magic || magic == machO64Cigam || magic == machO32Magic)
    {
        readMachOSlice (data, 0, image, ok);
        image.valid = ok;
        return image;
    }

    image.problem = "is not a Mach-O image";
    return image;
}

//==============================================================================
// ELF

namespace
{

constexpr juce::uint64 dtNull = 0;
constexpr juce::uint64 dtNeeded = 1;
constexpr juce::uint64 dtStrTab = 5;
constexpr juce::uint64 dtRunPath = 29;
constexpr juce::uint64 dtRpath = 15;

constexpr juce::uint32 ptLoad = 1;
constexpr juce::uint32 ptDynamic = 2;

juce::String elfArchitecture (juce::uint16 machine)
{
    switch (machine)
    {
        case 0x3e: return "x86_64";
        case 0xb7: return "arm64";
        case 0x28: return "arm";
        case 0x03: return "x86";
        default: break;
    }

    return "machine 0x" + juce::String::toHexString (static_cast<int> (machine));
}

} // namespace

BinaryImage readElf (const juce::File& file)
{
    BinaryImage image;
    juce::MemoryBlock data;

    if (! file.loadFileAsData (data))
    {
        image.problem = "could not be read";
        return image;
    }

    bool ok = true;

    // e_ident: the magic, then the class and the byte order.
    if (readAt<juce::uint32> (data, 0, ok) != 0x464c457f || ! ok)
    {
        image.problem = "is not an ELF image";
        return image;
    }

    const auto elfClass = readAt<juce::uint8> (data, 4, ok);
    const auto byteOrder = readAt<juce::uint8> (data, 5, ok);

    if (! ok || elfClass != 2)
    {
        image.problem = "is not a 64-bit ELF image";
        return image;
    }

    if (byteOrder != 1)
    {
        // Every platform Apollo targets is little-endian, and a reader that
        // pretended otherwise would be untested code.
        image.problem = "is big-endian, which this reader does not handle";
        return image;
    }

    image.is64Bit = true;
    image.architectures.add (elfArchitecture (readAt<juce::uint16> (data, 18, ok)));

    const auto programHeaderOffset = static_cast<std::size_t> (readAt<juce::uint64> (data, 32, ok));
    const auto programHeaderSize = readAt<juce::uint16> (data, 54, ok);
    const auto programHeaderCount = readAt<juce::uint16> (data, 56, ok);

    if (! ok || programHeaderSize < 56 || programHeaderCount == 0)
    {
        image.problem = "has no usable program headers";
        return image;
    }

    // PT_LOAD segments are what turn a virtual address into a file offset, and
    // PT_DYNAMIC is the table naming the libraries.
    struct Segment { juce::uint64 offset, virtualAddress, fileSize; };
    std::vector<Segment> loaded;
    Segment dynamic { 0, 0, 0 };
    auto foundDynamic = false;

    for (juce::uint16 i = 0; i < programHeaderCount && ok; ++i)
    {
        const auto header = programHeaderOffset + static_cast<std::size_t> (i) * programHeaderSize;
        const auto type = readAt<juce::uint32> (data, header, ok);
        const Segment segment { readAt<juce::uint64> (data, header + 8, ok),
                                readAt<juce::uint64> (data, header + 16, ok),
                                readAt<juce::uint64> (data, header + 32, ok) };

        if (! ok)
            break;

        if (type == ptLoad)
            loaded.push_back (segment);
        else if (type == ptDynamic)
        {
            dynamic = segment;
            foundDynamic = true;
        }
    }

    if (! ok)
    {
        image.problem = "has a truncated program header table";
        return image;
    }

    if (! foundDynamic)
    {
        // A statically linked binary needs nothing at load time. Valid, and
        // not something Apollo produces.
        image.valid = true;
        return image;
    }

    const auto toFileOffset = [&loaded] (juce::uint64 address, bool& found) -> std::size_t
    {
        for (const auto& segment : loaded)
            if (address >= segment.virtualAddress && address < segment.virtualAddress + segment.fileSize)
                return static_cast<std::size_t> (segment.offset + (address - segment.virtualAddress));

        found = false;
        return 0;
    };

    // Two passes: the string table's address is itself an entry in the table.
    juce::uint64 stringTableAddress = 0;
    std::vector<juce::uint64> neededOffsets;

    const auto entryCount = static_cast<std::size_t> (dynamic.fileSize / 16);

    if (entryCount == 0 || entryCount > 65536)
    {
        image.problem = "has an implausible dynamic section";
        return image;
    }

    for (std::size_t i = 0; i < entryCount && ok; ++i)
    {
        const auto entry = static_cast<std::size_t> (dynamic.offset) + i * 16;
        const auto tag = readAt<juce::uint64> (data, entry, ok);
        const auto value = readAt<juce::uint64> (data, entry + 8, ok);

        if (! ok)
            break;

        if (tag == dtNull)
            break;

        if (tag == dtNeeded)
            neededOffsets.push_back (value);
        else if (tag == dtStrTab)
            stringTableAddress = value;
        else if (tag == dtRunPath || tag == dtRpath)
            // Recorded as a dependency in its own right: a binary that searches
            // a path from the machine that built it is one that finds nothing
            // on anybody else's.
            image.dependencies.addIfNotAlreadyThere ("(runpath)");
    }

    if (! ok)
    {
        image.problem = "has a truncated dynamic section";
        return image;
    }

    if (stringTableAddress == 0)
    {
        image.problem = "names libraries but has no string table";
        return image;
    }

    auto addressFound = true;
    const auto stringTable = toFileOffset (stringTableAddress, addressFound);

    if (! addressFound)
    {
        image.problem = "has a string table outside its loadable segments";
        return image;
    }

    for (const auto offset : neededOffsets)
        image.dependencies.addIfNotAlreadyThere (
            stringAt (data, stringTable + static_cast<std::size_t> (offset), 512));

    image.valid = true;
    return image;
}

//==============================================================================
BinaryImage readPortableExecutable (const juce::File& file)
{
    BinaryImage image;
    juce::MemoryBlock data;

    if (! file.loadFileAsData (data))
    {
        image.problem = "could not be read";
        return image;
    }

    bool ok = true;

    if (readAt<juce::uint16> (data, 0, ok) != 0x5a4d || ! ok)
    {
        image.problem = "is not a PE image";
        return image;
    }

    const auto headerOffset = static_cast<std::size_t> (readAt<juce::uint32> (data, 0x3c, ok));

    if (! ok || readAt<juce::uint32> (data, headerOffset, ok) != 0x00004550)
    {
        image.problem = "has no PE signature";
        return image;
    }

    const auto machine = readAt<juce::uint16> (data, headerOffset + 4, ok);

    switch (machine)
    {
        case 0x8664: image.architectures.add ("x86_64"); break;
        case 0xaa64: image.architectures.add ("arm64"); break;
        case 0x014c: image.architectures.add ("x86"); break;
        default:     image.architectures.add ("machine 0x" + juce::String::toHexString (static_cast<int> (machine)));
                     break;
    }

    const auto sectionCount = readAt<juce::uint16> (data, headerOffset + 6, ok);
    const auto optionalHeaderSize = readAt<juce::uint16> (data, headerOffset + 20, ok);
    const auto optionalHeader = headerOffset + 24;
    const auto optionalMagic = readAt<juce::uint16> (data, optionalHeader, ok);

    if (! ok || (optionalMagic != 0x10b && optionalMagic != 0x20b))
    {
        image.problem = "has no recognisable optional header";
        return image;
    }

    image.is64Bit = (optionalMagic == 0x20b);

    // The import directory is entry 1 of the data directory, which follows the
    // optional header's fixed part — at a different offset for PE32 and PE32+.
    const auto dataDirectory = optionalHeader + (image.is64Bit ? 112 : 96);
    const auto importRva = readAt<juce::uint32> (data, dataDirectory + 8, ok);

    if (! ok)
    {
        image.problem = "has a truncated data directory";
        return image;
    }

    struct Section { juce::uint32 virtualAddress, virtualSize, rawAddress, rawSize; };
    std::vector<Section> sections;

    for (int i = 0; i < sectionCount && ok; ++i)
    {
        const auto header = optionalHeader + optionalHeaderSize + static_cast<std::size_t> (i) * 40;

        sections.push_back ({ readAt<juce::uint32> (data, header + 12, ok),
                              readAt<juce::uint32> (data, header + 8, ok),
                              readAt<juce::uint32> (data, header + 20, ok),
                              readAt<juce::uint32> (data, header + 16, ok) });
    }

    if (! ok)
    {
        image.problem = "has a truncated section table";
        return image;
    }

    const auto toFileOffset = [&sections] (juce::uint32 rva) -> std::size_t
    {
        for (const auto& section : sections)
            if (rva >= section.virtualAddress
                && rva < section.virtualAddress + juce::jmax (section.virtualSize, section.rawSize))
                return static_cast<std::size_t> (section.rawAddress + (rva - section.virtualAddress));

        return 0;
    };

    if (importRva == 0)
    {
        // A binary that imports nothing would be remarkable, but it is not
        // malformed.
        image.valid = ok;
        return image;
    }

    auto descriptor = toFileOffset (importRva);

    if (descriptor == 0)
    {
        image.problem = "has an import directory outside its sections";
        return image;
    }

    // Each descriptor is 20 bytes and the list ends with a zeroed one. The name
    // is at offset 12, as a virtual address of a NUL-terminated string.
    for (int i = 0; i < 4096 && ok; ++i, descriptor += 20)
    {
        const auto nameRva = readAt<juce::uint32> (data, descriptor + 12, ok);
        const auto firstThunk = readAt<juce::uint32> (data, descriptor + 16, ok);

        if (! ok || (nameRva == 0 && firstThunk == 0))
            break;

        const auto nameOffset = toFileOffset (nameRva);

        if (nameOffset != 0)
            image.dependencies.addIfNotAlreadyThere (stringAt (data, nameOffset, 256));
    }

    image.valid = true;
    return image;
}

//==============================================================================
BinaryImage readBinaryImage (const juce::File& file)
{
    juce::FileInputStream stream (file);

    if (! stream.openedOk())
    {
        BinaryImage image;
        image.problem = "could not be read";
        return image;
    }

    // By magic number rather than by the platform running the test, so a macOS
    // bundle can be inspected from anywhere.
    const auto first = static_cast<juce::uint32> (stream.readInt());

    if (first == machO64Magic || first == machO64Cigam || first == machO32Magic
        || first == fatMagic || first == fatCigam)
        return readMachO (file);

    if (first == 0x464c457f)
        return readElf (file);

    if ((first & 0xffff) == 0x5a4d)
        return readPortableExecutable (file);

    BinaryImage image;
    image.problem = "is not a binary this reader knows";
    return image;
}

//==============================================================================
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
        // (ADR-0076), the same floor the WebView2 runtime implies.
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

bool isMacOsSystemLibrary (const juce::String& name)
{
    // macOS records a full path, so the question is which tree it is in rather
    // than which name it has. Everything below is part of the operating
    // system; anything else — a Homebrew library, a framework copied from a
    // developer's machine — is a plugin that will not load on a user's.
    //
    // @rpath, @loader_path and @executable_path are deliberately NOT accepted:
    // they mean "a library travelling beside me", which for a bundle Apollo
    // ships as a single binary means one that is not there.
    return name.startsWith ("/usr/lib/")
        || name.startsWith ("/System/Library/")
        || name.startsWith ("/System/iOSSupport/");
}

LinuxDependency classifyLinuxDependency (const juce::String& soname)
{
    // Linux has no single answer to "what does the user already have", so
    // Apollo's dependencies are sorted into what any system running a desktop
    // application has, and what a user may have to install (ADR-0078). The
    // second list is what the installation notes name; the point of the
    // distinction is that it is written down rather than discovered by
    // somebody whose plugin will not load.
    //
    // Matched on the soname as the loader sees it, version suffix and all,
    // because that is what has to be present: libwebkit2gtk-4.1.so.0 does not
    // satisfy a binary asking for libwebkit2gtk-4.0.so.37.
    static const std::set<juce::String> alwaysPresent {
        "libc.so.6", "libm.so.6", "libdl.so.2", "librt.so.1", "libpthread.so.0",
        "libstdc++.so.6", "libgcc_s.so.1", "ld-linux-x86-64.so.2",
        "ld-linux-aarch64.so.1", "libresolv.so.2"
    };

    if (alwaysPresent.find (soname) != alwaysPresent.end())
        return LinuxDependency::alwaysPresent;

    // A desktop prerequisite is recognised by family rather than by exact
    // version: distributions differ on the suffix, and pinning it here would
    // turn every distribution's ordinary variation into a test failure.
    static const juce::StringArray desktopFamilies {
        "libX11.so", "libXext.so", "libXinerama.so", "libXcursor.so",
        "libXrandr.so", "libXrender.so", "libXcomposite.so", "libxcb.so",
        "libGL.so", "libGLX.so", "libGLdispatch.so", "libEGL.so",
        "libfreetype.so", "libfontconfig.so", "libz.so",
        "libasound.so", "libjack.so",
        "libgtk-3.so", "libgdk-3.so", "libgdk_pixbuf-2.0.so", "libgio-2.0.so",
        "libglib-2.0.so", "libgobject-2.0.so", "libgmodule-2.0.so",
        "libpango-1.0.so", "libpangocairo-1.0.so", "libcairo.so",
        "libcairo-gobject.so", "libatk-1.0.so", "libharfbuzz.so",
        "libwebkit2gtk-4.0.so", "libwebkit2gtk-4.1.so",
        "libjavascriptcoregtk-4.0.so", "libjavascriptcoregtk-4.1.so",
        "libsoup-2.4.so", "libsoup-3.0.so", "libcurl.so"
    };

    for (const auto& family : desktopFamilies)
        if (soname.startsWith (family))
            return LinuxDependency::desktopPrerequisite;

    return LinuxDependency::unexpected;
}

} // namespace apollo::package
