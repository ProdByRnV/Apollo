# Apollo — Project State

> **Purpose:** the authoritative, verified record of what actually exists in this
> repository. The specification documents describe what Apollo *should* become;
> this file describes what it *is*. Nothing is recorded as complete here unless it
> was built and run.
>
> Update this file at the end of every roadmap step.

**Last verified:** 2026-09-12
**Apollo version:** 0.1.0

---

## 1. Current position

| | |
|---|---|
| **Phase** | Phase 8 — Effects Rack, in progress; **8a complete** |
| **Status** | **8a complete.** A six-slot reorderable rack sits between the voice engine and the master gain, holding the distortion — three curves, 4x oversampled, with compensated drive, pre/post filtering and a latency-matched dry path |
| **Milestone** | M8 — Effects |
| **Next step** | Phase 8b — the delay: a stereo delay line with feedback, ping-pong, filtering and host-tempo sync |

**Apollo is a wavetable synthesizer.** Two band-limited wavetable oscillators,
each with up to 16 detuned and stereo-spread unison voices, plus a sine sub and
a stereo noise generator, mixed with per-source level and balance and played
polyphonically through the VST3 and standalone builds. Aliasing is measured, not
asserted: worst case -98.5 dBc against a -60 dBc budget.

Voices are now shaped by a real DAHDSR envelope with adjustable curve tension,
not the linear placeholder.

The instrument now has a real interface: a signal-flow layout of rotary controls,
segmented switches and a sixteen-slot matrix, written in React and TypeScript
under `WebUI/`, bundled by CMake and embedded in the binary (ADR-0038, ADR-0039,
ADR-0040, ADR-0050). It replaced a 139-slider placeholder page in work pulled
forward from Phase 7, was rebuilt from hand-written JavaScript into components in
7d, and no part of it states a parameter's range: every control is built from the
metadata the engine sends.

It is **gold on gunmetal** (ADR-0051). Apollo is named after the Greek god of the
sun and of music, and the accent — keyed to `#F7EF8A` — is where that is said: it
is the scope and wavetable trace, the value being held, and every hover, against
a neutral dark metallic grey. The deep violet the interface carried from Phase 6
through 7d came from a written default the developer had never intended, and the
specification documents were corrected with it.

Still placeholders: the four built-in wavetables are mathematically defined
morphs rather than designed factory content (Phase 9).

The instrument can now be *seen* as well as heard. Phase 7a built the lock-free
capture transport PRD §30.1 requires and put the first scope on the output; 7b
filled in the other five, so oscillator 1, oscillator 2, the sub, the noise and
the post-filter signal each draw their own waveform beside the controls that
shape them; 7c added everything that is not a waveform — a live trace of what
each of the four envelopes and four LFOs is producing, an output meter with real
ballistics and a clip indicator, the sounding voice count, and a display of the
wave each oscillator is actually reading. Capture runs only while an editor is
watching, and an instance with its window closed costs exactly what it did
before any of it existed.

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

### Frontend (Phase 7)

- **React and TypeScript under `WebUI/`** (Phase 7d), bundled by esbuild into the
  same three files the plugin has always embedded — `index.html`, `apollo.css`,
  `apollo.js` — through `juce_add_binary_data` (ADR-0038, ADR-0050). CMake runs
  the bundler, so the built page can never disagree with its source; editing a
  component relinks the plugin.
- **Twelve packages in the whole dependency tree**: react, react-dom, esbuild,
  typescript and their types. No dev server, no plugin ecosystem, and nothing
  loaded over a network at run time (CLAUDE.md §32, §40).
- **Building the plugin needs Node; the engine and its tests do not.** The test
  suite has never had a browser dependency and did not acquire one, and the
  sanitizer job configures the editor out entirely.
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
- **Values reach controls through stores, not props**: one listener set per
  parameter id read through `useSyncExternalStore`, so a knob re-renders when its
  own value moves and at no other time. The MIDI mappings and the modulation
  lighting are two more stores, shaped for how often each changes.
- **The eleven canvases are drawn imperatively**, outside the React tree: a frame
  is delivered by call to whoever subscribed to that source. Routing thirty
  frames a second through state would re-render eleven components to produce
  markup that never changes (ADR-0050).
- **Now also present:** every visualizer Apollo owes except a spectrum analyser
  (Phase 7a-7c). Six oscilloscopes — the output, both wavetable oscillators, the
  sub, the noise generator and the post-filter signal — from frames broadcast at
  30 Hz; eight modulator traces, an output meter with a clip indicator, the
  sounding voice count and a wavetable display per oscillator, at 15 Hz. Each
  sits beside the controls that shape it rather than in a panel of its own.

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
  pitch bend (±2 semitones by default, a control since Phase 6b), all-notes-off
  and all-sound-off.
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
- **Not yet present:** controller profiles (6c), 14-bit high-resolution CC,
  relative/endless encoders, and pickup ("takeover") mode. None is required for
  a generic controller to work.

### Per-note expression and MPE (Phase 6b)

- **All three kinds of aftertouch.** Channel pressure, polyphonic key pressure
  and MPE member-channel pressure all reach the same per-voice `aftertouch`
  modulation source. A plain keyboard is unchanged: with no zone active a
  channel message addresses every voice, which is what it always meant.
- **A voice records the channel its note arrived on**, and one rule decides what
  a channel message reaches — every voice with no zone, every voice in the zone
  from the manager channel, one channel's voices from a member channel. That is
  the whole of MPE: there is no second code path, no second voice allocator and
  no per-note controller registry (ADR-0044).
