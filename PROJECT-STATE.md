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
| **Phase** | Phase 1 — Build System & Application Foundation |
| **Status** | **Complete** (two exit criteria need manual verification — see §6) |
| **Milestone** | M1 — Audio Foundation |
| **Next step** | Phase 2 — Parameter, State & UI Binding Infrastructure |

Phase 2 has **not** been started. Apollo has no automatable parameters, no state
serialization and no UI bridge.

**Apollo does not make a sound.** The audio path runs, is real-time safe, and
outputs silence by design; synthesis begins in Phase 3.

---

## 2. What is implemented

Everything below was configured, built and executed on this machine.

### Build system and dependencies (Phase 0)

- CMake is authoritative; no Projucer project exists.
- Build modules in `CMake/`: options, warning policy, dependencies.
- Four configurations: `Debug`, `Release`, `RelWithDebInfo` (default), `MinSizeRel`.
- Options: `APOLLO_BUILD_TESTS`, `APOLLO_WARNINGS_AS_ERRORS`, `APOLLO_ENABLE_ASAN`,
  `APOLLO_ENABLE_UBSAN`, `APOLLO_ENABLE_IPO`, `APOLLO_JUCE_SOURCE_DIR`,
  `APOLLO_ALLOW_UNPINNED_JUCE`.
- **JUCE 8.0.15**, pinned to commit `91ad83ae…` and *verified* at configure time.
- No other third-party dependency.
- No hard-coded developer or machine-specific paths.

### Targets (Phase 1)

| Target | Kind | Contents |
|---|---|---|
| `apollo_core` | STATIC, JUCE-free | Parameter ID conventions and the generated version header. Strict warnings. |
| `apollo_engine` | INTERFACE | The JUCE-dependent engine, propagated to each final target (ADR-0010). |
| `Apollo` | `juce_add_plugin` | **VST3 + Standalone**, both produced from one shared engine. |
| `ApolloTests` | console app | The test suite. |

### Processor (Phase 1)

- `ApolloAudioProcessor`: full lifecycle — `prepareToPlay`, `releaseResources`,
  `reset`, repeated re-initialisation, and safe destruction.
- Tracks its own prepared sample rate and block size rather than relying on the
  host bookkeeping a wrapper performs, so the DSP is prepared correctly even when
  driven directly.
- Variable block sizes (1 … prepared maximum, including zero-length) and sample
  rates (22.05–192 kHz) handled.
- Bus layout policy: mono/stereo output, optional mono/stereo input; wider
  layouts rejected rather than silently mishandled.
- `processBlock` is real-time safe: no allocation, no locks, no I/O, denormals
  flushed, and every unwritten output channel cleared.
- Artefacts built: `Apollo.vst3` (bundle) and standalone `Apollo.exe`.

### Conventions and documentation (Phase 0)

| Artefact | Purpose |
|---|---|
| `Docs/BUILD.md` | Prerequisites, configurations, options, sanitizer builds |
| `Docs/TESTING.md` | Framework rationale, layout, how to add a test |
| `Docs/CODING-STANDARDS.md` | Coding standards and the two-tier warning policy |
| `Docs/VERSIONING.md` | The independent version axes and compatibility rules |
| `Docs/PARAMETER-CONVENTIONS.md` | Parameter naming and the permanent-ID contract |
| `Docs/REPOSITORY-LAYOUT.md` | Reconciled directory structure |
| `Docs/DECISIONS.md` | Decision log, ADR-0001 … ADR-0010 |
| `README.md` | Entry point and documentation index |
| `.clang-format`, `.editorconfig`, `.gitattributes` | Mechanical formatting |
| `.clangd` | Editor fallback flags (C++20) until a compile database exists |
| `.github/workflows/ci.yml` | Build validation strategy |

---

## 3. What is NOT implemented

| Phase | Absent |
|---|---|
| 2 | APVTS, parameter registry, state serialization, migration, UI bridge, WebView |
| 3 | Audio engine, voice allocation, polyphony, voice stealing, note handling |
| 4 | Wavetable oscillators, unison, sub oscillator, noise, anti-aliasing |
| 5 | Filters, envelopes, LFOs, modulation matrix |
| 6 | MIDI processing, MIDI Learn, controller profiles |
| 7 | The entire `WebUI/` frontend, visualizers, telemetry |
| 8 | Every effect and the FX rack |
| 9 | Presets, wavetable resources, resource packaging |
| 10–12 | DSP validation, profiling, host testing, packaging, release hardening |

Concretely, in the current code:

- `getStateInformation` / `setStateInformation` are stubs; there is no state to
  serialize because there are no parameters.
- `hasEditor()` returns `false` and `createEditor()` returns `nullptr`; hosts
  supply a generic editor.
- MIDI is ignored in `processBlock`; it is consumed by the voice engine in Phase 3.
- `Assets/`, `WebUI/`, and the `Source/` subdirectories beyond `Audio/`,
  `Parameters/` and `Plugin/` do not exist. Directories are created when they
  receive their first file — see `Docs/REPOSITORY-LAYOUT.md`.

---

## 4. Build status

**Verified on this machine** (Windows 11, MSVC 19.51 / VS 18 2026, Windows SDK
10.0.26100, CMake 4.4.0):

| Configuration | Result |
|---|---|
| `RelWithDebInfo` | Builds clean, no warnings |
| `Debug` | Builds clean, no warnings |
| `Release` + `APOLLO_WARNINGS_AS_ERRORS=ON` | Builds clean, no warnings — the CI gate passes |
| `APOLLO_JUCE_SOURCE_DIR` (local JUCE checkout) | Configures and builds |

