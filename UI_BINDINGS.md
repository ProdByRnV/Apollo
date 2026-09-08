# Apollo Synthesizer — UI Bindings

> **Developer Clause:** This document defines the stable communication contract between Apollo's native C++ engine and its web-based frontend. It is an implementation contract, not a host- or hardware-specific shortcut. The binding layer must remain asynchronous, versionable, validated, testable, and safe for real-time audio processing.

## 1. Communication Architecture

Apollo uses a strict separation between the WebView UI, parameter/state system, and real-time DSP engine.

```text
Web UI (React / TypeScript / CSS)
              │
       Validated Commands
       + State Snapshots
              │
              ▼
     UI Binding / Bridge
 Validation • Routing • Versioning
              │
              ▼
       APVTS / Parameters
              │
              ▼
        Native C++ DSP
```

### Core rules

- The WebView must never access DSP objects directly.
- The audio thread must never call into the WebView.
- The bridge must not introduce locks or blocking operations into real-time processing.
- UI commands must be validated before reaching the parameter system.
- Native/APVTS state is authoritative; React state is a UI representation/cache.
- Normalized parameter values `[0.0, 1.0]` should be the preferred transport representation for continuous parameters.
- The bridge must remain independent of any particular DAW, operating system, MIDI controller, audio driver, or physical device.
- Platform-specific WebView details must remain behind the bridge/application boundary.

---

## 2. Binding Responsibilities

The binding layer is responsible for:

1. Exposing controlled parameter/state information to the frontend.
2. Receiving validated user commands.
3. Translating commands into APVTS parameter changes.
4. Reporting authoritative parameter changes back to the frontend.
5. Synchronizing initial state.
6. Handling preset/state changes.
7. Communicating non-audio telemetry such as meters and visualization snapshots.
8. Reporting recoverable errors without destabilizing DSP.
9. Maintaining protocol compatibility as the application evolves.

The binding layer is **not** responsible for:

- Performing DSP.
- Owning synthesis state that belongs in the audio engine.
- Rendering UI components.
- Reading arbitrary files from frontend commands.
- Executing arbitrary native commands.
- Becoming a second parameter/state-management system.

---

## 3. Parameter ID Registry

Every automatable parameter exposed through APVTS should have one stable identifier.

Parameter IDs are part of Apollo's serialized-state and automation contract. Once released, an ID must not be silently repurposed.

Registry as implemented. `Source/Parameters/ParameterDefinitions.h` is authoritative; this table mirrors it and must be updated in the same change (Docs/PARAMETER-CONVENTIONS.md §6).

Parameters are added by the phase that implements the DSP giving them meaning, so no ID ships before it does something. The phase column records when each arrived.

Envelopes 2-4 and the four LFOs are deliberately absent: their generators exist or are coming, but they have nowhere to send their output until the modulation matrix, so their IDs ship with it.