- **Per-note pitch bend and the wheel add rather than replace.** Two ranges,
  because MPE uses two: the wheel's (`midi_bend_range`, default ±2) and the
  member channels' (`mpe_bend_range`, default ±48, which is the specification's).
- **`ModSource::timbre`** — MPE's CC 74 axis — is a new modulation source,
  bipolar around the controller's rest position so an untouched one reads zero.
  CC 74 is the timbre axis only on a member channel of an active zone;
  everywhere else it stays an ordinary control change and an ordinary MIDI Learn
  target.
- **A new note starts unpressed but adopts the bend and timbre already in
  force**, because pressure is a force and the other two are positions — and
  every MPE controller places a note's pitch before sending the note-on.
- **The pitch-bend range stopped being a constant.** It had been a hard-coded
  ±2 since Phase 3, with a comment saying it would become a parameter; it now is
  one, and the `pitchBend` modulation source reads the wheel *position* so it
  cannot change meaning when the range does.
- **An MPE controller configures Apollo itself.** RPN 6 (the MPE Configuration
  Message) and RPN 0 (pitch-bend sensitivity) are decoded from their control-
  change sequences and queue changes to the parameters that own those values, so
  a controller announcing itself is indistinguishable from the user setting the
  same controls by hand — visible, saved and undoable (ADR-0045). CC 6, 38 and
  98-101 became reserved controllers as a result.
- **Not yet present:** per-note routing of arbitrary controllers (only the three
  dimensions MPE defines are per-note), and MPE note-channel rotation policies
  beyond what the zone itself implies.

### Controller profiles (Phase 6c)

- **Two built-in profiles, neither of which names a manufacturer or a product.**
  The MIDI specification's **Sound Controllers** (CC 70-79, whose meanings it
  already fixes — brightness, harmonic intensity, attack, decay, release,
  vibrato rate) and its **General Purpose Controllers** (CC 16-19 and 80-83,
  which it deliberately leaves undefined, and which are therefore the right home
  for a generic bank of knobs). A list of device names would have been more
  immediately impressive and out of date within a year (ADR-0046).
- **A profile is a shortcut, never a mode.** Every entry goes through the same
  `assign` a learned mapping does, so the bijection still holds, replacements
  are still reported, and the resulting mappings can be relearned, released or
  cleared like any others. Nothing behaves differently because one was applied.
- **Replace or merge**, both named explicitly on their own button rather than
  hidden behind a default.
- **Kept strictly separate from the parameter registry**, which is the roadmap's
  own requirement: a profile refers to parameters by ID and resolves them when
  applied. An entry naming a parameter this build does not have is dropped, and
  the result says how many were — "eight of eight" and "five of eight" are
  different outcomes.
- **Not yet present:** user-supplied profile files. The seam is in place —
  `ControllerProfile` is plain data and `applyProfile` takes one by reference —
  but file loading, a user library path and the failure modes of both belong
  with the resource work in Phase 9.

### Visualisation transport and the output scope (Phase 7a)

- **A lock-free capture ring per source**, in `Source/Telemetry/`. The audio
  thread writes and moves an index; the message thread reads a window ending a
  margin *behind* that index and tells the writer nothing. One-way, allocation-
  free, and JUCE-free, so the whole transport is testable with no host, no
  device and no browser (ADR-0047).
- The samples are `std::atomic<float>`, which costs nothing at run time on the
  targets Apollo supports and is the difference between a stale sample and a
  data race.
- **Triggering, decimation and silence happen on the message thread.** A frame
  is aligned to the most recent rising zero crossing so a steady note stands
  still, and *says* when it could not find one rather than inventing stability.
  Each drawn point is the sample at that position, never an average: averaging
  would smooth an aliased or clipped waveform into a clean one, which is the
  opposite of what a scope is for.
- **The frame bridge is separate from the parameter bridge.** One is a
  conversation and the other a broadcast; sharing a class would mean sharing a
  rate. The frame timer follows the outbound handler, so a plugin with its
  editor closed builds and serializes nothing.
- **A source nothing captures reports *inactive*** and is omitted from the
  message entirely, so the interface can tell "this build does not capture that"
  from "that part is quiet".
- **Measured, not assumed** (PRD §30.1): see the per-source entry below.
- **The trace is drawn 1:1 and never auto-scaled.** A scope that stretched its
  input to fill the canvas would make every signal look equally loud and would
  hide the one thing a scope is best at showing. Apollo's gain staging is
  deliberately conservative, so an ordinary note draws a small trace; the caption
  carries the peak in decibels, and a display-scale control belongs with the
  metering work in 7c.
### The per-source oscilloscopes (Phase 7b)

- **A scope on each of the five sources** — oscillator 1, oscillator 2, the sub,
  the noise generator and the post-filter signal — beside the controls that shape
  it: each oscillator's in its own module, the sub's and the noise's in theirs,
  and the post-filter one in Filter 2 where the routing switch lives, labelled
  `POST-FILTER` because it belongs to the pair rather than to the filter it sits
  next to.
- **Each trace is the whole voice pool, not one voice.** The engine owns a mono
  accumulator per tap, every voice adds its contribution, and the sum is published
  once. What "oscillator 1" is producing is what every voice producing it is
  producing together — any other reading stops being true the moment a second
  note is held (ADR-0048).
- **The accumulators are cleared before every pass**, so a pass in which nothing
  sounds publishes silence. That is what makes a source which stops *seen* to
  stop: the zeroes are captured rather than inferred from the absence of a write
  (CLAUDE.md §26.1). Verified on screen — two seconds after a note-off all five
  read `silent` with a flat line, with their levels still up.
