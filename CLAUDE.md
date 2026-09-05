# CLAUDE.md — Apollo System Context & Agent Directives

> **Status:** Living project instructions  
> **Project:** Apollo  
> **Primary objective:** Build a production-grade, cross-platform, hardware-agnostic wavetable synthesizer using C++/JUCE with a modern WebView frontend.

---

## 1. Project Overview

### 1.1 Project Identity

- **Project Name:** Apollo
- **Description:** A high-performance, multi-oscillator wavetable synthesizer capable of advanced sound design, modulation, filtering, and multi-FX processing.
- **Primary Plugin Target:** VST3
- **Standalone Target:** Yes
- **Future Plugin Targets:** Architecturally possible additions include CLAP, AU, AAX, and other formats where technically and legally appropriate.
- **Framework:** JUCE 8
- **Frontend:** HTML/CSS/JavaScript/React through JUCE's WebView integration.
- **Build System:** CMake
- **Primary Language:** Modern C++
- **DSP Architecture:** Real-time safe, modular, testable, host-independent.

### 1.2 Product Philosophy

Apollo must be developed as a professional audio application rather than a hardware- or DAW-specific prototype.

The implementation should follow this principle:

> **The software adapts to the user's environment; the user's environment should not have to adapt to Apollo.**

Specific DAWs, MIDI controllers, operating systems, audio interfaces, and computers may be used for development and testing, but none should be a hard dependency of the product architecture.

---

# 2. Developer & Engineering Context

## 2.1 Development Background

The project is being developed with a strong emphasis on practical C++ DSP engineering, buffer management, modular audio processing, and real-time thread safety.

Previous DSP work may be used as architectural inspiration where appropriate, particularly established approaches to:

- Distortion.
- Delay.
- Reverb.
- Noise gating.
- Equalization.
- Audio buffer processing.
- Effect routing.

Previous projects should be treated as references and sources of reusable engineering patterns, not as constraints that prevent Apollo from adopting a better architecture.

## 2.2 Engineering Standard

The agent must prioritize:

1. Correctness.
2. Real-time safety.
3. Audio quality.
4. Maintainability.
5. Testability.
6. Performance.
7. Extensibility.
8. User experience.
9. Development speed.

Rapid prototyping is acceptable during exploration, but unstable prototype code must not be treated as production code.

## 2.3 Code Quality

Production C++ should:

- Use RAII.
- Prefer clear ownership semantics.
- Avoid unnecessary raw owning pointers.
- Avoid undefined behavior.
- Validate external/user-provided data.
- Minimize unnecessary copying.
- Avoid allocations in real-time code.
- Document non-obvious DSP algorithms.
- Keep DSP components independently testable.
- Use meaningful names.
- Keep interfaces small and explicit.
- Prefer deterministic behavior where appropriate.

---

# 3. Universal Platform & Hardware Requirements

Apollo must be hardware-agnostic and platform-independent wherever the underlying JUCE/platform APIs permit.

## 3.1 Supported Operating Systems

The architecture should target:

- Windows
- macOS
- Linux

Platform-specific implementation details must be isolated behind appropriate abstractions.

The DSP engine must not contain operating-system-specific assumptions.

## 3.2 CPU Architectures

Where supported by the selected build toolchain and dependencies, Apollo should support:

- x86-64
- ARM64

Architecture-specific optimizations may be added, but a portable implementation must remain available.

## 3.3 Development Environment

A developer may use any suitable development machine.

A particular laptop, manufacturer, CPU, or Windows installation may be used as the primary development environment, but:

> **Development hardware is not a product requirement.**

Do not introduce Lenovo-, Windows-only-, or machine-specific assumptions into core application code.

---

# 4. Audio Hardware & I/O

Apollo must work with supported audio devices exposed by the operating system/JUCE audio backends.

The standalone application should allow configuration of:

- Audio input device.
- Audio output device.
- Input channels.
- Output channels.
- Sample rate.
- Buffer size.
- Input monitoring.
- Output routing where supported.

The software must not assume:

