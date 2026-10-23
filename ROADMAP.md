# Apollo Synthesizer — Development Roadmap

> **Developer Clause:** This roadmap is the implementation plan for Apollo, a portable, host-agnostic, real-time-safe multi-oscillator wavetable synthesizer. It is intentionally iterative. Individual implementation details may change when profiling, DSP validation, platform constraints, host behavior, or user testing reveal a better solution. No phase is considered complete merely because the code compiles; each phase requires functional, performance, and regression validation appropriate to its scope.

---

## 1. Product & Engineering Direction

Apollo is being developed as a flagship-grade desktop software synthesizer with:

- Native C++/JUCE 8 DSP and application infrastructure.
- VST3 as the primary plugin format.
- Standalone as a first-class development and validation target.
- Architecture prepared for additional plugin formats where supported and appropriate.
- React/TypeScript/HTML/CSS frontend delivered through JUCE WebView.
- Dual primary wavetable oscillators.
- Sub oscillator and noise generation.
- Polyphonic voice architecture with configurable voice count and unison.
- DAHDSR envelopes, LFOs, and a flexible modulation matrix.
- Dual state-variable filtering.
- Reorderable effects rack.
- Preset/state management through APVTS and versioned serialization.
- Generic MIDI and MIDI Learn.
- Cross-platform design without dependency on one computer, DAW, controller, audio device, driver, or physical connector.

### Engineering priorities

| Priority | Principle |
|---|---|
| **P0** | Safety, correctness, crash resistance, real-time guarantees |
| **P1** | Audio quality and DSP integrity |
| **P2** | Real-time performance and deterministic behavior |
| **P3** | Host/platform compatibility |
| **P4** | UX and workflow |
| **P5** | Convenience and secondary features |

---

# 2. Roadmap at a Glance

```text
Phase 0  → Specification & Repository Setup
Phase 1  → Build System & Application Foundation
Phase 2  → Parameter / State / UI Binding Infrastructure
Phase 3  → Audio Engine & Voice Architecture
Phase 4  → Wavetable Oscillator System
Phase 5  → Filters, Envelopes & Modulation
Phase 6  → MIDI, Control & Interaction
Phase 7  → Web UI / UX System
Phase 8  → Effects Rack
Phase 9  → Presets, Resources & State Migration
Phase 10 → Performance, DSP Validation & Host Compatibility
Phase 11 → Cross-Platform Release Engineering
Phase 12 → Production Hardening
Phase 13 → Completing the Specification
Phase 14 → Release Candidate & Release
```

The phases are sequential at the architectural level but may overlap during implementation. Dependencies should be respected even when work proceeds in parallel.


## 2a. Sub-phase index

Phases 0 to 3 were built as single units. From Phase 4 onwards each phase is
split into sub-phases, and each sub-phase is finished — built, tested, driven
against the running application, committed and green in CI — before the next one
starts. The letters below are the ones used in commit messages, in
`PROJECT-STATE.md` and in the `(4b)`-style annotations on the tasks in this file.

The split for Phases 9 to 12 was made in one pass after Phase 8 closed, so that
the shape of the remaining work is visible rather than being invented a phase at
a time. It is a plan, not a contract: a sub-phase may be re-cut when the work in
front of it is better understood, the way 8f was.

✅ done · ⬜ not started · ⏸ deferred at the developer's direction, not blocked.

### Done

| | | |
|---|---|---|
| **0** | Specification & repository setup | ✅ |
| **1** | Build system & application foundation | ✅ |
| **2** | Parameter, state & UI binding infrastructure | ✅ |
| **3** | Audio engine & voice architecture | ✅ |
| **4** | **Wavetable oscillator system** | ✅ |
| 4a | The wavetable oscillator — mipmapped, band-limited, interpolated | ✅ |
| 4b | The source section — two oscillators, unison, sub, noise | ✅ |
| 4c | Oversampling — 2x and 4x polyphase halfband, built ahead of its consumers | ✅ |
| **5** | **Filters, envelopes & modulation** | ✅ |
| 5a | The amplitude envelope — DAHDSR with curve tension | ✅ |
| 5b | The filters — two state-variable filters, series or parallel | ✅ |
| 5c | The LFO generator — seven shapes, audio rate | ✅ |
| 5d | The modulation matrix — sixteen slots, fifteen sources, seventeen destinations | ✅ |
| **6** | **MIDI, control & interaction** | ✅ |
| 6a | MIDI Learn — any control, any CC, a bijection | ✅ |
| 6b | Per-note expression and MPE | ✅ |
| 6c | Controller profiles | ✅ |
| **7** | **Web UI / UX system** | ✅ |
| 7a | The visualisation transport and the output scope | ✅ |
| 7b | The five per-source oscilloscopes | ✅ |
| 7c | Modulator traces, metering, voice count, wavetable displays | ✅ |
| 7d | The React and TypeScript migration | ✅ |
| **8** | **Effects rack** | ✅ |
| 8a | The rack, and the distortion in it | ✅ |
| 8b | The delay | ✅ |
| 8c | The reverb | ✅ |
| 8d | The gate and the compressor | ✅ |
| 8e | The parametric equaliser | ✅ |
| 8f | Whole-chain validation | ✅ |
| **9** | **Presets, resources & state migration** | ✅ |
| 9a | The preset document — the `.rnv` reader and writer, its metadata, its version and its migration, and safe rejection of anything that is not Apollo state | ✅ |
| 9b | The library on disk — platform-appropriate user and factory locations, asynchronous scanning and indexing, and every filesystem failure mode | ✅ |
| 9c | The browser in the interface — listing, categories, search, load, save, save-as, and the bridge commands behind them | ✅ |
| 9d | Factory presets — content that demonstrates the instrument, and the init patch | ✅ |
| 9e | Wavetable resources — real tables in place of the four mathematical placeholders, a validated loader, asynchronous loading, and the fallback when one is missing | ✅ |
| **11** | **Cross-platform release engineering** | ✅ |
| 11a | Windows — release build validated in Release, install rules and a CPack archive, five package tests including the import table of both binaries, and the staged package driven through the whole host suite. Found and fixed a Visual C++ Redistributable dependency and an editor that opened taller than the screen (ADR-0076) | ✅ |
| 11b | macOS — deployment target and a universal binary, both of which fail silently when unset; a Mach-O reader tested against binaries built byte by byte on Windows; ad-hoc signing; and the staged universal package driven through the whole host suite in CI. What needs a Mac — the editor rendering, Retina, Developer ID signing and notarisation — is named rather than ticked (ADR-0077) | ✅ |
| 11c | Linux — an ELF reader for the dependency check, tested against shared objects assembled in the test; three classes of dependency because Linux has no single answer; and the finding that what the binary links is not what it needs, since JUCE opens X11, GTK and WebKitGTK at run time. INSTALL.txt carries the real list and CI checks every name in it resolves (ADR-0078) | ✅ |
| 11d | CPU architectures — an ARM64 Linux job that passed first time, because ADR-0073 and ADR-0074 left no intrinsics to port. The first evidence that the regression goldens travel across instruction sets: every render within 0.0005 dB of its reference on ARM64, against tolerances of 0.1 dB, while the renders are not bit-identical between platforms (ADR-0079) | ✅ |

### Left