- **The four source taps sit before the filter and the amplifier**, so a scope
  shows the wave at the level the source is set to, unshaped by the envelope —
  which is what makes it readable while a wavetable position is being moved. The
  post-filter tap sits after both: the voice's finished contribution, immediately
  before it joins the mix. Closing the filter therefore takes the post-filter and
  output scopes down while leaving the four sources where they were, and that
  disagreement is the difference between watching a source and watching the mix.
- **The pool is rendered in 512-sample chunks while capturing**, because
  `prepare` is told a sample rate and not a block length and a host may exceed
  the block size it promised. A voice rendered as two consecutive calls produces
  exactly what it produces as one, and a test asserts it sample for sample.
- **Capture runs only while something is watching.** The hub carries a flag the
  audio thread reads once per block; the editor sets it when it attaches its
  outbound handler and clears it when it detaches. Arming clears every ring
  first, so a viewer's opening frame can never be audio the last viewer left
  behind. With the flag clear the engine runs exactly the code it ran before any
  of this existed.
- **Measured, not assumed** (PRD §30.1): see §5b, re-measured in 7c with the
  two cases interleaved. Everything a watched instance pays adds 0.12 % of real
  time at one voice and 0.47 % at thirty-two. Scopes do not cost polyphony. But
  it is a meaningful fraction of the render, which is the whole reason for the
  gate: an instance nobody is watching should not pay for a picture nobody sees,
  and a session holding twenty instances shows one.

### Modulator traces, metering and the wavetable display (Phase 7c)

- **A live trace of what each of the eight modulators is producing**, not a
  picture of the shape it was configured with (CLAUDE.md §26.1). All four
  envelopes and all four LFOs, one entry every 7.8 ms over a one-second window,
  drawn green because a modulator moving *is* activity and green is what activity
  means here (§24.2) — the scopes stay gold, because audio is not a modulation
  indicator.
- **Sampled rather than captured**, which is the design decision worth knowing: a
  modulator moves at a few hertz, so the ring *is* the picture — 128 entries
  holding exactly the second the interface draws. There is nothing to decimate and
  so no decimation rule to defend, unlike the scopes' (ADR-0049). The entry is
  taken between capture chunks, and a chunk is shortened on the one in three that
  needs it to land exactly on the next trace boundary — which keeps the trace at an
  exact rate without shortening every chunk, a change that measured 1.85 % against
  0.76 % at thirty-two voices.
- **The trace follows the most recently started sounding voice.** Envelopes and
  LFOs are per voice and a trace has to choose: summing four envelopes describes
  nothing, and averaging would flatten what is being watched. The newest note is
  the one whose envelope you are listening to while you adjust it.
- **An unrouted modulator says so.** The engine does not advance an LFO nothing
  reads, so its trace is honestly a flat line — and a flat line with no
  explanation looks like a fault. The frame carries a `routed` flag and omits the
  trace entirely, which on the default patch is seven eighths of the message.
- **The envelope stage is reported in words** — idle, delay, attack, hold, decay,
  sustain, release — beside the value.
- **An output meter that is not the scope's peak.** Peak with an instant attack
  and a 20 dB/s fall, RMS over 300 ms beside it, and a clip held for a second and
  a half after the offending sample so it is seen by someone who was not watching
  at the time. Clipping is detected at full scale rather than below it: a float
  output survives it and whatever converts to integer downstream will not.
- **The first real use of the red overload token** (§24.2), and it is a word as
  well as a colour (§39).
- **A wavetable display per oscillator**, showing one cycle of the wave actually
  being read at the *effective* position — the parameter plus whatever the matrix
  is adding. Not telemetry: the table is immutable for the life of the instrument,
  so the audio thread publishes two numbers and the message thread renders the 128
  points from the table itself, which costs less than sending them would.
- **A display zoom on every scope**, deferred twice and now overdue: ×1 to ×8,
  stated on the button, magnifying the trace and never the decibel reading beside
  it. A control the user turns rather than a scale the page chooses, so a
  magnified trace can never be mistaken for a loud one.
- **Not yet present:** a spectrum analyser, which PRD and ROADMAP both mark
  optional; and the React migration (7d).

### The effects rack and the distortion (Phase 8a)

- **The rack is a permutation, not a container.** Six slots, each naming the
  effect that occupies it. Every effect is constructed and prepared once and
  lives in the rack for the instrument's lifetime, so rearranging the chain is
  six integers changing and nothing is created, destroyed or allocated while
  audio is running (ADR-0054). PRD §18's add, remove, reorder and bypass all land
  on that.
- **The slot parameters enumerate all six effects from the start**, including the
  five whose phases have not landed. A discrete parameter's range is part of the
  permanent automation contract — a host stores the normalised value — so a range
  that grew as effects arrived would remap every lane and every preset written
  before the change. The unbuilt entries read "(soon)" in the interface and
  resolve to an empty slot in the engine.
- **The default rack is empty**, and that is deliberate: a new instance sounds
  exactly as it did before the rack existed, costs what it cost, and reports zero
  latency. An instrument should not arrive distorted.
- **Bypass keeps its position and its latency.** A bypassed effect is routed
  *through* rather than around: the distortion pushes the signal through its
  compensation delay and does nothing else, so toggling bypass does not make the
  host re-plan its graph. Tail follows the active chain instead, because a
  bypassed reverb is not ringing out.