- A specific audio interface.
- A specific headphone amplifier.
- A 3.5 mm auxiliary output.
- USB audio.
- Bluetooth audio.
- A particular driver vendor.

## 4.1 Latency

Apollo should add as little processing latency as technically possible.

The core synthesizer should target effectively zero algorithmic latency where feasible.

However, the agent must **not** claim universal end-to-end zero latency because actual latency depends on:

- Audio hardware.
- Driver/backend.
- Operating system.
- Host.
- Buffer size.
- Sample rate.
- Device-specific buffering.

Avoid adding unnecessary lookahead or buffering.

---

# 5. Host & Plugin Compatibility

## 5.1 Plugin Architecture

VST3 is the primary plugin target.

The DSP engine must remain independent of the plugin wrapper.

Conceptually:

```text
                 ┌─────────────────┐
                 │   DSP Engine    │
                 └────────┬────────┘
                          │
          ┌───────────────┼────────────────┐
          │               │                │
     VST3 Wrapper     Standalone       Future Formats
          │               │                │
          ▼               ▼                ▼
        DAW          Audio Device      Host Format
```

This separation should allow additional plugin formats to be introduced without rewriting the core DSP.

## 5.2 DAW Compatibility

Apollo must be designed against the relevant plugin standards rather than around one DAW.

Testing should include multiple compatible hosts where available, such as:

- FL Studio
- Ableton Live
- Cubase
- Studio One
- Reaper
- Bitwig
- Logic Pro where the appropriate plugin format/build is supported
- Other standards-compliant hosts

Do not add FL Studio-specific behavior to the DSP or parameter architecture merely because FL Studio is used during development.

## 5.3 Host State & Automation

All automatable synthesis parameters must have stable parameter IDs and appropriate host-facing metadata.

Use:

```cpp
juce::AudioProcessorValueTreeState
```

or an equivalent robust parameter/state system.

The state system must support:

- Parameter automation.
- Project save/restore.
- Preset save/restore.
- Stable parameter identifiers.
- Version migration.
- Default values.
- Safe parameter ranges.
- Smooth parameter handling where required.

Parameters should include, where applicable:

- Oscillator pitch.
- Fine tuning.
- Level.
- Pan.
- Wavetable position.
- Warp amount.
- Unison.
- Detune.
- Filter cutoff.
- Filter resonance.
- Envelope parameters.
- LFO parameters.
- Modulation depth.
- FX parameters.
- Macro controls.

The architecture must not depend on a specific DAW to serialize these parameters correctly.

---

# 6. JUCE & WebView Architecture

## 6.1 JUCE

JUCE 8 is the primary application framework.

Use JUCE for:

- Audio processing.
- Plugin wrappers.
- Standalone application infrastructure.
- MIDI.
- Parameter/state infrastructure.
- Cross-platform abstractions.
- WebView integration.
- Audio-device management.
- General application infrastructure.

## 6.2 WebView

The UI should use JUCE's WebView facilities.

The frontend should use:

- HTML
- CSS
- JavaScript
- React

The implementation must **not hard-code one operating system's WebView backend** into the product architecture.

For example, do not assume that the application always uses Microsoft WebView2.

Instead:

> Use the appropriate JUCE-supported WebView implementation for the target platform.

Platform-specific WebView dependencies should be isolated inside the UI/platform layer.

## 6.3 Frontend/Backend Separation

The C++ backend is responsible for:

- DSP.
- Audio processing.
- Parameter/state management.
- MIDI processing.
- Resource management.
- Host integration.
- Audio-device integration.

The WebView frontend is responsible for:

- UI rendering.
- User interaction.
- Visual feedback.
- Parameter editing.
- Modulation editing.
- Preset browsing.
- Visualization.

The frontend must not directly manipulate unsafe DSP structures.

---

# 7. Real-Time Audio Thread — Golden Rule

> **The audio processing thread must remain deterministic, real-time safe, and free from blocking operations.**

## 7.1 Forbidden Operations on the Audio Thread

Do not perform:

- UI updates.
- JavaScript execution.
- WebView calls.
- File I/O.
- Network I/O.
- Memory allocation.
- Memory deallocation where avoidable.
- Blocking mutex operations.
- Waiting on condition variables.
- Expensive logging.
- Unbounded searches.
- Resource loading.
- Preset parsing.
- Dynamic container growth.

## 7.2 Permitted Operations

The audio thread may perform:

- DSP calculations.
- Preallocated buffer operations.
- Parameter reads.
- Atomic reads.
- Lock-free queue operations.
- Voice management.
- Preallocated state transitions.
- Real-time-safe metering.

## 7.3 Resource Preparation

Resources should be loaded/prepared outside the audio thread.

Examples:

- Wavetables.
- Noise samples.
- Presets.
- Impulse responses.
- Large lookup tables.
- UI assets.

Once prepared, the audio thread should receive safe references or preallocated data structures.

---

# 8. Threading Model

A recommended architecture is:

```text
┌───────────────────────────────┐
│          UI Thread            │
│ React / WebView / Interaction │
└───────────────┬───────────────┘
                │
                │ Async / Lock-Free Bridge
                ▼
┌───────────────────────────────┐
│      Application State        │
│ Parameter / Resource Manager  │
└───────────────┬───────────────┘
                │
                │ RT-safe state access
                ▼
┌───────────────────────────────┐
│        Audio Thread            │
│ Voice Engine / DSP / FX        │
└───────────────────────────────┘
```

Visualization data should flow in the opposite direction through a safe metering/visualization buffer:

```text
Audio Thread
     │
     ▼
Preallocated Meter/Scope Buffer
     │
     ▼
UI Thread
     │
     ▼
WebView Visualizer
```

The UI must never block waiting for the audio thread.

---

# 9. Memory Management

## 9.1 General Rules

Use:

- RAII.
- `std::unique_ptr` for exclusive ownership.
- `std::shared_ptr` only when shared ownership is genuinely required.
- References or non-owning pointers for borrowed objects.
- Preallocated buffers for real-time processing.

Avoid:

- Owning raw pointers.
- Manual lifetime management without a clear reason.
- Unnecessary heap allocation.
- Per-sample allocations.
- Per-block allocations.

## 9.2 Audio Buffers

Audio buffers should be allocated during initialization or preparation.

Use:

```cpp
prepareToPlay(...)
```

or equivalent initialization stages for allocation and DSP preparation.

Do not resize audio buffers from `processBlock()`.

## 9.3 Wavetables

Wavetables should be:

- Loaded asynchronously.
- Validated before use.
- Cached where appropriate.
- Shared when identical resources are used by multiple voices.
- Available to the audio thread without blocking.

---

# 10. DSP Architecture

DSP modules should be independent and composable.

Recommended conceptual structure:

```text
DSP/
├── Oscillators/
├── Filters/
├── Envelopes/
├── LFO/
├── Modulation/
├── Unison/
├── Distortion/
├── Delay/
├── Reverb/
├── Dynamics/
├── EQ/
├── Oversampling/
└── Utilities/
```

Each DSP module should ideally:

- Have a clear input/output contract.
- Be independently testable.
- Avoid UI dependencies.
- Avoid host dependencies.
- Avoid platform dependencies.
- Support sample-rate initialization.
- Handle parameter edge cases safely.

---

# 11. Oscillator Requirements

Apollo's oscillator engine should support:

- Two primary wavetable oscillators.
- Sub oscillator.
- Noise generator.
- Up to 16 unison voices per primary oscillator.
- Wavetable scanning.
- Warp processing.
- Pitch modulation.
- Stereo spread.
- Detuning.

The oscillator engine should be optimized for polyphonic operation.

---

# 12. Wavetable Engine

## 12.1 Wavetable Data

The target/reference format is:

- Up to 256 frames.
- High-resolution samples per frame.
- Interpolation between waveform samples.
- Interpolation between adjacent frames.

The implementation may support additional table formats if useful.

## 12.2 Anti-Aliasing

The implementation must actively address aliasing.

Possible approaches include:

- Band-limited wavetable mipmaps.
- Multi-resolution tables.
- Oversampling.
- High-quality interpolation.
- Specialized anti-aliasing techniques.

