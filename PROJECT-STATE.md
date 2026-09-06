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
| **Phase** | Phase 2 — Parameter, State & UI Binding Infrastructure |
| **Status** | **Complete** |
| **Milestone** | M2 — Parameter Bridge |
| **Next step** | Phase 3 — Audio Engine & Voice Architecture (the path to first sound, M3) |

**Apollo does not make a sound yet.** The audio path runs, is real-time safe, and
outputs silence by design; synthesis begins in Phase 3.

---

## 2. What is implemented

Everything below was configured, built and executed on this machine.

### Build system and dependencies (Phase 0)

- CMake is authoritative; no Projucer project exists.
- Four configurations: `Debug`, `Release`, `RelWithDebInfo` (default), `MinSizeRel`.
- Options: tests, warnings-as-errors, ASan, UBSan, IPO, WebView, local JUCE
  checkout, unpinned-JUCE override.
- **JUCE 8.0.15**, pinned to commit `91ad83ae…` and verified at configure time.
- **WebView2 SDK 1.0.2903.40** on Windows, fetched and pinned (ADR-0015). Nothing
  needs preinstalling.

### Targets

| Target | Kind | Contents |
|---|---|---|
| `apollo_core` | STATIC, JUCE-free | Parameter ID conventions, the parameter registry, version header. Strict warnings. |
| `apollo_engine` | INTERFACE | Processor, parameter layout, state, bridge protocol, parameter bridge. |
| `apollo_editor` | INTERFACE | The WebView editor, kept out of the engine (ADR-0016). |
| `Apollo` | `juce_add_plugin` | **VST3 + Standalone**, both from one engine. |
| `ApolloTests` | console app | The headless test suite — no browser dependency. |

### Processor and plugin (Phase 1)

- Full lifecycle: `prepareToPlay`, `releaseResources`, `reset`, repeated
  re-initialisation, safe destruction.
- Tracks its own prepared sample rate and block size rather than relying on host
  bookkeeping a wrapper performs.
- Variable block sizes (1 … prepared maximum, including zero-length) and sample
  rates (22.05–192 kHz).
- Bus policy: mono/stereo output, optional mono/stereo input; wider layouts
  rejected rather than silently mishandled.
- `processBlock` real-time safe: no allocation, no locks, no I/O, denormals
  flushed, unwritten output channels cleared.

### Parameters and state (Phase 2)

- **Authoritative registry** in `Source/Parameters/ParameterDefinitions.h` as
  JUCE-free `constexpr` data — the nine parameters documented in
  UI_BINDINGS.md §3, each with type, range, default, unit, skew, step,
  automation, modulation and smoothing metadata.
- The APVTS layout, the bridge metadata and state serialization are all generated
  from that one registry, so they cannot drift apart.
- State: serialization and restore, schema version stamped and validated, product
  metadata, an explicit migration architecture with a single extension point, and
  **rejection that preserves the current state**.

### UI bridge and editor (Phase 2)

- `BridgeProtocol` — parsing, validation and serialization, independent of both
  APVTS and any WebView (ADR-0011). Protocol version 1, 8 KB message bound,
  structured non-leaking errors, and a fixed command set with no path to
  arbitrary native behaviour.
- `ParameterBridge` — validated `setParameter`, gesture begin/end mapped to host
  automation boundaries, initial snapshot, parameter metadata, and asynchronous
  native→UI updates.
- **Audio-thread safety**: host automation arrives on the audio thread, where the
  bridge only sets a lock-free atomic flag. A 30 Hz message-thread timer
  coalesces those into outbound messages (ADR-0013).
- `ApolloWebViewEditor` — a `juce::WebBrowserComponent` wired to the bridge via
  `withNativeFunction` inbound and `emitEventIfBrowserIsVisible` outbound. It
  serves a built-in placeholder page that builds its controls **from the
  parameter metadata alone**, never from hard-coded ranges, and detects preset or
  project loads to resynchronise wholesale. The React frontend that replaces that
  page is Phase 7.

---

## 3. What is NOT implemented

| Phase | Absent |
|---|---|
| 3 | Audio engine, voice allocation, polyphony, voice stealing, note handling |
| 4 | Wavetable oscillators, unison, sub oscillator, noise, anti-aliasing |
| 5 | Filters, envelopes, LFOs, modulation matrix |
| 6 | MIDI processing, MIDI Learn, controller profiles |
| 7 | The `WebUI/` React frontend, visualizers, telemetry |
| 8 | Every effect and the FX rack |
| 9 | Presets, wavetable resources, resource packaging |
| 10–12 | DSP validation, profiling, host testing, packaging, release hardening |

MIDI is still ignored in `processBlock`; it is consumed by the voice engine in
Phase 3. The nine registered parameters are exposed to hosts and the UI but do
not yet affect audio, because there is no audio to affect.

---

