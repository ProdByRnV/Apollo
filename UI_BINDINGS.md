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
| `fx_slot1` | Selector / Dropdown | Discrete `[0, 6]` | `0` | 8a | What occupies chain position 1; index into `dsp::EffectType`, 0 empty |
| `fx_slot2` | Selector / Dropdown | Discrete `[0, 6]` | `0` | 8a | Chain position 2 |
| `fx_slot3` | Selector / Dropdown | Discrete `[0, 6]` | `0` | 8a | Chain position 3 |
| `fx_slot4` | Selector / Dropdown | Discrete `[0, 6]` | `0` | 8a | Chain position 4 |
| `fx_slot5` | Selector / Dropdown | Discrete `[0, 6]` | `0` | 8a | Chain position 5 |
| `fx_slot6` | Selector / Dropdown | Discrete `[0, 6]` | `0` | 8a | Chain position 6. The range covers every effect Apollo will have, because a discrete range is permanent (ADR-0054) |
| `fx_distortion_bypass` | Segmented | Discrete `[0, 1]` | `0` | 8a | Switched out of circuit while keeping its position and its latency |
| `fx_distortion_mode` | Segmented | Discrete `[0, 2]` | `0` | 8a | Transfer curve; index into `dsp::Distortion::Mode` — soft, hard, diode |
| `fx_distortion_drive` | Rotary Knob | `0–36 dB` | `12` | 8a | Gain into the shaper; compensated, so it changes the tone rather than the level |
| `fx_distortion_tone` | Rotary Knob | `500–20000 Hz` | `20000` | 8a | Post lowpass on the wet path; switched out at the top of its range |
| `fx_distortion_mix` | Rotary Knob | Normalized `[0, 1]` | `0` | 2 | Distortion dry/wet mix; the dry path is delayed to match the wet path's latency |
| `fx_distortion_output` | Rotary Knob | `-24–12 dB` | `0` | 8a | Output trim on the wet path |
| `fx_delay_time` | Rotary Knob | `1–2000 ms` | `500` | 2 | Delay time. Inert until Phase 8b builds the delay |
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

### 10.2 Per-note expression (Phase 6b)

Polyphonic aftertouch and MPE need **no bridge protocol of their own**. They are
per-note quantities inside the engine, and everything the frontend can see or
change about them is already an ordinary parameter:

| Parameter | Meaning |
|---|---|
| `midi_bend_range` | Pitch-wheel range in semitones. Default 2. |
| `mpe_zone` | 0 off, 1 lower, 2 upper. Default off. |
| `mpe_members` | Member channels in the zone, 1-15. |
| `mpe_bend_range` | Per-note bend range in semitones. Default 48. |

All four are **not automatable**: they describe the controller on the desk
rather than the patch, and a pitch-bend range moving on an automation lane is a
bug being recorded. They are still registered parameters, so they are saved with
the project and reach the page through the normal metadata and snapshot path.

A controller may set the last three itself, over RPN — the MPE Configuration
Message and pitch-bend sensitivity. Those changes are applied *to the
parameters* rather than to the engine directly (ADR-0045), so the frontend sees
them arrive as ordinary `parameterChanged` messages and needs to do nothing
special. That is the whole reason for routing them that way.

### 10.3 Controller profiles (Phase 6c)

Two more intents, on the same principle: the frontend asks for a *named* profile
to be applied and cannot describe one.

| Message | Payload | Meaning |
|---|---|---|
| `requestControllerProfiles` | — | Send the built-in profiles. |
| `applyControllerProfile` | `profile`, optional `mode` | Fill the mapping table from one. `mode` is `"replace"` (the default) or `"merge"`; anything else is rejected. |

The profile identifier is resolved against the built-in registry **during
parsing**, so nothing past the protocol layer ever holds one that does not exist
(§14). Applying replies with the ordinary `midiMappings` message, whose `status`
reads `PROFILE_APPLIED` and whose `statusMessage` says how much of the profile
was applied and what it displaced — one code path for "the mapping state is now
this", as in §10.1.

```json
{
  "type": "controllerProfiles",
  "version": 1,
  "profiles": [
    {
      "id": "sound_controllers",
      "name": "Sound Controllers",
      "description": "MIDI CC 70-79, whose meanings the specification already fixes.",
      "assignments": 8
    }
  ]
}
```

