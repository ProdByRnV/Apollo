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

Envelopes 2-4 and the four LFOs shipped in Phase 5d, with the modulation matrix that gave them destinations — which is the rule this table follows throughout: an identifier appears when the DSP behind it does something, not before.

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
| `filter1_type` | Selector / Dropdown | Discrete `[0, 4]` | `1` | 5b | Filter 1 mode: 0 off, 1 lowpass, 2 highpass, 3 bandpass, 4 notch |
| `filter1_cutoff` | Rotary Knob | `20–20000 Hz` | `20000` | 5b | Filter 1 cutoff frequency |
| `filter1_resonance` | Rotary Knob | Float `[0.1, 10.0]` | `0.707` | 5b | Filter 1 resonance as Q; 0.707 is Butterworth |
| `filter1_drive` | Rotary Knob | Normalized `[0, 1]` | `0` | 5b | Filter 1 input saturation |
| `filter2_type` | Selector / Dropdown | Discrete `[0, 4]` | `0` | 5b | Filter 2 mode; off by default |
| `filter2_cutoff` | Rotary Knob | `20–20000 Hz` | `20000` | 5b | Filter 2 cutoff frequency |
| `filter2_resonance` | Rotary Knob | Float `[0.1, 10.0]` | `0.707` | 5b | Filter 2 resonance as Q |
| `filter2_drive` | Rotary Knob | Normalized `[0, 1]` | `0` | 5b | Filter 2 input saturation |
| `filter_routing` | Selector / Dropdown | Discrete `[0, 1]` | `0` | 5b | 0 series, 1 parallel |
| `env2_delay` | Rotary Knob | `0–2000 ms` | `0` | 5d | Envelope 2 delay |
| `env2_attack` | Rotary Knob | `0–10000 ms` | `5` | 5d | Envelope 2 attack |
| `env2_hold` | Rotary Knob | `0–2000 ms` | `0` | 5d | Envelope 2 hold |
| `env2_decay` | Rotary Knob | `0–10000 ms` | `300` | 5d | Envelope 2 decay |
| `env2_sustain` | Rotary Knob | Normalized `[0, 1]` | `0` | 5d | Envelope 2 sustain; zero by default so an unrouted modulator does nothing |
| `env2_release` | Rotary Knob | `0–10000 ms` | `300` | 5d | Envelope 2 release |
| `env2_curve` | Rotary Knob | Bipolar `[-1, 1]` | `0.5` | 5d | Envelope 2 curve tension |
| `env3_delay` | Rotary Knob | `0–2000 ms` | `0` | 5d | Envelope 3 delay |
| `env3_attack` | Rotary Knob | `0–10000 ms` | `5` | 5d | Envelope 3 attack |
| `env3_hold` | Rotary Knob | `0–2000 ms` | `0` | 5d | Envelope 3 hold |
| `env3_decay` | Rotary Knob | `0–10000 ms` | `300` | 5d | Envelope 3 decay |
| `env3_sustain` | Rotary Knob | Normalized `[0, 1]` | `0` | 5d | Envelope 3 sustain; zero by default so an unrouted modulator does nothing |
| `env3_release` | Rotary Knob | `0–10000 ms` | `300` | 5d | Envelope 3 release |
| `env3_curve` | Rotary Knob | Bipolar `[-1, 1]` | `0.5` | 5d | Envelope 3 curve tension |
| `env4_delay` | Rotary Knob | `0–2000 ms` | `0` | 5d | Envelope 4 delay |
| `env4_attack` | Rotary Knob | `0–10000 ms` | `5` | 5d | Envelope 4 attack |
| `env4_hold` | Rotary Knob | `0–2000 ms` | `0` | 5d | Envelope 4 hold |
| `env4_decay` | Rotary Knob | `0–10000 ms` | `300` | 5d | Envelope 4 decay |
| `env4_sustain` | Rotary Knob | Normalized `[0, 1]` | `0` | 5d | Envelope 4 sustain; zero by default so an unrouted modulator does nothing |
| `env4_release` | Rotary Knob | `0–10000 ms` | `300` | 5d | Envelope 4 release |
| `env4_curve` | Rotary Knob | Bipolar `[-1, 1]` | `0.5` | 5d | Envelope 4 curve tension |
| `lfo1_shape` | Selector / Dropdown | Discrete `[0, 6]` | `0` | 5d | LFO 1: 0 sine, 1 triangle, 2 saw, 3 reverse saw, 4 square, 5 sample & hold, 6 step |
| `lfo1_rate` | Rotary Knob | `0.01–400 Hz` | `1` | 5d | LFO 1 rate |
| `lfo1_phase` | Rotary Knob | Normalized `[0, 1]` | `0` | 5d | LFO 1 start phase |
| `lfo1_retrigger` | Selector / Dropdown | Discrete `[0, 1]` | `1` | 5d | LFO 1: 0 free-running, 1 retrigger per note |
| `lfo1_fade` | Rotary Knob | `0–10000 ms` | `0` | 5d | LFO 1 fade-in |
| `lfo1_smoothing` | Rotary Knob | Normalized `[0, 1]` | `0` | 5d | LFO 1 output slew |
| `lfo1_polarity` | Selector / Dropdown | Discrete `[0, 1]` | `1` | 5d | LFO 1: 0 unipolar, 1 bipolar |
| `lfo1_steps` | Rotary Knob | Integer `[2, 32]` | `8` | 5d | LFO 1 steps, for the step shape |
| `lfo2_shape` | Selector / Dropdown | Discrete `[0, 6]` | `0` | 5d | LFO 2: 0 sine, 1 triangle, 2 saw, 3 reverse saw, 4 square, 5 sample & hold, 6 step |
| `lfo2_rate` | Rotary Knob | `0.01–400 Hz` | `1` | 5d | LFO 2 rate |
| `lfo2_phase` | Rotary Knob | Normalized `[0, 1]` | `0` | 5d | LFO 2 start phase |
| `lfo2_retrigger` | Selector / Dropdown | Discrete `[0, 1]` | `1` | 5d | LFO 2: 0 free-running, 1 retrigger per note |
| `lfo2_fade` | Rotary Knob | `0–10000 ms` | `0` | 5d | LFO 2 fade-in |
| `lfo2_smoothing` | Rotary Knob | Normalized `[0, 1]` | `0` | 5d | LFO 2 output slew |
| `lfo2_polarity` | Selector / Dropdown | Discrete `[0, 1]` | `1` | 5d | LFO 2: 0 unipolar, 1 bipolar |
| `lfo2_steps` | Rotary Knob | Integer `[2, 32]` | `8` | 5d | LFO 2 steps, for the step shape |
| `lfo3_shape` | Selector / Dropdown | Discrete `[0, 6]` | `0` | 5d | LFO 3: 0 sine, 1 triangle, 2 saw, 3 reverse saw, 4 square, 5 sample & hold, 6 step |
| `lfo3_rate` | Rotary Knob | `0.01–400 Hz` | `1` | 5d | LFO 3 rate |
| `lfo3_phase` | Rotary Knob | Normalized `[0, 1]` | `0` | 5d | LFO 3 start phase |
| `lfo3_retrigger` | Selector / Dropdown | Discrete `[0, 1]` | `1` | 5d | LFO 3: 0 free-running, 1 retrigger per note |
| `lfo3_fade` | Rotary Knob | `0–10000 ms` | `0` | 5d | LFO 3 fade-in |
| `lfo3_smoothing` | Rotary Knob | Normalized `[0, 1]` | `0` | 5d | LFO 3 output slew |
| `lfo3_polarity` | Selector / Dropdown | Discrete `[0, 1]` | `1` | 5d | LFO 3: 0 unipolar, 1 bipolar |
| `lfo3_steps` | Rotary Knob | Integer `[2, 32]` | `8` | 5d | LFO 3 steps, for the step shape |
| `lfo4_shape` | Selector / Dropdown | Discrete `[0, 6]` | `0` | 5d | LFO 4: 0 sine, 1 triangle, 2 saw, 3 reverse saw, 4 square, 5 sample & hold, 6 step |
| `lfo4_rate` | Rotary Knob | `0.01–400 Hz` | `1` | 5d | LFO 4 rate |
| `lfo4_phase` | Rotary Knob | Normalized `[0, 1]` | `0` | 5d | LFO 4 start phase |
| `lfo4_retrigger` | Selector / Dropdown | Discrete `[0, 1]` | `1` | 5d | LFO 4: 0 free-running, 1 retrigger per note |
| `lfo4_fade` | Rotary Knob | `0–10000 ms` | `0` | 5d | LFO 4 fade-in |
| `lfo4_smoothing` | Rotary Knob | Normalized `[0, 1]` | `0` | 5d | LFO 4 output slew |
| `lfo4_polarity` | Selector / Dropdown | Discrete `[0, 1]` | `1` | 5d | LFO 4: 0 unipolar, 1 bipolar |
| `lfo4_steps` | Rotary Knob | Integer `[2, 32]` | `8` | 5d | LFO 4 steps, for the step shape |
| `mod01_source` | Selector / Dropdown | Discrete `[0, 14]` | `0` | 5d | Slot 1 source; index into `dsp::ModSource` |
| `mod01_destination` | Selector / Dropdown | Discrete `[0, 16]` | `0` | 5d | Slot 1 destination; index into `dsp::ModDestination` |
| `mod01_depth` | Rotary Knob | Bipolar `[-1, 1]` | `0` | 5d | Slot 1 depth; negative inverts the source |
| `mod02_source` | Selector / Dropdown | Discrete `[0, 14]` | `0` | 5d | Slot 2 source; index into `dsp::ModSource` |
| `mod02_destination` | Selector / Dropdown | Discrete `[0, 16]` | `0` | 5d | Slot 2 destination; index into `dsp::ModDestination` |
| `mod02_depth` | Rotary Knob | Bipolar `[-1, 1]` | `0` | 5d | Slot 2 depth; negative inverts the source |
| `mod03_source` | Selector / Dropdown | Discrete `[0, 14]` | `0` | 5d | Slot 3 source; index into `dsp::ModSource` |
| `mod03_destination` | Selector / Dropdown | Discrete `[0, 16]` | `0` | 5d | Slot 3 destination; index into `dsp::ModDestination` |
| `mod03_depth` | Rotary Knob | Bipolar `[-1, 1]` | `0` | 5d | Slot 3 depth; negative inverts the source |
| `mod04_source` | Selector / Dropdown | Discrete `[0, 14]` | `0` | 5d | Slot 4 source; index into `dsp::ModSource` |
| `mod04_destination` | Selector / Dropdown | Discrete `[0, 16]` | `0` | 5d | Slot 4 destination; index into `dsp::ModDestination` |
| `mod04_depth` | Rotary Knob | Bipolar `[-1, 1]` | `0` | 5d | Slot 4 depth; negative inverts the source |
| `mod05_source` | Selector / Dropdown | Discrete `[0, 14]` | `0` | 5d | Slot 5 source; index into `dsp::ModSource` |
| `mod05_destination` | Selector / Dropdown | Discrete `[0, 16]` | `0` | 5d | Slot 5 destination; index into `dsp::ModDestination` |
| `mod05_depth` | Rotary Knob | Bipolar `[-1, 1]` | `0` | 5d | Slot 5 depth; negative inverts the source |
| `mod06_source` | Selector / Dropdown | Discrete `[0, 14]` | `0` | 5d | Slot 6 source; index into `dsp::ModSource` |
| `mod06_destination` | Selector / Dropdown | Discrete `[0, 16]` | `0` | 5d | Slot 6 destination; index into `dsp::ModDestination` |
| `mod06_depth` | Rotary Knob | Bipolar `[-1, 1]` | `0` | 5d | Slot 6 depth; negative inverts the source |
| `mod07_source` | Selector / Dropdown | Discrete `[0, 14]` | `0` | 5d | Slot 7 source; index into `dsp::ModSource` |
| `mod07_destination` | Selector / Dropdown | Discrete `[0, 16]` | `0` | 5d | Slot 7 destination; index into `dsp::ModDestination` |
| `mod07_depth` | Rotary Knob | Bipolar `[-1, 1]` | `0` | 5d | Slot 7 depth; negative inverts the source |
| `mod08_source` | Selector / Dropdown | Discrete `[0, 14]` | `0` | 5d | Slot 8 source; index into `dsp::ModSource` |
| `mod08_destination` | Selector / Dropdown | Discrete `[0, 16]` | `0` | 5d | Slot 8 destination; index into `dsp::ModDestination` |
| `mod08_depth` | Rotary Knob | Bipolar `[-1, 1]` | `0` | 5d | Slot 8 depth; negative inverts the source |
| `mod09_source` | Selector / Dropdown | Discrete `[0, 14]` | `0` | 5d | Slot 9 source; index into `dsp::ModSource` |
| `mod09_destination` | Selector / Dropdown | Discrete `[0, 16]` | `0` | 5d | Slot 9 destination; index into `dsp::ModDestination` |
| `mod09_depth` | Rotary Knob | Bipolar `[-1, 1]` | `0` | 5d | Slot 9 depth; negative inverts the source |
| `mod10_source` | Selector / Dropdown | Discrete `[0, 14]` | `0` | 5d | Slot 10 source; index into `dsp::ModSource` |
| `mod10_destination` | Selector / Dropdown | Discrete `[0, 16]` | `0` | 5d | Slot 10 destination; index into `dsp::ModDestination` |
| `mod10_depth` | Rotary Knob | Bipolar `[-1, 1]` | `0` | 5d | Slot 10 depth; negative inverts the source |
| `mod11_source` | Selector / Dropdown | Discrete `[0, 14]` | `0` | 5d | Slot 11 source; index into `dsp::ModSource` |
| `mod11_destination` | Selector / Dropdown | Discrete `[0, 16]` | `0` | 5d | Slot 11 destination; index into `dsp::ModDestination` |
| `mod11_depth` | Rotary Knob | Bipolar `[-1, 1]` | `0` | 5d | Slot 11 depth; negative inverts the source |
| `mod12_source` | Selector / Dropdown | Discrete `[0, 14]` | `0` | 5d | Slot 12 source; index into `dsp::ModSource` |
| `mod12_destination` | Selector / Dropdown | Discrete `[0, 16]` | `0` | 5d | Slot 12 destination; index into `dsp::ModDestination` |
| `mod12_depth` | Rotary Knob | Bipolar `[-1, 1]` | `0` | 5d | Slot 12 depth; negative inverts the source |
| `mod13_source` | Selector / Dropdown | Discrete `[0, 14]` | `0` | 5d | Slot 13 source; index into `dsp::ModSource` |
| `mod13_destination` | Selector / Dropdown | Discrete `[0, 16]` | `0` | 5d | Slot 13 destination; index into `dsp::ModDestination` |
| `mod13_depth` | Rotary Knob | Bipolar `[-1, 1]` | `0` | 5d | Slot 13 depth; negative inverts the source |
| `mod14_source` | Selector / Dropdown | Discrete `[0, 14]` | `0` | 5d | Slot 14 source; index into `dsp::ModSource` |
| `mod14_destination` | Selector / Dropdown | Discrete `[0, 16]` | `0` | 5d | Slot 14 destination; index into `dsp::ModDestination` |
| `mod14_depth` | Rotary Knob | Bipolar `[-1, 1]` | `0` | 5d | Slot 14 depth; negative inverts the source |
| `mod15_source` | Selector / Dropdown | Discrete `[0, 14]` | `0` | 5d | Slot 15 source; index into `dsp::ModSource` |
| `mod15_destination` | Selector / Dropdown | Discrete `[0, 16]` | `0` | 5d | Slot 15 destination; index into `dsp::ModDestination` |
| `mod15_depth` | Rotary Knob | Bipolar `[-1, 1]` | `0` | 5d | Slot 15 depth; negative inverts the source |
| `mod16_source` | Selector / Dropdown | Discrete `[0, 14]` | `0` | 5d | Slot 16 source; index into `dsp::ModSource` |
| `mod16_destination` | Selector / Dropdown | Discrete `[0, 16]` | `0` | 5d | Slot 16 destination; index into `dsp::ModDestination` |
| `mod16_depth` | Rotary Knob | Bipolar `[-1, 1]` | `0` | 5d | Slot 16 depth; negative inverts the source |
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
  "id": "filter1_cutoff",
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
  "id": "filter1_cutoff",
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
    "filter1_cutoff": 1.0,
    "filter1_resonance": 0.067,
    "filter1_drive": 0.0,
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
  "id": "filter1_cutoff",
  "state": "begin"
}
```

```json
{
  "type": "setParameter",
  "version": 1,
  "id": "filter1_cutoff",
  "normalizedValue": 0.73
}
```

```json
{
  "type": "gesture",
  "version": 1,
  "id": "filter1_cutoff",
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

### 10.1 Implemented protocol (Phase 6)

The bridge exposes MIDI Learn as **intents**, never as edits to the mapping
table. The frontend can ask for a control to be learned, cancelled or released;
it cannot construct a mapping, name a controller number, or set a scaling range.
That keeps the untrusted boundary exactly where §14 puts it: everything the page
can express is a parameter ID it already knows.

**Web → C++**

| Message | Payload | Meaning |
|---|---|---|
| `requestMidiMappings` | — | Send the current mapping state. |
| `midiLearnBegin` | `id` | Arm learn; the next assignable control moved is assigned to this parameter. |
| `midiLearnCancel` | — | Disarm without assigning. |
| `midiMappingRemove` | `id` | Release whatever drives this parameter. |
| `midiMappingClearAll` | — | Release every mapping. |

**C++ → Web** — every one of the five is answered with the same message, so the
frontend has one code path for "the mapping state is now this" and cannot render
a list its own request invalidated. It is also *pushed* unprompted, because a
learn completes when the user moves a physical control rather than when the page
sends anything.

```json
{
  "type": "midiMappings",
  "version": 1,
  "mappings": [
    { "id": "filter1_cutoff", "controller": 74, "channel": 0, "min": 0.0, "max": 1.0 }
  ],
  "learning": "",
  "status": "ADDED",
  "statusMessage": "Control assigned.",
  "capacity": 64
}
```

- `channel` is `0` for a mapping that answers on any channel, or 1-16.
- `learning` is the parameter learn is armed for, or an **empty string** — a
  state the UI must render, and an absent key is easier to mishandle than a
  present one.
- `status` is a stable token (`ADDED`, `REPLACED_CONTROLLER_MAPPING`,
  `REJECTED_RESERVED_CONTROLLER`, …) that the frontend may branch on;
  `statusMessage` is the displayable form. Both describe the most recent
  assignment, so a replacement is reported rather than discovered later.

Mappings are carried inside the same state document as the parameters
(ADR-0043), so a project load replaces them and the UI must resynchronise both.

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
    parameterId="filter1_cutoff"
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