## 4. Build status

**Verified on this machine** (Windows 11, MSVC 19.51 / VS 18 2026, Windows SDK
10.0.26100, CMake 4.4.0):

| Configuration | Result |
|---|---|
| `RelWithDebInfo` | Builds clean, no warnings |
| `Debug` | Builds clean, no warnings |
| `Release` + `APOLLO_WARNINGS_AS_ERRORS=ON` | Builds clean, no warnings — the CI gate |
| `APOLLO_JUCE_SOURCE_DIR` (local JUCE checkout) | Configures and builds |

**Verified by CI** on the Phase 0 commit: Linux (GCC), macOS (Apple Clang) and
Windows (MSVC) all configure, build and pass tests. The sanitizer job failed there
on a CI dependency-list defect that has since been fixed; that fix has not yet
been confirmed green.

**Still unverified:** ARM64, and the CI matrix against Phase 1/2 code.

---

## 5. Test status

**735 assertions, 0 failures**, across 7 test classes:

| Category | Class | Covers |
|---|---|---|
| Foundation | Build information | Version header reaches consumers; macro and constexpr forms agree |
| Foundation | Parameter identifier conventions | ID rules; every documented ID is well-formed |
| Audio | Processor lifecycle | Prepare/release/reset, variable block sizes, sample rates, bus policy, silence and finiteness |
| Parameters | Parameter registry | Uniqueness, conventions, ranges, defaults, APVTS agreement, normalisation round trip, discrete steps |
| State | State serialization | Round trip, schema stamping, empty/malformed/foreign/unsupported rejection, **state preservation on rejection**, migration boundaries, every parameter round-tripped, reload counter |
| UI | UI bridge protocol | Malformed JSON, non-object payloads, versioning, unknown types, ID validation, NaN/Inf/out-of-range, gesture states, size limit, error hygiene |
| UI | Parameter bridge | Snapshot matches APVTS, commands reach APVTS, invalid commands change nothing, external changes propagate, coalescing, detach safety, metadata completeness |

Passing under `Debug`, `RelWithDebInfo`, and `Release` with warnings as errors.

---

## 6. Known issues

| # | Issue | Severity | Notes |
|---|---|---|---|
| 1 | The editor has never been seen running | Medium | It compiles and links, and the bridge beneath it is thoroughly tested, but no one has opened the plugin and watched the WebView render. This needs a DAW or the standalone app. |
| 2 | VST3 has not been loaded in a DAW | Medium | The artefact builds; loading needs a host. A Phase 1 exit criterion still open. |
| 3 | Standalone has not been launched against an audio device | Medium | Same: manual verification, including device selection. |
| 4 | CI sanitizer job fix unconfirmed | Medium | Cause found and fixed (drifted dependency list); no green run observed yet. |
| 5 | Symbol visibility still unresolved | Low | ADR-0009 deferred the decision to Phase 1. Plugin targets now exist, so it can be closed. |
| 6 | No allocation/lock detector on the audio thread | Medium | Real-time safety is by construction and review, not enforced by a tool. Phase 10. |
| 7 | ARM64 unverified | Low | No ARM64 runner in the matrix. Phase 11. |
| 8 | `ROADMAP.md` refers to `UI-BINDINGS.md`; the file is `UI_BINDINGS.md` | Trivial | Not renamed silently; other documents cross-reference it. |
| 9 | JUCE 9.0.x exists upstream | Informational | Apollo pins JUCE 8 because the specification says JUCE 8 (ADR-0002). |
| 10 | `filter_*` parameters are un-indexed while the PRD specifies two filters | Low | A Phase 5 decision. **These IDs have now been written into the registry**, so changing them is a migration, not a rename (Docs/PARAMETER-CONVENTIONS.md §1). |
| 11 | Company name and plugin codes are inferred | Low | `ProdByRnV`, `Prnv`, `Apol`, `com.prodbyrnv.apollo` were inferred from the GitHub organisation. Easy to change now, **permanent once released** — please confirm. |

---

## 7. Blockers

**None.** Phase 3 can begin.

---

## 8. Recommended next action

Begin **Phase 3 — Audio Engine & Voice Architecture**, the path to first sound
(M3). Its scope:

1. The audio engine ownership model and voice/global processing boundaries.
2. Voice allocation with configurable polyphony and a deterministic voice-stealing
   policy.
3. Note-on/note-off, velocity, sustain and pitch bend, driven by the MIDI buffer
   `processBlock` currently ignores.
4. Per-voice state reset and reuse, with all voice resources preallocated.
5. Gain-staging rules through the engine.
6. Real-time requirements enforced throughout: no allocation, no locks, no
   filesystem, no WebView calls in the audio callback.

Worth doing early in Phase 3, both cheap: close ADR-0009 (symbol visibility) now
that plugin targets exist, and open the plugin once to confirm the editor renders.

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