The list is fixed at build time and never pushed unprompted.

---

### 10.4 Scope frames (Phase 7a, 7b)

Scope frames are a **broadcast**, not a conversation: the frontend asks for
nothing and acknowledges nothing, and a dropped frame costs one repaint. They
travel on their own timer at 30 Hz, separate from the parameter bridge's 30 Hz
*display* rate, because the two rates answer different questions and would
otherwise be coupled by accident.

```json
{
  "type": "scopeFrames",
  "version": 1,
  "scopes": [
    {
      "source": "output",
      "points": [0, 31, 62],
      "peak": 0.0812,
      "silent": false,
      "triggered": true
    }
  ]
}
```

- `source` is a stable token — `output`, `osc1`, `osc2`, `sub`, `noise`,
  `filter` — that the frontend keys its scopes on.
- `points` is the trace, oldest first, 192 values as **integer thousandths of
  full scale** — divide by 1000. A thousandth is below a pixel on any scope
  anyone will draw, and sending it as an integer rather than a rounded fraction
  is what keeps a frame at 5 KB instead of 18 (§12).
- `peak` is the largest absolute sample in the *window the frame came from*, not
  in the decimated points, so it is the real level rather than what survived
  decimation.
- `silent` is true when that peak is below the threshold. A stopped source must
  read as stopped rather than holding its last picture (CLAUDE.md §26.1).
- `triggered` is false when no zero crossing was found and the scope is
  free-running — worth knowing when a trace will not stand still.

**A source that is not captured is omitted from the array entirely.** "This build
does not capture that" is not "that part is quiet", and the frontend must not
draw them alike. All six are sent once capture has been running long enough to
fill a window; in the first frames after an editor opens, some or all may be
missing, and the page draws nothing for them rather than drawing a partial one.

**Where each source is tapped** matters when reading the numbers, because the six
are not all taken from the same place:

| Token | Taken |
|---|---|
| `osc1`, `osc2`, `sub`, `noise` | at the source, after its own level and balance, before the filter and the amplifier |
| `filter` | after the filter section, the envelope, velocity and any steal fade — the voice's finished contribution, before master gain |
| `output` | after master gain: what leaves the plugin |

So a source reads at a level the envelope has not touched, and closing the filter
takes `filter` and `output` down while leaving the four sources where they were.
That disagreement is the point: it is the difference between watching a source and
watching the mix.

Every source except `output` is the sum across the **whole voice pool**. What
oscillator 1 is producing is what every voice producing it is producing together,
which is the only reading that stays true when more than one note is held.

**The frame timer and the capture both follow the outbound handler.** A plugin
whose editor is closed builds nothing, serializes nothing and captures nothing:
the five source taps live inside the voice loop, so their cost rises with
polyphony, and an instance nobody is watching has no reason to pay it. Attaching
a handler also clears every ring, so the first frames a viewer sees can never be
audio left behind by the last one.

---

### 10.5 Instrument frames (Phase 7c)

The second broadcast, and deliberately not more of the first. It carries the
modulator traces, the output meter, the voice count and each oscillator's current
waveform — everything that moves at human speed rather than at the sample rate.

**It travels at half the scope rate**, 15 Hz. A scope is a moving picture and
reads as a slideshow below about twenty-five frames a second; a meter needle and
an envelope trace are perfectly legible at half that, and halving the rate of the
larger of the two messages is worth more than the tidiness of having one. Both
run off the same timer, so they can never drift apart.

```json
{
  "type": "instrumentFrame",
  "version": 1,
  "modulators": [
    { "source": "env1", "points": [0, 120, 240], "current": 0.82,
      "routed": true, "stage": 5 },
    { "source": "lfo1", "current": 0.0, "routed": false, "stage": 0 }
  ],
  "wavetables": [
    { "osc": 1, "points": [0, 49, 98], "position": 0.25, "table": 0 }
  ],
  "meter": { "active": true, "peak": [0.0512, 0.0498],
             "rms": [0.0311, 0.0305], "clipped": false },
  "voices": 1,
  "polyphony": 16
}
```

