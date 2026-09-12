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
Phase 12 → Release Candidate & Production Hardening
```

The phases are sequential at the architectural level but may overlap during implementation. Dependencies should be respected even when work proceeds in parallel.

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
  - [x] `UI-BINDINGS.md` — present as `UI_BINDINGS.md`; see `PROJECT-STATE.md` §6
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
- [ ] Configure resource embedding/packaging. — deferred to Phase 9; no resources exist yet
- [ ] Configure frontend asset packaging. — deferred to Phase 7; no frontend exists yet
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
- [x] Implement wavetable selection. — four built-in morph tables (4a)
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
- [x] Validate retrigger behavior. — a retriggered envelope continues from its current level rather than restarting at zero, which is what keeps the restart click-free; asserted in `Tests/DSP/EnvelopeTests.cpp` (5a)
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
- [ ] FX rack. — the two FX parameters that exist are in the Output module;
      the rack itself is Phase 8
- [x] Master/output controls.
- [ ] Preset browser. — Phase 9 owns the preset system
- [ ] MIDI Learn interface. — Phase 6 owns the mapping engine

### Visual system

- [x] Implement dark UI foundation.
- [x] Implement a gold visual language, keyed to `#F7EF8A`. — as a single
      interaction accent rather than as decoration (ADR-0039, ADR-0051). The
      deep-purple language this item originally asked for was built and shipped
      through Phase 7d, then replaced when the developer said it had never been
      the intent
- [x] Implement green active-state indicators. — modulation and activity only
- [ ] Implement red warning/error indicators. — the token and its meaning are
      defined; nothing yet produces a clipping or overload state to show
- [x] Define typography. — UI sans for labels, tabular monospace for every
      number so a readout cannot jitter as it counts
- [x] Define spacing. — a 4 px scale, with every value taken from a token
- [x] Define control states. — hover, focus-visible, held, modulated, inactive
- [ ] Define disabled/error/focus states. — focus and inactive are done;
      disabled and error need states the engine does not yet report
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
- [ ] Optional spectrum visualization. — the one item in this section still outstanding, and explicitly optional; it is an FFT and a log frequency axis rather than more of the transport built here
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
> all capture is gated on whether anything is watching. Two items remain
> unticked and both are marked optional or deferred: a spectrum analyser, and
> the hosts and browsers this environment cannot reach.

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

- [ ] Implement delay buffer.
- [ ] Implement delay time.
- [ ] Implement feedback.
- [ ] Implement wet/dry mix.
- [ ] Implement stereo/ping-pong behavior where required.
- [ ] Handle parameter smoothing.
- [ ] Validate feedback stability.

### Reverb

- [ ] Implement/reuse suitable reverb architecture.
- [ ] Validate decay stability.
- [ ] Handle denormals.
- [ ] Measure CPU cost.

### Gate / dynamics

- [ ] Implement noise gate.
- [ ] Implement compressor.
- [ ] Define envelope detector behavior.
- [ ] Validate attack/release behavior.

### EQ

- [ ] Implement EQ architecture.
- [ ] Validate filter stability.
- [ ] Provide predictable gain behavior.

### FX rack

- [x] Implement common effect-module interface. — `dsp::AudioEffect`: prepare, process, processBypassed, reset, latency, tail (8a)
- [x] Implement bypass. — per effect, keeping its position and its latency, so toggling it does not renegotiate the plugin's delay (8a)
- [x] Implement reordering. — six slot parameters naming what occupies each position; duplicates resolve to the first occurrence (8a)
- [x] Implement state serialization. — the chain is parameters, so it saves and restores with everything else; covered in `Tests/Audio/EffectsIntegrationTests.cpp` (8a)
- [x] Implement latency reporting where relevant. — summed over the chain, published to the host from the message thread when it changes (8a)
- [x] Implement tail reporting where relevant. — the longest tail in the active chain, added to the envelope's release (8a)
- [ ] Test arbitrary valid effect orderings. — the machinery is tested in 8a; ordering between two effects becomes testable in 8b, and the whole chain in 8f

## Exit Criteria

- Every effect operates independently.
- FX order can be changed safely.
- Feedback effects remain stable.
- Nonlinear effects pass aliasing validation.
- FX state survives preset/project recall.

---

# Phase 9 — Presets, Resources & State Migration

## Objective

Turn Apollo's parameter system into a reliable production preset/state architecture.

## Tasks

- [ ] Define preset file format. — the extension is **`.rnv`**, one preset per file, factory and user content alike (ADR-0053)
- [ ] Define preset metadata. — name, author, category, comment, carried in the file beside the state document
- [ ] Implement preset browser/indexing.
- [ ] Implement preset loading.
- [ ] Implement preset saving where required.
- [ ] Implement factory presets.
- [ ] Implement user preset locations using platform-appropriate paths.
- [ ] Implement resource validation.
- [ ] Implement wavetable resource management.
- [ ] Preload or prepare large DSP resources outside the audio callback.
- [ ] Implement state versioning.
- [ ] Implement state migration.
- [ ] Test older state versions.
- [ ] Test malformed/corrupt state.
- [ ] Test missing resources.

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