- **Latency reaches the host from the message thread.** The audio thread compares
  the rack's latency against what the host was last told — one relaxed load and
  one comparison per block — and only a chain change wakes an `AsyncUpdater` to
  call `setLatencySamples`, which notifies the host and can call straight back in.
- **The distortion is the first consumer of the Phase 4c oversampler**, which is
  what ADR-0033 said it would be: 4x, fixed, 59 samples of round trip, with the
  dry path delayed by exactly that so the mix blends instead of combing.
- **Three curves that share a derivative at the origin** — `tanh`, a hard
  ceiling, and an asymmetric exponential diode — so switching mode changes the
  harmonic content rather than the level, and a quiet signal passes through any
  of them untouched.
- **Drive is compensated at a -6 dBFS reference**, ramped with the drive itself,
  so the control changes the tone rather than the volume and an A/B is not won by
  whichever side is louder.
- **Four filters, each with a job**: a subsonic highpass in front so rumble
  cannot pump the clipper, a DC highpass after so the diode curve's asymmetry
  does not eat headroom, a tone lowpass on the wet path that switches out
  entirely at the top of its travel, and the oversampler's own halfbands.
- **Measured, not asserted.** Fold-back is 13 to 15 dB lower than the same curves
  run at the base rate and -100 dBc on a 1 kHz note at moderate drive; the stage
  costs 1.4 % of one core hard-clipping and 2.7 % through `tanh`, stereo
  (Docs/OVERSAMPLING.md). Hard driving still folds at about -29 dBc, which is the
  arithmetic of a near-square wave rather than a defect, and the test says so in
  those terms.
- **Not yet present:** the delay (8b), reverb (8c), gate and compressor (8d), EQ
  (8e), and the whole-chain validation that needs more than one effect to mean
  anything (8f).

## 3. What is NOT implemented

| Phase | Absent |
|---|---|
| 7 | **Complete**, but for an optional spectrum analyser. The transport landed in **7a**, a scope on the output and all five sources in **7b**, the modulator traces, output meter, voice count and wavetable displays in **7c**, and the React/TypeScript migration in **7d** |
| 8 | **8a is done** — the rack and the distortion, the first consumer of the Phase 4c oversampler. Absent: the delay (8b), reverb (8c), gate and compressor (8d), EQ (8e), and whole-chain validation (8f) |
| 9 | Presets — the file format is now fixed as `.rnv` (ADR-0053), but nothing reads or writes one yet — wavetable resources, resource packaging |
| 10–12 | DSP validation, profiling, host testing, packaging, release hardening |

**153 of the 154 registered parameters now affect audio** — the whole source
section, four envelopes, four LFOs, both filters, sixteen modulation slots, the
MIDI expression settings, `master_gain`, and since 8a the rack's six slots and
the distortion's six controls. Exactly one remains inert, `fx_delay_time`, and it
waits on the delay in 8b.

Phase 8a added eleven, and six of them are the rack's slots. Those enumerate all
six effects from the start, including the five not built yet, because a discrete
parameter's range is permanent once it ships (ADR-0054).

The registry grew from 38 to 139 in Phase 5d, which is what a modulation matrix
costs: 21 for envelopes 2-4, 32 for the four LFOs, and 48 for sixteen routing
slots. They were generated rather than typed (ADR-0037). Phase 6b added four
more — the pitch-bend range and the three that describe an MPE zone — and they
are the first parameters marked **not automatable**, because they describe the
controller on the desk rather than the patch.

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

**1,762,226 assertions, 0 failures**, across 27 test classes. The table below
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
| MIDI | Controller profiles | Every built-in profile checked against the registry — real parameters, assignable controllers, no duplicate controller or parameter inside one profile, unique identifiers; applying fills the table exactly; replace clears and merge keeps; an entry naming an unknown parameter is dropped while a reserved controller is refused, each counted separately; a profile's mappings are ordinary mappings that can be learned over, released and cleared; and every bridge command, including an unknown profile and an unknown mode |
| MIDI | MPE and per-note expression | Zone channel classification at both ends of both zones; manager-versus-member routing; RPN decoding, per-channel independence, the null selection and NRPN cancellation; polyphonic aftertouch pressing one note and not another; channel pressure still pressing everything; per-note bend independent per channel and adding to the wheel; note-off matching its channel; CC 74 as timbre inside a zone and as a MIDI Learn target outside one; the bend range as a control, applied to a held wheel; an MPE Configuration Message reaching the zone through the parameter; and pressure not being inherited by a new note while the bend is |
| MIDI | MIDI Learn | Learn assigns the moved control and disarms; a reserved control is refused and leaves learn armed; the learning message does not itself move the parameter; a mapped control sweeps its parameter; **rendering alone never writes a parameter**; a control being learned does not drive its old destination; removal and clearing stop it; sustain and the mod wheel keep their fixed behaviour; mappings round-trip through save and reload and still drive audio; unknown-parameter entries are dropped; every bridge command, including with no MIDI attached |
| Telemetry | Visualisation transport | An untouched source distinguishable from a silent one; the window read back in order, including across the ring's wrap; a stopped source seen to stop rather than holding its last picture; reset; the silence threshold; triggering, including that a flat line reports itself free-running; decimation that shows alternating samples rather than averaging them to nothing; per-source isolation and unique wire tokens; the processor capturing its own output through `processBlock`; and the bridge sending a frame whose points are finite, inside full scale, and actually a waveform. Phase 7b added the four claims that matter most: that an unwatched instance captures nothing and still sounds; that arming discards the picture the last viewer left behind; that the same note rendered with and without capture comes back **sample for sample identical**, including when the block is long enough for the capture path to split it; that a source at level zero is captured *as silence* rather than left uncaptured, and survives a closed filter that takes the mix away; and that a source's trace grows with the voice pool rather than showing one voice from it. Both frame builders also log and bound their message size, so an encoding change that multiplies the traffic is caught here rather than in a profiler |
| Telemetry | Instrument telemetry | The modulator traces, the meter, the voice count and the wavetable displays, added in 7c. A modulator nothing traces is distinguishable from one sitting still; the trace is the most recent second across the ring's wrap; the meter takes a peak in the block it happened in and gives it up over about a second and a half; peak and RMS disagree about a single spike and agree about a constant tone, which is why there are two of them; a clip is still reported half a second after the samples that caused it and clears itself after the hold; an envelope's trace shows its rise, settles at the sustain level and falls on release, with the stage reported at each point; the two ends of a wavetable draw different waves, so the display is following the position rather than ignoring it; an unrouted LFO does not claim to be running and starts to when something routes it; and the frame that reaches the interface names every modulator exactly once and both oscillators by number rather than by array position |

