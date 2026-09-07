# Apollo Synthesizer — Architecture

> **Developer Clause:** This architecture document is the foundational technical blueprint for Apollo. It is intentionally iterative: implementation details may evolve as DSP, performance, platform, accessibility, and host-integration requirements are validated. Changes must preserve the architectural priorities of audio correctness, real-time safety, portability, maintainability, and host compatibility.

## 1. System Overview & Design Philosophy

Apollo is a hybrid-architecture software synthesizer composed of:

1. A high-performance native C++ audio/DSP backend.
2. A standards-oriented plugin/standalone integration layer.
3. A web-based UI rendered through JUCE's platform-supported WebView facilities.
4. A deterministic state, preset, parameter, and automation system.
5. Platform and host abstraction layers that prevent unnecessary coupling to any particular computer, DAW, controller, driver, or audio device.

The architecture follows these principles:

- **Audio correctness first:** DSP must be mathematically sound, deterministic where practical, and free from avoidable aliasing, denormals, clipping, and timing artifacts.
- **Real-time safety:** The audio callback must never perform blocking operations, uncontrolled allocation, UI work, or other operations with unbounded execution time.
- **Separation of concerns:** DSP, parameter/state management, host integration, UI, persistence, MIDI, and platform services remain independently testable.
- **Portability:** Apollo must not depend on a specific OS, CPU architecture, DAW, audio interface, headphone connector, MIDI controller, or driver.
- **Standards-based integration:** Plugin parameters, state, MIDI, transport, and automation should use the facilities provided by JUCE and the target plugin format rather than host-specific shortcuts.
- **Iterative engineering:** Architecture decisions should be validated with profiling, automated tests, host testing, and listening tests before becoming hard requirements.

### Architectural layers

```text
Host / Standalone Application
            │
            ▼
┌──────────────────────────────┐
│ Plugin / App Integration     │
│ VST3 • Standalone • Future   │
│ CLAP / AU / AAX as supported │
└──────────────┬───────────────┘
               │
               ▼
┌──────────────────────────────┐
│ Parameter + State Layer      │
│ APVTS • Automation • Presets │
└──────────────┬───────────────┘
               │
       ┌───────┴────────┐
       ▼                ▼
┌──────────────┐  ┌──────────────┐
│ Audio / MIDI │  │ UI Bridge    │
│ Control      │  │ WebView      │
└──────┬───────┘  └──────┬───────┘
       │                 │
       ▼                 ▼
┌────────────────────────────────┐
│ Native C++ DSP Engine          │
│ Oscillators • Modulation • FX │
│ Filters • Dynamics • Mixing   │
└────────────────────────────────┘
```

---

## 2. Core Audio Engine — C++ DSP Backend

The DSP backend is the real-time-critical portion of Apollo. It must be isolated from UI rendering, filesystem operations, network activity, and other potentially blocking systems.

### 2.1 Audio processing contract

The processor must:

- Process buffers within the host-provided real-time deadline.
- Avoid dynamic memory allocation inside the audio callback.
- Avoid mutexes, blocking waits, synchronous IPC, and filesystem access on the audio thread.
- Avoid direct UI manipulation from the audio thread.
- Use preallocated buffers and reusable DSP objects.
- Handle variable block sizes supplied by different hosts.
- Handle sample-rate changes and processor reinitialization safely.
- Remain correct when the host bypasses, suspends, resumes, or resets processing.
- Gracefully handle mono, stereo, and other supported channel configurations.
- Avoid assumptions about a particular audio driver or physical output connector.

A practical target is **no avoidable allocations or locks in steady-state audio processing**. Any exception must be explicitly justified, bounded, and tested.

### 2.2 Wavetable synthesis

Apollo uses multi-oscillator wavetable synthesis with:

- Wavetables containing 256 frames per table where the format is appropriate to the selected table design.
- High-quality interpolation for both phase progression and wavetable/frame scanning.
- Continuous phase accumulation with correct wrapping.
- Independent oscillator tuning, detuning, level, pan, and modulation inputs.
- Support for multiple wavetable positions without discontinuities.
- Sample-rate-aware oscillator operation.

Anti-aliasing is a core design requirement. The oscillator implementation should be evaluated using spectral analysis rather than assuming that interpolation alone eliminates aliasing.