Choose the technique based on measured quality and CPU cost rather than blindly applying one algorithm.

---

# 13. Filters

Apollo should provide two state-variable filters.

Supported routing:

- Series.
- Parallel.
- Split/custom routing where practical.

Initial filter types:

- Low-pass.
- High-pass.
- Band-pass.
- Comb.
- Formant.

Filters must support modulation.

---

# 14. Modulation System

Apollo must provide a flexible modulation architecture.

## 14.1 Envelopes

Provide four DAHDSR envelopes:

- Delay.
- Attack.
- Hold.
- Decay.
- Sustain.
- Release.

Support:

- Adjustable curves.
- Linear behavior.
- Exponential behavior.
- Visual editing.
- Retriggering.
- MIDI-note synchronization where appropriate.

## 14.2 LFOs

Provide four LFOs.

Supported shapes should include:

- Sine.
- Triangle.
- Saw.
- Reverse saw.
- Square.
- Sample & Hold.
- Step.
- Custom drawable curves.

LFOs should support:

- Free-running mode.
- Retrigger mode.
- Tempo synchronization.
- Phase.
- Fade-in.
- Smoothing.
- Polarity.

---

# 15. Modulation Matrix

The modulation matrix must use a generic routing model:

```text
Source → Destination → Depth
```

Sources may include:

- ENV 1–4.
- LFO 1–4.
- Velocity.
- Note.
- Mod Wheel.
- Pitch Bend.
- Aftertouch.
- MPE dimensions.
- Macros.
- Random sources.
- Key tracking.

Destinations may include:

- Oscillator pitch.
- Wavetable position.
- Warp amount.
- Filter cutoff.
- Filter resonance.
- Level.
- Pan.
- FX parameters.
- Macro destinations.

Depth must support bipolar values.

The system should permit modulation of modulation depth where technically practical.

---

# 16. MIDI Architecture

Apollo must support standard MIDI devices without requiring a particular controller.

## 16.1 Required MIDI Input

Support:

- Note On.
- Note Off.
- Velocity.
- Pitch Bend.
- Mod Wheel.
- Sustain.
- MIDI CC.

Where supported:

- Channel aftertouch.
- Polyphonic aftertouch.
- MPE.

## 16.2 MIDI Learn

Any suitable parameter should be MIDI-mappable.

Workflow:

```text
Enter MIDI Learn
       ↓
Move Hardware Control
       ↓
Detect MIDI Message
       ↓
Assign Parameter
       ↓
Save Mapping
```

Mappings must be editable and removable.

## 16.3 Controller Profiles

Optional controller profiles may be supplied.

A profile can define:

- Macro mappings.
- Common parameter mappings.
- Pad mappings.
- Knob mappings.
- Transport mappings.
- Performance mappings.

Profiles must never be mandatory.

Apollo must remain fully usable with:

- Mouse.
- Keyboard.
- Touch input where supported.
- Generic MIDI controllers.
- Different controller layouts.

Do not hard-code assumptions about the number, location, or names of physical controls.

---

# 17. Effects Architecture

Apollo should provide a modular, reorderable FX rack.

Initial effects:

- Distortion.
- Delay.
- Reverb.
- Noise Gate.
- Compressor.
- Parametric EQ.

Effects should have a common processing interface where practical.

Example conceptual interface:

```cpp
class AudioEffect
{
public:
    virtual ~AudioEffect() = default;

    virtual void prepare(const juce::dsp::ProcessSpec& spec) = 0;
    virtual void reset() = 0;
    virtual void process(juce::AudioBuffer<float>& buffer) = 0;
};
```

The exact interface may evolve as implementation requirements become clearer.

---

# 18. Distortion & Nonlinear Processing

Distortion should support multiple modes.

Initial modes:

- Hard clipping.
- Soft clipping.
- Diode-style emulation.

A simple soft-clipping model may use:

\[
y = \tanh(gx)
\]

where:

- \(x\) is the input.
- \(g\) is the drive factor.
- \(y\) is the output.