| DSP | Distortion | The curve is bounded, monotonic and centred, and every mode passes a quiet signal through unchanged — which is what lets the mode be switched without a jump; drive compensation holds a -6 dBFS sine within 4 dB across the whole 0 to 36 dB range on all three curves; a fully dry mix is the input delayed by **exactly** the reported latency, asserted sample for sample rather than approximately, because a dry path that has been through arithmetic is a bug; latency does not move with mode, drive, mix or bypass; oversampling removes 13 to 15 dB of fold-back against the same curves at the base rate, and a musical note at moderate drive stays under -60 dBc; absurd and denormal input produces finite output, and a very hot signal leaves bounded by the curve's own ceiling; and a reset leaves no tail in the filters or the delay line |
| DSP | Effects rack | An empty rack is **bit-exactly** transparent and reports no latency or tail; an effect whose phase has not landed leaves its slot empty; the same effect in two slots runs once, in the earlier one; an effect sounds the same wherever in the chain it sits; latency counts what is in the chain whether bypassed or not, and drops only when the effect is removed; a bypassed effect delays the signal and does nothing else; and tail is reported for the active chain only |
| Audio | Effects rack in the processor | The parts that only exist once the rack is wired into a plugin: a new instance has an empty rack, reports zero latency and still sounds; putting the distortion in a slot changes what is heard, measurably and without running away with the level; the latency the rack adds reaches the host and does not change when a bypass is automated; a chain — slot, mode, drive, tone, mix — survives the save/restore a project or a preset puts it through and reports its latency again on prepare; and a slot naming an effect this build does not have is simply empty rather than surprising |

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

### MIDI Learn, MPE and controller profiles, 2026-09-10

The class of evidence the suite structurally cannot produce: the suite builds
headless, opens no browser and has no MIDI device. Every row below was driven by
hand against the running standalone, with real MIDI messages sent to a virtual
port Apollo was listening on.

| Checked | Result |
|---|---|
| All parameters reach the page | Footer reads **143 parameters bound · protocol v1**, and the "Unassigned" module does not appear — the four new expression parameters have a home |
| The MIDI module renders | Bend Range +2 st, MPE Zone OFF/LOWER/UPPER, Members 15, Note Bend +48 st, and the module renders dimmed while the zone is off |
| Assignment is a visible mode | The MIDI LEARN button turns green, every assignable control gains a dashed outline, and the footer hint changes to "Click a control to assign it · Delete to release · Escape to cancel" |
| Arming a control | Clicking Osc 1 Position gave it an amber **LEARN** badge, a green outline, the status line "Move a MIDI control to assign it to Osc 1 Position — Escape to cancel" and a matching tooltip |
| A real controller completes the learn | A **CC 74** message from the virtual port assigned it: green **CC 74** badge, masthead **MIDI 1** with its lamp lit, a **CLEAR ALL** button appearing, and the tooltip reading "MIDI CC 74 (any channel)" — the omni default |
| The learning message does not move the parameter | Position still read **0 %** immediately after the assignment |
| The mapping drives the parameter | CC 74 at 127 moved Position to **100 %**, arc and number together |
| Escape leaves the mode | Confirmed; the footer hint returns to "Drag a knob …" |
| Mappings survive a restart | Learned **CC 21 → Osc 1 Position**, closed the window normally, relaunched: **MIDI 1** and the CC 21 badge came back before the page asked for anything, and CC 21 at 127 drove Position to **100 %**. The mapping is restored *and* live |
| An MPE controller configures Apollo | An **MPE Configuration Message** (RPN 6, seven members) sent from the virtual port switched MPE Zone **OFF → LOWER**, Members **15 → 7**, and un-dimmed the module — the whole chain, from the audio thread through the parameter queue to the interface |
| A note sounds on a member channel | Note 60 on channel 2 under the lower zone: audio-session peak **0.0203** against 0.0000 silent |
| Per-note bend is per note | With filter 1 at 126 Hz, the same note bent fully up on **its own** channel read **0.0142** — the pitch moved out through the lowpass — while a full bend on **channel 3**, which owned no note, left it at **0.0203**, unchanged |
| The profile picker renders | The MIDI module shows a profile select, REPLACE and MERGE buttons with explanatory tooltips, and the line "MIDI CC 70-79, whose meanings the specification already fixes. · 8 assignments" |
| A profile applies | REPLACE with Sound Controllers took the masthead from **MIDI 1** to **MIDI 8**, released the CC 21 mapping that was there, and reported "**Applied 8 of 8 assignments from Sound Controllers.**" |
| The profile's assignments are visible | Green **CC 78 / CC 73 / CC 75 / CC 72** badges appeared on envelope 1's delay, attack, decay and release, and **CC 76** on LFO 1's rate — exactly the controllers the profile names |
| The profile's assignments are live | CC 74 at 127 moved filter 1's cutoff from **126 Hz to 20.00 kHz**, and CC 71 at 100 moved its resonance to **4.43** |