- [ ] Frequency-response testing.
- [ ] Oscillator pitch-accuracy testing.
- [ ] Aliasing measurements.
- [ ] THD/THD+N measurements where relevant.
- [ ] Noise-floor measurements.
- [ ] Impulse/step-response tests.
- [ ] Numerical stability tests.
- [ ] Regression audio renders.
- [ ] Denormal testing.
- [ ] NaN/Inf protection tests.

## Performance

- [ ] Measure CPU per voice.
- [ ] Measure CPU at multiple polyphony levels.
- [ ] Measure oversampling cost.
- [ ] Measure FX rack cost.
- [ ] Measure worst-case audio callback duration.
- [ ] Measure memory usage.
- [ ] Profile UI rendering separately.
- [ ] Profile resource loading.
- [ ] Identify worst-case combinations rather than relying on average CPU.

## Host compatibility

Test representative compatible environments for:

- [ ] Plugin discovery.
- [ ] Plugin loading.
- [ ] Plugin unloading/reloading.
- [ ] Parameter automation.
- [ ] State recall.
- [ ] Preset recall.
- [ ] MIDI.
- [ ] Variable block sizes.
- [ ] Sample-rate changes.
- [ ] Bypass.
- [ ] Transport behavior where applicable.
- [ ] Plugin latency reporting.
- [ ] Offline rendering.

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

- [ ] Release build validation.
- [ ] VST3 installation/package validation.
- [ ] Standalone validation.
- [ ] WebView backend validation.
- [ ] High-DPI validation.

### macOS

- [ ] Release build validation.
- [ ] VST3 validation.
- [ ] Standalone validation.
- [ ] WebView backend validation.
- [ ] Retina/scaling validation.
- [ ] Code-signing/notarization pipeline where required.

### Linux

- [ ] Build feasibility validation for the selected JUCE/WebView/plugin configuration.
- [ ] Release build validation where supported.
- [ ] Standalone validation.
- [ ] VST3 validation where supported.
- [ ] Document platform-specific limitations.

### CPU architectures

- [ ] Validate x86-64 builds.
- [ ] Validate ARM64 builds where toolchain/dependency/plugin ecosystem support is available.
- [ ] Avoid architecture-specific assumptions in DSP.

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

- [ ] Run long-duration stability tests.
- [ ] Test repeated plugin load/unload cycles.
- [ ] Test repeated preset changes.
- [ ] Test rapid parameter automation.
- [ ] Test maximum intended polyphony.
- [ ] Test extreme modulation.
- [ ] Test extreme feedback settings.
- [ ] Test repeated sample-rate changes.
- [ ] Test UI reload/recreation.
- [ ] Test malformed state and resource conditions.

### Audio quality

- [ ] Final listening tests.
- [ ] Final aliasing review.
- [ ] Final gain-staging review.
- [ ] Final noise-floor review.
- [ ] Final transient-response review.

### UX

- [ ] Verify all controls are discoverable.
- [ ] Verify keyboard/mouse interaction.
- [ ] Verify MIDI Learn workflow.
- [ ] Verify preset workflow.
- [ ] Verify error messages.
- [ ] Verify visual feedback.
- [ ] Verify accessibility basics and focus behavior.

### Documentation

- [ ] Update PRD.
- [ ] Update architecture documentation.
- [ ] Update UI bindings.
- [ ] Update roadmap.
- [ ] Document known limitations.
- [ ] Document supported platforms/formats.
- [ ] Document build requirements.
- [ ] Document preset/state compatibility policy.

### Release

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

- [ ] Core DSP is real-time safe.
- [ ] DSP has passed appropriate numerical and spectral validation.
- [ ] Wavetable synthesis has acceptable anti-aliasing behavior.
- [ ] Polyphony and unison perform within defined CPU budgets.
- [ ] APVTS is the authoritative parameter system.
- [ ] UI bindings are validated, asynchronous, and versioned.
- [ ] Host automation works correctly.
- [ ] Preset/project state restores correctly.
- [ ] State migration is implemented where required.
- [ ] MIDI and MIDI Learn are controller-agnostic.
- [ ] FX modules are stable and reorderable.
- [ ] UI telemetry cannot block DSP.
- [ ] No core subsystem depends on a specific DAW, OS, controller, driver, audio device, or physical connector.
- [ ] Supported platforms have reproducible builds.
- [ ] Representative hosts have been tested.
- [ ] Long-duration stability testing has passed.
- [ ] Known limitations are documented.
- [ ] Release artifacts can be produced from a clean environment.

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