Nonlinear processing should be appropriately oversampled.

---

# 19. Delay

The delay engine should support:

- Stereo operation.
- Ping-pong.
- Feedback.
- Mix.
- Millisecond timing.
- Host-tempo synchronization.
- Filtering.

Do not assume a particular DAW transport implementation.

If host tempo is unavailable, fall back gracefully to free-running time.

---

# 20. Reverb

Initial reverb modes:

- Room.
- Hall.

Parameters:

- Size.
- Decay.
- Damping.
- Pre-delay.
- Width.
- Mix.

The implementation should minimize latency and CPU consumption while maintaining acceptable quality.

---

# 21. Dynamics

## 21.1 Noise Gate

Parameters:

- Threshold.
- Attack.
- Hold.
- Release.
- Range.

## 21.2 Compressor

Initial compressor architecture:

- RMS-based detection.

Parameters:

- Threshold.
- Ratio.
- Attack.
- Release.
- Makeup gain.
- Mix.

Future support may include:

- Peak detection.
- Sidechain input.
- External sidechain routing.

---

# 22. Equalizer

Provide a parametric 4-band EQ.

Each band should support:

- Frequency.
- Gain.
- Q.
- Filter type.
- Enable/bypass.

Potential types:

- Bell.
- Low shelf.
- High shelf.
- Low-pass.
- High-pass.

---

# 23. Oversampling

Oversampling must be applied where it provides meaningful anti-aliasing benefits.

Minimum target:

- 2×.

Preferred quality mode:

- 4×.

Potential future modes:

- 8×.
- User-configurable quality settings.

Oversampling should be especially considered for:

- Distortion.
- Waveshaping.
- Nonlinear filters.
- Aggressive oscillator warp operations.

Do not oversample every processing stage without evidence that it improves quality.

Measure:

- CPU usage.
- Aliasing.
- Latency.
- Audio quality.

---

# 24. UI Design System

## 24.1 General Style

Apollo should use a dark-mode interface intended for long sound-design sessions.

Primary visual direction:

- Dark base.
- Deep purple accent system.
- Bright green active/modulation indicators.
- Red alerts and overload indicators.

## 24.2 Color Semantics

### Green

Use for:

- Active modulation.
- Enabled parameters.
- Active LFOs.
- Positive/normal activity states.

### Red

Use for:

- Clipping.
- Errors.
- Overload states.
- Aggressive distortion warnings.
- Critical status conditions.

Do not rely on color alone to communicate important information.

Use:

- Labels.
- Icons.
- Numeric values.
- Shapes.
- Tooltips.
- Animation/state changes.

---

# 25. UI Customization Process

The UI should be developed iteratively.

When a visual specification is genuinely ambiguous, ask for clarification rather than inventing a highly specific visual design.

However, do not ask the developer to specify every trivial implementation detail.

The agent may make reasonable design decisions when:

- The requirement is unambiguous.
- The choice does not materially affect the product direction.
- The implementation can remain easily changeable.

Ask for clarification when a decision would materially affect:

- UX architecture.
- Brand identity.
- Core navigation.
- Information hierarchy.
- Major interaction behavior.

---

# 26. Visualizers

Apollo should provide real-time visual feedback.

## 26.1 Oscilloscope

The oscilloscope should:

- Display output waveform data.
- Update smoothly.
- Run independently from audio rendering.
- Use a safe visualization buffer.
- Avoid audio-thread blocking.

## 26.2 Wavetable Visualization

Display:

- Current waveform.
- Wavetable position.
- Scanning behavior.
- Warp behavior where useful.

## 26.3 Modulation Visualization

Show:

- Active modulation.
- LFO motion.
- Envelope progression.
- Parameter modulation depth.

Visualization calculations must not block the audio thread.

---

# 27. UI ↔ C++ Communication

The UI bridge must use a clearly defined API.

Possible operations:

```text
UI → C++
- Set parameter
- Request preset
- Trigger MIDI Learn
- Load resource
- Change modulation routing

C++ → UI
- Parameter state
- Meter values
- Scope data
- LFO state
- Envelope state
- Resource status
- Error/status information
```

