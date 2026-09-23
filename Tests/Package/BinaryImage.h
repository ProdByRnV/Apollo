#pragma once

/*
    What a binary says about itself: what processors it runs on, and what it
    needs to be present before it will load.

    This is the question that found Apollo's dependency on the Visual C++
    Redistributable (ADR-0076): a plugin that needs a library the user does not
    have does not report a missing library — it fails to load, and the host says
    it could not find it. The same question has a different answer on each
    platform, and each platform keeps it in its own container format.

    Read here rather than shelled out to `dumpbin`, `otool` or `ldd`, so that the
    check runs wherever the tests run, needs no developer tools installed on the
    machine, and — the reason it is its own file — can be tested against
    synthetic binaries on every platform rather than only on the one whose format
    it describes (ADR-0077). The Mach-O reader below was written on Windows and
    exercised there, before any macOS runner saw it.

    These parsers are deliberately incurious. They read a header, a table of
    load commands or an import directory, and the names in it. They do not
    resolve anything, follow anything, or execute anything, and every field is
    bounds-checked against the file that actually arrived.
*/

#include <juce_core/juce_core.h>

namespace apollo::package
{

/** What a binary is, and what it wants. */
struct BinaryImage
{
    /** False if the file could not be read or is not a binary of the expected
        format. `problem` says which, in a sentence with no path in it.
    */
    bool valid = false;
    juce::String problem;

    /** The architectures the file contains, in the order they appear. One for
        an ordinary binary; several for a macOS universal binary.
    */
    juce::StringArray architectures;

    /** Every slice is a 64-bit image. */
    bool is64Bit = false;

    /** The libraries the loader must find, by the name recorded in the binary:
        `MSVCP140.dll`, `/usr/lib/libc++.1.dylib`, `libgtk-3.so.0`.
    */
    juce::StringArray dependencies;
};

/** Reads a Windows PE image (`.dll`, `.exe`, or a VST3 bundle's binary). */
[[nodiscard]] BinaryImage readPortableExecutable (const juce::File& file);

/** Reads a macOS Mach-O image, thin or universal. */
[[nodiscard]] BinaryImage readMachO (const juce::File& file);

/** Reads whichever format @p file is, by its magic number rather than by the
    platform the test happens to be running on — so a macOS bundle can be
    inspected from anywhere.

    @returns an invalid image, naming the problem, if the file is neither.
*/
[[nodiscard]] BinaryImage readBinaryImage (const juce::File& file);

/** @returns true if @p name is a library that ships with Windows, and that a
    user is therefore entitled to already have.
*/
[[nodiscard]] bool isWindowsSystemLibrary (const juce::String& name);

/** @returns true if @p name is a library or framework that ships with macOS.

    Anything outside `/usr/lib` and `/System/Library` is something the user
    would have to install — a Homebrew library, a framework copied from a
    developer's machine — and is a plugin that will not load on theirs.
*/
[[nodiscard]] bool isMacOsSystemLibrary (const juce::String& name);

} // namespace apollo::package