Where practical, the engine should support band-limited or otherwise anti-aliased wavetable data so that high-frequency partials are controlled before they fold into the audible spectrum.

### 2.3 Oversampling and nonlinear processing

Nonlinear processors such as distortion and waveshaping can generate harmonics above the host sample rate's Nyquist frequency. Apollo therefore uses oversampling where it materially improves signal quality.

Apollo implements its own oversampling in `Source/DSP/Oversampling/`, rather than using `juce::dsp::Oversampling`, so that it can live in the JUCE-free `apollo_core` alongside the stages that use it and stay under Apollo's strict warning set (ADR-0028). It provides:

- 2× oversampling as a baseline for appropriate nonlinear stages.
- 4× oversampling where testing demonstrates a meaningful quality benefit.
- Stage-specific oversampling where processing cost can be reduced without compromising audible quality.

Oversampling factors are implementation decisions, not immutable requirements. They must be validated using CPU profiling and spectral/listening tests.

The architecture must avoid unnecessarily oversampling linear processing that does not benefit from it.

### 2.4 Saturation and waveshaping

The drive system may provide multiple transfer characteristics, including:

- Smooth saturation based on `tanh(x)`.
- Hard clipping for aggressive digital coloration.
- Additional custom waveshapers as the design evolves.

Waveshapers must define predictable behavior for:

- Input/output range.
- Gain compensation.
- DC offset behavior where relevant.
- NaN/Inf protection.
- Oversampling requirements.
- Parameter smoothing.

### 2.5 Tone shaping

Filtering may be used before and after nonlinear stages:

- Pre-drive HPF to control unnecessary sub-bass energy.
- Post-drive LPF to control excessive high-frequency content.
- Additional filters where required by the synthesis and FX architecture.

Filters should use numerically stable implementations suitable for real-time processing. IIR implementations must be validated for stability across supported sample rates and parameter ranges.

### 2.6 Dynamic processing

Apollo may use envelope followers for signal-responsive modulation.

The implementation should distinguish between:

- Peak/envelope detection for fast transient response.
- RMS-style measurement for perceived energy.
- Attack/release smoothing.
- Audio-rate versus control-rate modulation requirements.

The architecture must not assume that one RMS window is universally appropriate. Window length and smoothing behavior should be parameterized or selected according to the modulation use case.

---

## 3. Synthesis Architecture

The synthesis engine is organized as modular DSP components rather than one monolithic processor.

A typical signal flow is:

```text
MIDI / Host Events
       │
       ▼
Voice Allocation
       │
       ▼
┌─────────────────────────────┐
│ Voice                       │
│                             │
│ Oscillator A ─┐             │
│ Oscillator B ─┼─► Mixer ─►  │
│ Sub Oscillator┘             │
│ Noise Generator             │
│                             │
│ Amp / Filter Envelopes      │
│ LFO / Modulation Sources    │
└──────────────┬──────────────┘
               ▼
        Voice / Global FX
               │
               ▼
            Output
```

### 3.1 Voice management

Voice allocation must support:

- Polyphonic operation.
- Configurable maximum voice count.
- Voice stealing according to a deterministic policy.
- Note-on/note-off handling.
- Sustain and relevant MIDI controller behavior.
- Pitch-bend handling.
- Per-voice envelopes and modulation state.
- Correct reset behavior when voices are reused.

Voice objects should be allocated or reserved during initialization rather than created dynamically from inside the audio callback.

### 3.2 Modulation architecture

The modulation system should provide explicit modulation sources and destinations.

Potential sources include:

- DAHDSR envelopes.
- LFOs.
- Velocity.
- Note/key tracking.
- Aftertouch where supported.
- Mod wheel and MIDI CC.
- Pitch.
- Audio/envelope-following sources.
- Host automation.

Modulation routing should use normalized internal representations where possible, with destination-specific scaling and curve handling.

The modulation engine must define how modulation is combined, clamped, smoothed, and prioritized when multiple sources affect the same destination.

### 3.3 Filters

The filter subsystem should support the required filter types through reusable DSP components.

Each filter must define:

- Cutoff range.
- Resonance range.
- Drive behavior if applicable.
- Modulation behavior.
- Sample-rate handling.
- Stability constraints.