The bridge must validate messages and avoid exposing arbitrary unsafe C++ operations to the WebView.

High-frequency data should be rate-limited or buffered appropriately.

---

# 28. State Management

All important synthesizer state must have a clear owner.

Use APVTS or an equivalent state system for automatable parameters.

Non-parameter state may include:

- Modulation routing topology.
- Preset metadata.
- Resource references.
- UI-only state.

Separate:

1. **Audio/host state**
2. **Preset state**
3. **UI state**
4. **Resource state**

Do not store transient UI rendering state in the audio engine unless required.

---

# 29. Presets

Presets should include:

- Oscillator configuration.
- Wavetables.
- Sub oscillator.
- Noise.
- Filters.
- Envelopes.
- LFOs.
- Modulation matrix.
- FX chain.
- Macros.

Preset data must be versioned.

Example:

```json
{
    "formatVersion": 1,
    "product": "Apollo",
    "parameters": {},
    "modulation": {},
    "effects": []
}
```

The exact format may change during development.

Backward compatibility and migration should be considered before changing serialized structures.

---

# 30. Resource Management

Resources may include:

- Wavetables.
- Noise samples.
- Presets.
- UI assets.
- Lookup tables.

Resource loading must occur outside the real-time audio thread.

The application should gracefully handle:

- Missing files.
- Corrupted files.
- Unsupported formats.
- Permission errors.
- Moved user libraries.

A missing resource must not cause an application crash.

---

# 31. CMake & Build Management

CMake is the authoritative build system.

Every new:

- C++ source file.
- Header.
- JUCE dependency/module.
- Web asset.
- Generated asset where appropriate.
- Test target.

must be correctly integrated into the build.

Do not use Projucer as the project's authoritative build configuration.

## 31.1 Build Configurations

Maintain at minimum:

- Debug.
- Release.

Where practical, support:

- RelWithDebInfo.
- Sanitizer builds.
- Unit-test builds.

## 31.2 Build Portability

Do not introduce:

- Hard-coded developer paths.
- Machine-specific include paths.
- Absolute asset paths.
- User-specific environment assumptions.

Use CMake targets and relative/project-managed resource paths.

---

# 32. Dependency Management

Third-party dependencies should be:

- Explicitly declared.
- Versioned or pinned where appropriate.
- Reproducible.
- Audited for licensing.
- Avoided when JUCE/standard-library functionality is sufficient.

Do not add a dependency solely for convenience if it creates unnecessary deployment complexity.

---

# 33. Error Handling

Errors must fail gracefully.

Examples:

### Missing Wavetable

- Display a user-facing status.
- Use a safe fallback.
- Keep the synth operational.

### Invalid Preset

- Reject safely.
- Preserve current state if possible.
- Display a meaningful error.

### MIDI Disconnect

- Continue running.
- Do not crash.
- Reconnect when available if supported.

### Audio Device Failure

- Display a clear error.
- Allow the user to select another device/configuration.

### WebView Failure

- Prevent the UI failure from corrupting DSP state.
- Report the issue safely.

---

# 34. Testing Requirements

Testing is mandatory for production DSP.

## 34.1 Unit Tests

Test:

- Oscillator generation.
- Wavetable interpolation.
- Warp algorithms.
- Unison calculations.
- Filter behavior.
- Envelope stages.
- LFO shapes.
- Modulation routing.
- Distortion.
- Delay.
- Reverb.
- Compressor.
- EQ.
- Serialization.

## 34.2 DSP Validation

Check:

- NaN propagation.
- Infinity propagation.
- Denormal behavior.
- Aliasing.
- Frequency response.
- Phase behavior.
- Oversampling.
- Extreme parameter values.
- Rapid parameter changes.

## 34.3 Integration Tests

Test:

- Plugin initialization.
- Plugin shutdown.
- State save/restore.
- Automation.
- Preset loading.
- MIDI input.
- Sample-rate changes.
- Buffer-size changes.
- Transport changes.
- Device changes.

## 34.4 Host Tests

Test multiple hosts rather than only the primary development DAW.

