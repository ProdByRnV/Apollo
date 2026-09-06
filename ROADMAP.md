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
- [ ] Validate standalone device selection. — requires manual verification on a real device
- [x] Verify operation without assuming a particular audio driver or physical connector.

## Exit Criteria

- [ ] VST3 loads in a representative compatible host. — **requires a DAW; not verifiable in this environment**
- [ ] Standalone launches and produces audio. — artefact builds; launch is manual verification. Interpreted as "the audio path runs cleanly": Apollo outputs silence until Phase 3, by design
- [x] Variable block sizes and sample-rate changes do not break processing. — covered by `Tests/Audio/ProcessorLifecycleTests.cpp`
- [x] No known real-time thread violations exist in the foundation.

> **Phase status:** implementation complete and unit-tested. The two unchecked
> exit criteria need a DAW and an audio device, which are manual steps. See
> `PROJECT-STATE.md` §6.

---

# Phase 2 — Parameter, State & UI Binding Infrastructure

## Objective

Build the authoritative parameter/state system and the C++ ↔ WebView communication layer before complex DSP is added.

## Tasks

### APVTS

- [ ] Create the central APVTS parameter registry.
- [ ] Implement stable parameter IDs.
- [ ] Define normalized and plain-value conversion.
- [ ] Define parameter ranges, defaults, units, steps, and skew.
- [ ] Define parameter smoothing requirements.
- [ ] Define modulation capabilities.
- [ ] Add parameter metadata required by the frontend.

### State

- [ ] Implement APVTS serialization.
- [ ] Implement state restore.
- [ ] Add state schema/version metadata.
- [ ] Establish state migration architecture.

### UI bridge

- [ ] Initialize the JUCE WebView layer.
- [ ] Implement bridge initialization.
- [ ] Implement protocol versioning.
- [ ] Implement initial state synchronization.
- [ ] Implement validated `setParameter` commands.
- [ ] Implement asynchronous native-to-Web parameter updates.
- [ ] Implement parameter gesture begin/update/end semantics.
- [ ] Implement structured bridge errors.
- [ ] Prevent arbitrary native command execution from the frontend.

### Testing

- [ ] Parameter registry tests.
- [ ] State serialization tests.
- [ ] State restoration tests.
- [ ] Bridge protocol tests.
- [ ] Invalid-message tests.
- [ ] Thread-safety tests.

## Exit Criteria

- Every initial parameter can be changed from native code and the UI.
- Native state and UI state remain synchronized.
- Host automation can change parameters without breaking synchronization.
- The WebView never blocks the real-time audio path.

---

# Phase 3 — Audio Engine & Voice Architecture

## Objective

Create the reusable native synthesis engine and polyphonic voice infrastructure.

## Tasks

- [ ] Define the audio engine ownership model.
- [ ] Implement voice allocation.
- [ ] Implement configurable polyphony.
- [ ] Implement deterministic voice stealing.
- [ ] Implement note-on/note-off handling.
- [ ] Implement velocity handling.
- [ ] Implement sustain behavior.
- [ ] Implement pitch bend.
- [ ] Implement per-voice state reset/reuse.
- [ ] Preallocate voice and audio resources.
- [ ] Establish voice/global processing boundaries.
- [ ] Define mono/stereo signal-flow conventions.
- [ ] Add gain-staging rules throughout the engine.

### Real-time requirements

- [ ] No uncontrolled allocation in the audio callback.
- [ ] No blocking mutexes in real-time processing.
- [ ] No filesystem access.
- [ ] No WebView calls.
- [ ] No synchronous logging.
- [ ] No unbounded work based on UI state.

## Exit Criteria

- Polyphonic test tones render correctly.
- Voice stealing behaves predictably.
- No audible artifacts occur during normal note transitions.
- Stress testing does not reveal fundamental audio-thread violations.

---

# Phase 4 — Wavetable Oscillator System

## Objective

Implement Apollo's primary synthesis engine with high-quality, anti-aliased wavetable oscillators.

## Tasks

### Oscillators

- [ ] Implement oscillator 1.
- [ ] Implement oscillator 2.
- [ ] Implement continuous phase accumulation.
- [ ] Implement phase wrapping.
- [ ] Implement frequency/tuning control.
- [ ] Implement wavetable selection.
- [ ] Implement wavetable-position/frame scanning.
- [ ] Implement high-quality interpolation.
- [ ] Implement oscillator level/pan controls.
- [ ] Implement unison.
- [ ] Implement detune/spread.
- [ ] Implement sub oscillator.
- [ ] Implement stereo noise generator.

### Anti-aliasing

- [ ] Establish wavetable band-limiting strategy.
- [ ] Test oscillator output spectrally across the frequency range.
- [ ] Measure aliasing at different pitches.
- [ ] Test high-frequency table scanning.
- [ ] Validate interpolation artifacts.
- [ ] Document acceptable aliasing thresholds.

### Nonlinear preparation

- [ ] Define where oversampling is required.
- [ ] Avoid unnecessary oversampling of purely linear stages.
- [ ] Build reusable oversampling infrastructure for nonlinear stages.

## Exit Criteria

- Oscillators track pitch accurately.
- Wavetable scanning is smooth.
- Unison remains stable at high voice counts.
- Spectral testing demonstrates acceptable anti-aliasing performance.
- CPU cost is measured across representative polyphony levels.

---

# Phase 5 — Filters, Envelopes & Modulation

## Objective

Build the expressive sound-design architecture around the oscillator core.