Filter parameter changes should be smoothed sufficiently to avoid zipper noise without introducing unacceptable modulation latency.

### 3.4 Mixing and gain staging

Every stage should have an explicit gain model.

The engine must:

- Avoid accidental clipping between modules.
- Prevent runaway resonance or feedback where applicable.
- Provide predictable output normalization.
- Preserve stereo information where intended.
- Use soft limiting or other protection only where musically appropriate rather than masking bad gain staging.

---

## 4. Effects Processing

Apollo's FX system should be modular and reorderable.

Potential modules include:

- Distortion / saturation.
- Delay.
- Reverb.
- Gate.
- Compressor.
- EQ.
- Additional effects added through the same module interface.

### FX module contract

Each effect should expose a common processing contract for:

- Preparation with sample rate and maximum block size.
- Audio processing.
- Parameter updates.
- State reset.
- Bypass.
- Latency reporting where relevant.
- Optional tail reporting where relevant.

The FX rack must support deterministic ordering and safe parameter changes.

Effects that contain feedback paths, such as delay/reverb networks, must be carefully tested for stability, denormal behavior, and runaway gain.

---

## 5. Web UI Bridge — JUCE 8 WebView

Apollo uses a modern web-based frontend rather than relying primarily on native C++ component painting.

### 5.1 UI technology

The frontend may use:

- HTML.
- CSS.
- JavaScript/TypeScript.
- React.
- A bundled frontend build integrated into the JUCE application.

The WebView implementation must use the platform-supported backend provided by the selected JUCE version. Apollo must not hard-code an OS-specific WebView technology into the core architecture.

### 5.2 Thread separation

The UI runs independently from the real-time audio callback.

Rules:

- UI rendering must never block the audio thread.
- The audio thread must never wait for UI messages.
- UI-to-audio communication should use APVTS/parameter APIs or lock-free/asynchronous mechanisms as appropriate.
- Audio-to-UI telemetry should be rate-limited and decoupled from the audio callback.
- High-frequency visualizers such as oscilloscopes must consume snapshots rather than access mutable DSP state directly.

### 5.3 UI visualization

The UI may provide:

- Oscilloscope.
- Spectrum or spectral visualization where practical.
- Wavetable/frame visualization.
- Envelope/LFO visualization.
- Parameter value displays.
- Voice/activity indicators.
- Preset browser.
- Modulation visualization.

Visualizers should degrade gracefully when CPU resources are constrained.

### 5.4 Visual design

The default design system uses:

- Dark background.
- Deep-purple primary visual language.
- Green active-state indicators.
- Red warning/error indicators.

These are design defaults rather than platform dependencies. The UI should remain usable across supported display sizes, scaling factors, input methods, and operating systems.

---

## 6. State Management, Presets & Host Automation

Apollo uses `juce::AudioProcessorValueTreeState` (APVTS) as the central parameter/state interface.

### 6.1 Parameter architecture

All automatable synthesis and FX parameters should have:

- Stable IDs.
- Human-readable names.
- Normalized ranges.
- Appropriate units.
- Default values.
- Automation metadata where supported.
- Clearly defined smoothing behavior.

Parameter IDs must remain stable once released unless a deliberate state-migration strategy exists.

### 6.2 Host automation

Apollo must use standards-based plugin parameter mechanisms so compatible hosts can:

- Automate parameters.
- Record parameter changes where supported.
- Restore parameter values.
- Save and recall plugin state.

No core parameter architecture should depend on a single DAW.

### 6.3 Serialization

State must include all information necessary to reconstruct the sound.

This may include:

- APVTS parameters.
- Oscillator/wavetable selections.
- Modulation routing.
- FX order and settings.
- Preset metadata where appropriate.
- Version information for migration.

State loading should validate input and handle older versions safely.

### 6.4 State migration

When the parameter schema changes, Apollo should use explicit versioning and migration logic rather than silently changing the meaning of existing IDs or serialized values.

---

## 7. MIDI & External Control

Apollo must support generic MIDI rather than being architecturally coupled to one controller.

### 7.1 MIDI input

Supported MIDI behavior may include:

- Note on/off.
- Velocity.
- Pitch bend.
- Sustain.
- Mod wheel.
- MIDI CC.
- Aftertouch where available.
- Program/preset selection where supported.

The implementation must correctly handle MIDI event ordering and sample-accurate event positions where provided by the host.

### 7.2 MIDI Learn

MIDI Learn should map controller messages to parameters dynamically.

Requirements:

- User-selectable learn mode.
- Persistent mappings.
- Clear/remove mapping.
- Conflict handling.
- Optional controller profiles.
- No hard-coded dependency on a particular physical keyboard.

Controller-specific factory mappings may be provided as optional profiles, but they must not be required for normal operation.

### 7.3 Standalone audio I/O

The standalone application should use JUCE's audio-device abstractions, including `juce::AudioDeviceManager` where appropriate.

The architecture must remain independent of:

- A particular ASIO driver.
- A particular vendor's audio interface.
- ASIO4ALL.
- A particular headphone or cable connection.
- A specific speaker/headphone model.

Device selection, sample rate, buffer size, input/output channels, and latency should be configurable through the normal platform/device APIs.

---

## 8. Plugin & Host Integration

VST3 is the primary plugin target for Apollo, with Standalone as a first-class development and testing target.

The architecture should leave room for additional formats where licensing, JUCE support, and project requirements permit them.

### Host compatibility principles

Apollo must:

- Follow the plugin format's lifecycle expectations.
- Correctly handle prepare/start/stop/reset behavior.
- Support variable host block sizes.
- Handle sample-rate changes.
- Respect bypass and transport state where applicable.
- Correctly report latency where effects introduce it.
- Preserve parameter and state compatibility.
- Avoid host-specific behavior unless isolated behind a compatibility layer.

Testing should cover a representative range of compatible hosts rather than treating one DAW as the canonical environment.

---

## 9. Platform & Portability Architecture

Apollo targets supported desktop platforms through JUCE and CMake.

The architecture should accommodate:

- Windows.
- macOS.
- Linux where supported by the chosen WebView/plugin configuration.

CPU architecture should be treated similarly:

- x86-64.
- ARM64 where the toolchain, JUCE version, dependencies, and plugin ecosystem support it.

Platform-specific code must be isolated behind small abstraction boundaries.

Avoid:

- Hard-coded filesystem paths.
- Hard-coded device names.
- Hard-coded OS assumptions.
- Compiler-specific behavior without a compatibility reason.
- Host-specific shortcuts in shared DSP code.

---

## 10. Build System & Dependency Architecture

CMake is the authoritative build system.

### Requirements

- No Projucer dependency.
- JUCE must be integrated reproducibly.
- Web frontend assets must be built and packaged deterministically.
- Plugin and standalone targets should share the same core engine where possible.
- Debug, release, and profiling configurations should be supported.
- Compiler warnings should be enabled at an appropriate project level.
- Third-party dependencies should be minimized and version-pinned where practical.

Recommended structure:

```text
Apollo/
├── CMakeLists.txt
├── CMake/
├── Source/
│   ├── Audio/
│   ├── DSP/
│   ├── Engine/
│   ├── Modulation/
│   ├── Effects/
│   ├── Parameters/
│   ├── Presets/
│   ├── MIDI/
│   ├── Plugin/
│   ├── Standalone/
│   ├── UI/
│   └── Platform/
├── Tests/
├── Web/
│   ├── src/
│   ├── public/
│   └── package.json
├── Resources/
├── Presets/
└── Docs/
```

---

## 11. Resource & Preset Management

Resource loading must be separated from real-time processing.

The engine should preload or otherwise prepare:

- Wavetables.
- Presets.
- UI assets.
- Impulse responses or other large DSP resources where applicable.

Large resources must not be synchronously loaded from disk during audio processing.

Preset operations should be performed asynchronously or outside the real-time callback, with validated handoff of prepared data to the DSP engine.

---

## 12. Concurrency & Communication Model

Apollo should explicitly distinguish between:

- **Audio thread:** deterministic, real-time-critical DSP.
- **Message/UI thread:** interface interaction and non-real-time control.
- **Worker/background threads:** resource loading, preset scanning, expensive preparation, analysis, or other non-real-time tasks.

Communication should prefer:

- Atomic values for simple scalar state.
- Lock-free structures for high-frequency event/telemetry exchange where justified.
- Immutable snapshots for UI visualization.
- Asynchronous message passing for non-real-time operations.

Avoid sharing mutable complex objects across threads without a clearly defined ownership protocol.

---

## 13. Error Handling & Fault Containment

Errors must fail safely.

The system should:

- Validate external state before applying it.
- Reject malformed preset/state data.
- Protect against invalid numerical values.
- Avoid throwing exceptions through real-time audio paths.
- Report non-fatal errors asynchronously.
- Keep UI failures from crashing DSP.
- Keep optional resource failures from making the entire plugin unusable where practical.

Any recovery behavior must preserve audio-thread safety.

---

## 14. Performance Engineering

Performance is a first-class architectural concern.

Measure:

- CPU usage per voice.
- CPU usage at different polyphony levels.
- Oversampling cost.
- FX rack cost.
- UI rendering cost.
- Memory usage.
- Audio callback execution time.
- Worst-case rather than only average processing time.

Optimization should be evidence-driven.

Do not sacrifice numerical correctness or maintainability for micro-optimizations without profiling evidence.

---

## 15. Testing & Validation

Apollo should use multiple layers of validation.

### Unit tests

Cover:

- Wavetable interpolation.
- Phase handling.
- Oscillator frequency accuracy.
- Filter stability.
- Envelope behavior.
- LFO behavior.
- Modulation routing.
- MIDI mapping.
- Parameter conversion.
- State serialization/migration.
- FX module behavior.

### DSP validation

Use automated analysis where possible:

- Frequency-response tests.
- Aliasing measurements.
- THD/THD+N where relevant.
- Noise-floor checks.
- Impulse/step-response checks.
- Regression renders.
- Numerical stability tests.

### Integration testing

Test:

- Plugin initialization.
- State recall.
- Automation.
- MIDI.
- Variable block sizes.
- Sample-rate changes.
- Bypass/reset.
- Preset loading.
- Multiple supported hosts.

### Cross-platform validation

Build and test on each supported platform/toolchain configuration before declaring a release candidate.

---

## 16. Security & Robustness

The WebView and resource systems must treat external content and data as untrusted.

Requirements include:

- No unnecessary network access.
- Sanitized/validated state data.
- Safe resource paths.
- Controlled communication between WebView and native code.
- No arbitrary native command execution from UI messages.
- Strict validation of parameter IDs and values.
- Defensive handling of malformed MIDI/state input.

The UI bridge should expose the smallest native API necessary for the frontend.

---

## 17. Development Workflow & Architectural Change Control

This architecture is a living document.

Before introducing a major architectural change, document:

1. The problem being solved.
2. The current limitation.
3. The proposed change.
4. Expected CPU/memory/latency impact.
5. Compatibility implications.
6. Testing required.
7. Migration or rollback strategy.

Implementation should favor small, testable changes over broad rewrites.

When a requirement conflicts with another requirement, use this priority order:

1. **P0 — Safety & correctness**
2. **P1 — Audio quality**
3. **P2 — Real-time performance**
4. **P3 — Platform/host compatibility**
5. **P4 — UX**
6. **P5 — Convenience**

---

## 18. Architectural Non-Goals

Apollo should not become dependent on:

- One specific DAW.
- One operating system.
- One CPU vendor or architecture.
- One MIDI keyboard/controller.
- One audio interface.
- One driver.
- One headphone/speaker model.
- A physical connector such as 3.5mm auxiliary.
- Host-specific automation hacks.
- A UI implementation that requires the DSP engine to know about rendering details.

---

## 19. Definition of Done — Architecture

An architectural feature is considered complete only when:

- It has a clear ownership boundary.
- It does not introduce avoidable real-time violations.
- It is testable independently where practical.
- It behaves correctly across supported sample rates and block sizes.
- It does not introduce unnecessary host/platform coupling.
- State and automation behavior are defined.
- Failure behavior is understood.
- Performance impact has been measured when material.
- Documentation reflects the implemented design.

> **Final Engineering Directive:** Build Apollo as a portable, host-agnostic, real-time-safe software instrument. Hardware, operating-system, audio-driver, DAW, and controller specifics may be used as optional test configurations, but they must never become hidden architectural dependencies.