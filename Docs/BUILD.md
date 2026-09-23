# Building Apollo

CMake is the authoritative build system. Projucer is not used and no Projucer
project is checked in (CLAUDE.md §31, ARCHITECTURE.md §10).

---

## 1. Prerequisites

| Requirement | Notes |
|---|---|
| CMake | 3.22 or newer |
| C++20 toolchain | MSVC 19.3x+ / Apple Clang 14+ / Clang 15+ / GCC 12+ |
| Git | Required at configure time to fetch and verify the pinned JUCE revision |
| Node.js 20+ | Required to **build the plugin**, which bundles the React/TypeScript interface. Not required for `apollo_core` or `ApolloTests`, and not required at all with `-DAPOLLO_ENABLE_WEBVIEW=OFF` |
| Network access | Required on the first configure only, unless `APOLLO_JUCE_SOURCE_DIR` is used, and on the first frontend build to install its twelve packages |

Platform toolchains:

- **Windows** — Visual Studio with the "Desktop development with C++" workload,
  or Clang. A Windows 10/11 SDK is required.
- **macOS** — Xcode command line tools.
- **Linux** — a C++20 compiler plus the JUCE system dependencies. For the
  current test-only build, ALSA headers are sufficient; the plugin and
  standalone targets added in Phase 1 will need the wider set listed in
  `.github/workflows/ci.yml`.

Apollo pins **JUCE 8.0.15** (commit `91ad83ae…`). The configure step verifies the
resolved commit and fails if it does not match, so the pin is enforced rather
than merely documented.

The frontend is pinned the same way, in `WebUI/package-lock.json`, and CMake
installs it with `npm ci` rather than `npm install` — the difference being that
`ci` installs exactly the locked tree and fails if the lock file and the manifest
disagree. Editing anything under `WebUI/src` rebuilds the bundle and relinks the
plugin; the built files live in the build tree and are never committed
(ADR-0050).

---

## 2. Configure and build

```sh
cmake -S . -B build
cmake --build build --config RelWithDebInfo
```

The first configure clones JUCE and builds JUCE's `juceaide` helper, which takes
a few minutes. Subsequent configures reuse the cached checkout.

Single-configuration generators (Ninja, Unix Makefiles) default to
`RelWithDebInfo` and take the configuration at configure time:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

### Supported configurations

| Configuration | Purpose |
|---|---|
| `Debug` | Unoptimised, assertions enabled. DSP debugging. |
| `RelWithDebInfo` | Optimised with debug info. **Default** — real-time audio work needs realistic performance while remaining debuggable. |
| `Release` | Shipping configuration. |
| `MinSizeRel` | Retained for packaging experiments. |

---

## 3. Running tests

```sh
ctest --test-dir build -C RelWithDebInfo --output-on-failure
```

The runner can also be invoked directly for faster iteration:

```sh
./build/Tests/ApolloTests_artefacts/RelWithDebInfo/ApolloTests --list
./build/Tests/ApolloTests_artefacts/RelWithDebInfo/ApolloTests --category Foundation
```

The host harness loads the VST3 bundle the build produced and drives it
through the VST3 interfaces, as a DAW does. It builds the plugin first:

```sh
cmake --build build --config RelWithDebInfo --target ApolloHostTests
ctest --test-dir build -C RelWithDebInfo -L host -V
```

See [TESTING.md](TESTING.md) for the test framework, for the host harness
(§8), and for how to add tests.

---

## 4. Build options

All options are ordinary CMake cache variables (`-DOPTION=ON`).

| Option | Default | Effect |
|---|---|---|
| `APOLLO_BUILD_TESTS` | `ON` when top-level | Build the test suite. |
| `APOLLO_WARNINGS_AS_ERRORS` | `OFF` | Promote Apollo's own warnings to errors. CI enables this. |
| `APOLLO_ENABLE_ASAN` | `OFF` | AddressSanitizer. Supported on MSVC, Clang and GCC. |
| `APOLLO_ENABLE_UBSAN` | `OFF` | UndefinedBehaviorSanitizer. Clang/GCC only; ignored with a warning on MSVC. |
| `APOLLO_ENABLE_IPO` | `OFF` | Interprocedural optimisation for Release/RelWithDebInfo, where supported. |
| `APOLLO_ENABLE_WEBVIEW` | `ON` | Build the WebView editor. With it off, the plugin falls back to a generic parameter editor and no WebView dependency is required. |
| `APOLLO_JUCE_SOURCE_DIR` | *(empty)* | Use an existing JUCE checkout instead of fetching. |
| `APOLLO_ALLOW_UNPINNED_JUCE` | `OFF` | Permit a JUCE checkout that does not match the pinned commit. |
| `APOLLO_COPY_STANDALONE_TO_ROOT` | `ON` | After building, copy the standalone application into the project root. |

### The standalone in the project root

Every build of the standalone leaves a runnable copy of it in the project root,
beside `README.md` and the other top-level documents — `Apollo.exe` on Windows,
`Apollo` on Linux, `Apollo.app` on macOS. It is there so the application can be
launched without walking down to
`build*/Source/Plugin/Apollo_artefacts/<config>/Standalone/`, and it is ignored
by git.

**One name, last build wins.** Building `Debug` and then `RelWithDebInfo` leaves
the `RelWithDebInfo` build in the root, because "the one I just built" is what
the shortcut is for. If you need to be certain which configuration is sitting
there, rebuild the one you want.

