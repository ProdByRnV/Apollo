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

See [TESTING.md](TESTING.md) for the test framework and for how to add tests.

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