**Modulators.** `source` is a stable token — `env1`-`env4`, `lfo1`-`lfo4`. A
modulator nothing has traced is omitted entirely, exactly as an uncaptured scope
source is.

- `points` is one second of history, oldest first, 128 integer thousandths — one
  entry every **7.8 ms**. Anything faster than that, a 5 ms attack for instance,
  shows as a single step rather than a ramp: that is the resolution of the
  picture, stated rather than hidden behind interpolation that would invent the
  values in between.
- `points` is **absent when `routed` is false**, and that is not an omission. An
  LFO nothing reads is not advanced by the engine at all, so its trace is a flat
  line by construction; the page draws the zero line and the word "unrouted"
  rather than a straight trace that would look like a fault. On the default
  patch, which routes nothing, this is seven eighths of the message.
- `current` is the newest entry, sent as a number because the interface wants it
  beside the picture and re-deriving "the last point" in the page would be a
  second place to get the trace's orientation wrong.
- `stage` is `dsp::EnvelopeStage`'s numeric value — 0 idle, 1 delay, 2 attack,
  3 hold, 4 decay, 5 sustain, 6 release — and is meaningless for an LFO.

**The trace follows the most recently started sounding voice.** Envelopes and
LFOs are per voice, so a trace has to choose one: summing four envelopes
describes nothing and averaging would flatten what is being watched. The newest
note is the one whose envelope you are listening to while you adjust it.

**Wavetables** are keyed by `osc` rather than by array position, for the same
reason the scopes are keyed by token: entries drop out when there is nothing to
report, and a position would then silently mean a different oscillator.
`position` is the **effective** position — the parameter plus whatever the matrix
is adding — so the display sweeps when the oscillator does.

**The meter** reports linear amplitude, not decibels: the conversion is
presentation, and doing it natively would mean choosing a floor for silence that
the page would have to know about anyway. `peak` falls at 20 dB/s from an instant
attack; `rms` is averaged over 300 ms; `clipped` is held for a second and a half
after the last sample at or beyond full scale, so a clip nobody was watching for
is still seen. `active` is false until a block has been measured, which is not
the same as silence.

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

**As built** (Phases 7a-7c): every item on that list exists except spectrum
data, and all of it takes the flow above. Scope waveforms and modulator traces
travel through per-source lock-free rings; the meter, the voice count and the
wavetable positions are plain atomics the audio thread refreshes once a block.
Nothing is requested and nothing waits.

**Two rates rather than one**, because 30-120 Hz is a range and the range is
wide for good reason: scopes go at 30 Hz because a waveform below about 25 reads
as a slideshow, and everything else goes at 15 because a meter needle and an
envelope trace do not (§10.4, §10.5). Capture itself is armed only while an
editor has a handler attached, so an instance nobody is watching pays nothing.

**How numbers are encoded matters more than it looks.** Points travel as integer
thousandths of full scale on a single line. Sending them as JSON numbers is the
obvious thing and is three and a half times larger: `juce::JSON` spells a double
between 0.1 and 1 to sixteen decimal places, and pretty-prints each array element
on its own indented line. Measured, one scope frame was 18,395 bytes thirty times
a second; it is now 5,199, and an instrument frame 2,765. Both are asserted as
budgets in `Tests/Telemetry` and logged, so a change that multiplies them is
noticed there rather than in a profiler (ADR-0049).

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

**As built** (Phase 7d). The store is not React state and this is the whole
design: parameter values arrive at 30 Hz and a hundred and forty-three of them
change at once when a project loads, so a `useState` at the top of the tree would
re-render the page on every echo. `state/parameters.ts` keeps one listener set
**per parameter id**, read through `useSyncExternalStore`; a knob re-renders when
its own value moves and at no other time.

Three more stores follow the same shape with the granularity each frequency
deserves. MIDI mappings change a few times a minute and share one listener set.
Which knobs a live routing lights is published by the matrix and read by each
knob as a **boolean**, so a knob re-renders when its own answer flips rather than
whenever any slot moves. Telemetry frames do not go through React at all
(§10.4, §10.5): a frame is delivered by call to whoever subscribed to that
source, and the canvas is drawn imperatively.

The metadata shape above is the wire shape, typed in `bridge/protocol.ts` — so a
component that reads a field the native side does not send no longer compiles
(ADR-0050).

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
