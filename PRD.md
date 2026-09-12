Apollo Synthesizer — Product Requirements Document
Document Status: Living Blueprint / Non-Final  
Product: Apollo  
Target Formats: VST3 Plugin + Standalone Application  
Primary UI Technology: JUCE 8 WebView + HTML/CSS/JavaScript/React  
Primary DSP Language: C++  
Platform Goal: Cross-platform and hardware-agnostic  
Version: 1.0 (Universalized PRD)
---
1. Executive Summary
Apollo is a flagship-grade, multi-oscillator wavetable synthesizer designed for professional music production, sound design, live performance, and experimentation. It combines a high-performance C++ DSP engine with a modern web-based user interface hosted through JUCE 8 WebView.
Apollo is intended to provide a workflow and depth comparable to established wavetable synthesizers while retaining a modular and structurally customizable architecture.
The product must be available in two forms:
VST3 plugin for use inside compatible digital audio workstations (DAWs).
Standalone application for independent sound design, MIDI performance, recording, and audio monitoring.
The architecture must be platform-independent and hardware-agnostic. No core feature may depend on a specific computer manufacturer, operating system, audio interface, MIDI controller, headphone connector, or DAW.
Where hardware-specific mappings are useful, Apollo should provide configurable MIDI mapping and optional controller profiles rather than hard-coded dependencies.
---
2. Product Vision
Apollo should provide:
Professional-quality wavetable synthesis.
Flexible oscillator and modulation architecture.
High-quality filters and nonlinear processing.
A modular, reorderable effects rack.
Low-latency real-time performance.
A highly interactive web-based UI.
Cross-platform operation.
Hardware-independent MIDI control.
DAW-independent plugin behavior where supported by the VST3 specification.
Reliable preset and project-state serialization.
A scalable architecture capable of supporting future synthesis and effects modules.
The guiding principle is:
> **Apollo should adapt to the user's hardware and host environment rather than requiring the user to adapt to Apollo.**
---
3. Goals
3.1 Primary Goals
Build a professional multi-oscillator wavetable synthesizer.
Support high-quality 3D wavetable scanning.
Provide flexible modulation routing.
Provide professional filters and nonlinear processing.
Provide an integrated multi-FX rack.
Maintain real-time audio-thread safety.
Provide responsive visual feedback.
Support VST3 and standalone operation.
Support common desktop operating systems through a portable C++/JUCE architecture.
Support arbitrary MIDI controllers through MIDI Learn and configurable mappings.
Support different sample rates and audio buffer sizes.
Preserve plugin state reliably in compatible DAWs.
Avoid unnecessary latency in the synthesis path.
3.2 Secondary Goals
Make the architecture extensible.
Allow future CLAP/AU/AAX or other formats to be added without rewriting the DSP engine.
Allow future hardware/controller profiles without changing core synthesis code.
Make the UI independently evolvable from the DSP engine.
Support user-created wavetables, presets, modulation shapes, and noise samples.
---
4. Non-Goals
The initial release does not need to:
Require a particular MIDI controller.
Require a particular audio interface.
Require a particular headphone connector.
Require a particular DAW.
Require a particular operating system.
Depend on proprietary hardware drivers.
Provide built-in cloud synchronization.
Provide online collaboration.
Provide a full DAW/sequencer.
Guarantee zero latency under every possible host/audio-device configuration.
The standalone application may provide low-latency operation, but actual end-to-end latency is ultimately affected by the selected audio device, operating-system audio subsystem, buffer size, driver, and host configuration.
---
5. Universal Compatibility Requirements
5.1 Operating Systems
Apollo should be architected to support, subject to JUCE and third-party dependency availability:
Windows
macOS
Linux
Platform-specific code must be isolated behind abstraction layers.
No DSP algorithm should contain operating-system-specific behavior.
5.2 CPU Architectures
The application should support commonly deployed desktop CPU architectures where the build toolchain permits:
x86-64
ARM64
The DSP engine should avoid unnecessary architecture-specific assumptions.
SIMD optimizations may be implemented through portable abstractions, with architecture-specific optimized paths where beneficial.
5.3 Audio Hardware
Apollo must support any audio device exposed through the selected platform's supported audio APIs.
The standalone application must allow the user to configure:
Audio device
Input/output channels
Sample rate
Buffer size
Input monitoring
Output routing
Apollo must not assume:
3.5 mm output
USB audio
Bluetooth audio
A particular audio interface
A particular headphone amplifier
Bluetooth or other high-latency devices may naturally introduce additional system latency; Apollo should not artificially add further processing latency unless required by DSP.
5.4 MIDI Hardware
Apollo must accept MIDI input from arbitrary compatible MIDI devices.
The application must provide:
MIDI device selection.
MIDI Learn.
CC assignment.
Note input.
Pitch bend.
Modulation wheel.
Aftertouch where supported.
MPE support where feasible.
Configurable controller mappings.
Saveable MIDI mapping profiles.
No synthesizer parameter may depend on a specific physical controller.
Controller profiles may be supplied as optional presets.
---
6. Plugin and Host Integration
6.1 Plugin Format
The primary plugin target is:
VST3
The DSP and parameter architecture must remain format-agnostic.
Future formats may include:
CLAP
Audio Unit
AAX
Other formats supported by the chosen framework and licensing constraints.
6.2 DAW Compatibility
Apollo must be designed around the official plugin format specifications rather than around a single DAW.
The plugin should operate correctly in compatible DAWs including, where supported by the plugin format and platform:
FL Studio
Ableton Live
Logic Pro
Cubase
Studio One
Reaper
Bitwig
Pro Tools through a future appropriate plugin format
Other standards-compliant hosts
No DAW-specific workaround should be placed in the core DSP architecture unless unavoidable.
6.3 Parameter State
All automatable parameters must be represented through a robust parameter/state system.
The implementation should use JUCE's `AudioProcessorValueTreeState` or an equivalent architecture.
State requirements:
Serialize all user-visible synthesizer parameters.
Serialize oscillator state.
Serialize wavetable selection.
Serialize modulation routing.
Serialize effect-chain configuration.
Serialize effect parameters.
Serialize preset metadata where appropriate.
Restore state accurately.
Handle version migrations.
Remain backward compatible with older preset/project states where practical.
---
7. Core Sound Engine
7.1 Signal Flow
A conceptual signal path is:
```text
MIDI
  ↓
Voice Allocation
  ↓
Oscillator A ─┐
Oscillator B ─┼→ Voice Mixer → Filters → FX → Master
Sub Oscillator ┤
Noise ─────────┘
```
The actual routing architecture may be expanded to support additional modulation and signal-routing configurations.
---
8. Wavetable Oscillators
8.1 Primary Oscillators
Apollo shall provide two primary wavetable oscillators:
Oscillator A
Oscillator B
Each oscillator must support:
Wavetable selection.
Wavetable position/index.
Pitch.
Fine tuning.
Level.
Pan.
Phase.
Unison.
Detune.
Stereo spread.
Warp.
Warp amount.
Optional oscillator-specific modulation.
8.2 Wavetable Structure
The target wavetable representation is:
Up to 256 frames per wavetable.
High-resolution waveform samples per frame.
Efficient interpolation between adjacent frames.
Efficient interpolation between waveform samples.
Optional interpolation quality modes.
The system should support variable table sizes internally where practical, even if 256 frames is the default/reference format.
8.3 Anti-Aliasing
Wavetable playback must minimize aliasing across the audible frequency range.
Possible techniques include:
Band-limited wavetable mipmaps.
Precomputed multi-resolution tables.
Oversampling.
PolyBLEP-style correction where appropriate.
High-quality interpolation.
The final implementation should choose the technique that provides the best quality/performance balance.
---
9. Sub Oscillator
Apollo shall include a dedicated sub oscillator.
Supported waveforms:
Sine
Triangle
Square
Parameters:
Octave
Level
Waveform
Phase
Optional tracking mode
The sub oscillator should be computationally inexpensive and suitable for reinforcing the fundamental frequency.
---
10. Noise Generator
Apollo shall include a stereo noise generator.
Initial noise types:
White noise
Pink noise
Attack/transient noise samples
Requirements:
Stereo-capable output.
Independent level control.
Optional key tracking.
Optional envelope control.
User-loadable noise samples where feasible.
Deterministic behavior when required for preset recall.
Noise generation must avoid unnecessary allocations on the real-time audio thread.
---
11. Unison Engine
Each primary oscillator shall support up to 16 unison voices.
Parameters:
Voice count.
Detune amount.
Detune curve.
Stereo spread.
Phase randomization.
Optional blend control.
Detune algorithms:
Linear
Exponential
The implementation should avoid excessive CPU consumption when unison voice counts are high.
---
12. Warp Engine
Apollo shall provide real-time waveform manipulation.
Initial warp modes:
Asymmetric Sync
PWM
Bend
Mirror
Each warp mode must provide:
Enable/disable.
Amount.
Appropriate modulation support.
Warp algorithms must be implemented in a way that minimizes aliasing.
---
13. Filter Architecture
Apollo shall provide two state-variable filters:
Filter 1
Filter 2
Supported routing:
Series
Parallel
Split/custom routing where technically feasible
13.1 Filter Types
Initial filter types:
Low-pass
High-pass
Band-pass
Comb
Formant
Additional filter models may be added later.
13.2 Filter Parameters
Common parameters:
Cutoff
Resonance
Drive
Mix
Key tracking
Filters should support modulation from the modulation matrix.
---
14. Drive and Saturation
Drive stages should be available before and/or after filtering.
The initial soft-clipping model may use:
[
y = \tanh(gx)
]
where:
(x) = input signal
(g) = drive gain
(y) = processed signal
Additional waveshaping models may be added.
The nonlinear processing architecture must support oversampling.
---
15. Modulation System
Apollo's modulation architecture is a core differentiating feature.
15.1 Envelopes
Apollo shall provide four DAHDSR envelopes.
Stages:
Delay
Attack
Hold
Decay
Sustain
Release
Each envelope must support:
Adjustable stage times.
Adjustable curve tension.
Linear behavior.
Exponential behavior.
Visual editing.
Retrigger options.
MIDI note synchronization.
Optional future features:
Looping envelopes.
Tempo synchronization.
Per-stage curves.
15.2 LFOs
Apollo shall provide four LFOs.
Supported modes:
Sine
Triangle
Saw
Reverse saw
Square
Sample & Hold
Step
Custom drawable curve
LFO parameters:
Rate
Sync
Phase
Fade-in
Smoothing
Retrigger
Polarity
LFOs should support both free-running and note-triggered operation.
---
16. Modulation Matrix
Apollo shall provide a flexible modulation matrix.
Each routing entry contains conceptually:
```text
Source → Destination → Depth
```
Supported source examples:
ENV 1–4
LFO 1–4
Velocity
Note
Mod Wheel
Pitch Bend
Aftertouch
MPE dimensions where available
Macro controls
Random
Key tracking
Destinations may include:
Oscillator pitch
Wavetable position
Warp amount
Filter cutoff
Filter resonance
Amplifier level
Pan
Effect parameters
Other exposed synthesizer parameters
Depth must support bipolar modulation.
The system should support modulation of modulation depth where technically feasible.
Example:
```text
LFO 1 → ENV 2 Depth
```
The matrix must be designed so additional sources and destinations can be added without rewriting the modulation engine.
---
17. Macro Controls
Apollo should provide user-configurable macro controls.
Macros must support:
MIDI Learn.
Multiple destinations.
Bipolar depth.
Minimum/maximum ranges.
Curve shaping.
Preset storage.
The default macro mapping must never assume a particular MIDI controller.
---
18. Multi-FX Rack
Apollo shall provide a reorderable effects rack.
The rack must execute entirely within the real-time DSP architecture.
Users should be able to:
Add effects.
Remove effects.
Reorder effects.
Bypass effects.
Adjust effect parameters.
Save effect-chain configurations with presets.
---
19. Distortion
The distortion unit should provide multiple modes:
Hard clipping
Soft clipping
Diode-style emulation
Potential future modes:
Wavefolding
Bitcrushing
Rectification
Custom waveshaping
All nonlinear modes must consider oversampling requirements.
---
20. Delay
Apollo shall provide stereo delay.
Features:
Left/right delay.
Ping-pong mode.
Feedback.
Mix.
Filtering.
Millisecond timing.
Host-tempo synchronization.
Note-value selection.
The delay should support:
Free time.
Tempo-synced time.
Modulation where CPU budget allows.
---
21. Reverb
Apollo shall provide algorithmic reverb.
Initial modes:
Room
Hall
Parameters:
Size
Decay
Damping
Pre-delay
Width
Mix
The implementation should avoid unnecessary latency.
---
22. Noise Gate
The dynamics section shall include a noise gate.
Parameters:
Threshold
Attack
Hold
Release
Range
The gate should be suitable for reducing unwanted noise while avoiding audible pumping where practical.
---
23. Compressor
Apollo shall include an RMS-based compressor.
Parameters:
Threshold
Ratio
Attack
Release
Makeup gain
Mix
Optional future features:
Peak detection.
Sidechain input.
External sidechain routing.
---
24. Equalizer
Apollo shall include a parametric 4-band EQ.
Each band should support:
Frequency
Gain
Q
Filter type
Enable/bypass
Potential filter types:
Bell
Low shelf
High shelf
Low-pass
High-pass
---
25. Oversampling
Oversampling shall be used where required to control nonlinear aliasing.
Minimum target:
2× oversampling
Preferred quality mode:
4× oversampling
Potential future modes:
8×
User-selectable quality modes
Oversampling should primarily be applied to nonlinear processing and any other stages where analysis demonstrates significant aliasing.
The implementation must balance:
CPU consumption.
Latency.
Audio quality.
Stability.
Oversampling should not be enabled indiscriminately if it provides no meaningful audio-quality benefit.
---
26. Real-Time Audio Architecture
The real-time audio thread is a critical system boundary.
26.1 Audio Thread Rules
The audio thread must avoid:
Memory allocation.
Deallocation.
Blocking locks.
File I/O.
Network operations.
UI operations.
JavaScript execution.
Expensive unpredictable operations.
All required resources should be prepared ahead of time.
26.2 Thread Isolation
The architecture should conceptually separate:
```text
UI Thread
   ↕
Message/Parameter Bridge
   ↕
DSP State
   ↕
Audio Thread
```
The UI must never directly manipulate unsafe DSP structures.
Parameter communication should use:
Atomic values where appropriate.
Lock-free queues where appropriate.
APVTS/host parameter mechanisms.
Double-buffered state where necessary.
---
27. UI Architecture
Apollo shall use JUCE 8 WebView integration.
The frontend should use:
HTML
CSS
JavaScript
React
The frontend should be component-based and structured for rapid iteration.
Where possible, development should support hot reloading in development builds without compromising release performance or stability.
---
28. UI Design Language
The default visual identity should use:
Dark-mode base, in a neutral and faintly cool metallic grey.
Gold primary theme, keyed to #F7EF8A — Apollo is named after the Greek god of the sun and of music. This replaces the deep purple theme previously specified here (ADR-0051).
Bright green parameter/modulation activity indicators.
Red indicators for clipping, critical warnings, and aggressive distortion states.
The UI must remain usable without relying solely on color.
Important states should also have:
Labels.
Icons.
Numerical values.
Tooltips.
Visual shape/animation differences.
---
29. UI Layout
Suggested high-level layout:
```text
┌─────────────────────────────────────────────┐
│                 Apollo Header               │
├─────────────────────────────────────────────┤
│ Osc A │ Osc B │ Sub │ Noise │ Master       │
├─────────────────────────────────────────────┤
│             Filter / Routing                │
├─────────────────────────────────────────────┤
│           Modulation Matrix                 │
├─────────────────────────────────────────────┤
│                FX Rack                      │
├─────────────────────────────────────────────┤
│ Visualizer / Oscilloscope / Status          │
└─────────────────────────────────────────────┘
```
The exact layout may evolve during implementation.
---
30. Visualizers
Apollo shall provide real-time visual feedback.
30.1 Oscilloscopes
Apollo shall provide an oscilloscope for the final output and, separately, for
every individual sound source the instrument offers.
The user must be able to see the waveform each source is producing on its own,
not only the sum, so that the wave being crafted is visible while it is being
crafted.
Sources requiring their own scope:
Oscillator 1, including its wavetable position, warp and unison.
Oscillator 2, likewise.
Sub oscillator.
Noise generator.
The post-filter signal, so the effect of the filters is visible.
The final output.
Where a source is a modulator rather than a sound, its motion shall be shown in
the same way: each envelope and each LFO must be visible as a live trace of the
value it is currently producing, not merely as a static picture of its shape.
Requirements:
Smooth rendering.
High refresh rate.
No blocking of the audio thread.
Decoupled visualization buffer per source.
Graceful degradation under CPU load.
A source that is silent or disabled shall be shown as silent rather than stale.
Capture must be cheap enough that scopes for every source do not materially
affect polyphony, and shall be measured rather than assumed.
30.2 Wavetable Visualizer
The wavetable display should show:
Current waveform.
Wavetable position.
Interpolation/scanning state.
Warp state where appropriate.
30.3 Modulation Visualization
Active modulation should be visually represented.
Examples:
Animated modulation depth.
Parameter activity indicators.
LFO movement.
Envelope progression.
Visualizer calculations must occur outside the real-time audio thread.
---
31. UI ↔ DSP Communication
The UI and DSP must communicate through a well-defined bridge.
The bridge should support:
Parameter updates.
Meter data.
Oscilloscope data.
Wavetable state.
Preset state.
Transport information.
MIDI state where appropriate.
The UI must not poll expensive DSP structures directly.
Metering and visualization data should be downsampled or buffered to an appropriate UI rate.
---
32. Preset System
Apollo shall support a structured preset system.
Preset data should contain:
Oscillator configuration.
Wavetables.
Sub oscillator.
Noise configuration.
Filter settings.
Modulation matrix.
Envelopes.
LFOs.
FX chain.
Macros.
Global settings where appropriate.
Preset format should be versioned.
Preset files shall use the extension .rnv — one preset per file, the same
extension for factory and user content, and the same validated reader for both
(ADR-0053). A preset file shall carry the versioned state document together with
its own metadata: name, author, category and comment.
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
The exact serialization format may be changed during implementation.
---
33. Wavetable Management
Users should be able to:
Browse installed wavetables.
Load custom wavetables.
Preview wavetables.
Organize wavetables.
Save wavetable references in presets.
Handle missing wavetable assets gracefully.
The system should distinguish between:
Factory content.
User content.
Embedded content.
External referenced content.
Where practical, presets should avoid breaking when a user moves their content directory.
---
34. MIDI System
Apollo shall support standard MIDI functionality.
Minimum requirements:
MIDI note input.
Velocity.
Pitch bend.
Mod wheel.
Sustain pedal.
MIDI CC.
Program/preset navigation where supported.
Additional support:
Aftertouch.
Polyphonic aftertouch.
MPE.
34.1 MIDI Learn
Any appropriate automatable parameter should support:
Enter MIDI Learn mode.
User moves a MIDI control.
Apollo identifies the MIDI source.
Parameter mapping is created.
Mapping is saved.
Mappings should be removable and editable.
---
35. Controller Profiles
Apollo may provide optional controller profiles.
Profiles must be:
User-selectable.
Editable.
Exportable/importable.
Independent from the DSP engine.
A controller profile must never be required to operate Apollo.
The application must work fully with a mouse, keyboard, touchscreen where supported, or arbitrary MIDI hardware.
---
36. Automation
Plugin parameters exposed to the host must support host automation.
Requirements:
Stable parameter IDs.
Human-readable parameter names.
Appropriate parameter ranges.
Normalized values where required by the host API.
Smooth parameter transitions where necessary.
Correct state restoration.
Parameter IDs should remain stable between releases whenever possible.
---
37. Transport Synchronization
When running as a plugin, Apollo should be able to access host transport information where available.
Potential uses:
Tempo-synced LFOs.
Tempo-synced delay.
Sequenced modulation.
Host play/stop synchronization.
The plugin must continue functioning sensibly when transport information is unavailable.
---
38. Audio Quality Requirements
Apollo should target professional production quality.
The engine should:
Avoid unexpected clipping.
Avoid denormal performance problems.
Handle sample-rate changes.
Handle buffer-size changes.
Remain stable during rapid parameter changes.
Avoid audible discontinuities during preset changes where practical.
Maintain phase consistency where required.
Prevent runaway feedback in effects.
---
39. Performance Requirements
Apollo must be optimized for real-time operation.
39.1 CPU
The application should scale gracefully with:
Polyphony.
Unison count.
Oversampling.
FX count.
Visualization load.
CPU-heavy optional features should have quality controls where necessary.
39.2 Memory
The engine must avoid unnecessary memory growth.
Large resources such as wavetables should be:
Cached.
Shared where possible.
Reference counted where appropriate.
Loaded asynchronously outside the audio thread.
39.3 Latency
Apollo should avoid adding unnecessary algorithmic latency.
The core synthesizer path should target effectively zero additional latency where technically possible.
Effects that inherently require lookahead or buffering must report/manage their latency correctly when applicable.
Actual end-to-end latency depends on:
Audio device.
Driver.
Host.
Operating system.
Buffer size.
Sample rate.
---
40. Error Handling
Apollo must fail gracefully.
Examples:
Missing Wavetable
Display a clear warning and use a safe fallback waveform.
Unsupported Audio Configuration
Display a configuration error and provide alternative supported settings.
Missing Preset Asset
Load the remaining preset state and identify the missing resource.
MIDI Device Disconnect
Continue operating without crashing and reconnect when the device becomes available.
WebView Failure
Provide a controlled fallback/error state rather than corrupting DSP state.
---
41. Security and Stability
The UI must not have unrestricted access to the local system.
WebView integration should use a controlled communication bridge.
External resources should not be loaded unnecessarily.
User-provided files must be validated before processing.
Malformed preset/wavetable files must not crash the application.
---
42. Testing Requirements
Testing should occur at multiple levels.
42.1 Unit Tests
Test:
Oscillator generation.
Wavetable interpolation.
Warp algorithms.
Filters.
Envelopes.
LFOs.
Modulation routing.
Distortion.
Delay.
Reverb.
Compressor.
EQ.
State serialization.
42.2 DSP Tests
Test:
NaN/Inf propagation.
Denormal behavior.
Frequency response.
Aliasing.
Phase behavior.
Oversampling.
Extreme parameter values.
42.3 Integration Tests
Test:
Plugin initialization.
State save/restore.
Automation.
Preset loading.
MIDI input.
Sample-rate changes.
Buffer-size changes.
Transport synchronization.
42.4 UI Tests
Test:
Parameter editing.
Drag-and-drop modulation.
MIDI Learn.
Preset browser.
Visualizers.
Responsive layout.
Error states.
42.5 Host Compatibility Tests
Test multiple standards-compliant DAWs rather than optimizing exclusively for one DAW.
---
43. Accessibility
Apollo should provide:
Readable text.
Adequate UI contrast.
Keyboard navigation where practical.
Tooltips.
Numeric parameter displays.
Non-color-only status communication.
Scalable UI where practical.
---
44. Architecture Principles
The project should follow these architectural principles:
Separation of Concerns
DSP, UI, platform, plugin wrapper, preset management, and resource management should be separated.
Real-Time Safety
Nothing unpredictable should execute on the audio thread.
Extensibility
New oscillators, filters, modulators, and effects should be addable without rewriting unrelated systems.
Hardware Independence
Hardware-specific behavior belongs in configurable profiles and adapters, not in the core engine.
Host Independence
The DSP engine should not depend on a specific DAW.
Testability
DSP modules should be testable without launching the UI or a DAW.
---
45. Suggested Project Structure
```text
Apollo/
├── CMakeLists.txt
├── README.md
├── PRD.md
├── LICENSE
│
├── Assets/
│   ├── Wavetables/
│   ├── Noise/
│   ├── Presets/
│   └── UI/
│
├── Source/
│   ├── Audio/
│   │   ├── ApolloAudioProcessor.*
│   │   ├── ApolloAudioProcessorEditor.*
│   │   ├── Voice.*
│   │   ├── VoiceManager.*
│   │   └── AudioEngine.*
│   │
│   ├── DSP/
│   │   ├── Oscillators/
│   │   ├── Filters/
│   │   ├── Envelopes/
│   │   ├── LFO/
│   │   ├── Modulation/
│   │   ├── Distortion/
│   │   ├── Delay/
│   │   ├── Reverb/
│   │   ├── Compressor/
│   │   ├── EQ/
│   │   └── Oversampling/
│   │
│   ├── State/
│   │   ├── ParameterState.*
│   │   ├── PresetManager.*
│   │   └── StateSerializer.*
│   │
│   ├── MIDI/
│   │   ├── MidiManager.*
│   │   ├── MidiLearn.*
│   │   └── ControllerProfile.*
│   │
│   ├── Resources/
│   │   ├── WavetableManager.*
│   │   └── ResourceCache.*
│   │
│   ├── Platform/
│   │   └── PlatformServices.*
│   │
│   └── UI/
│       ├── WebViewBridge.*
│       └── WebViewEditor.*
│
├── WebUI/
│   ├── package.json
│   ├── src/
│   │   ├── components/
│   │   ├── panels/
│   │   ├── modulation/
│   │   ├── visualizers/
│   │   └── App.*
│   └── public/
│
└── Tests/
    ├── DSP/
    ├── State/
    ├── MIDI/
    └── Integration/
```
This structure is a reference architecture rather than a mandatory implementation.
---
46. Development Phases
Phase 1 — Foundation
Deliver:
JUCE project.
CMake build system.
VST3 target.
Standalone target.
Basic audio engine.
MIDI note handling.
Basic UI/WebView communication.
Phase 2 — Oscillator Engine
Deliver:
Oscillator A.
Oscillator B.
Wavetable loading.
Wavetable interpolation.
Sub oscillator.
Noise generator.
Unison.
Phase 3 — Filters and Modulation
Deliver:
Dual filters.
Filter routing.
Envelopes.
LFOs.
Modulation matrix.
MIDI modulation sources.
Phase 4 — Effects
Deliver:
Distortion.
Delay.
Reverb.
Gate.
Compressor.
EQ.
FX ordering.
Phase 5 — Advanced UI
Deliver:
Wavetable visualizer.
Oscilloscope.
Modulation visualization.
Drag-and-drop modulation.
Preset browser.
MIDI Learn.
Phase 6 — Optimization
Deliver:
Profiling.
SIMD optimization.
Oversampling optimization.
Memory optimization.
UI performance optimization.
Audio-thread safety audit.
Phase 7 — Compatibility
Deliver:
Cross-platform builds.
Multiple DAW testing.
Multiple MIDI controller testing.
Multiple audio-device testing.
Sample-rate testing.
Buffer-size testing.
Phase 8 — Release Hardening
Deliver:
Crash testing.
State migration testing.
Preset validation.
Resource validation.
Installer/package testing.
Documentation.
Final QA.
---
47. Acceptance Criteria
Apollo is considered functionally ready for an initial production release when:
VST3 loads successfully in multiple compatible DAWs.
Standalone mode operates with supported audio devices.
MIDI input works with arbitrary standard MIDI controllers.
No specific MIDI controller is required.
Oscillators produce stable audio across supported sample rates.
Wavetables interpolate correctly.
Unison functions up to the supported voice count.
Filters operate correctly in supported routing modes.
Modulation routes correctly.
FX can be reordered.
Presets save and restore accurately.
Host automation works.
Host project state restores correctly.
UI communication does not block the audio thread.
Visualizers do not interrupt audio processing.
Nonlinear processing has appropriate oversampling.
Missing resources fail gracefully.
Audio configuration changes do not crash the application.
CPU usage is profiled and documented.
The application does not require proprietary hardware.
---
48. Future Expansion
The architecture should leave room for:
Additional wavetable oscillators.
Granular oscillator.
FM synthesis.
Ring modulation.
Physical modeling.
More filter models.
Additional distortion algorithms.
Convolution reverb.
Sidechain processing.
MPE enhancements.
CLAP support.
AU support.
AAX support.
User scripting/modulation.
Preset tagging and advanced browser search.
Custom UI themes.
Plugin sandboxing where applicable.
---
49. Key Architectural Decisions
The following decisions are considered foundational but remain revisable under the living-PRD principle:
C++/JUCE is the primary application and DSP framework.
JUCE WebView is the primary UI integration layer.
React is the preferred UI framework.
VST3 is the initial plugin format.
Standalone operation is a first-class target.
The DSP engine is host- and hardware-independent.
MIDI controller support is profile-based rather than hard-coded.
Real-time audio processing is strictly isolated from UI processing.
Oversampling is applied selectively to alias-sensitive processing.
APVTS or an equivalent robust parameter architecture is used for host automation and state management.
---
50. Living PRD / Developer Clause
This document is intentionally non-final.
During development, the developer may request:
Architectural changes.
DSP changes.
UI changes.
Feature additions.
Feature removals.
Compatibility changes.
Performance changes.
Workflow changes.
Technology changes.
Such changes should be integrated into the PRD as the project evolves.
When requirements conflict, the latest explicit developer-approved requirement takes precedence, provided that it remains technically and legally feasible.
The PRD should therefore be treated as a living technical specification, not a frozen contract.
---
51. Universalization Summary
The original hardware/host-specific requirements have been intentionally generalized.
Apollo must not require:
A specific MIDI keyboard.
A specific controller layout.
A specific DAW.
FL Studio specifically.
A particular audio interface.
A particular headphone connector.
A particular operating system.
A particular CPU architecture where portable builds are feasible.
Instead:
MIDI mappings are configurable.
Controller profiles are optional.
Host integration follows plugin standards.
Audio devices are user-selectable.
Standalone audio configuration is exposed to the user.
DSP remains independent from platform APIs.
Platform-specific code is isolated.
Plugin state follows stable parameter IDs and versioned serialization.
Future plugin formats can be added without redesigning the DSP engine.
Core principle: Apollo should be portable, configurable, and host-agnostic by design.