## 34.5 Hardware Tests

Use a variety of:

- MIDI controllers.
- Audio interfaces.
- Built-in audio devices.
- Sample rates.
- Buffer sizes.

Hardware-specific failures should be handled through adapters/configuration rather than hard-coded product dependencies.

---

# 35. Performance Profiling

Performance must be measured rather than assumed.

Profile:

- CPU per voice.
- CPU per unison voice.
- CPU per oscillator.
- Filter CPU.
- Modulation CPU.
- FX CPU.
- Oversampling CPU.
- UI rendering CPU.
- Memory usage.

Optimize only after identifying measurable bottlenecks.

Do not sacrifice audio correctness for premature micro-optimization.

---

# 36. Parameter Smoothing

Parameters that can produce audible discontinuities should use appropriate smoothing.

Candidates include:

- Filter cutoff.
- Filter resonance.
- Oscillator level.
- Pan.
- Wavetable position.
- Warp amount.
- Distortion drive.
- Delay time.
- Reverb parameters.

Do not blindly smooth every parameter.

Choose smoothing based on:

- Audio behavior.
- Musical response.
- CPU cost.
- User expectations.

---

# 37. Denormal Handling

The DSP engine must avoid performance degradation caused by denormal floating-point values.

Where appropriate:

- Use JUCE denormal protection.
- Use suitable DSP algorithms.
- Avoid unnecessary tiny feedback values.

---

# 38. Host Transport

When running as a plugin, Apollo may use host information for:

- Tempo synchronization.
- LFO synchronization.
- Delay synchronization.
- Sequenced modulation.

The system must remain functional if:

- Host tempo is unavailable.
- Transport is stopped.
- Host does not provide optional timing information.

---

# 39. Accessibility & UX

The UI should provide:

- Readable typography.
- Adequate contrast.
- Tooltips.
- Numeric parameter values.
- Clear active/inactive states.
- Keyboard interaction where practical.
- Scalable UI where practical.
- Non-color-only error communication.

---

# 40. Security

The WebView layer must use a controlled bridge.

Do not expose arbitrary native functionality to JavaScript.

Validate:

- UI messages.
- File paths.
- Preset files.
- Wavetable files.
- User-provided resources.

Avoid unnecessary network access.

Apollo should not require an internet connection for core synthesis functionality unless a future feature explicitly requires it.

---

# 41. Agent Working Rules

When modifying the project, the agent should follow this order:

1. Understand the existing architecture.
2. Identify affected modules.
3. Identify real-time/threading implications.
4. Identify state/serialization implications.
5. Identify UI implications.
6. Implement the smallest coherent architectural change.
7. Update CMake.
8. Update tests.
9. Build the affected targets.
10. Run relevant tests.
11. Review for real-time safety.
12. Review for platform assumptions.
13. Review for memory ownership issues.
14. Review for regression risks.
15. Document important architectural changes.

Do not blindly rewrite large portions of the codebase when a smaller change is sufficient.

---

# 42. Change Management

Apollo is a living project.

Developer requests may change:

- DSP algorithms.
- UI architecture.
- Plugin targets.
- Modulation architecture.
- Hardware support.
- Platform support.
- Performance requirements.
- Preset formats.
- Build structure.

The latest explicit developer requirement takes precedence over an older requirement when they conflict.

When a new requirement materially invalidates an existing architectural decision:

1. Identify the conflict.
2. Explain the affected components briefly.
3. Update the architecture coherently.
4. Update relevant documentation.
5. Update tests.
6. Avoid leaving obsolete assumptions in code.

---

# 43. Project Skills & Custom Commands

Custom agent instructions, scripts, and development skills may be added under:

```text
.claude/
└── skills/
```

When such project-specific instructions exist, inspect and follow the relevant instructions before performing the associated task.

Potential future skills may cover:

- Build automation.
- DSP analysis.
- Audio testing.
- UI development.
- Preset generation.
- Performance profiling.
- Release packaging.

Do not assume a skill exists until its project files are actually present.

---

# 44. Recommended Project Structure