| Parameter ID | UI Element | Range / Type | Default | Phase | Description |
|---|---|---|---:|---|---|
| `osc1_wavetable` | Selector / Dropdown | Discrete `[0, 3]` | `0` | 2 | Wavetable selection for oscillator 1 |
| `osc1_position` | Rotary Knob | Normalized `[0, 1]` | `0` | 4a | Wavetable scan position for oscillator 1 |
| `osc1_unison` | Rotary Knob | Integer `[1, 16]` | `1` | 2 | Unison voice count |
| `osc1_detune` | Rotary Knob | Normalized `[0, 1]` | `0.2` | 2 | Unison detune amount (±50 cents at full) |
| `osc1_spread` | Rotary Knob | Normalized `[0, 1]` | `0.5` | 4b | Unison stereo spread |
| `osc1_level` | Rotary Knob | Normalized `[0, 1]` | `1` | 4b | Oscillator 1 output level |
| `osc1_pan` | Rotary Knob | Bipolar `[-1, 1]` | `0` | 4b | Oscillator 1 stereo balance |
| `osc2_wavetable` | Selector / Dropdown | Discrete `[0, 3]` | `0` | 4b | Wavetable selection for oscillator 2 |
| `osc2_position` | Rotary Knob | Normalized `[0, 1]` | `0` | 4b | Wavetable scan position for oscillator 2 |
| `osc2_unison` | Rotary Knob | Integer `[1, 16]` | `1` | 4b | Unison voice count |
| `osc2_detune` | Rotary Knob | Normalized `[0, 1]` | `0.2` | 4b | Unison detune amount |
| `osc2_spread` | Rotary Knob | Normalized `[0, 1]` | `0.5` | 4b | Unison stereo spread |
| `osc2_level` | Rotary Knob | Normalized `[0, 1]` | `0` | 4b | Oscillator 2 output level (silent by default) |
| `osc2_pan` | Rotary Knob | Bipolar `[-1, 1]` | `0` | 4b | Oscillator 2 stereo balance |
| `osc2_semitones` | Rotary Knob | Integer `[-24, 24]` | `0` | 4b | Oscillator 2 coarse transposition |
| `osc2_fine` | Rotary Knob | `-100–100 cents` | `0` | 4b | Oscillator 2 fine tuning |
| `sub_level` | Rotary Knob | Normalized `[0, 1]` | `0` | 4b | Sub oscillator level (silent by default) |
| `sub_octave` | Selector / Dropdown | Integer `[-2, -1]` | `-1` | 4b | Sub oscillator octave below the note |
| `noise_level` | Rotary Knob | Normalized `[0, 1]` | `0` | 4b | Noise generator level (silent by default) |
| `env1_delay` | Rotary Knob | `0–2000 ms` | `0` | 5a | Envelope 1 delay before the attack |
| `env1_attack` | Rotary Knob | `0–10000 ms` | `5` | 5a | Envelope 1 attack time |
| `env1_hold` | Rotary Knob | `0–2000 ms` | `0` | 5a | Envelope 1 hold at full level |
| `env1_decay` | Rotary Knob | `0–10000 ms` | `100` | 5a | Envelope 1 decay to the sustain level |
| `env1_sustain` | Rotary Knob | Normalized `[0, 1]` | `1` | 5a | Envelope 1 sustain level |
| `env1_release` | Rotary Knob | `0–10000 ms` | `50` | 5a | Envelope 1 release time |
| `env1_curve` | Rotary Knob | Bipolar `[-1, 1]` | `0.5` | 5a | Envelope 1 curve tension; 0 is linear, positive is the analog shape |
| `filter_cutoff` | Rotary Knob | `20–20000 Hz` | `20000` | 2 | Filter cutoff frequency |
| `filter_resonance` | Rotary Knob | Float `[0.1, 10.0]` | `0.707` | 2 | Filter resonance/Q |
| `filter_drive` | Rotary Knob | Normalized `[0, 1]` | `0` | 2 | Filter/pre-filter drive |
| `fx_distortion_mix` | Rotary Knob | Normalized `[0, 1]` | `0` | 2 | Distortion dry/wet mix |
| `fx_delay_time` | Rotary Knob | `1–2000 ms` | `500` | 2 | Delay time |
| `master_gain` | Rotary Knob | `-60–6 dB` | `0` | 2 | Master output gain |

The registry is generated from one authoritative native parameter definition system rather than duplicated manually: `createParameterLayout()` builds the APVTS layout from the definitions above, the bridge derives its metadata from the same source, and `Tests/Parameters/ParameterRegistryTests.cpp` asserts that this documented list and the native registry agree.

Each definition should specify:

- Stable ID.
- Display name.
- Type.
- Normalized representation.
- User-facing range.
- Default.
- Unit.
- Step count or continuous behavior.
- Skew/curve.
- Automation capability.
- Smoothing requirements.
- Modulation capability.
- UI hints.

---

## 4. Parameter Value Model

The bridge distinguishes between:

### Normalized value

A logical value in `[0.0, 1.0]`, preferred for transport.

### Plain value

The user-facing value, such as Hz, milliseconds, dB, or voice count.

Native parameter definitions remain authoritative for conversion.

### Discrete values

Discrete parameters must preserve their step count. An integer control must not be treated as an arbitrary continuous float.

---

## 5. C++ → Web State Updates

Native state changes may originate from:

- User interaction.
- Host automation.
- Preset loading.
- Project/session restoration.
- MIDI control.
- Programmatic parameter changes.

Updates are delivered asynchronously.

Example:

```json
{
  "type": "parameterChanged",
  "version": 1,
  "id": "filter_cutoff",
  "normalizedValue": 0.73
}
```

Optional informational metadata may identify the source without changing parameter semantics.

### Rules

- The audio thread must not synchronously wait for delivery.
- Rapid UI updates may be coalesced when intermediate values are not required.
- UI update frequency may be throttled independently of host automation accuracy.
- The UI must reflect authoritative native state.

---

## 6. Web → C++ Parameter Commands

Example:

```json
{
  "type": "setParameter",
  "version": 1,
  "id": "filter_cutoff",
  "normalizedValue": 0.73
}
```

Native handling must:

1. Parse the message.
2. Validate the protocol version.
3. Validate the parameter ID.
4. Validate the value type.
5. Validate numeric finiteness.
6. Clamp or reject values according to the parameter definition.
7. Resolve the APVTS parameter.
8. Apply the change through the appropriate parameter API.
9. Allow the resulting authoritative state notification to update the UI.

The frontend must not assume every submitted value is accepted.

---

## 7. UI Initialization & State Synchronization

Recommended sequence:

```text
WebView Created
      │
      ▼
Bridge Ready
      │
      ▼
Request Initial State
      │
      ▼
Native State Snapshot
      │
      ▼
Frontend Applies Snapshot
      │
      ▼
UI Ready
```

Example request:

```json
{
  "type": "requestState",
  "version": 1
}
```

Example response:

```json
{
  "type": "stateSnapshot",
  "version": 1,
  "parameters": {
    "osc1_wavetable": 0.0,
    "osc1_unison": 0.0,
    "osc1_detune": 0.2,
    "filter_cutoff": 1.0,
    "filter_resonance": 0.067,
    "filter_drive": 0.0,
    "fx_distortion_mix": 0.0,
    "fx_delay_time": 0.25,
    "master_gain": 0.909
  }
}
```

Exact normalized values must be generated from authoritative parameter definitions rather than manually encoded.

---

## 8. Parameter Gesture Semantics

UI controls should distinguish gesture start, updates, and gesture end.

```json
{
  "type": "gesture",
  "version": 1,
  "id": "filter_cutoff",
  "state": "begin"
}
```

```json
{
  "type": "setParameter",
  "version": 1,
  "id": "filter_cutoff",
  "normalizedValue": 0.73
}
```

```json
{
  "type": "gesture",
  "version": 1,
  "id": "filter_cutoff",
  "state": "end"
}
```

Where supported, gesture boundaries should map to the plugin parameter APIs required for correct host automation and undo behavior.

The bridge must not generate excessive host automation events merely because a control renders at a high frame rate.

---

## 9. Host Automation & External Changes

The UI is not the authoritative owner of parameter values.

Changes caused by host automation, preset recall, MIDI, another UI instance, or native logic must flow through the native parameter system and then back to the UI.

This prevents visual controls from becoming desynchronized from the actual DSP parameter.

---

## 10. MIDI & Controller Binding

MIDI mappings must remain separate from UI bindings.

```text
MIDI Input
    │
    ▼
MIDI Mapping Layer
    │
    ▼
APVTS Parameter
    │
    ▼
UI State Update
```

Optional controller profiles may be provided, but Apollo must remain fully usable with mouse, keyboard, touch where supported, generic MIDI controllers, host automation, and direct UI interaction.

No physical controller layout is required by the bridge protocol.

---

## 11. Modulation Visualization

Base parameter values and modulation should remain conceptually separate:

```text
Base Parameter + Modulation Sources
                    │
                    ▼
             Effective DSP Value
```

The UI may display:

- Base value.
- Modulation amount.
- Effective/current value.
- Modulation source indicators.

The frontend must not overwrite the base APVTS value simply to visualize modulation.

Audio-rate or extremely high-frequency modulation should be represented by sampled/smoothed UI data rather than every DSP update.

---

## 12. Meters & High-Frequency Telemetry

Meters and visualizers use a separate telemetry path.

Possible telemetry includes:

- Output peak.
- RMS level.
- Voice activity.
- Oscilloscope waveform.
- Spectrum data.
- Envelope activity.
- LFO visualization.
- CPU/load indicators where appropriate.

Recommended flow:

```text
DSP Thread
    │
    ▼
Atomic / Lock-Free Snapshot
    │
    ▼
UI / Message Thread
    │
    ▼
WebView
```

UI telemetry should normally be sampled at a display-appropriate rate, approximately 30–120 Hz depending on the visualization.

The audio thread must never wait for the UI to consume telemetry.

---

## 13. Error & Validation Messages

Use structured errors.

```json
{
  "type": "error",
  "version": 1,
  "code": "INVALID_PARAMETER_VALUE",
  "message": "Parameter value was outside the permitted range."
}
```

Errors must not expose memory addresses, sensitive filesystem paths, internal object details, or other implementation-sensitive information.

Recoverable UI errors must remain isolated from the audio engine.

---

## 14. Security Boundary

The WebView is an untrusted input boundary.

Native code must validate:

- Message type.
- Protocol version.
- Parameter ID.
- Numeric values.
- Array/object sizes.
- String lengths.
- Enum/discrete values.
- Resource identifiers.
- Command permissions.

The native bridge should expose only the smallest API required by the frontend.

The frontend must not receive unrestricted access to filesystem APIs, shell/process execution, arbitrary native commands, or unnecessary network capabilities.

---

## 15. Protocol Versioning

Every structured bridge message should include a protocol version.

```json
{
  "type": "setParameter",
  "version": 1,
  "id": "master_gain",
  "normalizedValue": 0.9
}
```

Incompatible protocol changes must:

- Increment the protocol version.
- Define migration/compatibility behavior where practical.
- Reject unsupported versions cleanly.
- Never silently reinterpret a newer schema as an older one.

Parameter ID compatibility and bridge protocol compatibility are separate concerns.

---

## 16. Frontend State Architecture