One defect was found by looking rather than by testing, and is fixed: appending
`timbre` to the modulation sources left the frontend's label list one longer
than the `modNN_source` parameter's range, so the guard that only trusts a label
table when it matches the engine's own range correctly refused it — and every
matrix source read "0" instead of "—". The parameter range was widened to match
the enum (ADR-0044).

### The output oscilloscope, 2026-09-11

The scope is the one part of Phase 7a the suite cannot judge: it can assert that
the right numbers reach the page, and not that the page draws them.

| Checked | Result |
|---|---|
| The scope sits beside its controls | In the Output module, 200 px wide, with the three knobs still in the module beside it. The first version took its width from the container, grew to 370 px tall and pushed the knobs out — fixed, and the reason is in the stylesheet |
| An idle synthesiser reads as idle | A flat zero line, the canvas dimmed, and the caption reading **silent** — not an empty panel, which is what a scope that had failed would look like |
| A held note draws its waveform | A violet trace of about two cycles across the sweep, with the caption reading **−34.1 dB** |
| The reading is the real level | Opening filter 1 from 126 Hz to 20 kHz took the same note from −34.1 dB to **−26.0 dB**, and the trace grew with it. Nothing is auto-scaled: the trace is small because Apollo's output genuinely is (ADR-0017) |
| A released note leaves no stale trace | Two seconds after the note-off the scope was back to a flat line and **silent** (PRD §30.1, CLAUDE.md §26.1) |

### The five per-source oscilloscopes, 2026-09-11

Driven with real MIDI into the running standalone, at 1920×1080 with 150 %
display scaling. What the suite cannot judge here is not only whether the page
draws the numbers, but whether the five scopes are telling five different stories
— which is the entire point of having them.

| Checked | Result |
|---|---|
| Each scope sits beside what it shows | Oscillator 1's and 2's in their own modules under the wavetable selector, the sub's and the noise's in theirs, and the post-filter one in Filter 2 captioned **POST-FILTER**. All at 200 CSS px, with the knobs still beside them |
| A source that is off reads as off | With the default patch — oscillator 1 alone — a held A2 drew a clean two-cycle sine at **−0.0 dB** while oscillator 2, sub and noise all showed a flat line and **silent**. Not blank panels: the silence was captured |
| Each source shows its own signal at its own level | With all four up, oscillator 1 read **−0.0 dB**, oscillator 2 **−10.0 dB** at 32 % level and the same pitch, the sub **−11.0 dB** at half the frequency and visibly fewer cycles across the same sweep, and the noise **−13.7 dB** as a dense hash rather than a waveform |
| The taps are where they are documented to be | The post-filter scope read **−0.0 dB** and the output scope **−22.0 dB** at the same instant, with master at 0 dB — the engine's polyphony headroom gain, visible as the difference between the two (ADR-0017) |
| A released note leaves no stale trace anywhere | Two seconds after the note-off all five read **silent** with a flat line, with the source levels still up — so the silence came from the capture, not from the levels |

### Modulator traces, metering and wavetable displays, 2026-09-12

Driven with real MIDI into the running standalone, at 1920×1080 with 150 %
display scaling. Almost none of this can be judged by the suite: it can assert
that the right numbers are produced, and not that the picture drawn from them
means what it is supposed to.

| Checked | Result |
|---|---|
| The wavetable display follows the position | At position 0 % both oscillators drew a pure sine; taken to 25 % the wave became visibly asymmetric with a steeper falling edge — the morph towards a saw, captioned **WAVE 25 %** |
| The display and the scope say different things about the same oscillator | Beside each other in the Oscillator 1 module: the display showing the shape at the current position, the scope showing what that shape produced at **−0.6 dB** once level and unison had had their say |
| An envelope trace is the envelope, not its settings | With a 10 s attack, a clean rising ramp captioned **attack · 0.54**. With a 1 s attack captured later in the note, the 100 ms decay and then a flat line at **sustain · 0.82** — the decay is a shape no static outline would have placed at that moment |
| An idle envelope reads idle | Flat on the floor of the canvas, captioned **idle · 0.00**, rather than an empty panel |
| An unrouted LFO says so | LFO 1 with nothing routed: a flat line, dimmed, captioned **unrouted** |
| A routed LFO traces at the right rate | Routed to filter 1 cutoff at +100 %: exactly one sine cycle across the one-second window at **1.00 Hz**, captioned with its live value |
| The meter agrees with the scope | A held A2 read **−26.3 dB** on the meter against the output scope's **−26.2 dB** at the same instant, with **1/16** voices |
| Clipping is visible and said in words | A ten-note chord at maximum master drove the meter to full, the bar caps to red, the channel wash to red and the caption to **CLIP**, with **10/16** voices. The peak had already fallen to −2.1 dB by then while the clip was still held — which is exactly why both exist |
| The display zoom magnifies the trace and nothing else | ×4 on the output scope: the trace four times larger, the button lit, and the reading still **−29.6 dB** — matching the meter beside it |

### The React interface, 2026-09-12

