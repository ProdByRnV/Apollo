# Apollo — Project State

> **Purpose:** the authoritative, verified record of what actually exists in this
> repository. The specification documents describe what Apollo *should* become;
> this file describes what it *is*. Nothing is recorded as complete here unless it
> was built and run.
>
> Update this file at the end of every roadmap step.

**Last verified:** 2026-09-06
**Apollo version:** 0.1.0

---

## 1. Current position

| | |
|---|---|
| **Phase** | Phase 0 — Specification & Repository Setup |
| **Status** | **Complete and verified** |
| **Milestone** | M0 — Repository Ready |
| **Next step** | Phase 1 — Build System & Application Foundation |

Phase 1 has **not** been started. No plugin or standalone target exists.

---

## 2. What is implemented

Everything below was configured, built and executed on this machine.

### Build system

- CMake is the authoritative build system. No Projucer project exists.
- Top-level `CMakeLists.txt` with in-source-build and sub-project guards.
- Build modules in `CMake/`: options, warning policy, dependencies.
- Four configurations: `Debug`, `Release`, `RelWithDebInfo` (default),
  `MinSizeRel`.
- Options: `APOLLO_BUILD_TESTS`, `APOLLO_WARNINGS_AS_ERRORS`,
  `APOLLO_ENABLE_ASAN`, `APOLLO_ENABLE_UBSAN`, `APOLLO_ENABLE_IPO`,
  `APOLLO_JUCE_SOURCE_DIR`, `APOLLO_ALLOW_UNPINNED_JUCE`.
- Sanitizer wiring, including removal of MSVC `/RTC1` and incremental linking,
  which are incompatible with ASan.
- No hard-coded developer or machine-specific paths.

### Dependencies

- **JUCE 8.0.15**, pinned to commit `91ad83ae34a81e0833b1a2b0866f54846370ae53`.
- The pin is *verified*: configure compares the resolved commit against the pin
  and fails on mismatch, because tags are mutable server-side.
- `APOLLO_JUCE_SOURCE_DIR` supports offline builds and CI caching from an
  existing checkout — verified working.
- No other third-party dependency.

### Source

- `apollo_core` (static library) — the shared, host-independent library that the
  plugin, standalone and test targets will all link against.
- `Source/ApolloVersion.h.in` → generated `ApolloVersion.h`; the top-level
  `CMakeLists.txt` is the single source of truth for the product version.
- `Source/Parameters/ParameterId.h/.cpp` — `constexpr` validation of Apollo's
  parameter-ID conventions, with safe, non-leaking issue descriptions.

### Tests

- Framework: `juce::UnitTest` (ships with `juce_core`; no new dependency).
- `ApolloTests` console runner with `--list`, `--category`, `--seed`, `--help`.
- Registered as CTest test `apollo.unit` (label `unit`).
- `Tests/Foundation/BuildInfoTests.cpp` — version header reaches consumers; macro
  and `constexpr` forms agree.
- `Tests/Foundation/ParameterIdTests.cpp` — conventions, including a test that
  every parameter ID documented in UI_BINDINGS.md §3 satisfies them, so the
  documented contract and the implemented rules cannot drift apart.
- **60 assertions, all passing.**

### Conventions and documentation

| Artefact | Purpose |
|---|---|
| `Docs/BUILD.md` | Prerequisites, configurations, options, sanitizer builds |
| `Docs/TESTING.md` | Framework rationale, layout, how to add a test |
| `Docs/CODING-STANDARDS.md` | Coding standards and the two-tier warning policy |
| `Docs/VERSIONING.md` | The independent version axes and compatibility rules |
| `Docs/PARAMETER-CONVENTIONS.md` | Parameter naming and the permanent-ID contract |
| `Docs/REPOSITORY-LAYOUT.md` | Reconciled directory structure |
| `Docs/DECISIONS.md` | Decision log, ADR-0001 … ADR-0009 |
| `README.md` | Entry point and documentation index |
| `.clang-format`, `.editorconfig`, `.gitattributes` | Mechanical formatting |
| `.clangd` | Editor fallback flags (C++20) until a compile database exists |
| `.github/workflows/ci.yml` | Build validation strategy |

---

## 3. What is NOT implemented

**Apollo does not produce audio and does not build a plugin.** Nothing in
Phases 1–12 exists.

Absent, by phase:

| Phase | Absent |
|---|---|
| 1 | `AudioProcessor`, VST3 target, standalone target, audio pass-through, lifecycle handling |
| 2 | APVTS, parameter registry, state serialization, migration, WebView bridge |
| 3 | Audio engine, voice allocation, polyphony, voice stealing, note handling |
| 4 | Wavetable oscillators, unison, sub oscillator, noise, anti-aliasing |
| 5 | Filters, envelopes, LFOs, modulation matrix |
| 6 | MIDI processing, MIDI Learn, controller profiles |
| 7 | The entire `WebUI/` frontend, visualizers, telemetry |
| 8 | Every effect and the FX rack |
| 9 | Presets, wavetable resources, state migration |
| 10–12 | DSP validation, profiling, host testing, packaging, release hardening |

Also absent: `Assets/`, `WebUI/`, and the `Source/` subdirectories beyond
`Parameters/`. These are created when they receive their first file rather than
pre-created empty — see `Docs/REPOSITORY-LAYOUT.md`.

