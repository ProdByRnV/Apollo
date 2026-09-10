# Apollo — Project State

> **Purpose:** the authoritative, verified record of what actually exists in this
> repository. The specification documents describe what Apollo *should* become;
> this file describes what it *is*. Nothing is recorded as complete here unless it
> was built and run.
>
> Update this file at the end of every roadmap step.

**Last verified:** 2026-09-10
**Apollo version:** 0.1.0

---

## 1. Current position

| | |
|---|---|
| **Phase** | Phase 6 — MIDI, Control & Interaction |
| **Status** | **6a complete** (MIDI Learn). 6b (per-note expression) and 6c (controller profiles) remain |
| **Milestone** | M6 — Control |
| **Next step** | Phase 6b — polyphonic aftertouch and MPE |

**Apollo is a wavetable synthesizer.** Two band-limited wavetable oscillators,
each with up to 16 detuned and stereo-spread unison voices, plus a sine sub and
a stereo noise generator, mixed with per-source level and balance and played
polyphonically through the VST3 and standalone builds. Aliasing is measured, not
asserted: worst case -98.5 dBc against a -60 dBc budget.

Voices are now shaped by a real DAHDSR envelope with adjustable curve tension,
not the linear placeholder.

The instrument now has a real interface. The 139-slider placeholder page was
replaced with a signal-flow layout of rotary controls, segmented switches and a
sixteen-slot matrix, built from real files under `Source/UI/Web` and embedded at
build time (ADR-0038, ADR-0039, ADR-0040). This is Phase 7 work pulled forward
because the instrument had become unusable to audition by hand; it does not
close Phase 7, which still owns the React migration and every visualizer.

Still placeholders: the four built-in wavetables are mathematically defined
morphs rather than designed factory content (Phase 9). There are no
oscilloscopes yet — per-source scopes are a Phase 7 requirement (PRD §30.1) and
need a lock-free capture buffer per source, which does not exist.

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
| `apollo_core` | STATIC, JUCE-free | The whole synthesis engine — wavetables, oscillators, unison, noise, voices — plus parameter ID conventions, the parameter registry and the version header. Strict warnings. |
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
  detects preset or project loads and resynchronises wholesale, and serves the
  frontend from a fixed table of path, resource name and MIME type; any other
  path is refused rather than mapped onto the filesystem.

### Frontend (Phase 7, brought forward)

- `Source/UI/Web/{index.html, apollo.css, apollo.js}`, compiled into the binary
  by `juce_add_binary_data` (ADR-0038). Editing the CSS relinks the plugin.
- Controls are built **from the parameter metadata alone** — ranges, defaults,
  steps and skews are never restated in the page (UI_BINDINGS.md §16). Discrete
  *labels* are presentation and do live in the page, applied only when the label
  count matches the range the engine reported.
- Rotary knobs with 270° travel, shift for fine, double-click or Delete to
  reset, and full keyboard control; segmented switches for short enumerations;
  a two-bank table for the sixteen matrix slots with bipolar depth rails.
- Laid out by signal flow, with envelopes and LFOs behind tab strips, and an
  "Unassigned" module that catches any parameter the layout forgot (ADR-0040).
- Accessibility: `role="slider"` with live `aria-valuenow`/`aria-valuetext`,
  `radiogroup` semantics on switches, visible focus, and no state signalled by
  colour alone (ADR-0039, CLAUDE.md §39).
- No framework and no network: the page has no dependencies and loads nothing
  over the wire (CLAUDE.md §40). The React migration remains Phase 7's.
- **Not yet present:** oscilloscopes and any other visualizer.

### Synthesis engine (Phase 3)

- `VoiceEngine` owns a fixed pool of 32 `Voice` objects by value. Nothing in the
  audio path allocates, and no voice is created or destroyed while audio runs;
  polyphony (1-32, default 16) only selects how many are eligible.
- The engine is **JUCE-free** and renders into raw float buffers, so it is held
  to Apollo's strict warning set and is testable with no host, device or message
  loop (ADR-0018).
- Voice allocation: free voice first, else the oldest releasing voice, else the
  oldest overall, with ties resolved to the lowest index — deterministic by
  construction.
- Stealing fades the old note over 2 ms and starts the new one only at silence,
  so the transition contains no step (ADR-0019).
- Note-on/off with velocity, velocity-zero treated as note-off, sustain pedal,
  pitch bend (±2 semitones), all-notes-off and all-sound-off.
- MIDI is applied **sample-accurately**: the processor renders the block in
  segments between events rather than quantising them to block boundaries.
- Gain staging measured rather than assumed, including per-voice start phases to
  stop chord attacks summing coherently (ADR-0017).
- Master gain is smoothed multiplicatively over 20 ms.

### Wavetable oscillator engine (Phase 4a)

- `Wavetable` stores each waveform as an **11-level mipmap**, each level
  band-limited to half as many harmonics as the last. The oscillator selects the
  most detailed level whose harmonics all stay below Nyquist, once per note
  rather than per sample, so aliasing is removed at the source.
- Levels are generated **additively** — only the harmonics a level may keep are
  ever synthesised — so band-limiting is exact by construction, with no filter
  design and no ringing (ADR-0020).