The migration changes nothing a user can see, which makes "looks the same" the
entire acceptance criterion and a test suite almost useless for checking it. All
of this was driven by hand against the running standalone, with the application
left open throughout at the developer's request.

| Checked | Result |
|---|---|
| The page is the page | Side by side with the build before it: same masthead, same modules in the same order, same knobs, same values, same footer. The only difference found was the footer sitting below the window, from an extra wrapper element between `body` and the application frame — fixed by making the React root *be* the frame |
| A knob still tracks the hand | Dragging Position moved it and the wavetable display followed; releasing returned the control to rest. The first build did **not**: the held flag lived in a ref, and a ref mutated on pointer-up re-renders nothing, so the control stayed lit. Now `useState` in both draggable controls |
| Pictures still arrive at frame rate | A held A2 drew the wavetable shape at 18 %, the scope beside it at **−0.6 dB**, envelope 1 at **sustain · 1.00**, and LFO 1 as exactly one sine cycle across the one-second window at **1.00 Hz** |
| The matrix still lights what it moves | Routing LFO 1 → Filter 1 Cutoff at +100 % turned the masthead to **MOD 1/16** with its lamp lit, and filter 1's Cutoff knob green while Resonance stayed violet |
| MIDI Learn is still a mode | Toggling it outlined every assignable control, revealed **Clear all**, and changed the footer hint. Clicking Oscillator 1's Level armed learn — amber **LEARN** badge, status line naming the parameter |
| A learn still completes and a mapping still drives | CC 74 completed the learn and the badge turned green; sending CC 74 = 40 moved Level to **31 %**. Tabbing to it in learn mode and pressing Delete released it again, leaving the instrument as it was found |

### Gold on gunmetal, 2026-09-12

The recolour (ADR-0051) is a token change, so the suite cannot judge it at all:
every rule below the token block reads a variable, and the compiler is perfectly
happy with a palette nobody can read. Driven by hand against the running
standalone at 1920×1080, 150 % scaling.

| Checked | Result |
|---|---|
| The accent is gold everywhere it was violet | Knob arcs, the filled chip of a selected switch, module index badges, the rule under the wordmark, focus rings and the masthead border |
| `#F7EF8A` is where it is loudest | The scope, wavetable and post-filter traces, drawn on the dark display insets; a held value's readout; and every hover |
| A filled gold chip stays readable | The selected wavetable and LFO-shape buttons carry the ground's own near-black as their label. Light text on gold was unreadable at ten pixels and was changed |
| Green and red kept their jobs | The MIDI badge and lamps green, the matrix's assigned-row wash green, the clip indicator red — unchanged in meaning, only in shade (§24.2) |
| The displays are dark insets again | And a silent scope's canvas dims cleanly against the dark ground, which is exactly what failed in the light build |
| Two identical modules look identical | Oscillator 1 and Oscillator 2 render at the same shade with the same knob and trace brightness, with an **OFF** chip in the heading as the only difference when one is at level zero. The 55 % opacity that used to mark an inactive module made a matched pair look like two different products, and is gone everywhere — sub, noise, filter 2 and the MIDI module carry the chip too (ADR-0052) |
| MIDI Learn still reads as a mode | A dashed hairline outline on every assignable control — grey, not gold, and deliberately: the accent means *interaction*, and in this mode nothing has been interacted with yet. It turns green on hover and solid green once mapped. Green **MIDI LEARN**, **CLEAR ALL** revealed, footer hint changed |

An interim light build — ivory and gold — was made and discarded the same day.
The reason it failed is recorded in ADR-0051 rather than here, because it is a
design fact worth not repeating rather than a verification result.

### The effects rack, 2026-09-12

Driven against the running standalone after 8a. What was confirmed on screen:

| Checked | Result |
|---|---|
| The rack renders as a chain | **FX RACK** across the width of the workspace: six numbered positions, each a dropdown, all reading `—` on a fresh instance |
| The option list is honest about what exists | `—`, `DISTORTION`, then `DELAY (soon)`, `REVERB (soon)`, `GATE (soon)`, `COMP (soon)`, `EQ (soon)` — the five whose phases have not landed say so rather than appearing to work |
| Choosing an effect fills the slot | Selecting `DISTORTION` in position 1 set the slot, and the position number turned gold. A slot naming an unbuilt effect leaves its number dim, because the engine will resolve it to empty and the interface must not claim otherwise |
| The panel follows the chain | The **DISTORTION** panel carries the `OFF` chip while nothing in the rack selects it, and the chip cleared the moment the slot was filled (ADR-0052) |
| The panel reads correctly | Curve `SOFT`/`HARD`/`DIODE`, State `ACTIVE`/`BYPASS`, and four knobs — Drive `+12.0 dB`, Tone `20.00 kHz`, Output `+0.0 dB`, Mix `0 %` — each with its unit, matching the registry |
| The panel is reachable from the keyboard | The whole interaction above was performed with Tab and the arrow keys, including the type-ahead that selects an effect by name (CLAUDE.md §39) |

**Not driven by hand this session, and stated plainly rather than implied:** the
audio through the rack. Synthetic mouse input did not reach the WebView in this
environment — native JUCE menus and keyboard input did — and no MIDI source was
attached to play a note into the configured chain. What that check would have
shown is instead asserted directly against the processor in
`Tests/Audio/EffectsIntegrationTests.cpp`: the same note rendered with an empty
rack and with the distortion at full mix differs by 0.377 RMS, stays bounded and
finite, reports its latency to the host, and survives a state round trip.

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