**Not verified on any machine:** macOS, Linux, ARM64, sanitizer builds.

---

## 5. Test status

**208 assertions, 0 failures**, across 3 test classes:

| Category | Class | Covers |
|---|---|---|
| Foundation | Build information | Version header reaches consumers; macro and constexpr forms agree |
| Foundation | Parameter identifier conventions | ID rules; every ID documented in UI_BINDINGS.md §3 is well-formed |
| Audio | Processor lifecycle | Prepare/release/reset, repeated re-initialisation, variable block sizes, sample-rate changes, bus layout policy, output silence and finiteness, metadata |

Passing under `Debug`, `RelWithDebInfo`, and `Release` with warnings as errors.
The runner's failure path was explicitly verified in Phase 0.

### A bug the lifecycle tests caught

The first version of the processor relied on `juce::AudioProcessor::getSampleRate()`
to report what it had been prepared with. That value is set by the plugin
*wrapper* via `setRateAndBufferSizeDetails`, not by `prepareToPlay`, so it read
zero whenever the processor was driven directly — by the test suite, by an
offline renderer, or by a host that skips the call. The processor now records its
own prepared sample rate and block size.

---

## 6. Known issues

| # | Issue | Severity | Notes |
|---|---|---|---|
| 1 | VST3 has not been loaded in a DAW | Medium | The artefact builds; loading needs a host. Phase 1's exit criterion cannot be closed without one. |
| 2 | Standalone has not been launched against an audio device | Medium | Same: needs manual verification, including device selection. |
| 3 | Only Windows/MSVC has been built | Medium | macOS, Linux and ARM64 unverified. Cross-platform validation is Phase 11, but surprises are cheaper to find early. |
| 4 | Symbol visibility still unresolved | Low | ADR-0009 deferred the decision to Phase 1, where real plugin targets would exist. They now do, but the decision was not revisited. Worth closing early in Phase 2. |
| 5 | CI has never executed | Low | The workflow exists and the repository has a remote, but no run has been observed. |
| 6 | No allocation/lock detector on the audio thread | Medium | Real-time safety is currently by construction and review, not enforced by a tool. Phase 10. |
| 7 | `ROADMAP.md` refers to `UI-BINDINGS.md`; the file is `UI_BINDINGS.md` | Trivial | Not renamed silently; other documents cross-reference it. |
| 8 | JUCE 9.0.x exists upstream | Informational | Apollo pins JUCE 8 because the specification says JUCE 8 (ADR-0002). |
| 9 | Company name and plugin codes are inferred | Low | `ProdByRnV`, `Prnv`, `Apol`, `com.prodbyrnv.apollo` were inferred from the GitHub organisation. Easy to change now, **permanent once released** — please confirm. |

---

## 7. Blockers

**None for Phase 2.**

Two Phase 1 exit criteria remain open — loading the VST3 in a DAW, and launching
the standalone against an audio device — but both are manual verification steps
rather than blockers, and neither gates Phase 2 work.

Toolchain confirmed available: CMake 4.4.0, MSVC 14.51, Windows SDK 10.0.26100,
Git 2.53, Node 24.15 / npm 11.12 (for the Phase 7 frontend), network access for
dependency fetching.

One thing to settle before the UI bridge is attached to a WebView: on Windows,
JUCE 8 links WebView2 through `NEEDS_WEBVIEW2`, which requires the **WebView2
NuGet package** (`extras/Build/CMake/FindWebView2.cmake`). That package is not
installed on this machine. The parameter and protocol layers do not depend on it,
so most of Phase 2 can proceed regardless.

---

## 8. Recommended next action

Begin **Phase 2 — Parameter, State & UI Binding Infrastructure**. Its scope:

1. The authoritative APVTS parameter registry, with stable IDs conforming to
   `Docs/PARAMETER-CONVENTIONS.md`, and ranges, defaults, units, steps, skew,
   smoothing and modulation metadata.
2. State serialization and restore, with schema version metadata and an explicit
   migration architecture. Invalid state must be rejected without destroying the
   state already loaded.
3. The UI bridge: WebView initialisation, protocol versioning, initial state
   synchronisation, validated `setParameter`, asynchronous native-to-Web updates,
   gesture semantics, and structured errors — with no path from a frontend
   message to arbitrary native behaviour.
4. Tests for the registry, serialization, restoration, bridge protocol, invalid
   messages and thread safety.

Phase 2 exit criteria are in `ROADMAP.md`. Note that the bridge's validation
surface is far easier to test if the protocol layer is kept independent of any
WebView, which also lets the WebView itself be attached separately.

---

## 9. Version control policy

Set by the developer and not subject to agent discretion:

- **Remote:** `https://github.com/ProdByRnV/Apollo`
- **The agent never pushes.** When a push is needed, the agent prints the exact
  commands and the developer runs them.
- **The agent does not initialise or alter version-control state** on its own
  initiative.
- **Feature work goes on its own branch, never directly on `main`.**

---

## 10. How to keep this file honest

- Record only what has been built and run. "It compiles" is not "it works"
  (CLAUDE.md §45).
- When a phase completes, tick its `ROADMAP.md` tasks and update §1–§8 here.
- When something is discovered to be broken or unverified, add it to §6 rather
  than quietly removing the claim.
- Architectural decisions go in `Docs/DECISIONS.md`, not here — this file records
  state, not reasoning.