---

## 4. Build status

**Verified on this machine** (Windows 11, MSVC 19.51 / VS 18 2026, Windows SDK
10.0.26100, CMake 4.4.0):

| Configuration | Result |
|---|---|
| `RelWithDebInfo` | Builds clean, no warnings |
| `Debug` | Builds clean, no warnings |
| `Release` + `APOLLO_WARNINGS_AS_ERRORS=ON` | Builds clean, no warnings |
| `APOLLO_JUCE_SOURCE_DIR` (local JUCE checkout) | Configures and builds |

Clean-checkout configure — including the JUCE fetch and pin verification —
succeeded in ~171 s; a warm configure takes ~85 s.

**Not yet verified on any machine:** macOS, Linux, ARM64, and the sanitizer
builds. These are exercised by the CI workflow but CI has not run, because the
repository has no remote (see §6).

---

## 5. Test status

```text
ctest --test-dir build -C RelWithDebInfo --output-on-failure
    Start 1: apollo.unit
1/1 Test #1: apollo.unit ......................   Passed    0.64 sec
100% tests passed out of 1
```

- 2 test classes, 7 test sections, **60 assertions, 0 failures**.
- Passing under `Debug`, `RelWithDebInfo`, and `Release` with warnings as errors.
- **The failure path was explicitly verified**: a temporary failing test was
  added, and the runner exited 1 and CTest reported failure, before it was
  removed. A test harness that cannot fail proves nothing, so this was checked
  rather than assumed.

---

## 6. Known issues

| # | Issue | Severity | Notes |
|---|---|---|---|
| 1 | `ROADMAP.md` §Phase 0 refers to `UI-BINDINGS.md`; the file is `UI_BINDINGS.md` | Trivial | Not renamed silently — the file is the source of truth. Rename the file or fix the reference, but do it deliberately, as other documents cross-reference it. |
| 2 | Not under version control yet, so CI has never executed | Low | The intended remote is `https://github.com/ProdByRnV/Apollo`. Version control is initialised and pushed by the developer, not by the agent — see §10. The CI workflow is the documented strategy and stays unvalidated until then. |
| 3 | Only Windows/MSVC has been built | Medium | macOS, Linux and ARM64 are unverified. Cross-platform validation is Phase 11, but a surprise there is cheaper to find early. |
| 4 | Visual Studio generator emits no `compile_commands.json` | Low | `.clangd` supplies C++20 fallback flags; a Ninja build directory gives full editor accuracy. See `Docs/BUILD.md` §5. |
| 5 | JUCE 9.0.x exists upstream | Informational | Apollo pins JUCE 8 because the specification says JUCE 8. Moving to 9 is a product decision — ADR-0002. |
| 6 | `filter_cutoff` etc. are un-indexed in UI_BINDINGS.md §3 while the PRD specifies two filters | Low | A Phase 2 decision (rename vs. keep and add `filter2_*`). Recorded in `Docs/PARAMETER-CONVENTIONS.md` §3. |

---

## 7. Blockers

**None.** Phase 1 can begin.

Toolchain confirmed available: CMake 4.4.0, MSVC 14.51, Windows SDK 10.0.26100,
Git 2.53, Node 24.15 / npm 11.12 (for the Phase 7 frontend), network access for
dependency fetching.

Not installed, and needed later: Ninja (convenient but optional), a DAW for
host testing (Phase 10), macOS and Linux machines or CI runners (Phase 11).

---

## 8. Recommended next action

Begin **Phase 1 — Build System & Application Foundation**, which is the earliest
incomplete roadmap step. Its scope:

1. VST3 and standalone targets via `juce_add_plugin`, both thin shells over
   `apollo_core` (ADR-0005, `Docs/REPOSITORY-LAYOUT.md` §3).
2. `ApolloAudioProcessor`: initialization, `prepareToPlay`, `releaseResources`,
   `reset`, bypass, destruction/reinitialization.
3. Variable block sizes and sample-rate changes handled correctly.
4. A clean audio pass-through, with channel configuration validated.
5. Standalone device selection through JUCE's device abstractions, with no
   assumption about driver, interface or connector.
6. Resolve ADR-0009 — symbol visibility — now that real plugin targets exist to
   validate it against.
7. Tests for the processor lifecycle, block-size changes and sample-rate changes.

Phase 1 exit criteria are in `ROADMAP.md`. Note that "VST3 loads in a
representative compatible host" needs a DAW, which is a manual verification step
that cannot be automated here.

---

## 9. How to keep this file honest

- Record only what has been built and run. "It compiles" is not "it works"
  (CLAUDE.md §45).
- When a phase completes, tick its `ROADMAP.md` tasks and update §1–§8 here.
- When something is discovered to be broken or unverified, add it to §6 rather
  than quietly removing the claim.
- Architectural decisions go in `Docs/DECISIONS.md`, not here — this file records
  state, not reasoning.

---

## 10. Version control policy

Set by the developer and not subject to agent discretion:

- **Remote:** `https://github.com/ProdByRnV/Apollo`
- **The agent never pushes.** When a push is needed, the agent prints the exact
  commands and the developer runs them.
- **The agent does not initialise or alter version-control state** on its own
  initiative.
- **Feature work goes on its own branch, never directly on `main`.**