Turn it off with `-DAPOLLO_COPY_STANDALONE_TO_ROOT=OFF`, which is what a
packaging or CI job should do: neither has any use for the copy.

### Sanitizer builds

```sh
cmake -S . -B build-asan -DAPOLLO_ENABLE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

On MSVC the Debug run-time checks (`/RTC1`) and incremental linking are removed
automatically when ASan is enabled, because they are mutually exclusive.

### The WebView backend

JUCE selects the WebView backend per platform. macOS uses WKWebView and Linux
uses WebKitGTK, both from the system. **Windows needs the WebView2 SDK**, which
Apollo fetches at a pinned version during configure — nothing has to be
preinstalled (`CMake/ApolloWebView.cmake`, ADR-0015). The WebView2 *runtime*
ships with Microsoft Edge and is already present on essentially every Windows
10/11 machine.

On Linux, `libwebkit2gtk-4.1-dev` is required; it is in the dependency list the
CI workflow installs.

To build without any WebView dependency at all:

```sh
cmake -S . -B build -DAPOLLO_ENABLE_WEBVIEW=OFF
```

The plugin then presents a generic parameter editor instead. The parameter,
state and bridge-protocol layers are unaffected — they do not depend on the
WebView (ADR-0011, ADR-0016).

### Offline / shared JUCE checkout

```sh
cmake -S . -B build -DAPOLLO_JUCE_SOURCE_DIR=/path/to/JUCE
```

The path is a cache variable and must never be hard-coded into a committed
CMake file (CLAUDE.md §31.2).

---

## 5. Editor tooling

`CMAKE_EXPORT_COMPILE_COMMANDS` is enabled, but only the Ninja and Makefile
generators emit `compile_commands.json`. For clangd, configure a secondary build
directory:

```sh
cmake -S . -B build-clangd -G Ninja
```

and point clangd at it, or copy `build-clangd/compile_commands.json` to the
repository root. Without a compile database, `.clangd` supplies fallback flags so
that editor diagnostics still use C++20 rather than clangd's default.

---

## 6. Clean rebuild

```sh
rm -rf build
cmake -S . -B build
```

Removing `build/` also discards the cached JUCE checkout, so the next configure
re-clones it.

---

## 7. Packaging

What a user is given: a ZIP holding the plugin, the standalone and a file
saying where to put them (Phase 11a, ADR-0076).

```sh
cmake --build build --config Release --target Apollo_All
cmake --install build --config Release --prefix staged
cd build && cpack -C Release -G ZIP
```

The staged tree, which is exactly what the archive contains:

```text
VST3/Apollo.vst3          the plugin, as a bundle
Standalone/Apollo.exe     the standalone application
INSTALL.txt               where the plugin goes, per platform
README.md
```

The archive is named `Apollo-<version>-<system>-<arch>.zip`, so two downloads
cannot be confused for each other.

**No installer.** Apollo is a folder to copy, and uninstalling is deleting it.
An installer would want administrator rights, an uninstaller and a signing
identity, for a format whose whole convention is a folder in a known place.

**Test the package, not the build tree.** The install step is exactly where a
file goes missing, so the host harness is pointed at the staged bundle:

```sh
./build/Tests/ApolloHostTests_artefacts/Release/ApolloHostTests --plugin staged/VST3/Apollo.vst3
```

That runs the whole host suite — discovery, automation, state, MIDI, bypass —
against the thing that ships, plus the package tests: the bundle layout,
`moduleinfo.json`, no debug leftovers, no path from the build machine, and, on
Windows, the import table of both binaries.

### macOS

A macOS release has to run on both processors and on Macs older than the one
that built it, and neither happens by default (ADR-0077):

```sh
cmake -S . -B build -DAPOLLO_MACOS_UNIVERSAL=ON
cmake --build build --target Apollo_All
cmake --install build --prefix staged
codesign --force --sign - --deep staged/VST3/Apollo.vst3
codesign --force --sign - --deep staged/Standalone/Apollo.app
cd build && cpack -G ZIP
```

| Setting | Default | What it does |
|---|---|---|
| `APOLLO_MACOS_DEPLOYMENT_TARGET` | `11.0` | The oldest macOS the build runs on. Unset, a build demands the version of the machine that made it |
| `APOLLO_MACOS_UNIVERSAL` | `OFF` | Builds `arm64;x86_64` in one binary. Off for ordinary work because it doubles compile time; on for anything shipped, or every Intel Mac gets nothing |

The package test asserts the architectures are really in the binary, so a flag
that silently did nothing is caught.

**Signing.** The staged bundles are signed ad-hoc, which needs no certificate
and is enough for macOS to load them locally. A package for distribution needs a
Developer ID certificate and Apple notarisation, which need credentials this
project does not have; a downloaded plugin without them is refused as coming
from an unidentified developer. On Windows nothing is signed, and an unsigned
plugin is copied without complaint.

### Runtime dependencies

`APOLLO_MSVC_STATIC_RUNTIME` (on by default) links the Visual C++ runtime
statically, so nothing Apollo ships needs the Redistributable installed. This
was measured rather than assumed: with the default dynamic runtime the plugin
imported `MSVCP140.dll`, `VCRUNTIME140.dll` and `VCRUNTIME140_1.dll`, and a
machine without them does not report a missing runtime — the plugin fails to
load and the host says it could not find it.

The package test asserts the whole import table against the set of libraries
that ship with Windows, so a new dependency fails the build rather than
appearing as a support request.