## Tasks

### Filters

- [ ] Implement filter 1.
- [ ] Implement filter 2.
- [ ] Implement required low-pass, high-pass, and band-pass modes.
- [ ] Implement cutoff.
- [ ] Implement resonance.
- [ ] Implement filter drive where applicable.
- [ ] Validate stability across supported sample rates.
- [ ] Smooth filter parameter changes.

### Envelopes

- [ ] Implement four DAHDSR envelopes.
- [ ] Implement attack.
- [ ] Implement hold.
- [ ] Implement decay.
- [ ] Implement sustain.
- [ ] Implement release.
- [ ] Implement delay if included in the final envelope definition.
- [ ] Validate retrigger behavior.
- [ ] Validate note-off behavior.

### LFOs

- [ ] Implement four LFOs.
- [ ] Implement selectable waveform shapes.
- [ ] Implement rate.
- [ ] Implement phase/reset behavior.
- [ ] Define free-running and note-synced behavior where required.
- [ ] Define control-rate/audio-rate usage.

### Modulation matrix

- [ ] Define modulation source registry.
- [ ] Define destination registry.
- [ ] Implement modulation amount.
- [ ] Implement source-to-destination routing.
- [ ] Implement modulation combination rules.
- [ ] Implement clamping/scaling.
- [ ] Implement velocity/key tracking.
- [ ] Implement mod wheel/CC sources.
- [ ] Implement aftertouch where supported.
- [ ] Implement host automation as a compatible control source where appropriate.
- [ ] Implement envelope-following/audio-responsive modulation.

## Exit Criteria

- Envelopes respond predictably across polyphony.
- LFOs remain stable and phase-correct.
- Multiple modulation sources can safely target a parameter.
- Modulation does not corrupt base parameter values.
- Filter behavior is stable and free from zipper artifacts.

---

# Phase 6 — MIDI, Control & Interaction

## Objective

Provide standards-based external control without coupling Apollo to a specific controller.

## Tasks

- [ ] Implement MIDI note processing.
- [ ] Implement MIDI CC processing.
- [ ] Implement pitch bend.
- [ ] Implement sustain.
- [ ] Implement modulation wheel.
- [ ] Implement aftertouch where available.
- [ ] Implement MIDI Learn.
- [ ] Implement mapping persistence.
- [ ] Implement mapping removal/clearing.
- [ ] Implement conflict handling.
- [ ] Implement optional controller profiles.
- [ ] Keep controller profiles separate from core parameter definitions.
- [ ] Ensure MIDI event ordering is correct.
- [ ] Preserve sample-accurate event positions where provided by the host.

## Exit Criteria

- Apollo works correctly with generic MIDI input.
- MIDI mappings survive state save/load where intended.
- No specific controller is required for normal operation.
- MIDI processing remains real-time safe.

---

# Phase 7 — Web UI / UX System

## Objective

Build the complete interactive Apollo interface on top of the established binding architecture.

## Tasks

### Frontend foundation

- [ ] Establish React/TypeScript project structure.
- [ ] Establish frontend build process.
- [ ] Integrate compiled assets with CMake/JUCE.
- [ ] Establish reusable UI component system.
- [ ] Implement parameter-control abstraction.

### Core interface

- [ ] Oscillator panels.
- [ ] Wavetable selectors.
- [ ] Unison controls.
- [ ] Detune controls.
- [ ] Filter controls.
- [ ] Envelope controls.
- [ ] LFO controls.
- [ ] Modulation matrix.
- [ ] FX rack.
- [ ] Master/output controls.
- [ ] Preset browser.
- [ ] MIDI Learn interface.

### Visual system

- [ ] Implement dark UI foundation.
- [ ] Implement deep-purple visual language.
- [ ] Implement green active-state indicators.
- [ ] Implement red warning/error indicators.
- [ ] Define typography.
- [ ] Define spacing.
- [ ] Define control states.
- [ ] Define disabled/error/focus states.
- [ ] Support appropriate display scaling and window sizes.

### Visualization

- [ ] Oscilloscope.
- [ ] Wavetable visualization.
- [ ] Envelope visualization.
- [ ] LFO visualization.
- [ ] Optional spectrum visualization.
- [ ] Voice/activity indicators.
- [ ] Output metering.

### Telemetry

- [ ] Implement lock-free/atomic telemetry snapshots.
- [ ] Rate-limit UI updates.
- [ ] Ensure visualizers never access mutable DSP state directly.
- [ ] Verify UI CPU usage independently of DSP CPU usage.

## Exit Criteria

- UI controls accurately represent native state.
- UI interactions produce correct parameter changes.
- UI reload restores state correctly.
- Visualizers remain responsive without affecting audio processing.
- UI remains usable across supported display configurations.

---

# Phase 8 — Effects Rack

## Objective

Implement Apollo's modular, reorderable effects architecture.

## Tasks

### Distortion / saturation

- [ ] Implement smooth saturation.
- [ ] Implement hard clipping.
- [ ] Implement gain compensation.
- [ ] Implement pre/post filtering.
- [ ] Validate nonlinear behavior with oversampling.

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

- [ ] Implement common effect-module interface.
- [ ] Implement bypass.
- [ ] Implement reordering.
- [ ] Implement state serialization.
- [ ] Implement latency reporting where relevant.
- [ ] Implement tail reporting where relevant.
- [ ] Test arbitrary valid effect orderings.

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

- [ ] Define preset file format.
- [ ] Define preset metadata.
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