React state should represent UI state and cached native state, not replace APVTS.

Recommended structure:

```text
Native / APVTS State
        │
        ▼
Binding Store
        │
        ▼
React State / Selectors
        │
        ▼
UI Components
```

Reusable controls should consume parameter metadata rather than hard-coded ranges.

Conceptual metadata:

```text
ParameterDefinition
├── id
├── name
├── type
├── normalizedValue
├── plainValue
├── min
├── max
├── unit
├── step
├── skew
└── capabilities
```

---

## 17. UI Component Contract

Every parameterized component should:

- Identify its parameter by stable ID.
- Display the authoritative cached value.
- Send normalized changes through the binding layer.
- Support gesture semantics.
- Respect discrete/continuous behavior.
- Display units and formatting from metadata.
- Handle disabled/bypassed parameters.
- Handle unavailable parameters gracefully.
- Avoid uncontrolled feedback loops.

Example:

```text
<ParameterKnob
    parameterId="filter_cutoff"
    label="Cutoff"
/>
```

The component should not need to know which DSP subsystem owns the parameter.

---

## 18. Preset & Project State Interaction

Preset operations occur outside the real-time callback.

```text
Preset File
    │
    ▼
Validation
    │
    ▼
State Migration
    │
    ▼
APVTS State
    │
    ├──────────────► DSP
    │
    └──────────────► UI Snapshot / Updates
```

The frontend must not independently reconstruct the complete synthesizer state.

Preset loading produces authoritative native state changes, which then propagate to the UI.

---

## 19. Performance Requirements

### Audio thread

Must not:

- Serialize JSON.
- Allocate frontend messages.
- Call JavaScript/WebView APIs.
- Wait for the WebView.
- Render UI.
- Perform synchronous logging.
- Access the filesystem.

### UI thread

May serialize bridge messages, render React components, format values, and update visualizations, but should still throttle unnecessary work.

### Background/native non-real-time threads

May prepare state snapshots, scan resources, perform preset operations, serialize larger messages, and prepare expensive visualization data.

---

## 20. Testing Requirements

### Parameter mapping

Test:

- Every registered parameter resolves correctly.
- Unknown IDs are rejected.
- Duplicate IDs are rejected.
- Ranges/defaults are correct.
- Discrete parameters preserve their step behavior.

### Protocol

Test:

- Valid messages.
- Malformed messages.
- Unsupported versions.
- Invalid numeric values.
- Oversized messages.
- Invalid IDs and enum values.

### Synchronization

Test:

- Initial UI state matches APVTS.
- Host automation updates the UI.
- Preset loading updates the UI.
- UI changes update APVTS.
- UI reload correctly resynchronizes.

### Thread safety

Verify:

- No WebView calls occur on the audio thread.
- No blocking bridge operation occurs during real-time processing.
- Telemetry exchange introduces no audio-thread locks.

### Integration

Test across supported plugin/standalone configurations and representative compatible hosts.

---

## 21. Initial Implementation Workflow

1. **Define the native parameter registry.**
2. **Expose parameter metadata** to the frontend.
3. **Implement initial state synchronization.**
4. **Implement validated parameter writes.**
5. **Implement asynchronous native-to-Web updates.**
6. **Implement gesture semantics** for automation and undo compatibility.
7. **Add MIDI Learn** independently of the UI bridge.
8. **Add telemetry** for meters and visualization.
9. **Add protocol versioning and validation.**
10. **Add automated integration and thread-safety tests.**

---

## 22. Architectural Non-Goals

The UI binding system must not become:

- A second DSP engine.
- A second parameter/state system.
- A DAW-specific automation layer.
- A controller-specific mapping layer.
- A direct C++ object exposure mechanism.
- A synchronous RPC system involving the audio callback.
- A mechanism for arbitrary native code execution.
- A requirement for one specific WebView implementation on every platform.

---

## 23. Definition of Done

The UI binding architecture is production-ready when:

- Every exposed parameter has a stable ID.
- Native parameter definitions are authoritative.
- Initial synchronization is deterministic.
- UI changes are validated before reaching APVTS.
- Host automation correctly propagates to the UI.
- Preset/state changes correctly propagate to the UI.
- MIDI mappings remain independent of UI implementation.
- High-frequency telemetry is separated from parameter updates.
- No WebView operation can block real-time DSP.
- Bridge messages are versioned and validated.
- Malformed input fails safely.
- The frontend cannot execute arbitrary native operations.
- The bridge is tested independently and in representative environments.
- The architecture remains portable across supported operating systems and hosts.

> **Final Engineering Directive:** Treat the C++ parameter/state system as the authoritative source of truth and the WebView as a presentation and interaction layer. Keep the bridge asynchronous, validated, versioned, host-agnostic, and completely isolated from real-time DSP execution.