| | | |
|---|---|---|
| **10** | **Performance, DSP validation & host compatibility** — complete but for the DAW work, 10e-2 and 10e-3, deferred at the developer's direction | ⏸ |
| 10a | The DSP validation suite — frequency response, pitch accuracy, aliasing, THD+N, noise floor, impulse and step response, numerical stability, denormals, NaN and infinity | ✅ |
| 10b | Regression audio renders — golden renders and the harness that compares against them, which is the machinery every later phase leans on | ✅ |
| 10c | Performance profiling — CPU per voice and at each polyphony level, oversampling, the rack, worst-case callback duration, memory, the interface, resource loading, and worst-case *combinations*. Needs a measurement environment that does not drift (PROJECT-STATE §5b) | ✅ |
| 10d | Optimisation of whatever 10c identifies — the known candidate is the heaviest patch at full polyphony (issue 13) | ✅ |
| 10d-1 | The per-sample table read — a mask in place of eight integer divisions, one phase tap in place of two, and one cubic in place of two, all identities. Voice path 1.8x cheaper; the worst patch stopped missing its deadline (ADR-0070) | ✅ |
| 10d-2 | The rest of the read hoisted out of the per-sample path — a Reader resolved once per modulation block rather than once per sample. Another 1.5x, making the voice path ~2.8x cheaper than before Phase 10d (ADR-0071) | ✅ |
| 10d-3 | The measurement harness — a second, memory-bound reference; drift measured at the far end of the run; a witness that refuses contaminated traces; and a warm-up that holds the clock at a speed the machine can sustain. Apollo's most expensive row went from reproducing to 23 % between runs to 0.4 % (ADR-0072) | ✅ |
| 10d-4 | Price SIMD across unison voices against the scalar path, and decide. **Decided against**: decomposing the 1.76x available showed 1.39x comes from data layout alone, at no accuracy cost and with no vector code (ADR-0073) | ✅ |
| 10d-5 | The flat unison stack — parallel arrays rather than sixteen oscillator objects, rendered as a gather pass and an arithmetic pass, with `read` and the stack sharing one gather and one interpolate. **1.28x on the heaviest patch** (ADR-0074) | ✅ |
| 10e | Host compatibility — discovery, load and unload, automation, state and preset recall, MIDI, block sizes, sample-rate changes, bypass, transport, latency reporting, offline rendering | ⬜ |
| 10e-1 | The host harness — the built VST3 bundle loaded through a VST3 host implementation and driven as a DAW drives it, in CI on all three platforms. Found and fixed four defects no direct test could see: every integer parameter published to hosts as continuous, mono output refused, a note or pedal released during a host bypass never ending, and the default bypass asserting under latency. The VST3 parameter IDs a saved project stores are now pinned (ADR-0075) | ✅ |
| 10e-2 | FL Studio, by hand — scanning, insertion, the editor, automation, project save and reopen, host presets, MIDI from the piano roll, bypass, rendering; and Steinberg's validator alongside it. **Deferred at the developer's direction**: DAW testing is set aside for now | ⏸ |
| 10e-3 | What 10e-2 finds, and the harness extended to cover it. **Deferred with 10e-2** | ⏸ |
| **12** | **Release candidate & production hardening** | ⬜ |
| 12a | Reliability — a soak suite in the host harness with `--soak N`, asserting finiteness, boundedness, no upward trend, that it can still stop, that memory comes back and that nothing illegal survives corruption. Found an over-long block silently skipping the oversampled effects, and three of its own assertions wrong — one of which looked exactly like a stuck voice in the engine (ADR-0080) | ✅ |
| 12b | Audio quality — final listening, aliasing, gain staging, noise floor and transient review. Closed on the developer's hands-on review from two MIDI keyboards, over the measurements 10a already made; a whole-keyboard aliasing sweep and attack timing against the envelope were not run, and issues found later are fixed as they arise | ✅ |
| **12c** | **UX — discoverability, keyboard and mouse, MIDI Learn, the preset workflow, error messages, visual feedback, accessibility. Cut into three after an audit of the running interface over the DevTools protocol** | ✅ |
| 12c-1 | Keyboard and accessibility — one Tab stop per group with the arrows moving inside it (switches, tab strips, the preset list, the reset menu), a heading and region per panel, real tab semantics, a reset menu that no longer leaks keys into the knob behind it and hands focus back, and the interface's first test suite. Tab stops 253 → 169 (ADR-0083) | ✅ |
| 12c-2 | Discoverability and feedback — a value can be typed into any knob (Enter, or Enter value… on its menu), read in the readout's own units and refused rather than guessed; the preset list has column headings; the status line says "Ready." and the "SR" readout shows the sample rate instead of the protocol version; and the mouse half of keyboard and mouse, driven control by control. The first screen keeps its layout at the developer's direction (ADR-0084) | ✅ |
| 12c-3 | Workflows and errors — MIDI Learn and the preset workflow walked end to end. A replace prompt that survived a name change and could overwrite a file nobody was asked about; the list and masthead disagreeing about what was loaded after a restart or a reopened window; a MIDI panel marked OFF by default; and every message read as a user would read it, with refusals shown where the user is looking (ADR-0085) | ✅ |
| **12d** | **Documentation — PRD, architecture, UI bindings, roadmap, known limitations, supported platforms, build requirements, state compatibility policy** | ✅ |
| 12d-1 | The front door — README.md rewritten to what Apollo is; Docs/PLATFORMS.md, with what has been seen working per platform rather than only built; Docs/KNOWN-LIMITATIONS.md, which found specified features never built (PROJECT-STATE issue 26); Docs/BUILD.md corrected; and a configure check for the Node version the interface tests need | ✅ |
| 12d-2 | The specification documents — the PRD keeps every requirement and gains a status note per section (45, marked built, partly built, not built or decided against); ARCHITECTURE gains §20, the structure as built; UI_BINDINGS records everything since 9c; REPOSITORY-LAYOUT shows the tree that exists (ADR-0087) | ✅ |
| 12d-3 | Compatibility and the roadmap — Docs/COMPATIBILITY.md, each promise naming the test that holds it, with a preset 0.1.0 wrote frozen in the suite and two promises newly tested (an unmentioned parameter returns to its default; an unknown one is ignored), each shown failing against a deliberately broken loader. The roadmap brought up to date (ADR-0088) | ✅ |
| 12e | The release candidate — **moved to Phase 14** when the developer decided the specified features belong in the first release (PROJECT-STATE issue 26, ADR-0089) | → 14 |
| **13** | **Completing the specification — every feature the PRD specifies and 0.1.0 did not have, in the first release at the developer's direction (ADR-0089)** | ⬜ |
| 13a | The oscillators' missing controls — oscillator 1's coarse and fine tuning, a start phase per source, unison phase randomisation (reproducible per note), detune curve and blend, and the sub's triangle and square, phase and tracking. Thirteen parameters, every default reproducing the old sound, which the regression renders confirm | ✅ |
| 13b | The warp engine — sync, asym, PWM, bend and mirror (the PRD's "Asymmetric Sync" names two techniques, built as a mode each), an amount per oscillator, warp as a modulation destination, and the wave display drawing the warp. Aliasing treated by mip selection for the fastest read, polyBLEP/polyBLAMP at the edges and DC removal, measured and pinned; oversampling priced and declined for CPU (ADR-0090) | ✅ |
| 13c | Filters, in three parts | ✅ |
| 13c-1 | Comb and formant filter types, appended to the type list so every existing type keeps its number (ADR-0091) | ✅ |
| 13c-2 | A mix and key tracking per filter — four parameters, both defaults reproducing the old sound (ADR-0092) | ✅ |
| 13c-3 | Split routing — each source sends itself to filter 1, filter 2 or past both; four parameters, and series and parallel unchanged (ADR-0093) | ✅ |
| 13d | Noise, in two parts | ✅ |
| 13d-1 | Pink noise and key tracking — a type and a tracking amount, both defaulting to the white noise that was there (ADR-0094) | ✅ |
| 13d-2 | Built-in transient samples — click, pick, thump and chiff, synthesised once per process, played at each note's start and pitched by the key tracking without aliasing (ADR-0095) | ✅ |
| 13e | Modulation, in five parts | ✅ |
| 13e-1 | LFO tempo sync — a rate in hertz or a division of the host's tempo, a free-running synced LFO locked to the bar (ADR-0096) | ✅ |
| 13e-2 | Envelope retrigger options — continue from the level the last note left, the default and Apollo's behaviour before, or restart from silence so a stolen voice's note gets its attack (ADR-0097) | ✅ |
| 13e-3 | Macros — four knobs that are matrix sources, each with a range and a curve, learnable and stored in presets, assigned to no controller (ADR-0098) | ✅ |
| 13e-4 | Filter drive, mix and key tracking as destinations, and a via source per slot that modulates its depth (ADR-0099) | ✅ |
| 13e-5 | Effect parameters as destinations — six controls the effects smooth, following the newest note, or the engine-wide sources when nothing sounds (ADR-0100) | ✅ |
| 13f | The drawing editors, in two parts | ✅ |
| 13f-1 | Drawn LFO curves — a DRAWN shape on every LFO: up to 32 points with a bend per segment, dragged on the LFO page, stored in presets and projects beside the parameters, played from a table handed over lock-free. The default curve is the triangle and plays as it (ADR-0102) | ✅ |
| 13f-2 | Envelopes edited by dragging — every envelope drawn as a note held and released, its corners setting the stage times and the sustain and its diamonds the curve; another way of moving the same knobs, on a time axis scaled as the knobs are. Drawn with the engine's curve, held to it by a test (ADR-0103) | ✅ |
| 13g | User wavetables and noise samples, in four parts — the engine, the patch, the library, the interface | ✅ |
| 13g-1 | The engine — a USER choice on each oscillator playing a table of its own, a SAMPLE noise type, ONCE or LOOP, and a band-limited resampler for files at other rates (ADR-0104) | ✅ |
| 13g-2 | The patch — loaded content carried inside projects and presets as lossless 24-bit audio rather than as a path, built on a worker, a failure changing nothing (ADR-0104) | ✅ |
| 13g-3 | The library — user and factory folders with banks, scanned by header; the system's own file dialog; Save to library; the page naming numbers, never paths (ADR-0104) | ✅ |
| 13g-4 | The interface — a line and a browser on each oscillator and the noise, driven by hand with the real dialog (ADR-0104) | ✅ |
| 13h | The rest, in four parts — the delay's two sides and its modulation, program change, editable and exportable controller profiles, drag-and-drop modulation. A spectrum analyser was here and is done, as the equaliser's display (ADR-0101) | ⬜ |
| 13h-1 | The delay's two sides and its modulation — a link switch and the right side's own time and note value; a sine on the time, a quarter of a cycle apart on the two sides; linked and unmodulated, the old delay sample for sample (ADR-0105) | ✅ |
| 13h-2 | Program change — the built-in sounds as the host's program list, MIDI program changes and bank select by the same numbering, loaded off the audio thread through the browser's path (ADR-0106) | ✅ |
| 13h-3 | Controller profiles that can be edited, exported and imported | ⬜ |
| 13h-4 | Drag-and-drop modulation | ⬜ |
| **14** | **Release candidate & release — clean-machine installation, discovery, standalone launch, uninstall and update, artifacts, and the tag; formerly 12e** | ⬜ |

### Carried forward

Items deferred out of a closed phase, each annotated in place on its own task:

| From | Item | Lands in |
|---|---|---|
| 1 | Resource embedding and packaging | 9d, 9e |
| 1 | VST3 loads in a representative host | 10e — through a VST3 host implementation in 10e-1; in a DAW in 10e-2 |
| 2 | Thread-safety tests — an allocation and lock detector on the audio thread | unscheduled; 10a did not take it, because the obvious detector would miss the allocations most likely to happen (PROJECT-STATE issue 6) |
| 5 | Host automation as a modulation *source* | needs modulation-of-modulation; unscheduled |
| 5 | Envelope-following modulation | needs a follower on the audio path; unscheduled |
| 13c-1 | A comb's delay line allocated only when a comb is chosen — every voice holds four, 2.05 MB per instance, found by 13d-2's benchmark (PROJECT-STATE §5b) | 14, the release's memory review |

---

# Phase 0 — Specification & Repository Setup

## Objective

Establish the repository, engineering rules, documentation, and source-of-truth architecture before substantial DSP implementation begins.

## Tasks

- [x] Establish the repository structure. — reconciled across CLAUDE.md §44 / PRD §45 / ARCHITECTURE.md §10 in `Docs/REPOSITORY-LAYOUT.md`
- [x] Add and validate:
  - [x] `PRD.md`
  - [x] `CLAUDE.md`
  - [x] `ARCHITECTURE.md`
  - [x] `ROADMAP.md`
  - [x] `UI_BINDINGS.md`
- [x] Establish CMake as the authoritative build system.
- [x] Select and pin the intended JUCE 8 revision. — 8.0.15, pinned by commit and verified at configure time (ADR-0002)
- [x] Define supported build configurations. — Debug, Release, RelWithDebInfo (default), MinSizeRel
- [x] Establish coding standards and compiler warning policy. — `Docs/CODING-STANDARDS.md`, `.clang-format`, `.editorconfig`
- [x] Define debug, release, sanitization, and profiling configurations where supported. — ASan/UBSan options; profiling uses RelWithDebInfo
- [x] Establish the initial test framework. — `juce::UnitTest` + CTest (ADR-0004); failure path verified
- [x] Create CI/build validation strategy. — `.github/workflows/ci.yml`; not yet executed (no remote)
- [x] Establish versioning conventions. — `Docs/VERSIONING.md`
- [x] Define the initial parameter naming and ID conventions. — `Docs/PARAMETER-CONVENTIONS.md`, enforced by `Source/Parameters/ParameterId.h`

## Exit Criteria

- [x] Repository builds from a clean checkout. — verified on Windows/MSVC in Debug, RelWithDebInfo, and Release with warnings as errors
- [x] Documentation agrees on platform, host, DSP, UI, and threading requirements. — conflicts found were structural only, and are reconciled in `Docs/REPOSITORY-LAYOUT.md` §1
- [x] No hardware- or DAW-specific dependency exists in the core architecture.
- [x] A basic automated build/test invocation is documented. — `Docs/BUILD.md`, mirrored by the CI workflow

> **Phase status:** complete and verified on Windows/MSVC. macOS, Linux and ARM64
> remain unbuilt; see `PROJECT-STATE.md` §4 and §6.

---

# Phase 1 — Build System & Application Foundation

## Objective

Create the smallest functional Apollo application capable of running as both a plugin and standalone application.

## Tasks

### CMake / JUCE

- [x] Integrate JUCE through CMake.
- [x] Configure the VST3 target.
- [x] Configure the standalone target.
- [x] Establish shared source modules between plugin and standalone builds. — one `juce_add_plugin` target produces both formats from one engine
- [x] Configure resource embedding/packaging. — deferred to Phase 9; factory presets and wavetables are compiled into the plugin (9d, 9e, ADR-0065, ADR-0066) and packaging is 11a (ticked in 12d-3)
- [x] Configure frontend asset packaging. — deferred to Phase 7; the bundle is built by CMake and embedded in the binary (7d, ADR-0050; ticked in 12d-3)
- [x] Avoid Projucer dependencies.
- [x] Add platform-specific code only behind appropriate abstraction boundaries. — none required so far

### Processor lifecycle

- [x] Implement `AudioProcessor`.
- [x] Implement processor initialization.
- [x] Implement `prepareToPlay`.
- [x] Implement processing lifecycle/reset behavior.
- [x] Handle variable host block sizes.
- [x] Handle sample-rate changes.
- [x] Handle bypass behavior. — no latency or tail to compensate; revisit when effects land in Phase 8
- [x] Handle processor destruction/reinitialization safely.

### Audio validation

- [x] Implement a clean audio pass-through.
- [x] Validate input/output channel handling.
- [x] Validate standalone device selection. — verified 2026-09-08: Windows Audio, Speakers (Realtek), 48 kHz, 480 samples, channels 1+2, MIDI inputs listed (PROJECT-STATE.md §5a)
- [x] Verify operation without assuming a particular audio driver or physical connector.

## Exit Criteria

- [ ] VST3 loads in a representative compatible host. — **requires a DAW; not verifiable in this environment**
- [x] Standalone launches and produces audio. — verified 2026-09-08 by measurement, not by ear: a MIDI note drove Apollo's Windows audio-session peak to 0.1829 against 0.0000 either side (PROJECT-STATE.md §5a)
- [x] Variable block sizes and sample-rate changes do not break processing. — covered by `Tests/Audio/ProcessorLifecycleTests.cpp`
- [x] No known real-time thread violations exist in the foundation.

> **Phase status:** implementation complete and unit-tested. The audio-device
> criteria were closed on 2026-09-08 by running the standalone and measuring its
> output (`PROJECT-STATE.md` §5a) — which is also how the WebView2 backend defect
> in §6, issue 12 was found. One exit criterion remains open and needs a DAW:
> loading the VST3 in a host.

---

# Phase 2 — Parameter, State & UI Binding Infrastructure

## Objective

Build the authoritative parameter/state system and the C++ ↔ WebView communication layer before complex DSP is added.

## Tasks

### APVTS

- [x] Create the central APVTS parameter registry. — `Source/Parameters/ParameterDefinitions.h`, JUCE-free `constexpr` data
- [x] Implement stable parameter IDs. — validated against the conventions by test
- [x] Define normalized and plain-value conversion.
- [x] Define parameter ranges, defaults, units, steps, and skew.
- [x] Define parameter smoothing requirements. — declared per parameter; applied when the DSP that needs it lands
- [x] Define modulation capabilities. — declared per parameter; consumed by the Phase 5 matrix
- [x] Add parameter metadata required by the frontend.

### State

- [x] Implement APVTS serialization.
- [x] Implement state restore.
- [x] Add state schema/version metadata.
- [x] Establish state migration architecture. — versioned, single extension point, boundary-tested

### UI bridge

- [x] Initialize the JUCE WebView layer. — `Source/UI/ApolloWebViewEditor.*`; WebView2 SDK fetched and pinned on Windows (ADR-0015)
- [x] Implement bridge initialization.
- [x] Implement protocol versioning.
- [x] Implement initial state synchronization.
- [x] Implement validated `setParameter` commands.
- [x] Implement asynchronous native-to-Web parameter updates. — coalesced at 30 Hz off the audio thread
- [x] Implement parameter gesture begin/update/end semantics.
- [x] Implement structured bridge errors.
- [x] Prevent arbitrary native command execution from the frontend. — the protocol maps only to a fixed command set

### Testing

- [x] Parameter registry tests.
- [x] State serialization tests.
- [x] State restoration tests.
- [x] Bridge protocol tests.
- [x] Invalid-message tests.
- [ ] Thread-safety tests. — audio-thread safety is by construction and review; no allocation/lock detector is wired up yet (Phase 10)

## Exit Criteria

- [x] Every initial parameter can be changed from native code and the UI. — UI path covered by `Tests/UI/ParameterBridgeTests.cpp`
- [x] Native state and UI state remain synchronized. — externally originated changes propagate back to the UI
- [x] Host automation can change parameters without breaking synchronization.
- [x] The WebView never blocks the real-time audio path. — the audio thread only sets a lock-free flag

> **Phase status:** complete. 735 assertions passing. The WebView editor renders
> a placeholder page that drives the real bridge end to end; the React frontend
> that replaces it is Phase 7. The editor has not yet been viewed running in a
> host — see `PROJECT-STATE.md` §6.

---

# Phase 3 — Audio Engine & Voice Architecture

## Objective

Create the reusable native synthesis engine and polyphonic voice infrastructure.

## Tasks

- [x] Define the audio engine ownership model. — the processor owns one VoiceEngine, which owns every Voice by value in a fixed-size array
- [x] Implement voice allocation.
- [x] Implement configurable polyphony. — engine-level, 1..32; exposing it as a user parameter is a deliberate later decision (PROJECT-STATE.md §6)
- [x] Implement deterministic voice stealing. — free voice, else oldest releasing, else oldest overall; ties resolve to the lowest index
- [x] Implement note-on/note-off handling. — sample-accurate, including velocity-zero as note-off
- [x] Implement velocity handling.
- [x] Implement sustain behavior.
- [x] Implement pitch bend. — ±2 semitones, applied to sounding and subsequent notes
- [x] Implement per-voice state reset/reuse. — a reused voice renders identically to a fresh one, verified by test
- [x] Preallocate voice and audio resources. — the voice pool is fixed at compile time; voices render additively with no scratch buffer
- [x] Establish voice/global processing boundaries. — voices carry velocity, the engine scales the sum, the processor applies master gain
- [x] Define mono/stereo signal-flow conventions. — voices are centred; panning and stereo spread arrive with Phase 4
- [x] Add gain-staging rules throughout the engine. — measured rather than assumed (ADR-0017)

### Real-time requirements

- [x] No uncontrolled allocation in the audio callback.
- [x] No blocking mutexes in real-time processing.
- [x] No filesystem access.
- [x] No WebView calls.
- [x] No synchronous logging.
- [x] No unbounded work based on UI state.

## Exit Criteria

- [x] Polyphonic test tones render correctly. — pitch verified against four reference notes to within a tenth of a semitone
- [x] Voice stealing behaves predictably. — determinism asserted by repetition; the releasing-voice preference by construction
- [x] No audible artifacts occur during normal note transitions. — measured as the largest sample-to-sample step across a steal
- [x] Stress testing does not reveal fundamental audio-thread violations. — extreme notes, extreme bends and 500 rapid note changes all stay finite

> **Phase status:** complete. Apollo produces sound. 1637 assertions passing.
> The oscillator is a sine and the envelope a simple attack/release; the
> wavetable engine (Phase 4) and the DAHDSR envelopes (Phase 5) replace them.

---

# Phase 4 — Wavetable Oscillator System

## Objective

Implement Apollo's primary synthesis engine with high-quality, anti-aliased wavetable oscillators.

## Tasks

### Oscillators

- [x] Implement oscillator 1. — `UnisonOscillator` over the mipmap wavetable (4a, 4b)
- [x] Implement oscillator 2. — structurally identical, with its own coarse and fine tuning; silent by default (4b)
- [x] Implement continuous phase accumulation. (4a)
- [x] Implement phase wrapping. — wrapped before scaling to an index, see ADR-0022 (4a)
- [x] Implement frequency/tuning control. — note tracking, pitch bend, and coarse/fine on oscillator 2 (4a, 4b)
- [x] Implement wavetable selection. — four built-in tables, morphs between classic shapes until 9e replaced them with a swept saw, a narrowing pulse, a climbing formant and a wavefolder (4a, 9e)
- [x] Implement wavetable-position/frame scanning. (4a)
- [x] Implement high-quality interpolation. — 4-point cubic Hermite with wrapped neighbours (4a)
- [x] Implement oscillator level/pan controls. — level and stereo balance, smoothed per sample (4b)
- [x] Implement unison. — up to 16 voices per oscillator, from a layout shared by the whole engine (4b)
- [x] Implement detune/spread. — symmetric in cents, constant-power stereo spread, ADR-0024 (4b)
- [x] Implement sub oscillator. — sine, one or two octaves below the note (4b)
- [x] Implement stereo noise generator. — decorrelated channels, one seed per voice (4b)

### Anti-aliasing

- [x] Establish wavetable band-limiting strategy. — additive mipmap synthesis, ADR-0020 (4a)
- [x] Test oscillator output spectrally across the frequency range. (4a)
- [x] Measure aliasing at different pitches. — worst measured -98.5 dBc (4a)
- [x] Test high-frequency table scanning. (4a)
- [x] Validate interpolation artifacts. (4a)
- [x] Document acceptable aliasing thresholds. — -60 dBc, stated and enforced in `Tests/DSP/WavetableTests.cpp` (4a)

### Nonlinear preparation

- [x] Define where oversampling is required. — `Docs/OVERSAMPLING.md` §2, per stage, with the factor each is expected to want and a note that each is re-measured when the stage is built (4c)
- [x] Avoid unnecessary oversampling of purely linear stages. — `Docs/OVERSAMPLING.md` §3 records what is deliberately excluded: the oscillators are band-limited at the source by their mipmap, the sub is a single harmonic, noise has nothing above Nyquist to fold, and linear stages create no new frequencies (4c)
- [x] Build reusable oversampling infrastructure for nonlinear stages. — `Source/DSP/Oversampling/`: linear-phase polyphase halfband, 2x and 4x, JUCE-free and real-time safe, with whole-sample latency so a dry path can be aligned (ADR-0028) (4c)

## Exit Criteria

- [x] Oscillators track pitch accurately. — verified against reference notes, oscillator 2 transpositions and both sub octaves, to within a sixth of a semitone
- [x] Wavetable scanning is smooth. — a full position sweep produces no discontinuity, and aliasing stays inside budget at every scan position
- [x] Unison remains stable at high voice counts. — 32 voices with 16-voice unison on both oscillators and every source sounding stays finite and bounded
- [x] Spectral testing demonstrates acceptable anti-aliasing performance. — worst measured -98.5 dBc against a -60 dBc budget
- [x] CPU cost is measured across representative polyphony levels. — `ApolloTests --benchmark`, recorded in `PROJECT-STATE.md` §5b. The default patch costs 0.15 % of a core per voice and 4.86 % at full polyphony; the heaviest patch reaches 150 % at 32 voices and **cannot run in real time**, which is published as a limit rather than hidden (ADR-0029)

> **Phase status:** complete. Oscillators and the source section landed in 4a and
> 4b; the oversampling infrastructure and the CPU measurement complete 4c.
> 211024 assertions passing. The measurement produced one finding worth carrying
> forward: the heaviest patch cannot sustain full polyphony in real time
> (ADR-0029), which Phase 10 owns.

---

# Phase 5 — Filters, Envelopes & Modulation

## Objective

Build the expressive sound-design architecture around the oscillator core.

## Tasks

### Filters

- [x] Implement filter 1. — a topology-preserving state variable filter, stereo, per voice (5b)
- [x] Implement filter 2. — identical, off by default so adding it changed no existing patch; series or parallel routing (5b)
- [x] Implement required low-pass, high-pass, and band-pass modes. — plus notch, which the same two state variables give for free (5b)
- [x] Implement cutoff. — measured at -3.01 dB at its cutoff across five frequencies and four sample rates (5b)
- [x] Implement resonance. — as Q, the unit the registry already used; peaks land on 20·log₁₀(Q) (5b)
- [x] Implement filter drive where applicable. — deliberately gentle, because its aliasing was measured rather than assumed (ADR-0033) (5b)
- [x] Validate stability across supported sample rates. — 44.1 to 192 kHz, and under a cutoff swept full-range at 100 Hz, which is the case the topology was chosen for (5b)
- [x] Smooth filter parameter changes. — resolved in 5d as promised. A modulated cutoff re-resolves every 16 samples, a 3 kHz rate, and the largest sample-to-sample step of a resonant LFO sweep measures 0.002167 against 0.001292 for the same patch unmodulated — 1.7x, not the orders of magnitude a staircase would give (ADR-0035)

### Envelopes

- [x] Implement four DAHDSR envelopes. — the generator landed in 5a driving voice amplitude; envelopes 2-4 became reachable in 5d, when the matrix gave them destinations (5a, 5d)
- [x] Implement attack. (5a)
- [x] Implement hold. (5a)
- [x] Implement decay. (5a)
- [x] Implement sustain. (5a)
- [x] Implement release. (5a)
- [x] Implement delay if included in the final envelope definition. — included; PRD §15.1 specifies DAHDSR (5a)
- [x] Validate retrigger behavior. — a retriggered envelope continues from its current level rather than restarting at zero, which is what keeps the restart click-free; asserted in `Tests/DSP/EnvelopeTests.cpp` (5a) *Since 13e-2 that is the CONTINUE option, still the default; RESTART begins from silence (ADR-0097).*
- [x] Validate note-off behavior. — release begins from wherever the envelope is, from any stage, with no step; asserted across four note-off points (5a)

### LFOs

- [x] Implement four LFOs. — the generator landed in 5c; the four per-voice instances, their free-running shared phases and their routing arrived in 5d (5c, 5d)
- [x] Implement selectable waveform shapes. — sine, triangle, saw, reverse saw, square, sample & hold and step, the set PRD §15.2 specifies. Custom drawable curves need the editor that draws them and the preset format that stores them, so they follow Phases 7 and 9 (5c)
- [x] Implement rate. — 0.01 to 400 Hz, measured accurate to three decimal places and unchanged from 44.1 to 192 kHz; plus `tempoSyncedRateHz`, which converts a host tempo and a note division into hertz and falls back to 120 bpm when the host reports nothing usable (5c)
- [x] Implement phase/reset behavior. — a phase offset that starts the shape where it says, and a reset that returns it to that offset (5c)
- [x] Implement free-running and note-synced behavior. — retrigger restarts the shape on every note; free-running adopts a phase handed in by the engine, so voices started at different times stay in step with one another (5c)
- [x] Define control-rate/audio-rate usage. — **audio rate**, decided on measurement rather than assumption: a block-rate LFO on a filter cutoff is the zipper artefact the exit criteria forbid, and 128 instances cost 1.7–4.4 % of a core (ADR-0034) (5c)

### Modulation matrix

- [x] Define modulation source registry. — `dsp::ModSource`: four envelopes, four LFOs, velocity, key tracking, mod wheel, pitch bend, aftertouch and a per-note random. Its numeric order is part of the saved-state contract, like a parameter ID (5d)
- [x] Define destination registry. — `dsp::ModDestination`: pitch (all, or per oscillator), wavetable position, level, pan, sub and noise level, both filters' cutoff and resonance, and amplitude. Engine concepts rather than parameter IDs, because the obvious target — oscillator 1's pitch — has no parameter behind it (5d)
- [x] Implement modulation amount. — bipolar depth per slot, scaled by each destination's own range so a depth means the same thing on a cutoff as on a pan (5d)
- [x] Implement source-to-destination routing. — sixteen slots, each a source, a destination and a depth (5d)
- [x] Implement modulation combination rules. — contributions to one destination add; opposite depths cancel (5d)
- [x] Implement clamping/scaling. — clamped by the consumer rather than centrally, because a level cannot go below zero, a pitch can go anywhere, and a cutoff is bounded in octaves and again in hertz. Amplitude attenuates and never boosts (ADR-0036) (5d)
- [x] Implement velocity/key tracking. — both are sources; key tracking is bipolar around middle C so it does the opposite thing below the centre from above it (5d)
- [x] Implement mod wheel/CC sources. — CC 1 is read as a first-class source rather than through MIDI Learn, because it is a modulation source rather than a mapping to a parameter (5d)
- [x] Implement aftertouch where supported. — channel pressure. Polyphonic aftertouch is a per-note source and needs the per-note routing that arrives with MPE in Phase 6 (5d)
- [ ] Implement host automation as a compatible control source where appropriate. — every routing control is itself an automatable parameter, so a host can already automate depth; automation *as a matrix source* needs the per-parameter modulation-of-modulation the matrix does not yet support
- [ ] Implement envelope-following/audio-responsive modulation. — needs an envelope follower on the audio path, which belongs with the dynamics processors in Phase 8

## Exit Criteria

- [x] Envelopes respond predictably across polyphony. — voice zero's envelope trajectory is **bit-identical** whether 1, 2, 8 or 16 voices are sounding
- [x] LFOs remain stable and phase-correct. — rates measured exact to three decimals and unchanged from 44.1 to 192 kHz; two free-running LFOs handed the same phase stay bit-identical
- [x] Multiple modulation sources can safely target a parameter. — contributions add, opposite depths cancel, and every destination driven at full depth from fast LFOs across 24 voices stays finite and peaks at 3.2
- [x] Modulation does not corrupt base parameter values. — asserted directly: after a heavily modulated note, every base parameter reads back exactly as set, and removing the routing returns the engine to within 1e-6 of one that never had it
- [x] Filter behavior is stable and free from zipper artifacts. — a resonant LFO sweep's largest sample step is 0.002167 against 0.001292 unmodulated (ADR-0035); the filter itself is stable under a full-range cutoff sweep at 100 Hz and from 44.1 to 192 kHz (5b)

---

> **Phase status:** complete for the four subsystems this phase is named for.
> Envelopes (5a), filters (5b), LFOs (5c) and the modulation matrix (5d) are all
> in and measured, and every exit criterion is met.
>
> Two tasks are deliberately carried forward rather than ticked, each to the
> phase that owns the thing it needs: host automation *as a matrix source* wants
> modulation of modulation depth, and envelope-following wants an envelope
> follower on the audio path, which arrives with the dynamics processors in
> Phase 8.

---

# Phase 6 — MIDI, Control & Interaction

## Objective

Provide standards-based external control without coupling Apollo to a specific controller.

## Tasks

- [x] Implement MIDI note processing. — note on/off with velocity, velocity-zero treated as note-off, all-notes-off and all-sound-off (Phase 3)
- [x] Implement MIDI CC processing. — every control-change message is offered to the mapping layer and then to Apollo's own fixed functions, because CC 1 is legitimately both (6a)
- [x] Implement pitch bend. — ±2 semitones, the near-universal default (Phase 3)
- [x] Implement sustain. — CC 64, and it is one of the two controller groups a mapping may never take over (Phase 3, 6a)
- [x] Implement modulation wheel. — CC 1, read as a first-class modulation source rather than through MIDI Learn, and mappable as well (Phase 5d, 6a)
- [x] Implement aftertouch where available. — all three kinds. Channel pressure (5d), **polyphonic key pressure**, and **MPE** channel pressure, all reaching the same per-voice `aftertouch` source; a plain keyboard's channel pressure still reaches every voice, because with no zone active it addresses all of them (ADR-0044) (6b)
- [x] Implement MIDI Learn. — a mode in the interface rather than a hidden right-click, so it is keyboard-reachable and visible to someone who has not been told it exists (6a)
- [x] Implement mapping persistence. — a `<MIDIMAP>` child of the APVTS state root, written after every edit rather than at save time, so a host that asks for state without warning gets the current mappings (ADR-0043) (6a)
- [x] Implement mapping removal/clearing. — per parameter and wholesale, from the interface and from the bridge (6a)
- [x] Implement conflict handling. — a bijection: one control drives one parameter, one parameter has one control, and every replacement is reported rather than performed silently (ADR-0041) (6a)
- [x] Implement optional controller profiles. — two built-in profiles, both **standards-based rather than device-specific**: the MIDI specification's Sound Controllers (CC 70-79, whose meanings it already fixes) and its General Purpose Controllers (CC 16-19 and 80-83, which it deliberately leaves undefined). Applied by replacing or merging, from the interface, and every entry goes through the same validation a learned mapping does (ADR-0046) (6c)
- [x] Keep controller profiles separate from core parameter definitions. — a profile lives in its own file, refers to parameters by ID, and resolves them when applied. The registry gained nothing: it describes what Apollo has and is permanent, where a profile describes somebody's hardware and is disposable (6c)
- [x] Ensure MIDI event ordering is correct. — messages are applied in the order the host presents them, and the block is rendered in segments between them (Phase 3)
- [x] Preserve sample-accurate event positions where provided by the host. — asserted directly: the same sequence produces **identical** output across nine block sizes (Phase 3)

## Exit Criteria

- [x] Apollo works correctly with generic MIDI input. — nothing in the mapping layer names a controller, a manufacturer or a layout; a mapping is whatever number the user's device sent
- [x] MIDI mappings survive state save/load where intended. — asserted through the processor's own `getStateInformation`/`setStateInformation`, including the channel and the scaling range, and the restored mapping is shown to still drive its parameter rather than merely to exist
- [x] No specific controller is required for normal operation. — every mapping is learned, none is required, and the whole instrument remains playable with mouse and keyboard alone
- [x] MIDI processing remains real-time safe. — the audio thread does a bounded scan of a preallocated table and writes one atomic slot; the test that proves it renders a whole 128-step sweep and asserts the parameter has **not** moved until the message thread flushes (ADR-0042)

---

> **Phase status:** complete. 6a brought MIDI Learn, 6b per-note expression —
> polyphonic aftertouch, MPE, and the RPNs that configure them — and 6c
> controller profiles. Every task and every exit criterion is met, and the
> interface half of all three was driven by hand against the running standalone
> with real MIDI messages (`PROJECT-STATE.md` §5a).

---

# Phase 7 — Web UI / UX System

## Objective

Build the complete interactive Apollo interface on top of the established binding architecture.

## Tasks

Part of this phase was brought forward and landed against the Phase 5 engine:
the interface itself, first as a dependency-free page embedded by CMake
(ADR-0038, ADR-0039, ADR-0040) and since 7d as a React and TypeScript
application under `WebUI/` bundled by the same CMake step (ADR-0050). The
visualizers followed in 7a-7c.

### Frontend foundation

- [x] Establish React/TypeScript project structure. — `WebUI/`, with the page
      split into components, layout modules, parameter mapping, the wire contract
      as types, and four stores (ADR-0050) (7d)
- [x] Establish frontend build process. — esbuild and `tsc --noEmit`, twelve
      packages in total, run from CMake so the bundle can never disagree with its
      source. Building the plugin needs Node; the engine and its tests do not (7d)
- [x] Integrate compiled assets with CMake/JUCE. — `juce_add_binary_data`, with
      the editor serving a fixed path/MIME table (ADR-0038), now fed by the
      bundler's output rather than by three hand-written files (7d)
- [x] Establish reusable UI component system. — knob, segmented switch, select,
      bipolar rail, module frame and tab strip, all built from metadata, and since
      7d all real components with typed props
- [x] Implement parameter-control abstraction. — normalised↔plain mapping
      mirroring `juce::NormalisableRange`, gesture begin/end around every edit,
      and echo suppression while a control is held. Since 7d a store with one
      listener set per parameter id, so a knob re-renders when its own value moves
      and at no other time (7d)

### Core interface

- [x] Oscillator panels.
- [x] Wavetable selectors.
- [x] Unison controls.
- [x] Detune controls.
- [x] Filter controls. — both filters and the routing switch
- [x] Envelope controls. — all four, behind a tab strip
- [x] LFO controls. — all four, behind a tab strip
- [x] Modulation matrix. — sixteen slots in two banks, bipolar depth rails, with
      assigned routings marked and their destination knobs lit
- [x] FX rack. — the two FX parameters that exist are in the Output module;
      the rack itself is Phase 8, which built its interface with it (8a; ticked in 12d-3)
- [x] Master/output controls.
- [x] Preset browser. — Phase 9 owns the preset system; built in 9c (ticked in 12d-3)
- [x] MIDI Learn interface. — Phase 6 owns the mapping engine; its interface was built in 6a and walked end to end in 12c-3 (ticked in 12d-3)

### Visual system

- [x] Implement dark UI foundation.
- [x] Implement a gold visual language, keyed to `#F7EF8A`. — as a single
      interaction accent rather than as decoration (ADR-0039, ADR-0051). The
      deep-purple language this item originally asked for was built and shipped
      through Phase 7d, then replaced when the developer said it had never been
      the intent
- [x] Implement green active-state indicators. — modulation and activity only
- [x] Implement red warning/error indicators. — the token and its meaning are
      defined; the output meter's clip indicator uses it (7c), and so does an
      error on the status line, with a warning sign beside it (12c-3; ticked in 12d-3)
- [x] Define typography. — UI sans for labels, tabular monospace for every
      number so a readout cannot jitter as it counts
- [x] Define spacing. — a 4 px scale, with every value taken from a token
- [x] Define control states. — hover, focus-visible, held, modulated, inactive
- [x] Define disabled/error/focus states. — focus and inactive are done;
      focus made visible on every control including a chosen chip (12c-1), a
      disabled menu item greyed rather than hidden, and error and caution states
      on the status line and in the save form (12c-3; ticked in 12d-3)
- [x] Support appropriate display scaling and window sizes. — verified at
      1920x1080 with 150 % scaling; modules reflow and the matrix falls back
      from two banks to one

### Visualization

- [x] Oscilloscope for the final output. — a lock-free capture ring per source, triggered and decimated on the message thread, drawn on a canvas at 30 Hz. Silent, sounding and released all verified on screen (ADR-0047) (7a)
- [x] Per-source oscilloscopes — oscillator 1, oscillator 2, sub, noise and the post-filter signal, each with its own scope. Tapped inside the voice loop and summed across the whole pool, so a source shows what every voice producing it is producing together; the four source taps sit before the filter and the amplifier, the post-filter tap after both (ADR-0048) (7b)
- [x] Wavetable visualization. — one cycle of the wave each oscillator is actually reading, at the *effective* position including whatever the matrix adds, rendered on the message thread from the immutable table rather than captured (ADR-0049) (7c)
- [x] Envelope visualization — a live trace of the value being produced, not a static picture of the shape. All four, one entry every 7.8 ms over a one-second window, following the most recently started sounding voice, with the DAHDSR stage named in words (7c)
- [x] LFO visualization — likewise, and an LFO nothing routes says "unrouted" rather than leaving a flat line to look like a fault (7c)
- [x] Measure the cost of capture across polyphony, and show a silent source as silent rather than stale. — everything a watched instance pays adds 0.12 % of real time at one voice and 0.47 % at thirty-two; six scope frames cost 0.05 % at 30 Hz on the message thread. Because it is a meaningful fraction of the render, capture runs only while an editor is watching, and an unwatched instance costs what it did before scopes existed. A stopped source reads as silent because the capture records the silence rather than inferring it from the absence of a write (7a, 7b, 7c)
- [x] Optional spectrum visualization. — built after Phase 13e, at the developer's request, as the equaliser's display: the spectrum of what goes into the equaliser and of what comes out, live behind its curve, through two more capture rings either side of it and a 4096-point transform on the message thread (ADR-0101)
- [x] Voice/activity indicators. — sounding voices against the polyphony ceiling, in the meter's caption where it is read alongside the level (7c)
- [x] Output metering. — peak with a 20 dB/s fall beside RMS over 300 ms, and a clip held for a second and a half after the sample that caused it. The first real use of the red overload token, said in a word as well as a colour (ADR-0049) (7c)

### Telemetry

- [x] Implement lock-free/atomic telemetry snapshots. — `Telemetry/ScopeBuffer.h`: one-way, allocation-free, and read behind the writer so the reader and writer are a ring apart (7a)
- [x] Rate-limit UI updates. — scope frames at 30 Hz decimated to 192 points; instrument frames (traces, meter, wavetables) at 15 Hz, because a meter needle reads perfectly at half the rate a waveform needs. Points travel as integer thousandths on one line rather than as pretty-printed doubles, which took a scope frame from 18 KB to 5 KB (ADR-0049). The timer follows the outbound handler, so a closed editor serializes nothing (7a, 7c)
- [x] Ensure visualizers never access mutable DSP state directly. — the interface never sees a voice, a buffer or an engine; it sees a frame of floats that was copied out of a ring (7a)
- [x] Verify UI CPU usage independently of DSP CPU usage. — the frame builders are timed separately on the message thread and reported as their own row: six scope frames cost 0.05 % of real time at 30 Hz, which is the whole of the interface's share of a core (7a, 7c)

## Exit Criteria

- [x] UI controls accurately represent native state. — every control is built from parameter metadata and echoes the engine's own value back (Phase 5, brought forward)
- [x] UI interactions produce correct parameter changes. — verified by hand and by the parameter-bridge tests
- [x] UI reload restores state correctly. — a project load bumps the reload counter and resynchronises parameters, MIDI mappings and the learn state wholesale
- [x] Visualizers remain responsive without affecting audio processing. — measured across everything a watched instance pays, with the two cases interleaved so the difference is not a measurement of the laptop warming up: 0.12 % of real time at one voice, 0.47 % at thirty-two, and nothing at all in an instance nobody is watching. A test renders the same note with and without capture and compares the audio sample for sample (7a, 7b, 7c)
- [x] UI remains usable across supported display configurations. — verified at 1920x1080 with 150 % scaling

---

> **Phase status:** complete. 7a built the visualisation transport, 7b put a
> scope on every source, 7c added the modulator traces, the output meter and the
> wavetable displays, and 7d moved the whole interface to React and TypeScript
> with no change a user can see. Every cost is measured rather than assumed and
> all capture is gated on whether anything is watching. One item remains
> unticked, and it is deferred: the hosts and browsers this environment cannot
> reach. The optional spectrum analyser was built later, as the equaliser's
> display (ADR-0101).

---

# Phase 8 — Effects Rack

## Objective

Implement Apollo's modular, reorderable effects architecture.

## Tasks

### Distortion / saturation

- [x] Implement smooth saturation. — `tanh`, and a third asymmetric `diode` curve PRD §19 also names, which is what produces even harmonics (8a)
- [x] Implement hard clipping. — a flat ceiling, untouched below the threshold (8a)
- [x] Implement gain compensation. — the shaper's gain at a -6 dBFS reference, divided out and ramped with the drive, so the control changes the tone rather than the level (8a)
- [x] Implement pre/post filtering. — a subsonic highpass before the shaper, a DC highpass after it for the asymmetric curve, and a tone lowpass on the wet path (8a)
- [x] Validate nonlinear behavior with oversampling. — 4x, measured: 13 to 15 dB less fold-back than the same curves at the base rate, and -100 dBc on a musical note (Docs/OVERSAMPLING.md §2)

### Delay

- [x] Implement delay buffer. — `dsp::DelayLine`, four seconds, fractional reads, kept separate from the effect so the reverb in 8c can use the same buffer without the musical part (8b)
- [x] Implement delay time. — free in milliseconds or synced to the host's tempo across fourteen note values, with 120 BPM as the fallback when no transport reports one (8b)
- [x] Implement feedback. — bounded by construction at 0.95, with a lowpass and a highpass inside the loop so each repeat is darker than the last (8b)
- [x] Implement wet/dry mix. — and a dry mix is bit-exactly the input, because a delay adds no latency to compensate for (8b)
- [x] Implement stereo/ping-pong behavior where required. — crossed feedback, so a repeat alternates sides (8b)
- [x] Handle parameter smoothing. — the time glides rather than stepping, which is the tape behaviour a swept delay is expected to have; measured as the absence of any discontinuity while sweeping (ADR-0056) (8b)
- [x] Validate feedback stability. — thirty seconds at maximum feedback with no input: every second quieter than the last, nothing ever louder than what went in, and near silence by the end (8b)

### Reverb

- [x] Implement/reuse suitable reverb architecture. — an eight-line feedback delay network with a Hadamard mix, four diffusion allpasses per channel, damping inside every line, pre-delay, and room and hall base lengths (ADR-0057) (8c)
- [x] Validate decay stability. — the orthogonal matrix preserves energy exactly, so the decay gains are the only thing that can make the tail grow; measured over a minute at the longest decay with no input, against the envelope RT60 describes (8c)
- [x] Handle denormals. — every line's feedback write is flushed below 1e-18, and a test asserts the tail reaches **exactly** zero rather than grinding on inaudibly (8c)
- [x] Measure CPU cost. — about 1.2 % of one core, stereo, and nearly the same bypassed, because a bypassed reverb still has to let its tail decay (PROJECT-STATE.md §5b) (8c)

### Gate / dynamics

- [x] Implement noise gate. — threshold, attack, hold, release and range, with peak detection because a gate must not miss the transient that arrives while it is shut (8d)
- [x] Implement compressor. — feed-forward, RMS-detected, hard knee, with threshold, ratio, attack, release, makeup and a mix control that makes it a parallel compressor (8d)
- [x] Define envelope detector behavior. — peak for the gate and RMS for the compressor, both feeding one stereo-linked detector so neither can move the stereo image; the choice and its reasoning are in `LevelDetector.h` and ADR-0058 (8d)
- [x] Validate attack/release behavior. — against the stated convention: the ramp travels 1 - 1/e of the way in the time it is given, measured to within a percentage point at three settings, with the detector's own lag measured separately rather than hidden inside it (8d)

### EQ

- [x] Implement EQ architecture. — seven RBJ biquad bands in series, in double precision, each with a type, a frequency, a gain, a bandwidth in octaves, a slope and a mute, plus an output trim. Seven rather than four because the developer asked for parity with Fruity Parametric EQ 2 and the later requirement governs (ADR-0059) (8e)
- [x] Validate filter stability. — every design in the whole parameter space, at four sample rates, has both poles inside the unit circle; the frequency is clamped below Nyquist before any trigonometry, so no setting a parameter can express can produce an unstable one (8e)
- [x] Provide predictable gain behavior. — a bell applies exactly the decibels on its dial at its own centre, a shelf reaches its full gain on its own side and nothing on the other, and a slope of N applies the gain N times over rather than being silently normalised: predictable, which is what was asked for, rather than constant (8e)

### FX rack

- [x] Implement common effect-module interface. — `dsp::AudioEffect`: prepare, process, processBypassed, reset, latency, tail (8a)
- [x] Implement bypass. — per effect, keeping its position and its latency, so toggling it does not renegotiate the plugin's delay (8a)
- [x] Implement reordering. — six slot parameters naming what occupies each position; duplicates resolve to the first occurrence (8a)
- [x] Implement state serialization. — the chain is parameters, so it saves and restores with everything else; covered in `Tests/Audio/EffectsIntegrationTests.cpp` (8a)
- [x] Implement latency reporting where relevant. — summed over the chain, published to the host from the message thread when it changes (8a)
- [x] Implement tail reporting where relevant. — every active effect's tail summed along the chain, added to the envelope's release. The longest until 8f measured a delay feeding a reverb still sounding at the moment the longest-in-the-chain rule declared it finished (ADR-0060) (8a, corrected in 8f)
- [x] Test arbitrary valid effect orderings. — **every** one of the 720 orderings of all six effects, rendered in full rather than a representative handful, plus a chain rearranged on every block for 240 blocks; `Tests/DSP/EffectsChainTests.cpp` (8f)

## Exit Criteria

- [x] **Every effect operates independently.** — switching any one of the six out of a full chain measurably changes the output, so none of them is being swallowed by the five around it (8f)
- [x] **FX order can be changed safely.** — all 720 orderings render finite and bounded, the reported latency is the same for every one of them, and the chain rearranged on every block for 240 blocks stays bounded (8f)
- [x] **Feedback effects remain stable.** — the delay alone over 30 s (8b), the reverb alone over a minute (8c), and a full rack at the top of every range — maximum feedback, a 20 s decay, 12 dB of makeup and 12 dB on every EQ band — decaying to exact silence over a minute (8f)
- [x] **Nonlinear effects pass aliasing validation.** — 4x oversampling measured at 13 to 15 dB less fold-back than the base rate, and -100 dBc on a musical note (8a, `Docs/OVERSAMPLING.md` §2)
- [x] **FX state survives preset/project recall.** — a full rack of six effects, in an order that is not the enum's and every one of them dialled away from its defaults, round-tripped through `getStateInformation`/`setStateInformation` and still sounding afterwards (8f)

Phase 8 is complete.

---

# Phase 9 — Presets, Resources & State Migration

## Objective

Turn Apollo's parameter system into a reliable production preset/state architecture.

## Tasks

- [x] Define preset file format. — the extension is **`.rnv`**, one preset per file, factory and user content alike (ADR-0053); implemented in 9a as the versioned state document written as XML text, read back through the same validator host state goes through (9a)
- [x] Define preset metadata. — name, author, category, comment, in a `<PRESET>` child of the state root, so it needs no schema bump and travels into the host project with the sound it names (9a)
- [x] Implement preset browser/indexing. — both roots are walked on a background thread, bounded in count and depth, abandonable, and the finished index is sorted, numbered and published to a browser that lists, searches and filters it. The page asks for a preset by the number the backend gave it and can express no path at all (ADR-0063) (9b, 9c)
- [x] Implement preset loading. — a file is read with its size checked before it, validated by the same reader host state goes through, and a failure leaves the loaded sound alone (9a, 9b)
- [x] Implement preset saving where required. — atomically: the bytes go to a temporary file beside the target and are moved into place once complete, so an interrupted save cannot destroy the preset that was already there (ADR-0062) (9b)
- [x] Implement factory presets. — ten sounds compiled into the plugin rather than installed as files, stored as the parameters each one changes rather than as rendered documents, so a setting naming a parameter that does not exist is a test failure instead of a preset that quietly loses part of itself. Every one of them renders into an ordinary `.rnv`, loads through the ordinary reader, and is played a note by the suite to prove it makes a sound. The init patch is the empty case: it overrides nothing, so it *is* the registry's defaults (ADR-0065) (9d)
- [x] Implement user preset locations using platform-appropriate paths. — JUCE's special-location lookup supplies the per-user and shared roots; Apollo names only the two folders beneath them, so nothing spells out a platform's path (ADR-0062) (9b)
- [~] Implement resource validation. — done for presets: the extension is not evidence, every file is validated, and what cannot be read is counted rather than hidden. Wavetable resources are 9e (9b)
- [x] Implement wavetable resource management. — tables are built from spectra rather than compiled in as constants, so the four built-ins and anything read from a file take one road into the engine; built once per process and shared, because they are immutable and a host makes forty instruments; and a slot can be replaced while a note sounds, with the pointer exchanged atomically, the audio thread told to take the new one, and the old one retired rather than freed (ADR-0066) (9e)
- [x] Preload or prepare large DSP resources outside the audio callback. — wavetables are built once per process and shared, and a loaded one is prepared on its own thread and handed over by an atomic exchange (9e, ADR-0066; ticked in 12d-3)
- [x] Implement state versioning. — built in Phase 2 and inherited by presets rather than reimplemented: a `.rnv` carries the same `schemaVersion` a project does (9a)
- [x] Implement state migration. — likewise inherited. A version 1 preset migrates through the same code a version 1 project does, asserted end to end in `Tests/State/PresetDocumentTests.cpp` (9a)
- [x] Test older state versions. — a version 1 preset, written as Apollo would have written it before Phase 5b indexed the filters, opens with its cutoff intact under the new name (9a)
- [x] Test malformed/corrupt state. — empty, plain text, truncated, well-formed XML that is not Apollo, Apollo state with no version, a version from the future and a version that is not a number: each refused for its own reason, each leaving the loaded sound and its name untouched (9a)
- [x] Test missing resources. — for presets: a missing folder, a missing file, an empty file, a folder wearing the extension, a file too large, and a tree deeper than the bound are each handled without a crash and reported as themselves (9b). For wavetables: missing, too large, not audio, empty, not a whole number of frames, silent and non-finite are each refused by name, and every one of them leaves the instrument playing the table it already had (9e)

## Exit Criteria

- Presets load deterministically.
- Projects restore correctly.
- Missing/invalid resources fail gracefully.
- Older supported state versions migrate correctly.
- No preset operation blocks real-time DSP.

---

# Phase 10 — Performance, DSP Validation & Host Compatibility

## Objective

Perform rigorous engineering validation before release hardening.

## DSP validation

- [x] Frequency-response testing. — per module since Phase 5 (filter corners, EQ curves, halfband stopbands); Phase 10a adds the assembled instrument's, measured through a whole voice rather than through a component (10a)
- [x] Oscillator pitch-accuracy testing. — every third note from 24 to 96 measured against equal temperament through the whole pitch path, and the same note at four sample rates. Worst error under a cent (10a)
- [x] Aliasing measurements. — per oscillator since Phase 4; 10a measures the assembled voice, scanning every bin that is not a harmonic rather than checking named frequencies (10a)
- [x] THD/THD+N measurements where relevant. — on the sub oscillator, the one source whose output is meant to be a sine and therefore the only one where the figure means anything (10a)
- [x] Noise-floor measurements. — an instrument at rest produces **exact** silence, with every source at full level and after a release, rather than merely something quiet (10a)
- [x] Impulse/step-response tests. — the step response of a cutoff swept across its range between two blocks (10a), and each feedback effect's response to a burst in its own tests (8b, 8c; ticked in 12d-3)
- [x] Numerical stability tests. — a minute of the worst patch the controls allow, measured per ten-second window to prove nothing compounds (10a)
- [x] Regression audio renders. — fifteen renders, ten of them the factory presets, each compared against a reference compiled in beside it. A reference is a *description* of the audio rather than a wave file, because bit-exact samples are not something four compilers agree on (10b, ADR-0068)
- [x] Denormal testing. — measured by its consequence: a chain with two feedback effects reaches exact zero after a release, as do the reverb and equaliser tails, rather than grinding on as denormals (10a, 8c, 8e; ticked in 12d-3)
- [x] NaN/Inf protection tests. — every registered parameter driven to both ends and the middle of its range with a note held; output stays finite and bounded (10a)

## Performance

- [x] Measure CPU per voice. — 0.29 % of a core per voice on the default patch, 5.2 % on the heaviest, 0.53 % with four modulation routings (10c)
- [x] Measure CPU at multiple polyphony levels. — 1, 2, 4, 8, 16 and 32 voices; close to linear in all three patches (10c)
- [x] Measure oversampling cost. — per channel, including a real drive stage, at bypass, 2x and 4x (4c, re-measured 10c)
- [x] Measure FX rack cost. — every effect alone and all six at once, at 2.8 % of one core (10c)
- [x] Measure worst-case audio callback duration. — 938 callbacks timed individually per patch, as median, p99, p99.9 and worst against the block deadline, with notes arriving *during* the trace. The worst patch the controls can build misses the deadline one block in a hundred (10c, issue 13)
- [x] Measure memory usage. — 2.6 MB per instrument, 4.7 MB for the first in a process (10c)
- [x] Profile UI rendering separately. — capture adds 0.52 % of a core at full polyphony, frame building 0.05 % at 30 Hz on the message thread (7c, re-measured 10c)
- [x] Profile resource loading. — construction, prepare, and rendering and loading the whole factory library (10c)
- [x] Identify worst-case combinations rather than relying on average CPU. — the heaviest patch *with* the modulation matrix *with* a full rack at full polyphony, which nothing before 10c had measured together (10c)

## Host compatibility

Test representative compatible environments for:

Each item below is covered through the VST3 interfaces by the host harness
(10e-1, ADR-0075), which runs in CI on all three platforms. None is ticked
until a real DAW has confirmed it as well: the harness proves conformance to the
interfaces, not what a particular host does beyond them.

- [ ] Plugin discovery. — harness: one class, an instrument, identical across scans (10e-1)
- [ ] Plugin loading. — harness: stereo out, no input, no latency, the right tail; mono accepted after a fix (10e-1)
- [ ] Plugin unloading/reloading. — harness: forty cycles, and sixteen independent instances at once (10e-1)
- [ ] Parameter automation. — harness: automation reaches the sound, a stepped lane arrives as a ramp, every VST3 parameter ID pinned, and every integer parameter published as discrete after a fix (10e-1)
- [ ] State recall. — harness: every parameter back, in the plugin and in the host, sample-identical; before prepare; mid-note; foreign state refused (10e-1)
- [ ] Preset recall. — harness: a host's own .vstpreset written and read (10e-1)
- [ ] MIDI. — harness: sample-accurate onsets; sustain, pitch bend and All Notes Off through IMidiMapping (10e-1)
- [ ] Variable block sizes. — harness: blocks of 0 to 512 samples sample-identical to fixed blocks (10e-1)
- [ ] Sample-rate changes. — harness: one instance re-prepared at six rates, in tune at each (10e-1)
- [ ] Bypass. — harness: silent, nothing stuck and nothing resumed afterwards, latency unchanged; the lost-MIDI and resumed-tail defects fixed (10e-1)
- [ ] Transport behavior where applicable. — harness: the host's tempo playing and stopped, and its absence (10e-1)
- [ ] Plugin latency reporting. — harness: reported equals measured, follows the rack, recalled before the first block (10e-1)
- [ ] Offline rendering. — harness: offline at 8192-sample blocks identical to realtime (10e-1)

The goal is standards compliance and broad compatibility, not optimization for one canonical host.

## Exit Criteria

- No critical DSP correctness failures remain.
- Performance bottlenecks are understood.
- Representative host tests pass.
- Regression renders remain stable.
- Known limitations are documented.

---

# Phase 11 — Cross-Platform Release Engineering

## Objective

Validate Apollo as a portable product rather than a development-machine-specific application.

## Tasks

### Windows

- [x] Release build validation. — Release builds clean with warnings as errors, and both suites pass in it: 2,320,361 unit assertions and 181 host assertions, the regression renders included (11a)
- [x] VST3 installation/package validation. — `cmake --install` stages the bundle, the standalone and an INSTALL.txt, CPack archives it, and **the staged bundle is run through the whole host suite** rather than inspected. The import table is asserted against the libraries that ship with Windows, which is what found the Visual C++ Redistributable dependency (11a, ADR-0076)
- [x] Standalone validation. — the packaged standalone launches, renders its full interface, and closes with exit code 0 (11a, PROJECT-STATE §5a)
- [x] WebView backend validation. — WebView2 renders the full interface from the packaged build, with its user-data folder under the per-user application data directory (ADR-0027) rather than beside the executable (11a)
- [x] High-DPI validation. — measured rather than eyeballed, and it found the editor opening **102 physical pixels taller than the screen** at 150% scaling, since the interface was built. Fixed, and the sizing rule moved somewhere a test can reach it (11a, ADR-0076)

### macOS

There is no Mac on this project. CI builds, tests, packages, signs ad-hoc and
archives; it cannot open a window. Ticked below means a CI runner proved it;
everything else says what is missing rather than being ticked on inference
(11b, ADR-0077).

- [ ] Release build validation. — CI gates on RelWithDebInfo. Windows validated Release by hand in 11a; nobody has on a Mac
- [x] VST3 validation. — the staged universal bundle is loaded through a VST3 host and passes the whole suite: 189 assertions. Layout, Info.plist, moduleinfo.json, both processors read back out of the binary, 26 dependencies all macOS frameworks, ad-hoc signature valid and satisfying its Designated Requirement (11b)
- [ ] Standalone validation. — the `.app` is built universal, packaged, signed and its binary inspected. It has never been launched
- [ ] WebView backend validation. — WebKit is linked, which is the WKWebView backend being compiled in. Nobody has seen the editor render on a Mac
- [ ] Retina/scaling validation. — the sizing rule 11a fixed is arithmetic and is tested, but a 2x display has never shown it
- [ ] Code-signing/notarization pipeline where required. — ad-hoc signing is in CI and is enough to load locally. Developer ID signing and notarisation need an Apple developer account, and a downloaded plugin without them is refused as coming from an unidentified developer

### Linux

Built and validated on Ubuntu in CI; there is no Linux desktop on this project,
so the same rule applies as for macOS — ticked means a runner proved it
(11c, ADR-0078).

- [x] Build feasibility validation for the selected JUCE/WebView/plugin configuration. — configures, builds and passes the whole suite on Ubuntu, and has since Phase 0; 11c adds the package (11c)
- [ ] Release build validation where supported. — CI gates on RelWithDebInfo, as it does on macOS
- [ ] Standalone validation. — packaged and its ELF inspected; it has never been launched
- [x] VST3 validation where supported. — the staged bundle is loaded through a VST3 host and passes the whole suite: 182 assertions, layout, moduleinfo.json, no build leftovers, and every linked library either part of any system or a named prerequisite (11c)
- [x] Document platform-specific limitations. — `INSTALL.txt` names every library Apollo opens at run time and the package to install on Debian and Ubuntu, Fedora and Arch; **CI checks each of those names resolves on a real system**, because JUCE opens X11, GTK and WebKitGTK with dlopen and nothing in the build would catch a typo in a library nobody links (11c, ADR-0078)

### CPU architectures

- [x] Validate x86-64 builds. — Windows, macOS and Linux, on every push since Phase 0 (11d)
- [x] Validate ARM64 builds where toolchain/dependency/plugin ecosystem support is available. — a fifth CI job on an `ubuntu-24.04-arm` runner: the whole suite, the host suite against a staged `aarch64-linux` package, and an archive of its own. macOS ships arm64 as half of its universal binary (11b). **Windows on ARM64 is untried** — no runner, and JUCE targets `arm64ec` there, which is a different target rather than a rebuild (11d, ADR-0079)
- [x] Avoid architecture-specific assumptions in DSP. — there are none to avoid: no intrinsics, no inline assembly, no `_mm_` or `__m128` anywhere under `Source/`, because ADR-0073 declined SIMD on measured evidence and ADR-0074 took the data layout instead. Denormal handling is `juce::ScopedNoDenormals`, which sets the ARM FPCR as readily as the x86 MXCSR (11d)

## Exit Criteria

- Each officially supported platform has a documented build/test path.
- Packaging does not depend on developer-machine paths.
- Platform-specific behavior is isolated and documented.

---

# Phase 12 — Release Candidate & Production Hardening

## Objective

Convert the validated build into a stable release candidate.

## Tasks

### Reliability

A `Reliability` category in the host harness, driven through a real VST3 host,
with `--soak N` to run any of it longer (12a, ADR-0080).

- [x] Run long-duration stability tests. — notes, patch changes and a transport for as long as the scale says; every sample finite, bounded, and the median level of the last third against the first (12a)
- [x] Test repeated plugin load/unload cycles. — dozens of load-prepare-play-unload cycles, each playing, with the resident footprint after settling compared against the end (12a)
- [x] Test repeated preset changes. — hundreds of state recalls through the host under a sounding voice (12a)
- [x] Test rapid parameter automation. — every parameter swept, thousands of moves, the rack rebuilt hundreds of times a second (12a)
- [x] Test maximum intended polyphony. — the hostile patch is sixteen unison on both oscillators plus sub and noise, held for the length of the soak (12a)
- [x] Test extreme modulation. — part of the long session's patch rotation; the modulation matrix at dominating depth is also a pinned regression render (§5d)
- [x] Test extreme feedback settings. — maximum delay feedback, a twenty-second reverb and two resonant filters at once: bounded, not compounding, and decaying (12a)
- [x] Test repeated sample-rate changes. — six rates against six block sizes, repeatedly, with the instrument still within half a hertz of 220 Hz after each (12a)
- [ ] Test UI reload/recreation. — the headless harness cannot open an editor. The bridge's attach and detach are covered by `Tests/UI/ParameterBridgeTests.cpp`, and opening and closing the window was driven by hand on Windows (§5a); repeated recreation inside a host is not tested
- [x] Test malformed state and resource conditions. — a stream of damaged documents mid-render, with every parameter still legal afterwards, and eight refusable documents each checked to be refused with nothing applied (12a)

### Audio quality

- [x] Final listening tests. — the nine factory sounds, Init and the four wavetables played by the developer from two MIDI keyboards, each producing the sound it is meant to (12b, 2026-10-18; PROJECT-STATE §5a)
- [x] Final aliasing review. — reviewed by hand by the developer on two keyboards; measured at -98.5 dBc worst case at four notes in 10a (PROJECT-STATE §5c). A sweep across the whole keyboard was not run (12b)
- [x] Final gain-staging review. — reviewed by hand by the developer; every factory preset measured audible and below full scale, and gain staging across five voicings, in the suite (12b)
- [x] Final noise-floor review. — reviewed by hand by the developer; exact silence at rest, DC at -112.7 dBFS and inharmonic content at -121.9 dBc measured in 10a (12b)
- [x] Final transient-response review. — reviewed by hand by the developer; the filter step response is measured in 10a. Attack timing against the envelope's own figures was not measured (12b)

### UX

- [x] Verify all controls are discoverable. — every interaction a knob has is offered in its right-click menu or named in the footer hint; the preset list says what its columns are; the status line and readout say things a user can act on (12c-2, ADR-0084). The first screen's layout is unchanged at the developer's direction
- [x] Verify keyboard/mouse interaction. — keyboard in 12c-1: every control reachable, one stop per group, arrows inside it (ADR-0083). Mouse in 12c-2: drag, Shift-drag, double-click, both menu items, switches, tabs, the depth rail and the preset list, each driven with real pointer events (ADR-0084). No mouse wheel on knobs, deliberately
- [x] Verify MIDI Learn workflow. — the mode, the LEARN badge and instruction, Escape twice, a profile, Delete to release one, Clear all; release and clear now report what they did (12c-3, ADR-0085)
- [x] Verify preset workflow. — save, the replace prompt, Cancel, Replace it, a refused name, the highlight after a restart; found and fixed a replace prompt that survived a name change (12c-3, ADR-0085)
- [x] Verify error messages. — every message the engine can show was read in place; refusals now appear where the user is looking, say what would make the request work, and carry a warning sign as well as a colour (12c-3, ADR-0085)
- [x] Verify visual feedback. — a focus ring on every control including a chosen chip (12c-1), a typed-value field that says Not understood (12c-2), status tones with a shape as well as a colour, and a MIDI panel no longer marked off when it is not (12c-3)
- [x] Verify accessibility basics and focus behavior. — a heading and region per panel, valid tab and radio semantics, a focus ring visible on every control including a chosen chip, and focus returned after the reset menu closes (12c-1, ADR-0083)

### Documentation

- [x] Update PRD. — a status note at the end of each section, from the code; requirements kept, not deleted, because which unbuilt ones are still wanted is the developer's decision (12d-2, ADR-0087)
- [x] Update architecture documentation. — ARCHITECTURE.md §20 "As built", and REPOSITORY-LAYOUT.md redrawn from the real tree (12d-2)
- [x] Update UI bindings. — the loaded-preset rule, refusals in the save form, every error code, the new stores and the interface tests (12d-2)
- [x] Update roadmap. — closed phases moved to Done in the index, tasks later phases finished ticked with their evidence, and the project definition of done read against what was built (12d-3)
- [x] Document known limitations. — Docs/KNOWN-LIMITATIONS.md: not yet verified, specified but not built, decided against, and distribution, kept apart because they mean different things (12d-1)
- [x] Document supported platforms/formats. — Docs/PLATFORMS.md, including a table of what has been *seen* working per platform rather than only built (12d-1)
- [x] Document build requirements. — Docs/BUILD.md §1: Node 22.18 (which the interface tests need, now checked at configure time) and the real Linux package list (12d-1)
- [x] Document preset/state compatibility policy. — Docs/COMPATIBILITY.md, each promise naming the test that holds it; a preset 0.1.0 wrote is frozen in the suite and every later build must read it (12d-3, ADR-0088)

### Release

**Moved to Phase 14** (ADR-0089). The tasks stay listed here, where they were
written, and Phase 14 below takes them.

- [ ] Create release candidate.
- [ ] Perform clean-machine installation tests.
- [ ] Verify plugin discovery.
- [ ] Verify standalone launch.
- [ ] Verify uninstall/update behavior where applicable.
- [ ] Generate release artifacts.
- [ ] Tag the release in version control.

## Exit Criteria

Apollo is considered release-ready only when there are no unresolved P0/P1 issues and all supported-platform release criteria have passed.

---

# Phase 13 — Completing the Specification

## Objective

Build every feature the PRD specifies that 0.1.0 does not have, before the
first release. Added after Phase 12d found them (PROJECT-STATE issue 26) and the
developer decided they belong in it (ADR-0089). Docs/KNOWN-LIMITATIONS.md lists
them; each sub-phase removes its rows from that list as it lands.

## Rules every sub-phase follows

- **A new parameter's default reproduces the sound from before it.** The
  regression renders are the proof: they must pass unchanged, against
  references recorded before the parameter existed (Docs/COMPATIBILITY.md §3).
- **The pinned VST3 parameter numbers only grow** — regenerated with
  `ApolloHostTests --parameter-ids`, and the diff must be additions.
- **Every control reaches the sound**, tested through a processor rendering a
  note, not only through the DSP module.

## Tasks

### Oscillators (13a)

- [x] Coarse and fine tuning for oscillator 1, which only oscillator 2 had (PRD §8.1). — `osc1_semitones`, `osc1_fine`; measured a fifth and a quarter tone up to within 2 cents (13a)
- [x] A start phase per source (PRD §8.1, §9). — `osc1_phase`, `osc2_phase`, `sub_phase`; half a cycle on the sine sub measures as its exact negative (13a)
- [x] Unison phase randomisation (PRD §11). — `oscN_phase_random`; a hash of each voice's per-note random state, so it scatters differently per note and renders identically every time (13a)
- [x] Unison detune curve, linear or exponential (PRD §11). — `oscN_detune_curve`; the inner voices at the square of their position (13a)
- [x] Unison blend (PRD §11). — `oscN_unison_blend`; the outer voices against the centre at constant energy (13a)
- [x] Sub oscillator triangle and square (PRD §9). — `sub_waveform`; built from their spectra and band-limited, harmonics measured at 1/k² and 1/k to within 0.1 dB (13a)
- [x] Sub tracking (PRD §9). — `sub_tracking`; the note, or oscillator 1 including its tuning and pitch modulation (13a)
- [ ] Interpolation quality modes (PRD §8.2, optional).

### Warp (13b)

- [x] Asymmetric sync, PWM, bend and mirror (PRD §12), each with enable and amount, alias-measured. — five modes, off by default; aliasing measured at four notes and pinned 3 dB above the figures. Every mode is above the plain oscillator's −60 dBc budget by C7 — bend stays under it to C6 — and sync, PWM and mirror are above it from middle C (Docs/KNOWN-LIMITATIONS.md) (13b)
- [x] Warp amount as a modulation destination (PRD §16). — destinations 17 and 18, appended so every existing destination keeps its number (13b)

### Filters (13c)

- [x] Comb and formant types (PRD §13.1). — comb tuned by the cutoff with an allpass-interpolated period so its loudness does not change from note to note; formant sweeping U-O-A-E-I with the cutoff, the resonance narrowing it (13c-1, ADR-0091)
- [x] Per-filter mix and key tracking (PRD §13.2). — mix smoothed per sample and bit-exact at full; key tracking −100 % to +100 % about middle C, following the note and its bend (13c-2, ADR-0092)
- [x] Split routing (PRD §13). — a third routing in which each oscillator, the sub and the noise chooses filter 1, filter 2 or neither, summed rather than halved; every path exactly the routing it stands for (13c-3, ADR-0093)

### Noise (13d)

- [x] Pink noise and key tracking (PRD §10). — Kellet's pink filter at white's level; key tracking plays the noise slower below middle C, darker and not quieter, its level corrected per sample (13d-1, ADR-0094)
- [x] Built-in transient samples (PRD §10). — four, synthesised rather than recorded, each kept at six bandwidths so a tracked transient played high reads one that cannot alias (13d-2, ADR-0095)

### Modulation (13e)

- [x] LFO tempo sync (PRD §15.2). — seventeen divisions from 8/1 to 1/32 at the host's tempo, 120 without one; a free-running synced LFO takes its phase from the bar while the transport runs (13e-1, ADR-0096)
- [x] Envelope retrigger options (PRD §15.1). — per envelope, CONTINUE (the default, as before) or RESTART; only a stolen voice can tell them apart, and restarting gives its note the whole attack it otherwise skips (13e-2, ADR-0097)
- [x] Macro controls: MIDI-learnable, several destinations, bipolar depth, ranges, curves, stored in presets (PRD §17). — four, each a matrix source shaped by its own minimum, maximum and curve; one macro took one oscillator down and another up, measured exactly against each alone (13e-3, ADR-0098)
- [x] Filter drive, mix and key tracking as destinations, and modulation of a routing's depth (PRD §16). — six destinations appended, each meaning what its control means, measured equal to setting the control; a via source per slot scales its contribution (13e-4, ADR-0099)
- [x] Effect parameters as modulation destinations (PRD §16). — distortion drive and mix, delay feedback and mix, reverb mix, compressor mix; a routed constant measured identical to the control set there (13e-5, ADR-0100)

### Editors (13f)

- [x] Drawn LFO curves, stored in presets (PRD §15.2; ADR-0034 deferred them for this). — up to 32 points with a bend per segment, dragged on the LFO page; carried in presets and projects as an element beside the parameters, played from a table handed to the audio thread through a triple buffer. The default curve plays as the triangle, to 6e-8 (13f-1, ADR-0102)
- [x] Envelopes edited by dragging (PRD §15.1). — the shape of a note held to its sustain and released: corners for the five times and the sustain, diamonds for the curve, a double-click to reset and the whole of it from the keyboard. No new state: each handle moves the existing parameters with a knob's gestures. The engine and the page are held to one table of the curve's values (13f-2, ADR-0103)

### Resources (13g)

- [x] Choose a wavetable or noise sample from a file, through a native chooser rather than a path across the bridge (PRD §33, CLAUDE.md §40). — from the library by number, or from anywhere through the system's own dialog, which the page can only ask the editor to open; each oscillator has its own USER table, and the noise a SAMPLE played once or looped (13g, ADR-0104)
- [x] Presets refer to user wavetables and samples, with a fallback when a file has moved (PRD §32, §33). — better than a reference: the audio itself travels in the project or preset, lossless at 24 bits, so a moved or deleted file changes nothing; content that cannot be read empties its slot, which still sounds (13g, ADR-0104)

### The rest (13h)

- [x] Independent left and right delay times, and delay modulation (PRD §20). — a link switch, linked by default, and the right side's own time and note value; a sine on the read position up to 10 ms either way, the right side a quarter of a cycle behind. Linked and unmodulated, the delay is the old one sample for sample (13h-1, ADR-0105)
- [x] Program change (PRD §34). — the built-in sounds as the host's program list, and MIDI program changes by the same numbering with bank select; loaded on the message thread through the browser's own path; a switch to ignore them; a project remembers its program so a host setting it again cannot load over the user's sound (13h-2, ADR-0106)
- [ ] Controller profiles that can be edited, exported and imported (PRD §35).
- [ ] Drag-and-drop modulation (PRD §42.4).
- [x] A spectrum analyser (PRD §30, optional). — built early, as the equaliser's input and output spectra behind its curve (ADR-0101).

## Exit Criteria

Docs/KNOWN-LIMITATIONS.md has no "specified but not built" rows left, or each
remaining one carries a decision of the developer's to leave it.

---

# Phase 14 — Release Candidate & Release

## Objective

The first release. Formerly 12e, moved here so that it follows Phase 13
(ADR-0089). Its tasks are the ones listed under Phase 12 "Release", unchanged.

## Exit Criteria

As Phase 12's: no unresolved P0/P1 issues, and every supported platform's
release criteria passed.

---

# 3. Cross-Phase Engineering Gates

These requirements apply throughout the entire roadmap.

## Real-Time Safety Gate

No feature may be accepted if it introduces avoidable:

- Memory allocation on the audio thread.
- Blocking locks.
- Filesystem access.
- Network access.
- WebView calls.
- Synchronous logging.
- Unbounded computation.
- Cross-thread ownership ambiguity.

## DSP Quality Gate

Material DSP changes require appropriate:

- Unit tests.
- Spectral analysis.
- Listening tests.
- Regression renders.
- Numerical stability validation.

## State Compatibility Gate

Any change to parameters or serialized state must consider:

- Existing parameter IDs.
- Existing presets.
- Existing project state.
- State migration.
- Host automation compatibility.

## Portability Gate

New functionality must not silently introduce:

- OS-specific assumptions.
- Host-specific behavior.
- Controller-specific requirements.
- Driver-specific behavior.
- Hard-coded filesystem paths.
- Architecture-specific DSP behavior without justification.

## Performance Gate

Material performance changes must be measured using representative:

- Voice counts.
- Sample rates.
- Buffer sizes.
- Oversampling factors.
- FX configurations.
- UI states.

---

# 4. Dependency Graph

```text
Phase 0
   │
   ▼
Phase 1 ────────────────┐
   │                    │
   ▼                    ▼
Phase 2              Phase 3
   │                    │
   └──────────┬─────────┘
              ▼
           Phase 4
              │
              ▼
           Phase 5
              │
       ┌──────┴──────┐
       ▼             ▼
    Phase 6       Phase 7
       │             │
       └──────┬──────┘
              ▼
           Phase 8
              │
              ▼
           Phase 9
              │
              ▼
          Phase 10
              │
              ▼
          Phase 11
              │
              ▼
          Phase 12
```

UI work may begin during Phase 2 using mock/native test parameters, but final UI completion depends on the authoritative parameter and binding architecture.

FX development may proceed in parallel with late synthesis development once the shared audio-engine contracts are stable.

---

# 5. Milestone Definitions

## M0 — Repository Ready

CMake/JUCE project builds and the architecture/documentation baseline is established.

## M1 — Audio Foundation

Standalone and VST3 targets initialize correctly with a verified real-time-safe audio path.

## M2 — Parameter Bridge

APVTS, serialization, host automation, and WebView binding are functional.

## M3 — First Sound

A polyphonic voice can produce stable audio through the native engine.

## M4 — Wavetable Engine Complete

Dual oscillators, wavetable scanning, unison, sub/noise, and anti-aliasing are functional.

## M5 — Synthesis Core Complete

Filters, envelopes, LFOs, and modulation matrix are functional.

## M6 — Control Complete

Generic MIDI, MIDI Learn, automation, and parameter interaction are functional.

## M7 — UI Alpha

The primary sound-design interface and visualization system are usable end-to-end.

## M8 — FX Complete

The modular effects rack is functional, reorderable, stable, and serialized.

## M9 — Preset Complete

Preset/resource management and state migration are production-capable.

## M10 — Beta

DSP, performance, state, UI, MIDI, and representative host compatibility testing are substantially complete.

## M11 — Release Candidate

Cross-platform builds, packaging, regression testing, and production hardening are complete.

## M12 — Release

All P0/P1 issues are resolved and documented release criteria are satisfied.

---

# 6. Risk Register

| Risk | Impact | Mitigation |
|---|---|---|
| Wavetable aliasing | High | Band-limited tables, spectral tests, oversampling where appropriate |
| Excessive polyphonic CPU | High | Profiling, voice budgeting, efficient DSP, measured oversampling |
| Audio-thread blocking | Critical | Strict threading rules, code review, stress tests |
| WebView latency/CPU | Medium | Snapshot-based telemetry, throttling, asynchronous bridge |
| Host parameter incompatibility | High | Stable IDs, APVTS, representative host testing |
| State migration failure | High | Versioned schema and migration tests |
| Platform WebView differences | Medium | JUCE abstraction and platform-specific isolation |
| Feedback instability | High | Bounded parameters, stability tests, numerical protection |
| UI/DSP desynchronization | High | Native state as single source of truth |
| Resource-loading glitches | High | Preload/background preparation and validated handoff |
| Plugin lifecycle edge cases | High | Repeated load/unload/reset testing |
| ARM64/platform differences | Medium | Cross-architecture CI/build validation where supported |
| Overengineering before profiling | Medium | Incremental implementation and evidence-driven optimization |

---

# 7. Definition of Done — Project

Apollo is not considered complete merely because every planned feature exists.

A production-ready build must satisfy all of the following:

- [ ] Core DSP is real-time safe. — by construction and by review, but not enforced by a tool: there is no allocation or lock detector on the audio thread (PROJECT-STATE issue 6) (read in 12d-3)
- [x] DSP has passed appropriate numerical and spectral validation. — tuning, THD+N, silence, DC, inharmonic content, step response, stability, regression renders (10a, 10b) (read in 12d-3)
- [x] Wavetable synthesis has acceptable anti-aliasing behavior. — −98.5 dBc worst case against a −60 dBc budget (4a, 10a) (read in 12d-3)
- [x] Polyphony and unison perform within defined CPU budgets. — the worst patch the controls allow keeps up, measured per callback (10c, 10d) (read in 12d-3)
- [x] APVTS is the authoritative parameter system. — 227 parameters, every one affecting the sound (2, 8) (read in 12d-3)
- [x] UI bindings are validated, asynchronous, and versioned. — UI_BINDINGS.md; every message validated, refused cleanly, and versioned (2, 9c, 12c) (read in 12d-3)
- [x] Host automation works correctly. — through the VST3 interfaces on every platform (10e-1). Not yet in a DAW — see "Representative hosts" (read in 12d-3)
- [x] Preset/project state restores correctly. — every parameter round-trips; a preset 0.1.0 wrote is read by every build (2, 9a, 12d-3) (read in 12d-3)
- [x] State migration is implemented where required. — version 1 to 2 (ADR-0032), with the policy in Docs/COMPATIBILITY.md (12d-3) (read in 12d-3)
- [x] MIDI and MIDI Learn are controller-agnostic. — any CC, any controller, profiles optional; played from two different keyboards (6, 12b) (read in 12d-3)
- [x] FX modules are stable and reorderable. — all 720 orderings, rearranged every block (8f) (read in 12d-3)
- [x] UI telemetry cannot block DSP. — lock-free rings, and audio sample-for-sample identical with capture on and off (7a, 7b) (read in 12d-3)
- [x] No core subsystem depends on a specific DAW, OS, controller, driver, audio device, or physical connector. — one platform conditional in the whole engine, choosing the WebView (ARCHITECTURE.md §20.1) (read in 12d-3)
- [x] Supported platforms have reproducible builds. — pinned JUCE and a locked frontend, built on four platforms in CI (0, 11) (read in 12d-3)
- [ ] Representative hosts have been tested. — only through a VST3 host implementation; no DAW yet (10e-2, deferred by the developer) (read in 12d-3)
- [x] Long-duration stability testing has passed. — the reliability soak (12a) (read in 12d-3)
- [x] Known limitations are documented. — Docs/KNOWN-LIMITATIONS.md (12d-1) (read in 12d-3)
- [x] Release artifacts can be produced from a clean environment. — every CI run packages Apollo on a fresh runner for each platform (11) (read in 12d-3)

---

# 8. Living Roadmap Policy

This roadmap should evolve with the implementation.

When a planned task becomes technically obsolete, it should be revised rather than followed blindly.

For significant roadmap changes, document:

1. Why the original approach is insufficient.
2. What technical evidence motivated the change.
3. What new implementation is proposed.
4. What dependencies are affected.
5. What tests must be added or changed.
6. What compatibility risks exist.
7. Whether the change affects the PRD, architecture, or UI binding contract.

> **Final Engineering Directive:** Build Apollo incrementally from a verified real-time-safe foundation toward a complete, portable instrument. Prefer measurable engineering progress over premature polish, preserve stable contracts between subsystems, and never allow a convenient development-machine configuration to become a hidden product dependency.