- Coarse levels keep a 512-sample floor, and all levels of a frame share one
  normalisation taken from the loudest level. Both were reversed from the obvious
  implementation after measurement (ADR-0021).
- Interpolation is 4-point cubic Hermite with wrapped neighbours, across samples,
  and linear across frames for scanning.
- Four built-in morph tables of 16 frames: sine→saw, sine→square, triangle→saw,
  saw→square. Placeholder factory content.
- Phase is wrapped into range **before** it is scaled to a table index, and
  non-finite phase is rejected — a fix for undefined behaviour that MSVC hid and
  only the Linux/Clang sanitizer job caught (ADR-0022).

### Source section (Phase 4b)

- **Two primary oscillators**, structurally identical. Oscillator 2 adds coarse
  (±24 semitones) and fine (±100 cents) tuning and is silent by default, so
  adding it changed no existing patch.
- **Unison**, up to 16 voices per oscillator, symmetrically detuned in cents
  (±50 at full) and distributed across the stereo field. The layout is derived
  once per block by the engine and shared by every voice, rather than recomputed
  per voice (ADR-0023).
- **Per-oscillator level and stereo balance**, smoothed per sample over 20 ms so
  automation cannot step. The unison spread is anchored at unity in the centre,
  which is what keeps the default patch at exactly the level the Phase 3 gain
  staging was measured for — verified as 0.0800, unchanged (ADR-0024).
- **Sub oscillator**: a sine, one or two octaves below the note. It cannot alias
  at any pitch or transposition.
- **Stereo noise generator**: independent xorshift streams per channel, seeded
  per voice so simultaneous voices produce independent noise rather than N copies
  of one stream.
- A source at exactly zero level, and not still ramping down to it, is skipped
  rather than rendered and multiplied by nothing — which is why the default patch
  costs what it did before the other three sources existed.
- The voice's whole source section is set by **one struct**, compared wholesale,
  so a parameter cannot silently fail to reach the engine (ADR-0026).
- `VoiceEngine` is deliberately non-copyable and non-movable: voices hold
  pointers into it.

(The amplitude envelope was a linear placeholder here; Phase 5a replaced it.)

---

### Oversampling (Phase 4c)

- A linear-phase polyphase halfband oversampler, 2x and 4x, plus a genuine
  pass-through setting so a quality control can switch it off without the
  surrounding code branching. Apollo's own rather than JUCE's, so it can live in
  the JUCE-free `apollo_core` under the strict warning set (ADR-0028).
- Measured: passband flat to 20 kHz within 0.0001 dB, stopband -100.7 dB, and a
  hard clipper's worst non-harmonic content improving -35.8 → -45.3 → -62.1 dBc.
- Latency is whole samples by construction — 39 at 2x, 59 at 4x — so a dry path
  can be aligned exactly. The two cascaded stages use different tap counts
  purely to make that number whole.
- **Nothing uses it yet.** It exists ahead of the nonlinear stages that need it.
  `Docs/OVERSAMPLING.md` records where it will be applied and, more usefully,
  where it deliberately will not be — including the oscillators, which are
  band-limited at the source and gain nothing from it.

### Amplitude envelope (Phase 5a)

- A **DAHDSR** envelope generator: delay, attack, hold, decay, sustain, release,
  with one curve-tension control spanning linear through analog-like shapes.
- Segments are shaped incrementally — one multiply per sample, one `exp()` per
  segment — and land on their endpoints **exactly**, so the attack reaches full
  scale, the sustain sits where it was set, and the release reaches true silence
  rather than a denormal tail the voice has to threshold away (ADR-0030).
- Stage durations are exact and sample-rate independent: measured within one
  sample at 48 kHz, and within 0.02 ms of the requested 20 ms across
  44.1–192 kHz.
- Zero-length stages are skipped rather than costing a sample each, so an
  all-zero envelope arrives at its sustain level with no samples elapsed.
- A time changed mid-segment retimes that segment from wherever the envelope is,
  preserving the fraction already travelled, so a knob moved during a held note
  is audible immediately.
- The **steal fade is now separate** from the envelope and freezes it while it
  runs (ADR-0031). It had to be: stealing must free a voice in about two
  milliseconds whatever the patch says, and the release is now a user control
  reaching ten seconds.
- The host-reported tail length tracks `env1_release` instead of a constant, so
  an offline render of a long-release patch is no longer truncated.
- Only **envelope 1** is registered as parameters. Envelopes 2-4 have nowhere to
  send their output until the modulation matrix, so their IDs ship with it.

### Filters (Phase 5b)

- **Two state-variable filters per voice**, stereo, in a **topology-preserving**
  form rather than a biquad. That choice is the reason the cutoff can be swept as
  fast as anything can sweep it without the filter misbehaving — a biquad's
  feedback path assumes coefficients it may no longer be using.
- Modes: off, lowpass, highpass, bandpass, notch, all from the same two state
  variables. "Off" is a mode rather than a separate switch, so nothing can
  disagree about whether a filter is running.
- Routing: **series or parallel**, with the parallel sum halved so switching
  routing does not change loudness by 6 dB.