### Effects rack

Measured in Phase 8a, stereo, on the finished mix — once per block no matter how
many notes are held, which is the whole argument for putting a shaper here rather
than in the voice (ADR-0033).

| Rack | Cost |
|---|---:|
| Empty | 0.062 % |
| Distortion, bypassed | 0.075 % |
| Distortion, hard clip at 4x | 1.374 % |
| Distortion, diode (`exp`) at 4x | 2.152 % |
| Distortion, soft (`tanh`) at 4x | 2.666 % |

The empty row is the honest baseline: it is the benchmark's own input loop, not
the rack, which does nothing at all when no slot is filled. Hard clipping against
the soft curve separates the conversion from the transcendental — roughly 1.3 %
each — which is where any later optimisation should start, and it is Phase 10
that owns profiling.

### Visualisation capture

PRD §30.1's own requirement: scopes for every source must not materially reduce
polyphony, and that has to be measured rather than argued. Re-measured in Phase
7c with everything a watched instance pays — the five taps inside the voice loop,
the modulation traces sampled between chunks, and the output tap and meter after
them.

| Voices | Render alone (nothing watching) | Render + everything | Capture adds |
|---:|---:|---:|---:|
| 1 | 0.303 % | 0.418 % | 0.115 % |
| 8 | 2.565 % | 2.771 % | 0.207 % |
| 32 | 9.352 % | 9.820 % | 0.468 % |

Building frames is the message thread's work, not the audio thread's:

| Work | Cost |
|---|---:|
| All six scope frames, per block | 0.147 % |
| The same at 30 frames a second | **0.047 %** |

**So the whole of visualisation costs well under one per cent of a core**, and
the answer to PRD §30.1 is that it does not cost polyphony.

It is not free, though, and that is the interesting half of the measurement. The
output tap alone was flat in voice count — one linear pass over the finished
buffer, falling as a fraction of the render because the render grew and the pass
did not. The five source taps are inside the voice loop and scale with polyphony
instead. An instance whose editor is closed has no reason to pay any of it, and a
session holding twenty instances is showing one — so capture is armed by the
editor attaching its outbound handler and disarmed when it detaches, and the
first column above is also the measurement that the gate works (ADR-0048).

**These two cases are now measured alternately rather than one after the other**,
and that change matters more than any of the numbers. Measured sequentially, each
for twelve seconds, the two are taken on a machine that has warmed up in between:
the 7b figures were produced that way, and re-running them sequentially after 7c
gave a capture overhead that was *lower* at eight voices than at one and outright
negative at thirty-two. Alternating puts both under the same drift. The figures
above are therefore not comparable with 7b's, and the difference is method rather
than code (`measurePair`, Tests/Performance/Benchmarks.cpp).

### Interface traffic

Not a CPU measurement, but measured for the same reason and found the same way —
by looking rather than assuming.

| Message | Rate | Before 7c | After |
|---|---:|---:|---:|
| `scopeFrames` | 30 Hz | 18,395 B | **5,199 B** |
| `instrumentFrame` | 15 Hz | 20,093 B | **2,765 B** |

The before column is not a straw man: it is what 7a and 7b were shipping.
ADR-0047 rounded each point to three decimals believing that halved the message,
and never measured it. `juce::JSON` spells a double between 0.1 and 1 to sixteen
decimal places — so the rounding made the text *longer* — and pretty-prints every
array element on its own indented line. Points now travel as integer thousandths
on one line. Both figures are asserted as budgets and logged by the tests, so a
change that multiplies them is caught there (ADR-0049).

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
| 14 | ~~The MIDI Learn interface has not been driven by hand~~ | **Closed** | Verified 2026-09-10 (§5a). The learn mode, the badges, the tooltips, Escape, a real controller completing a learn, a mapping surviving a clean restart and still driving its parameter, and an MPE Configuration Message reconfiguring Apollo from the MIDI stream were all driven by hand against the running standalone. |

---

## 7. Blockers

**None.** Phase 8b can begin.

---

## 8. Recommended next action

Continue with **Phase 8b — the delay**. The rack and its first effect are done;
8b is the first effect to put the rack's ordering to a real test, because it is
the first time there are two things to order.

1. **It is the first effect with memory.** The distortion is memoryless apart
   from its filters, so nothing it does depends on what came before. A delay line
   makes `reset` load-bearing — a transport jump must not leave the previous
   position in the buffer — and makes tail reporting mean something for the first
   time, since the rack's tail rule has so far only been asserted, never
   exercised.
2. **Feedback needs a stability argument, not a clamp.** ROADMAP asks for
   validated feedback stability; the honest version measures what a feedback path
   at its maximum setting actually does over minutes, including with the delay
   time being swept while it rings.
3. **Host tempo arrives with it.** PRD §20 and CLAUDE.md §38 both require sync
   that degrades gracefully when the host offers no tempo — which the standalone
   never will, so the fallback is the normal case rather than the edge one.
4. **`fx_delay_time` is the last inert parameter.** It has been in the registry
   and on screen since Phase 2, connected to nothing. 8b is what closes that gap
   and makes every registered parameter one that affects audio.

Also still open from 8a: whole-chain validation — arbitrary orderings, and
aliasing measured across a chain rather than one stage — which is 8f and needs
more than one effect to mean anything.

A spectrum analyser remains the one unticked Visualization item, and both PRD and
ROADMAP mark it optional. It is an FFT, a window function and a log frequency
axis — genuinely separate DSP rather than more of the transport built in 7a-7c —
so it is better placed after the effects work than before it.

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
