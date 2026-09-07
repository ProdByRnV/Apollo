# Apollo Coding Standards & Warning Policy

Derived from CLAUDE.md §2.3 and ARCHITECTURE.md. This document records the
mechanically enforced rules and the conventions a compiler cannot check.

---

## 1. Language

- **C++20**, extensions disabled (`CMAKE_CXX_EXTENSIONS OFF`), so the same source
  compiles identically under MSVC, Clang and GCC.
- The standard library and JUCE come first; a third-party dependency needs a
  justification recorded in [DECISIONS.md](DECISIONS.md).

---

## 2. Warning policy

Two INTERFACE targets, defined in `CMake/ApolloCompilerWarnings.cmake`:

### `apollo::project_options`

Language level, UTF-8 source/execution encoding, parallel compilation, and any
sanitizer flags. Applied to **every** Apollo target, including targets that
compile JUCE module sources.

### `apollo::strict_warnings`

The aggressive diagnostic set. Applied **PRIVATE to Apollo-owned libraries only**.

| Toolchain | Flags |
|---|---|
| MSVC | `/W4 /permissive- /Zc:__cplusplus /Zc:preprocessor /Zc:inline` |
| GCC / Clang | `-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual -Wold-style-cast -Wcast-align -Wunused -Wconversion -Wsign-conversion -Wdouble-promotion -Wformat=2 -Wnull-dereference` |

`-Wconversion`, `-Wsign-conversion` and `-Wdouble-promotion` are included
deliberately: in DSP code a silent narrowing or an accidental `float`→`double`
promotion inside a per-sample loop is an audio-quality or performance defect, not
a style nit.

`APOLLO_WARNINGS_AS_ERRORS=ON` promotes these to errors. It is **off by default
locally and on in CI**, so exploratory work is not blocked while the mainline
stays clean.

### Why the split exists

Targets that compile JUCE module sources (the test runner today; the plugin and
standalone shells from Phase 1) use JUCE's own recommended warning flags. Applying
Apollo's strict set to third-party sources would bury Apollo's diagnostics in
noise, and the usual reaction to noisy warnings is to disable them.

The practical consequence is an architectural rule that is worth stating plainly:

> **Apollo code belongs in `apollo_core` (and the libraries that follow it), not
> in the plugin/standalone/test shells.** Those shells stay thin.

This is the same boundary ARCHITECTURE.md §5.1 requires between the DSP engine
and the plugin wrapper, enforced by the build system rather than by convention.

### The compilers disagree, so a clean local build proves less than it looks

The four CI jobs do not diagnose the same code identically, and the differences
are not academic — each of the cases below reached `main` green on a developer
machine and was caught only by CI.

- **`-Wshadow` and nested classes.** Clang treats a nested class's function
  parameter as shadowing a field of the *enclosing* class. GCC and MSVC do not.
  A parameter named after an outer member therefore compiles cleanly on three
  jobs and fails the two Clang ones (Phase 4b, `Voice::SourceGain::reset`).
- **Undefined behaviour that optimises away quietly.** An out-of-range
  floating-point to integer conversion produced plausible audio under MSVC and
  was caught only by the Linux Clang sanitizer job (ADR-0022).

The practical rule:

> **A clean MSVC build is not evidence that the strict set passes.** Before
> pushing work that touches `apollo_core`, check it against Clang, which is
> stricter than MSVC on exactly the things Apollo enables the strict set for.

A syntax-only pass is enough to catch the diagnostic differences and takes
seconds, so there is no reason to skip it:

```sh
clang++ -std=c++20 -fsyntax-only -Werror -ISource \
    -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual \
    -Wold-style-cast -Wcast-align -Wunused -Wconversion -Wsign-conversion \
    -Wdouble-promotion -Wformat=2 -Wnull-dereference \
    Source/Engine/Voice.cpp
```

It does not replace the sanitizer job: undefined behaviour needs the code to
actually run under ASan and UBSan, which only CI does.

---

## 3. Formatting

`.clang-format` and `.editorconfig` are authoritative:

- 4-space indentation, no tabs; 120-column soft limit.
- Allman braces, matching JUCE house style.
- `PointerAlignment: Left` (`float* buffer`, not `float *buffer`).
- LF line endings in the repository (`.gitattributes`), regardless of platform.
- `SortIncludes: Never` — include order in JUCE projects is occasionally
  significant, and a reordering tool must not be able to break a build silently.

---

## 4. Conventions a formatter cannot enforce

### Naming

| Entity | Convention | Example |
|---|---|---|
| Namespace | lower case | `apollo::params` |
| Type | `PascalCase` | `WavetableOscillator` |
| Function / method | `camelCase` | `renderNextBlock` |
| Variable / member | `camelCase` | `sampleRate` |
| Constant / `constexpr` | `camelCase` | `maxParameterIdLength` |
| Macro | `APOLLO_UPPER_SNAKE` | `APOLLO_VERSION_MAJOR` |
| File | `PascalCase.h/.cpp` matching its primary type | `ParameterId.h` |

Parameter **IDs** are a separate contract with their own rules — see
[PARAMETER-CONVENTIONS.md](PARAMETER-CONVENTIONS.md).

### Ownership

- RAII everywhere; `std::unique_ptr` for exclusive ownership.
- `std::shared_ptr` only where ownership is genuinely shared, and the sharing is
  documented at the declaration.
- Raw pointers and references are non-owning, always.
- No owning raw pointers.

### Real-time code

Any function reachable from `processBlock` must not allocate, free, lock, log,
touch the filesystem, call the WebView, or perform unbounded work (CLAUDE.md §7).
Mark such functions `noexcept` where the contract allows it, and state the
real-time contract in a comment at the declaration when it is not obvious from
context.

### Interfaces

- Small and explicit; prefer a narrow function over a configuration struct that
  accumulates flags.
- `[[nodiscard]]` on anything whose result is the point of calling it.
- Document non-obvious DSP algorithms with the reasoning and, where relevant, a
  reference — not a restatement of the code.

### Comments

Explain *why*. The code already says what. A comment that will be false after the
next edit is worse than no comment.

---

## 5. Floating point

Fast-math is **not** enabled and must not be. Apollo depends on IEEE-754 semantics
for the NaN/Inf guards and denormal handling required by CLAUDE.md §34.2 and §37;
fast-math lets the compiler assume those values never occur and delete the guards.
Any change here requires measured evidence and a decision record.