```text
Apollo/
├── CMakeLists.txt
├── CLAUDE.md
├── PRD.md
├── README.md
│
├── Assets/
│   ├── Wavetables/
│   ├── Noise/
│   ├── Presets/
│   └── UI/
│
├── Source/
│   ├── Audio/
│   ├── DSP/
│   │   ├── Oscillators/
│   │   ├── Filters/
│   │   ├── Envelopes/
│   │   ├── LFO/
│   │   ├── Modulation/
│   │   ├── Unison/
│   │   ├── Distortion/
│   │   ├── Delay/
│   │   ├── Reverb/
│   │   ├── Dynamics/
│   │   ├── EQ/
│   │   └── Oversampling/
│   │
│   ├── MIDI/
│   ├── State/
│   ├── Resources/
│   ├── Platform/
│   └── UI/
│
├── WebUI/
│   ├── package.json
│   ├── src/
│   └── public/
│
├── Tests/
│   ├── DSP/
│   ├── State/
│   ├── MIDI/
│   └── Integration/
│
└── .claude/
    └── skills/
```

This is a recommended structure, not an absolute requirement.

---

# 45. Definition of Done

A feature is not considered complete merely because it compiles.

A production-ready feature should, where applicable:

- Compile successfully.
- Integrate with CMake.
- Have clear ownership.
- Be real-time safe.
- Handle invalid inputs.
- Handle extreme parameter values.
- Avoid unnecessary allocations.
- Be testable.
- Have relevant unit/integration tests.
- Work independently of specific hardware.
- Work independently of a specific DAW.
- Avoid unnecessary platform-specific code.
- Serialize state correctly if stateful.
- Expose UI functionality correctly if user-facing.
- Avoid regressions in existing DSP.

---

# 46. Universalization Rules

The following rules replace hardware- and host-specific assumptions that may appear in older project documentation.

## Never hard-code:

- Lenovo hardware.
- Windows-only behavior.
- FL Studio-only behavior.
- A specific MIDI keyboard.
- A specific MIDI controller layout.
- A specific audio interface.
- A specific headphone connector.
- A specific driver.
- A specific CPU vendor.
- A specific WebView backend when JUCE provides a cross-platform abstraction.

## Instead use:

- Cross-platform JUCE APIs.
- CMake platform abstraction.
- Generic MIDI Learn.
- Controller profiles.
- Configurable audio-device selection.
- Host-standard parameter/state mechanisms.
- Platform-specific adapters isolated behind interfaces.
- JUCE's appropriate platform WebView backend.

---

# 47. Priority Hierarchy

When making implementation decisions, use the following priority order:

### P0 — Safety & Correctness

- No crashes.
- No undefined behavior.
- No audio-thread blocking.
- No memory corruption.
- No invalid DSP state.

### P1 — Audio Quality

- Stable synthesis.
- Low aliasing.
- Correct filtering.
- Correct modulation.
- Stable nonlinear processing.

### P2 — Real-Time Performance

- Low CPU usage.
- Predictable execution.
- Efficient memory access.
- Scalable polyphony.

### P3 — Compatibility

- Cross-platform behavior.
- Multiple DAWs.
- Multiple MIDI devices.
- Multiple audio devices.

### P4 — UX

- Responsive UI.
- Clear controls.
- Useful visualization.
- Accessible interaction.

### P5 — Convenience

- Advanced customization.
- Extra integrations.
- Optional workflow enhancements.

Never sacrifice P0 or P1 merely to improve P5.

---

# 48. Final Agent Directive

Apollo is a professional, evolving audio-engineering project.

The agent must behave as a production software engineer and DSP engineer, not merely as a code generator.

Every implementation should be evaluated against:

```text
Correctness
    ↓
Real-Time Safety
    ↓
Audio Quality
    ↓
Performance
    ↓
Portability
    ↓
Maintainability
    ↓
User Experience
```

The final architecture should allow Apollo to run on a broad range of supported computers, audio devices, MIDI controllers, DAWs, and operating systems without requiring product-specific assumptions.

**Build Apollo as a portable audio instrument, not as software tied to the developer's machine.**