- Measured, not assumed: the cutoff lands at **-3.01 dB** at 100 Hz, 500 Hz,
  2 kHz, 8 kHz and 15 kHz, and at 44.1, 48, 96 and 192 kHz. Resonance peaks land
  on 20·log₁₀(Q) — 0 dB at Q 1, +6.02 at Q 2, +20.00 at Q 10. The notch nulls to
  -120 dB.
- Coefficients are resolved **once per block by the engine** and shared by all
  voices, the same pattern as the unison layout: 2 tangents per block rather than
  128 (ADR-0023's reasoning, applied again).
- **Filter drive** saturates the input, and its range is small because its
  aliasing was measured rather than assumed. -146 dBc at zero, about -45 dBc at
  full. It is the one place Apollo's -60 dBc budget is not met, and ADR-0033
  records why that is the honest answer rather than a wider budget or a
  per-voice oversampler.
- The three un-indexed `filter_*` parameters became `filter1_*`, joined by
  `filter2_*` and `filter_routing`. That is a **state migration**, schema version
  1 to 2, and the first real use of a code path built in Phase 2 and never
  exercised (ADR-0032).

### LFO generator (Phase 5c)

- Seven shapes — sine, triangle, saw, reverse saw, square, sample & hold and a
  stepped staircase — plus rate, phase offset, retrigger or free-running,
  fade-in, output smoothing and a polarity switch.
- **Not band-limited, and that is the point.** An LFO is never heard, so a square
  is exactly ±1 with no intermediate values at all, which the tests assert. The
  smoothing control exists for when those edges do need rounding, because it can
  be adjusted and switched off; band-limiting would round them always (ADR-0034).
- **Audio rate**, decided on a measurement: a block-rate LFO on a filter cutoff is
  the zipper artefact the phase's exit criteria forbid, and 128 instances cost
  1.7–4.4 % of a core.
- Measured: rates land on 0.500, 1.000, 5.000 and 20.000 Hz exactly, and stay
  there from 44.1 to 192 kHz. Smoothing at 0.5 reduces the largest sample-to-
  sample step of a square from 2.0 to 0.000333.
- Free-running LFOs adopt a phase handed in by the caller, so voices started at
  different times stay in step. Sample & hold is seeded per instance, so several
  voices produce independent random values rather than one value applied N times.
- `tempoSyncedRateHz` converts a host tempo and a note division into hertz, and
  falls back to 120 bpm when the host reports nothing usable (CLAUDE.md §38).
- **Nothing instantiates one yet.** Four LFOs per voice, and the host-tempo
  plumbing that feeds a sync division, arrive with the modulation matrix that
  gives them destinations — the same reason envelopes 2-4 are not registered.

### Modulation matrix (Phase 5d)

- **Sixteen routing slots**, each a source, a destination and a bipolar depth, in
  the generic model CLAUDE.md §15 asks for: nothing in the engine knows which
  pairings are musically sensible, because a matrix that only allows the
  combinations someone thought of is a list of features.
- **Fifteen sources**: four envelopes, four LFOs, velocity, key tracking, mod
  wheel, pitch bend, aftertouch, and a random value chosen once per note.
- **Seventeen destinations**: pitch (all three sources together, or either
  oscillator alone), wavetable position, level, pan, sub and noise level, both
  filters' cutoff and resonance, and voice amplitude. They are engine concepts
  rather than parameter IDs — oscillator 1's pitch, the most obvious target of
  all, has no parameter behind it.
- Depth is scaled by each destination's own range, so half a depth feels like
  half whether it lands on a cutoff measured in octaves or a pan measured in
  fractions.
- Contributions to one destination **add**, and are clamped by the consumer
  rather than centrally: a level cannot go below zero, a pitch can go anywhere,
  and amplitude attenuates but never boosts, which is what keeps the headroom
  guarantee intact (ADR-0036).
- Evaluated every **16 samples** — a 3 kHz rate — rather than per sample, because
  a modulated cutoff costs a `tan` and a modulated pitch a `pow`. Measured
  against the zipper criterion rather than argued: a resonant LFO sweep's largest
  sample step is 0.002167 against 0.001292 unmodulated (ADR-0035).
- **Base parameters are never written to.** Modulation is an offset applied on
  the way to the consumer, asserted directly: after a heavily modulated note
  every base value reads back exactly as set, and removing the routing returns
  the engine to within 1e-6 of one that never had it.
- Free-running LFO phases belong to the *engine*, not to a note, so voices
  started at different times move together.
- A voice with no active slot skips evaluation entirely and only advances the
  generators something actually routes, which is why an unmodulated patch costs
  what it did in Phase 5b.
- Envelopes 2-4 and the four LFOs became reachable here; mod wheel (CC 1) and
  channel aftertouch are now read from MIDI as first-class sources.

### MIDI Learn (Phase 6a)

- **Any registered parameter can be driven by any MIDI control-change number**,
  learned from whatever the user physically moves. Nothing anywhere names a
  controller, a manufacturer or a layout (CLAUDE.md §46).
- The model — the table, the conflict policy, the value scaling — is **JUCE-free**
  and lives in `apollo_core`, so what happens when a control is learned twice,
  when the table fills, or when a scaling end arrives as infinity is all testable
  with no host, no device and no message loop.
- **The conflict policy is a bijection**: one control drives one parameter, one
  parameter has one control, and every replacement is *reported* rather than
  performed silently. A control that should move many things at once is a macro,
  and Apollo already has sixteen routing slots with bipolar depths for that
  (ADR-0041).
- **Learned mappings are omni-channel.** A channel-specific mapping still exists
  and beats an omni one on the same controller number, but it is reached through
  the explicit assign path, not by pointing at a knob.
- **Two controller groups are refused**: CC 64, because Apollo acts on the
  sustain pedal directly, and CC 120-127, which are commands rather than
  controls. CC 1 is deliberately *not* refused — the mod wheel is a modulation
  source and a legitimate mapping target, and a user who asks for both gets both.
- **The audio thread never writes a parameter.** It scans a preallocated table
  and stores one atomic value plus a flag; a 60 Hz message-thread timer applies
  those to APVTS with automation gestures bracketed around each move (ADR-0042).
  The test that proves it renders a whole 128-step sweep and asserts the
  parameter has not moved, then flushes once and finds it at the newest value.
- The table reaches the audio thread through a single-producer ring of **whole
  tables**, so there is no window in which a half-applied change can be observed,
  and a producer that outruns the ring is refused rather than raced.
- **Mappings are stored in the state tree** as a `<MIDIMAP>` child, written after
  every edit rather than at save time, and needed **no schema bump** — a document
  written before this phase simply has no such child (ADR-0043). Entries are
  stored by parameter ID, and one naming a parameter this build does not have is
  dropped rather than failing the load.
- The interface exposes it as a **mode**, not a hidden right-click: a MIDI Learn
  button in the masthead, after which clicking any control assigns it. Assigned
  controls carry a monospace `CC 74` badge and a tooltip naming the channel;
  Delete releases one, Escape cancels, and the state is legible in greyscale
  (CLAUDE.md §39).
- **Not yet present:** polyphonic aftertouch and MPE (6b), controller profiles
  (6c), 14-bit high-resolution CC, relative/endless encoders, and pickup
  ("takeover") mode. None is required for a generic controller to work.

## 3. What is NOT implemented

| Phase | Absent |
|---|---|
| 6 | Controller profiles (6c), polyphonic aftertouch and MPE (6b). Note, velocity, pitch bend and sustain landed in Phase 3; mod wheel and channel aftertouch in Phase 5d, as modulation sources; **MIDI Learn in 6a** |
| 7 | The React migration and **every visualizer** — including the **per-source oscilloscopes** added to PRD §30.1 in Phase 5d, which need a lock-free capture buffer per source; telemetry. The interface itself landed early (§2, Frontend): it is a framework-free page, and nothing in it draws a waveform yet |
| 8 | Every effect and the FX rack — and the first consumer of the Phase 4c oversampler |
| 9 | Presets, wavetable resources, resource packaging |
| 10–12 | DSP validation, profiling, host testing, packaging, release hardening |

**137 of the 139 registered parameters now affect audio** — the whole source
section, four envelopes, four LFOs, both filters, sixteen modulation slots and
`master_gain`. Only two remain inert (`fx_distortion_mix` and `fx_delay_time`),
and they wait on the effects rack in Phase 8.

The registry grew from 38 to 139 in Phase 5d, which is what a modulation matrix
costs: 21 for envelopes 2-4, 32 for the four LFOs, and 48 for sixteen routing
slots. They were generated rather than typed (ADR-0037).

---

## 4. Build status

**Verified on this machine** (Windows 11, MSVC 19.51 / VS 18 2026, Windows SDK
10.0.26100, CMake 4.4.0):

| Configuration | Result |
|---|---|
| `RelWithDebInfo` + `APOLLO_WARNINGS_AS_ERRORS=ON` | Builds clean, no warnings — the CI gate. `ApolloTests`, `Apollo_All`, the VST3 bundle and the standalone binary all built and linked; the suite passes. |
| `Debug` + `APOLLO_WARNINGS_AS_ERRORS=ON` | Builds clean, no warnings; the suite passes |
| `Release` | Builds clean, no warnings |
| `APOLLO_JUCE_SOURCE_DIR` (local JUCE checkout) | Configures and builds |

**Verified by CI against Phase 4b code** (run 34160411849, all four jobs green):
Linux (GCC), macOS (Apple Clang), Windows (MSVC) and the Linux Clang sanitizer
job all configure, build and pass with `APOLLO_WARNINGS_AS_ERRORS=ON`. The macOS
job was confirmed to link the VST3 bundle and the standalone `.app` and to
actually execute the suite, rather than passing by building nothing.

The first attempt (run 34159444471) failed on both Clang jobs, for two unrelated
reasons, and neither reproduced on Windows or Linux/GCC:

- **macOS, a `-Wshadow` error.** `Voice::SourceGain::reset` took a parameter
  named `sampleRate`, and the struct is nested inside `Voice`, which has a
  `sampleRate` field. Clang treats a nested class's parameter as shadowing the
  enclosing class's member; GCC and MSVC do not. Every `apollo_core` translation
  unit was afterwards re-checked against the full strict warning set under Clang
  with `-Werror`, rather than only the file the build stopped at.
- **Sanitizers, a CTest timeout — not a test failure.** The suite reached 92% of
  its classes and was killed at 300.02 s. It now completes in **322.34 s** under
  ASan and UBSan, so the limit was 22 seconds short of what the Phase 4b tests
  legitimately need. Raised to 1800 s, with the reasoning recorded in
  `Tests/CMakeLists.txt`: it is a deadlock guard, not a performance budget.

Suite runtime, for scale: 24 s optimised and 82 s in Debug on the development
machine, 32 s on the macOS runner, 322 s under the Linux sanitizers.

**Still unverified:** ARM64 — no ARM64 runner is in the matrix (§6, issue 7).

---

## 5. Test status

**1,711,449 assertions, 0 failures**, across 20 test classes. The table below
lists the ones whose coverage is not obvious from their name; the DSP classes —
Wavetable oscillator, Unison, Source section, Envelope, Filter, LFO, Modulation
matrix, Oversampling, Noise generator — are described in §2 alongside the
subsystems they test.

| Category | Class | Covers |
|---|---|---|
| Foundation | Build information | Version header reaches consumers; macro and constexpr forms agree |
| Foundation | Parameter identifier conventions | ID rules; every documented ID is well-formed |
| Audio | Processor lifecycle | Prepare/release/reset, variable block sizes, sample rates, bus policy, silence and finiteness |
| Parameters | Parameter registry | Uniqueness, conventions, ranges, defaults, APVTS agreement, normalisation round trip, discrete steps |
| State | State serialization | Round trip, schema stamping, empty/malformed/foreign/unsupported rejection, **state preservation on rejection**, migration boundaries, every parameter round-tripped, reload counter |
| UI | UI bridge protocol | Malformed JSON, non-object payloads, versioning, unknown types, ID validation, NaN/Inf/out-of-range, gesture states, size limit, error hygiene |
| UI | Parameter bridge | Snapshot matches APVTS, commands reach APVTS, invalid commands change nothing, external changes propagate, coalescing, detach safety, metadata completeness |
| Engine | Voice engine | Pitch accuracy against four reference notes, velocity scaling, release to silence, polyphony, allocation order, deterministic stealing, click-free steal, sustain, pitch bend, voice reuse, extreme input, gain staging across five voicings |
| Audio | MIDI rendering | Sample-accurate event placement, **identical output across nine block sizes**, sustain via CC 64, pitch wheel, all-notes-off, master gain scaling |
| MIDI | MIDI mapping model | Address and range validation, reserved controllers, value scaling including inversion and clamping, the bijection and every replacement case, channel-specific beating omni in both learning orders, removal, capacity, and the publication ring — including a producer that outruns it |
| MIDI | MIDI Learn | Learn assigns the moved control and disarms; a reserved control is refused and leaves learn armed; the learning message does not itself move the parameter; a mapped control sweeps its parameter; **rendering alone never writes a parameter**; a control being learned does not drive its old destination; removal and clearing stop it; sustain and the mod wheel keep their fixed behaviour; mappings round-trip through save and reload and still drive audio; unknown-parameter entries are dropped; every bridge command, including with no MIDI attached |

Passing under `Debug`, `RelWithDebInfo`, and `Release` with warnings as errors.

---

## 5a. Manual verification of the running application

Everything above this point is automated. This section records what was
confirmed by **running Apollo**, on 2026-09-08, on the development machine
(Windows 11, WebView2 runtime 152.0.4191.66, 1920x1080 at 150% display scaling).
It is separate because it is the class of evidence the test suite structurally
cannot produce: the suite builds headless with `APOLLO_WITH_WEBVIEW=0` and never
opens a browser, a window or an audio device.

| Checked | Result |
|---|---|
| Standalone launches and stays up | Window titled "Apollo"; exits cleanly with code 0 |
| WebView editor renders | Yes — **after** the fix in §6, issue 12; before it, the UI was an error page |
| Controls built from metadata | All 25 parameters present, each with name, ID, unit and value |
| Defaults match the registry | Verified by eye across all 25, including `osc2_level` 0.00 %, `sub_octave` -1 oct, `osc2_fine` 0.00 cents, `filter_cutoff` 20000.00 Hz |
| UI to engine round trip | Dragging `osc1_unison` produced **9 voices** — the correct integer quantisation of the drag position, echoed back from the engine rather than from the page |
| Audio device selection | Windows Audio, Speakers (Realtek), 48 kHz, 480 samples (10.0 ms), output channels 1+2, MIDI inputs listed. No hard-coded device assumptions |
| Audio actually produced | MIDI note to a virtual port; Apollo's Windows audio-session peak read **0.1829** while held, **0.0000** before and after |
| Level is plausible | 0.08 engine gain x 0.787 velocity = 0.063 for one default note; the measured 0.18 is consistent with the 9-voice unison then in force, whose alignment reaches root-N (ADR-0024) |
| Notes across the range | 48, 60 and 72 all sound, at identical peaks — expected, since level tracks velocity rather than pitch, and repeatable because voice start phases are fixed (ADR-0017) |
| State reset resynchronises the UI | "Reset to default state" returned `osc1_unison` from 9 to 1 **in the UI**, exercising the reload-counter poll and full-snapshot path |
| VST3 bundle | Loads as a library; exports `GetPluginFactory`, `InitDll`, `ExitDll`; `moduleinfo.json` correct. **Not** yet instantiated in a host |

**Not verified, and needing something this machine does not have:** the VST3
running inside a DAW (§6, issue 2), and any host-specific behaviour —
automation, state save/restore through a project, transport. Those remain open.

### Interface, 2026-09-10

Re-run against the rebuilt frontend, on the same machine and runtime.

| Checked | Result |
|---|---|
| All parameters reach the page | Footer reads **139 parameters bound · protocol v1**, and the "Unassigned" safety-net module does not appear — so every registry entry has a home in the layout |
| Defaults match the registry | Verified by eye across the sections: `env1_attack` 5.0 ms, `env1_decay` 100 ms, `env1_sustain` 100 %, `env1_release` 50 ms, `env1_curve` 0.50, `lfo1_rate` 1.00 Hz, `lfo1_steps` 8, `master_gain` +0.0 dB, `filter2_cutoff` 20.00 kHz |
| Tab strips switch pages | ENV 1-4 and LFO 1-4 each show one page. A first version showed all four at once — `.cluster { display: flex }` beat the `hidden` attribute — and is fixed with an explicit `[hidden]` rule |
| Inactive modules recede | Oscillator 2, sub, noise (all at level 0) and filter 2 (type OFF) render dimmed while staying readable |
| Discrete controls | Wavetable, filter type, filter routing, LFO shape, trigger and polarity all show named positions, not indices |
| Matrix round trip | Slot 01 set to **LFO 1 → Filter 1 Cutoff** at depth **+95 %** through the page's own selects and depth rail |
| Modulation is shown, not just stored | The slot 01 row lit green with its number in green, the masthead lamp lit and read **MOD 1/16**, and the Filter 1 Cutoff knob's arc turned green — the destination knob, found through the destination enum |
| The page reaches the DSP | With that routing live, the audio-session peak traced a clean periodic sweep: 91 samples over 3.0 s (≈33 ms each), nulls 31 samples apart ≈ 1.02 s ≈ **0.98 Hz**, matching LFO 1's 1.00 Hz. The UI's writes are audible, not merely stored |
| State restored | Slot 01 returned to source —, destination —, depth 0 %; lamp off, **MOD 0/16** |

Two defects were found by looking rather than by testing, and both are fixed:
the hidden-page bug above, and "20.00 kHz" wrapping inside a 66 px knob, which
pushed its own label down and broke the alignment of every knob beside it.

---

## 5b. CPU measurements

Phase 4's remaining exit criterion. Produced by `ApolloTests --benchmark`, which
is built on every platform but run by no CI job — a CPU figure is a measurement,
not a pass or a fail, so it is recorded here rather than asserted in the suite
(`Tests/Performance/Benchmarks.h`).

**Machine:** the development laptop, Windows 11, MSVC, `RelWithDebInfo`,
48 kHz, 512-sample blocks. Figures are **per cent of one core's real time**;
lower is better, and 100 % means the render exactly keeps up with playback.

### Voice engine

Re-measured in Phase 5d. These figures include the DAHDSR envelope and the filter
section but **no active modulation** — see the modulation table below for what
the matrix costs when it is used.

| Voices | Default patch | Heaviest patch |
|---:|---:|---:|
| 1 | 0.23 % | 4.52 % |
| 8 | 1.79 % | 37.7 % |
| 32 | **7.14 %** | **175 %** |

"Default patch" is oscillator 1 alone with no unison — what Apollo loads with.
"Heaviest patch" is both oscillators at 16-voice unison plus the sub and the
noise generator: 34 oscillators per voice, and 1088 of them at 32 voices.

**On the ranges.** Repeating the whole benchmark gives figures that differ by
about ±6 % run to run, even though each measurement is the best of three passes.
That is the machine, not Apollo, and it is stated here because a single decimal
place would imply a precision these numbers do not have. Anything below roughly
a ten per cent difference should be read as unchanged.

**What the filters cost.** The default patch rose when the filter section landed:
`filter1` defaults to a lowpass, so a stereo state variable filter runs on every
voice even though it sits wide open at 20 kHz. Setting a filter to `off` costs
nothing — it returns its input on the first line — so a patch that does not want
one does not pay for it, but the *default* patch does. Phase 5b measured 9.35 %
at full polyphony and Phase 5d measures 7.14 %; the two differ by more than the
stated variance, so the honest reading is that the machine was busier during the
first measurement, not that anything got faster.

That default was kept deliberately. A subtractive synthesiser whose filter is
out of circuit until you also change a type control is a worse instrument than
one that costs four per cent of a core, and 9.35 % at 32 voices is still under a
tenth of one core.

The DAHDSR envelope before it cost a few per cent on the heaviest patch and
nothing distinguishable from noise on the default one, which is the expected
shape: a held note spends almost all its time in the sustain stage, which is a
switch and a return.

Two things follow, and both are recorded rather than smoothed over:

- **The default patch is cheap.** Cost is linear in voices at under 0.2 % each,
  so full 32-voice polyphony costs under six per cent of one core. Polyphony is
  not a performance consideration for ordinary patches.
- **The heaviest patch cannot run at full polyphony.** Over 160 % of real time at
  32 voices means it will not keep up on this machine, and 87 % at 16 voices is
  already too close to the edge to be safe. This is inherent arithmetic rather
  than a defect; ADR-0029 records the decision to publish the limit rather than
  lower the ceilings, and Phase 10 owns the optimisation.

### Modulation

The default patch again, with four routings active — a filter sweep from an
envelope, a vibrato, velocity on level and an LFO on pan.

| Voices | Unmodulated | With four routings |
|---:|---:|---:|
| 1 | 0.232 % | 0.368 % |
| 8 | 1.786 % | 3.143 % |
| 32 | **7.14 %** | **12.51 %** |

About 0.39 % per voice against 0.22 % — three quarters more for a patch that is
using the matrix. A voice with **no** active slot skips evaluation entirely and
advances only the generators something routes, so the unmodulated column is
unchanged from Phase 5b and an unmodulated patch pays nothing at all for the
matrix existing.

### LFO

Per instance, per sample. Nothing runs one yet; this was measured in Phase 5c
specifically to decide whether LFOs could afford to run at audio rate, and the
right-hand column is what four LFOs across 32 voices would cost.

| Shape | One instance | x128 |
|---|---:|---:|
| Sine | 0.034 % | 4.41 % |
| Triangle | 0.015 % | 1.91 % |
| Square | 0.014 % | 1.74 % |
| Sample & hold | 0.014 % | 1.74 % |
| Sine, smoothed | 0.037 % | 4.69 % |

Affordable, so audio rate it is (ADR-0034). The sine costs two and a half times
every other shape because it calls `std::sin` per sample — the first thing to
replace with a polynomial or a small table if Phase 5d pushes the total
somewhere uncomfortable.

### Oversampler

Per channel, including a `tanh` drive, so the figure reflects a real oversampled
stage rather than the conversion alone. A stereo stage costs twice this.

| Setting | Cost | Round-trip latency |
|---|---:|---|
| Bypass (`Factor::none`) | 0.034 % | 0 samples |
| 2x | 0.287 % | 39 samples (0.81 ms) |
| 4x | 0.809 % | 59 samples (1.23 ms) |

Cheap next to the voice engine: a stereo 4x-oversampled distortion costs about
1.6 % of one core, which is why 4x is a reasonable default for a distortion stage
rather than a luxury.

---

## 6. Known issues

| # | Issue | Severity | Notes |
|---|---|---|---|
| 1 | ~~The editor has never been seen running~~ | **Closed** | Verified 2026-09-08 by launching the standalone. The WebView renders, and the page builds all 25 controls from parameter metadata alone, with every default matching the registry. Finding it running is also what exposed the WebView2 backend defect below. |
| 2 | VST3 has not been loaded in a DAW | Medium | **Partially verified.** The bundle loads as a library and exports `GetPluginFactory`, `InitDll` and `ExitDll`; `moduleinfo.json` declares the correct vendor, version and `Instrument`/`Synth` subcategories. That is not the same as instantiating in a host, which still needs a DAW. |
| 3 | ~~Standalone has not been launched against an audio device~~ | **Closed** | Verified 2026-09-08. Windows Audio, Speakers (Realtek) at 48 kHz / 480 samples, output channels 1+2. A MIDI note sent to a virtual port drove Apollo's output to a measured session peak of **0.1829**, against 0.0000 before and after — read from the Windows audio-session meter, not inferred. |
| 4 | Linux CI job failed twice, both causes fixed and confirmed | Resolved | Kept as a record rather than deleted, because both fixes are load-bearing and neither is obvious from the code. **(a) GTK include paths.** `juce_gui_extra.cpp: fatal error: gtk/gtk.h: No such file or directory`, preceded by `warning: "JUCE_WEB_BROWSER" redefined`. Apollo hand-defined `JUCE_WEB_BROWSER=1` instead of setting JUCE's `NEEDS_WEB_BROWSER`. On Linux `_juce_link_optional_libraries` reads that property to decide *both* the define and whether to link `juce::pkgconfig_JUCE_BROWSER_LINUX_DEPS` — the only source of the GTK/WebKitGTK include paths — so the manual define switched the include on while the path was never supplied. **(b) Runner memory exhaustion.** The build then reached 77%, every in-flight compile was SIGTERMed at one instant with no diagnostic, and the runner reported a shutdown signal. No newer run existed, so `cancel-in-progress` was not responsible. The Linux job compiles all of JUCE twice (plugin + test runner, ADR-0010) and since Phase 2 those units also pull in GTK/WebKit headers. Build parallelism is now capped per platform (Linux 2) and the two halves are built as separate targets. Both fixes are confirmed by the green run recorded in §4. |
| 5 | Symbol visibility still unresolved | Low | ADR-0009 deferred the decision to Phase 1. Plugin targets now exist, so it can be closed. |
| 6 | No allocation/lock detector on the audio thread | Medium | Real-time safety is by construction and review, not enforced by a tool. Phase 10. |
| 7 | ARM64 unverified | Low | No ARM64 runner in the matrix. Phase 11. |
| 8 | `ROADMAP.md` refers to `UI-BINDINGS.md`; the file is `UI_BINDINGS.md` | Trivial | Not renamed silently; other documents cross-reference it. |
| 9 | JUCE 9.0.x exists upstream | Informational | Apollo pins JUCE 8 because the specification says JUCE 8 (ADR-0002). |
| 10 | ~~`filter_*` parameters are un-indexed while the PRD specifies two filters~~ | **Closed** | Resolved in Phase 5b. They became `filter1_*`, joined by `filter2_*` and `filter_routing`, through a schema version 1 to 2 migration rather than a bare rename — the first real use of the migration path built in Phase 2 (ADR-0032). Done while Apollo is pre-1.0, which is the only window in which it is cheap. |
| 11 | Company name and plugin codes are inferred | Low | `ProdByRnV`, `Prnv`, `Apol`, `com.prodbyrnv.apollo` were inferred from the GitHub organisation. Easy to change now, **permanent once released** — please confirm. |
| 12 | Standalone showed "Navigation to the webpage was canceled" instead of the UI | **Fixed** | Found on 2026-09-08, the first time anyone ran the application. The editor never selected a WebView backend, so JUCE built the legacy Internet Explorer control despite `JUCE_USE_WIN_WEBVIEW2=1` and `NEEDS_WEBVIEW2` — necessary but not sufficient, per JUCE's own documentation. The IE control supports neither the resource provider nor the native integration, so the page could not load. Fixed by naming the backend per platform and by giving WebView2 a writable per-user data folder, which also prevents the same silent fallback in hosts whose program directory is read-only (ADR-0027). |
| 13 | The heaviest patch cannot sustain full polyphony in real time | Medium | Measured in Phase 4c, not inferred: 2 x 16-voice unison plus sub and noise costs **150 % of one core at 32 voices** and 82 % at 16 (§5b). The default patch is unaffected at 4.86 %. This is inherent arithmetic — 1088 interpolating oscillators — rather than a defect, so the fix is SIMD and interpolation work in Phase 10, which owns profiling. ADR-0029 records why the ceilings were published rather than lowered. |
| 14 | The MIDI Learn interface has not been driven by hand | Medium | The engine half is verified end to end automatically — a real control-change message renders through `processBlock`, a mapping is learned, applied, saved, reloaded and shown to still drive its parameter. What is unverified is the *page*: the masthead toggle, the badges, the Escape and Delete keys, and a genuine learn completed by moving a physical control. The workstation was locked when 6a landed and neither input injection nor a screenshot is possible in that state, so this was left open rather than claimed. |

---
## 7. Blockers

**None for development.** One verification is outstanding rather than blocked:
the MIDI Learn interface has not yet been driven by hand, because the
development workstation was locked when 6a landed (§6, issue 14). The engine
half is verified automatically end to end; what is unverified is the *page*.

---

## 8. Recommended next action

Continue **Phase 6**. Two of its tasks remain, each with its own sub-phase:

1. **6b — per-note expression.** Polyphonic aftertouch and MPE. Channel pressure
   is already a modulation source (5d), but a per-note source needs per-note
   controller routing that does not exist: today a controller value is held by
   the engine and pushed to every voice. MPE additionally needs per-note pitch
   bend and a channel-allocation model, which is why the two belong together
   rather than in the phase that merely reads the messages.
2. **6c — controller profiles.** An optional, never-required way to fill the
   mapping table quickly, kept strictly separate from the parameter registry
   (CLAUDE.md §16.3). The mechanism it needs already exists: `assign()` takes a
   complete mapping, including a channel, which is exactly what a profile
   supplies.

Then drive the MIDI Learn interface by hand and record it in §5a — the class of
evidence the suite structurally cannot produce, since it builds headless and
never opens a browser.

---

## 9. Version control policy

Set by the developer and not subject to agent discretion. The developer changed
this policy after the first round of feature branches produced pull requests
they did not want; the rules below supersede the earlier "the agent never
pushes, feature work goes on its own branch" wording.

- **Remote:** `https://github.com/ProdByRnV/Apollo`
- **The agent runs the git commands itself**, and the developer approves each
  push at the permission prompt. Whatever commands the agent shows must be
  exactly the commands it runs.
- **Work goes directly on `main`.** Everything was reset onto `main` during that
  consolidation, and `main` is now the only branch.
- **No Claude attribution anywhere** — no `Co-Authored-By` trailer, no "generated
  with" line, no session link, in commit messages or anywhere else. This is
  enforced in the developer's Claude Code settings rather than left to habit.
- **After pushing, the agent verifies CI rather than assuming it passes**,
  diagnoses any failure from the run logs, and fixes and pushes again until the
  run is green.
- **The agent does not initialise or alter version-control state** — creating
  repositories, branches or remotes — on its own initiative.

---

## 10. How to keep this file honest

- Record only what has been built and run. "It compiles" is not "it works"
  (CLAUDE.md §45).
- When a phase completes, tick its `ROADMAP.md` tasks and update §1–§8 here.
- When something is discovered to be broken or unverified, add it to §6 rather
  than quietly removing the claim.
- Architectural decisions go in `Docs/DECISIONS.md`, not here — this file records
  state, not reasoning.
