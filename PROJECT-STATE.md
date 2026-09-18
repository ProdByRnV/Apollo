# Apollo — Project State

> **Purpose:** the authoritative, verified record of what actually exists in this
> repository. The specification documents describe what Apollo *should* become;
> this file describes what it *is*. Nothing is recorded as complete here unless it
> was built and run.
>
> Update this file at the end of every roadmap step.

**Last verified:** 2026-09-17
**Apollo version:** 0.1.0

---

## 1. Current position

| | |
|---|---|
| **Phase** | Phase 10 — Performance, DSP Validation & Host Compatibility, in progress; **10a, 10b and 10c complete** |
| **Status** | **Phases 8 and 9 are done, and Phase 10 is three sub-phases in.** Apollo is now measured, pinned, and profiled on a harness that reports how much it should be believed. 10c replaced a benchmark whose figures moved forty per cent between idle runs with one that pins its thread, normalises against a fixed reference kernel, reports the spread of every row, and says at the top when the machine was too busy to be measured on (ADR-0069) — and then measured what nothing had measured before: the **worst-case callback** rather than an average, and the worst patch the controls can build with everything running at once, which misses its deadline one block in a hundred (§5b, issue 13). 10b added the machinery every later phase leans on: fifteen renders — ten of them the sounds Apollo ships — each compared against a reference compiled in beside it, so a change that alters what the instrument sounds like fails the build instead of going unnoticed (§5d, ADR-0068). A reference is a description of the audio rather than a wave file, because bit-exact samples are not something four compilers agree on. The assembled instrument is now measured rather than only its parts: in tune to within three quarters of a cent, 0.0013 % THD+N on its one sine source, exactly silent at rest, no DC offset, no inharmonic content above -121 dBc, and stable for a minute at the worst settings its own controls allow (§5c, ADR-0067). A `.rnv` preset is written, read, validated, migrated and bounded, sharing one validator with host state and deliberately carrying no part of the user’s controller setup (ADR-0061); the library it lives in is located per platform, scanned on a background thread under three bounds, and saved to atomically so an interrupted save cannot destroy the preset already there (ADR-0062); and the browser lists, searches, filters, loads and saves it, asking for a preset by a number the backend assigned and unable to express a path at all (ADR-0063); and the library it opens with is ten factory sounds and the init patch, compiled into the plugin rather than installed, stored as the parameters each one changes and played a note by the suite to prove each makes one (ADR-0065). Behind them, all six rack slots hold all six effects: the distortion (three curves, 4x oversampled), a stereo delay (gliding time, bounded feedback, ping-pong, tempo sync), a reverb (an eight-line feedback delay network whose decay measures as RT60), a dynamics pair — a peak-detected gate with hold, and a feed-forward RMS compressor with parallel mix — and a seven-band parametric equaliser of RBJ biquads in double precision, whose response curve the interface draws and whose handles can be dragged. 8f validated the whole chain: all 720 orderings, a rack rearranged every block, a full rack at the top of every range decaying to exact silence, and a six-effect preset round trip. A full rack costs 2.83 % of one core |
| **Milestone** | M10 — Performance, validation & host compatibility |
| **Next step** | Phase 10d — optimisation of what 10c identified. The target is now stated precisely rather than as a suspicion: the worst patch the controls can build misses its callback deadline one block in a hundred at full polyphony (issue 13), and the cost is 1088 interpolating oscillators rather than anything in the effects rack, which costs 2.8 % of a core in total. Two measurement questions are carried with it (issues 6 and 18) (ROADMAP §2a) |

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
- Four built-in tables of 16 frames — Sweep, Pulse, Formant and Fold — described
  as spectra and rendered into their mipmaps by one builder that a table read
  from a file goes through as well (9e, ADR-0066). They began as morphs between
  classic shapes and were replaced once there was something to replace them
  with.
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
  bypassed reverb is not ringing out. (The tail was the longest figure in the
  chain until 8f measured two ringing effects in series and found it should be
  their sum — ADR-0060.)
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
- **Not yet present in 8a:** the delay, reverb, gate and compressor, EQ, and the
  whole-chain validation that needs more than one effect to mean anything (8f).

### The delay (Phase 8b)

- **A stereo delay with a glide, not a jump.** Moving the time moves the read
  pointer, and Apollo glides it: the repeats stretch and pitch as the time
  changes, which is the tape behaviour a swept delay is reached for. The line
  reads at a fractional position so the glide is continuous rather than stepping
  a sample at a time (ADR-0056).
- **Feedback that cannot run away**, because the control cannot ask it to: the
  parameter scales to a loop gain of 0.95, below unity with every filter out of
  the way. Thirty seconds at the top of the range with silence going in decays
  every second, never exceeds what went in, and ends in near silence — which is
  ROADMAP's "validate feedback stability" as something that passes rather than
  something asserted.
- **The filters are inside the loop**, so each repeat is darker than the last
  rather than every repeat being equally dark — a lowpass for damping and a
  highpass that keeps DC and rumble from circulating for minutes. Both switch out
  entirely at the ends of their travel.
- **Free or synced.** Milliseconds, or one of fourteen note values against the
  host's tempo, with dotted and triplet forms. The playhead is read once per
  block in `processBlock`, and every optional step of that — no playhead, no
  position, no tempo — falls through to 120 BPM, so a synced delay still repeats
  musically in the standalone, which has no transport at all (CLAUDE.md §38).
- **Ping-pong is crossed feedback**, so a repeat alternates sides; a sound that
  entered on the left comes back on the right.
- **It adds no latency.** A delay is not a lookahead: the dry path leaves when it
  arrives, and at a dry mix the output is the input sample for sample. What it
  reports instead is a tail, derived from the loop gain, and the processor adds
  that to the envelope's release because the two are in series.
- **Bypass empties the lines rather than freezing them**, so switching the effect
  back in cannot replay what was playing when it left. That is the first real
  difference between two effects' bypass behaviour: the distortion's exists to
  preserve latency, the delay's to forget.
- **`fx_delay_time` is finally connected.** It had been in the registry and on
  screen since Phase 2, attached to nothing; every registered parameter now
  affects audio.
- **Not yet present in 8b:** reverb, gate and compressor, EQ, and whole-chain
  validation. Also deliberately absent: per-channel delay times and a
  self-oscillating feedback setting (ADR-0056).

### The reverb (Phase 8c)

- **A feedback delay network, not a bank of combs.** Eight delay lines of
  mutually prime length, mixed into each other by a Hadamard matrix, with four
  diffusion allpasses per channel in front and a pre-delay before those. The
  classic comb arrangement is simpler and rings at each comb's own harmonic
  series, which is what gives a bad reverb a pitch; mixing every line into every
  other one spreads the modes by construction (ADR-0057).
- **Stability is a property, not a measurement.** An orthonormal matrix preserves
  energy exactly, so the only thing that can make the tail grow is the decay gain
  on each line, and those are below one by construction. The test is then a check
  of something already believed rather than a hope: a minute at the longest decay
  with nothing going in, staying under the envelope RT60 describes, never louder
  than what went in, and gone by the end.
- **Decay means RT60 and measures like it.** Each line's gain is derived from its
  own length, so every line decays at the same rate in time; the tail falls
  56 dB in a 1 s decay and 61 dB in a 3 s one, measured as a slope between two
  later times because a reverb *builds up* before it decays.
- **The denormal work is real here.** A tail spends most of its life below
  -200 dB, and every line's feedback write is flushed below 1e-18. The test
  asserts the tail reaches **exactly zero** rather than merely getting small.
- **Two level decisions that were measured.** Input and output are both scaled by
  1/sqrt(4): energy-preserving for the decorrelated tail, and — the reason it
  matters — enough to stop the first pass, where all four lines hold the same
  signal, from summing to twice the input. It did, before the scaling.
- **Room and hall are different lengths**, not different decays: 26-57 ms against
  52-119 ms, scaled by size. A hall is not a room with a longer tail; its walls
  are further away.
- **Bypass keeps decaying** rather than freezing, and costs almost exactly what
  running it costs — 1.28 % against 1.36 % — because the tail has to go somewhere.
  Removing it from the chain is the free option.
- **Not yet present in 8c:** gate and compressor, EQ, and whole-chain validation.
  Deliberately absent from the reverb itself: modulated line lengths, modelled
  early reflections, and a diffusion control (ADR-0057).

### The gate and the compressor (Phase 8d)

- **The first effects whose gain depends on what the signal is doing.** Both are
  a detector and a gain computer, and they share the detector class because they
  disagree about only one thing in it.
- **Peak for the gate, RMS for the compressor**, and that is not a preference:
  RMS follows how loud something sounds and ignores transients too short to hear
  as level, which is right for a compressor and wrong for a gate, whose whole job
  is catching the transient that arrives while it is shut (ADR-0058).
- **The attack convention is stated and measured.** "10 ms" means the gain
  travels 1 - 1/e — about 63 % — of the way in 10 ms; the ramp is measured
  against that to within a percentage point at three settings. The compressor as
  a whole is slower, because the RMS detector has to notice the signal first, and
  that lag is measured separately rather than folded in silently.
- **Stereo is linked**, so a loud left channel cannot duck only the left and drag
  the image sideways. One detector, fed the louder channel, applied to both.
- **Hold is what makes a gate a gate.** A tone swinging 6 dB either side of the
  threshold eight times a second moved an unheld gate eleven times in two thirds
  of a second; with 60 ms of hold, once. That is PRD §22's "avoiding audible
  pumping" as a measurement.
- **Feed-forward, hard knee, no lookahead.** The ratio on the dial is the ratio
  applied, and neither processor adds latency.
- **A real bug the tests caught**: `prepare` and `setSettings` each derived the
  gate's ramp times and only one performed the attack/release swap, so a prepared
  gate opened slowly and shut quickly — backwards, and deaf to anything short.
  One `refreshDerived` now serves both.
- **Cheap**: the gate costs about 0.03 % of a core above an empty rack and the
  compressor about 0.11 %, stereo — an order of magnitude less than the
  distortion, because neither runs a filter bank or a delay network.
- **Not yet present in 8d:** the EQ (8e) and whole-chain validation (8f).
  Deliberately absent: a soft knee, peak detection on the compressor, and
  sidechain input, all of which PRD §23 lists as future work.

### The parametric equaliser (Phase 8e)

- **Seven bands, not the four the specification asked for.** The developer asked
  for the equivalent of Fruity Parametric EQ 2, and the later requirement governs
  (CLAUDE.md §42; ADR-0059). CLAUDE.md §22 and PRD §24 were corrected rather than
  left to disagree with the code. Four bands force a choice between a high pass,
  a low shelf, a midrange cut and an air shelf, when the work usually wants all
  four *and* somewhere to notch a resonance.
- **A biquad, where the voice filter is deliberately not one.** The state-variable
  argument still stands for a synthesiser, whose cutoff is swept constantly; an
  equaliser's bands sit still, and in exchange the biquad gives the named,
  standard response shapes an equaliser is expected to have. The RBJ cookbook is
  where the coefficients come from, in **double precision**, because a narrow band
  low in the spectrum at a high sample rate puts the poles close enough to the
  unit circle that single-precision quantises their position audibly.
- **Stability is checked, not asserted.** Every design in the whole parameter
  space — eight shapes, the full frequency, gain and bandwidth ranges, four sample
  rates — has both poles inside the unit circle, and the frequency is clamped
  below Nyquist before any trigonometry, so no setting a parameter can express can
  produce an unstable one.
- **Slope is instances, and the gain multiplies with it.** Order N is N identical
  sections in series: 12, 24, 36 or 48 dB per octave for a pass filter, and N
  times the gain for a bell or a shelf. Not normalised away, deliberately — an
  equaliser that quietly divided the gain to compensate would be one whose
  displayed gain is not its gain. ROADMAP asks for *predictable* gain behaviour,
  and a stated rule is what makes it predictable.
- **Bandwidth in octaves rather than Q**, because it is the half of that pair a
  musician can hear: one octave is one octave wherever the band sits, whereas the
  Q that produces it is a different number at 50 Hz than at 5 kHz.
- **A transparent band is skipped, not processed.** A band that is off, muted, or
  a gain shape at exactly 0 dB is the identity — at 0 dB the cookbook's numerator
  becomes its denominator term for term — so it is bypassed entirely. A freshly
  placed equaliser is bit-transparent and costs **0.037 % of a core against an
  empty rack's 0.035 %**, which is what makes it safe to leave in the chain.
- **Seven bands dialled in cost 0.35 % at one instance and 0.48 % at four.** Four
  times the sections for a third more time, measured at every order in between: a
  biquad's state update waits on the previous sample's, so one section per band
  leaves most of the processor's execution units idle and the extra sections fill
  those slots rather than queueing behind them. Raising the slope is close to
  free, and that is a measurement rather than an argument about the arithmetic.
- **No latency and no linear-phase mode.** These are IIR filters working on the
  samples in front of them. The reference's linear-phase mode is an FFT and a
  latency budget rather than a variation on this, and is deliberately absent.
- **Smoothed per block, in the units the ear uses** — frequency in the log domain
  and gain in decibels, so a swept band travels evenly instead of rushing through
  the bottom of its range. Coefficients are never interpolated, only the settings
  they are designed from: an interpolated coefficient set is not necessarily a
  stable filter, while a filter designed from an intermediate frequency always is.
- **A band coming back into circuit starts from silence**, rather than pouring
  seconds-old audio back in — the bug the reverb's bypass had in 8c.
- **The interface draws the response curve, and this is the one place the page
  holds DSP arithmetic.** A curve is a continuous function of frequency, and
  sending it as points would mean the engine re-serialising a hundred and sixty
  values every time a knob moves, for a picture that depends on nothing the page
  does not already have. `WebUI/src/params/eqCurve.ts` transcribes the cookbook;
  `Tests/DSP/EqualiserTests.cpp` pins the exact decibels a table of reference
  settings must produce, and every number in that table was produced by the
  TypeScript and is asserted against the C++.
- **The instrument frame now carries the engine's sample rate**, which is why.
  The same +6 dB shelf at 15 kHz reads 5.60 dB at 20 kHz at 44.1 kHz and 6.62 dB
  at 96 kHz: the bilinear transform compresses the top of the spectrum, and how
  much depends on where Nyquist is. A curve drawn at an assumed rate would be a
  picture the sound does not agree with.
- **The curve is a control as well as a display**, and the only one on the page
  that is both. Dragging a handle sets that band's frequency and gain, bracketed
  as one automation gesture per parameter; a shape with no gain moves only in
  frequency, so dragging a low pass up the screen does not write a value the
  engine would ignore. The knobs remain, still built from metadata, still the way
  to set an exact value and the way to reach the control from a keyboard.
- **Forty-four parameters, all arriving at once**, because a band added later
  could not be numbered without renumbering the bands after it and a parameter ID
  is permanent (ADR-0054's reasoning, applied again).
- **The rack is now complete.** `EffectsRack::isImplemented` returns true for
  every effect the enum names, and the test that used to assert an unbuilt effect
  resolved to an empty slot now asserts the opposite: the only value that means
  nothing is the one that means nothing.
- **Not yet present:** whole-chain validation (8f). Deliberately absent: a
  linear-phase mode, per-band solo, a spectrum analyser behind the curve, and
  reorderable bands — a cascade of minimum-phase filters commutes, so reordering
  would be a control that changes nothing (ADR-0059).

### Whole-chain validation (Phase 8f)

- **Every ordering, not a representative few.** Six effects arrange 720 ways, and
  all 720 are rendered: each finite, each bounded, the loudest sample anywhere
  0.6861 from a 0.5 input through a chain that distorts, compresses with makeup
  and boosts an equaliser band. Rendering all of them costs a fraction of a
  second and removes the question of whether the representative orderings were
  the interesting ones.
- **Latency is a property of the set, not the order**, asserted across all 720.
  It cannot fail today — latency is summed and addition commutes — and it is
  asserted anyway because the *host* depends on it: an ordering that changed the
  reported latency would make delay compensation wrong every time somebody
  rearranged the rack, and a future effect whose latency depended on what fed it
  would break it silently.
- **Every effect is audibly in the chain.** Switching any one of the six out of a
  full rack changes the output measurably. That is Phase 8's "every effect
  operates independently" as a measurement: six effects in series give plenty of
  room for one to be quietly swallowed — a gate that never opens, settings that
  never reach an effect, a slot resolved away — and none of those would fail any
  test that looks at one effect alone.
- **The chain is rearranged on every block for 240 blocks**, with effects leaving
  the chain entirely and rejoining it, while audio flows. Automation can do this
  at block rate and a preset load can rearrange all six slots at once.
- **A full rack at the top of every range decays to exact silence.** Maximum
  delay feedback, a 20 s reverb decay, 12 dB of compressor makeup, 12 dB on every
  equaliser band and 6 dB of output trim, behind a hard clipper at full drive. It
  peaks at 14.59 as the chain fills — which it is *supposed* to do, with that
  much gain in it — turns around, and is at exact zero after a minute of silence.
- **A full rack of six, in an order that is not the enum's and every effect
  dialled away from its defaults, survives a preset round trip** and still sounds
  afterwards. Phase 8's last exit criterion.
- **A full rack costs 2.83 % of one core**, stereo. Against the 150 % the heaviest
  patch costs at full polyphony, the effects are not where Apollo's CPU goes.

**Two defects were found here that nothing testing one effect could have found**,
and both are fixed (ADR-0060):

- **A chain's tail was the longest in it, and should be the sum along it.** Right
  for one tail-producing effect, which was the only case the rack's tests ever
  built. Put a delay in front of a reverb and the delay is still emitting repeats
  seconds later — so the reverb is still being *fed* at that point and takes its
  own decay to fall silent from there. Measured: a delay reporting 5.095 s and a
  reverb reporting 2.020 s were still audible at 5.095 s, the moment the old rule
  declared the chain finished. The tail is what an offline render waits for after
  the last note, so under-reporting it truncates a bounce.
- **`reset` meant something slightly different in each effect.** Fifteen smoothed
  controls live across the six, and only two of them — the delay's time and the
  equaliser's trim — were placed on their targets by a reset; the other thirteen
  were left wherever their ramp had reached. The delay's own code already carried
  the argument for why that is wrong, in a comment on the one line that did it.
  All six now settle every control they own.

**Fixing the second broke three tests that had been passing because of it**, and
all three are stronger now. Each put a fully wet delay or reverb in the chain and
asserted a note sounded within a few blocks. A fully wet effect has no dry path,
so there is nothing to hear until the first repeat or reflection arrives —
120 ms, 500 ms and 40 ms respectively, far beyond what those tests rendered. They
passed because `prepare` set the mix from the settings it had at the time, which
were still the dry defaults, and the ramp towards wet leaked dry signal through
the opening blocks. The audible consequence of the fix: a prepared plugin is now
*at* its settings rather than gliding to them, so pressing play no longer swells
into a wet effect the session was saved with.

- **One interface defect, found by building a chain by hand** (§5a): a duplicated
  slot lit up as though it were in the chain, when the engine has resolved
  duplicates to first-occurrence-wins since 8a. The page now runs the same
  resolution the engine does, and a duplicated slot says `duplicate` with its
  number unlit.

**Phase 8 is complete.** All five exit criteria are closed.

### The preset document (Phase 9a)

- **A `.rnv` is the state document Apollo already writes into a host project,
  as text, plus four things a project does not need** — name, author, category
  and comment (ADR-0053). One file, one sound. About 10.6 KB for a default
  patch, and legible: it opens in a text editor, diffs against another preset,
  and can be repaired by hand when something goes wrong.
- **One reader, reached two ways.** `readState` was split into the part that
  unwraps JUCE's binary blob and the part that validates a parsed document, and
  the preset reader calls the second half. Everything that decides whether a
  document may be loaded — the product check, the version bounds, the migration
  path, the guarantee that a rejection leaves the current sound alone — is
  therefore the same code for a project and for a file on disk. The tests offer
  the same malformed documents to both paths and assert they are refused for the
  *same reason*, so the sharing is checked rather than assumed (ADR-0061).
- **A preset carries the patch, not the controller.** The learned MIDI mappings
  and the four expression parameters describe the machine in front of the user
  rather than the sound they made, so they are stripped when a preset is written
  and preserved when one is read. Sending somebody a patch must not send them
  your keyboard's setup, and auditioning one must not rewire theirs.
- **Preserved, not merely omitted**, which is the half that is easy to get
  wrong: a document that does not mention a parameter is loaded as that
  parameter's *default*, so a preset that simply left the MPE zone out would
  reset it. The reader captures the controller state before applying anything
  and puts it back afterwards.
- **The exclusion rule is the registry's own.** Those four are already the only
  parameters marked *not automatable*, for exactly this reason, so
  `isExcludedFromPresets` reads that flag rather than keeping a second list that
  could fall out of step.
- **The metadata is a child of the state tree, not a wrapper around it.** The
  root stays the state tree, so one reader serves both paths; a document written
  before metadata existed simply has no such child and needed no schema bump
  (ADR-0043's argument, applied again); and because it lives in the tree, the
  name of the loaded preset travels into the host project, which is what will
  let a session reopen showing the sound it was on rather than "Init".
- **Metadata can be read without loading the sound**, which is what an index is
  built from: the browser will read a folder of these and must not change the
  instrument to do it. Validated on the same terms a full read is — a file this
  build could not load has nothing worth listing — but it stops before building
  a tree of every parameter, which is most of the cost.
- **Every field is bounded and every field is optional.** A name is free text
  from a file on disk that ends up in a list the interface draws; a
  megabyte-long one is not a name. Long fields are cut rather than refused,
  because the length of a comment is not a reason to reject a sound, and the
  bound is applied on the way out as well as in, so Apollo cannot write a file
  it would read back differently.
- **The failures are weighted the way host state's are.** The worst outcome is
  not that a preset failed to load but that loading a bad one destroyed the
  sound already there. Seven ways a document can be refused are each tested for
  the right reason *and* for leaving three parameters, and the loaded preset's
  name, exactly as they were.
- **Something far too large to be a preset is refused on its size alone**, so
  pointing Apollo at a video file costs a length check rather than an attempt to
  build a DOM from it.

**Not present in 9a, by design:** any file I/O at all. This layer converts
between a document and text; the disk, the library paths, the scanning and the
filesystem failure modes are 9b. There is correspondingly nothing here a user
can see yet — the browser is 9c.

### The library on disk (Phase 9b)

- **Two roots, both optional.** The user's library sits under the platform's own
  per-user application-data location and the factory's under the shared one,
  each in `Apollo/Presets`. Nothing in Apollo spells out `%APPDATA%`,
  `~/Library/Application Support` or `~/.config` — JUCE's special-location
  lookup knows those, and Apollo contributes the two folder names beneath
  (CLAUDE.md §46, ADR-0062).
- **Missing is reported separately from empty**, because only one of them
  suggests something is wrong. A user who has never saved a preset has no user
  folder, and every build until 9d has no factory folder; neither is an error.
- **A bank is a folder**, so the index mirrors the tree rather than flattening
  it, with the relative path normalised to forward slashes so a bank reads the
  same whichever platform wrote it.
- **What cannot be read is counted, not hidden.** A `.rnv` that is corrupt,
  truncated, from a newer build or never a preset is counted, so the interface
  can say "four files in this folder are not presets" rather than quietly
  listing four fewer than are there. Files without the extension are ignored
  rather than counted — a preset folder with a readme in it is a normal folder.
- **Scanning happens on its own thread and is bounded three ways**: ten thousand
  presets, eight levels of depth, and abandonment between files when the answer
  is no longer wanted. The walk is iterative with an explicit depth per entry
  rather than recursive, so a symbolic link pointing at one of its own parents
  costs a bound rather than a stack. Hitting a bound sets `truncated`, so a
  partial list can be shown as what it is.
- **Nothing scans by itself.** Constructing the library touches no disk. Some
  hosts instantiate a plugin dozens of times while building a menu, and walking
  the user's preset folder on each would be work done for nobody; the editor asks
  for a scan when it has somewhere to show the result (9c).
- **A save is atomic or it does not happen.** The bytes go to a temporary file
  beside the target and are moved into place only once complete. Losing the
  preset *being* saved is an inconvenience the user can repeat in ten seconds;
  losing the one that was already there is lost work, and the failure mode is
  chosen to be the first. A finished save leaves no temporary files behind,
  which is asserted rather than assumed.
- **A preset name is not a filename.** Separators are stripped, the
  parent-directory token cannot survive, Windows' reserved device names are
  refused whatever follows the dot — `CON`, `con.rnv` and `Con.txt` are one
  refusal — and the trailing dots and spaces Windows silently discards are
  trimmed, which would otherwise make "Bell." and "Bell" the same file. A name
  that leaves nothing usable is refused rather than turned into a file called
  nothing, and the save checks where the path actually *resolved to* rather than
  trusting the filters that produced it (CLAUDE.md §40).
- **The library has an owner**: the processor, for the same reason it owns the
  MIDI mappings — a preset load changes the sound, and it must work whether or
  not an editor was ever opened.

**A cross-component bug, found by its own test, and the one worth remembering:**
a leading dot survived the first sanitiser. On Unix that makes a hidden file,
and the scanner skips hidden files — so a preset called `..bell` would have
saved successfully and then been invisible to the library that saved it. Neither
component was wrong alone. It is the same class of defect 8f found in the tail
rule and 8e found in the octave formatter: two things that are each correct and
disagree at the seam.

**These are the only tests in the suite that touch a disk**, and deliberately: a
scanner tested against a mock filesystem is a scanner tested against the
filesystem somebody imagined. Each builds a tree under the system temporary
directory and removes it afterwards, and none touches the real preset locations
— a test that wrote into the developer's own library would be a test that could
lose their work.

**Not present in 9b:** anything a user can see. The browser, the bridge commands
behind it and the trigger that starts a scan are 9c. Also deliberately absent: a
filesystem watcher, which would mean a per-platform watcher and a debounce
policy for a library that changes when the user saves something — an event
Apollo already knows about (ADR-0062).

### The browser in the interface (Phase 9c)

The panel at the top of the workspace, above the signal path, because a preset
is what you pick before there is a signal. It lists what the last scan found,
searches, filters, loads and saves — and it is the first part of Apollo's
interface that reaches a filesystem, which is what shaped every decision in it.

- **The page names a number, never a path.** A scan sorts what it found —
  factory first, then by bank, then by name, with the filename breaking ties —
  and numbers it from 1. `loadPreset` carries that number; the bridge resolves
  it against the index it published itself; an id that is not in it reaches no
  file at all. **There is no message in the protocol that can express a path**
  (ADR-0063). Nothing travels outward either: a listed entry carries a name, an
  author, a category, a bank and an origin, and no location of any kind.
- **Sorted before it is numbered**, because directory iteration order is
  whatever the filesystem feels like and two scans of an unchanged library need
  not agree. Without that, an id would mean a different sound depending on which
  scan produced it, and the list would reshuffle every time anything was saved.
- **A save is a name, not a destination.** There is no field that chooses where
  to write: saving goes to the user library, and the bank becomes one safe path
  segment through the same sanitiser a preset name goes through (9b). The
  factory root is not somewhere Apollo offers to write.
- **Absent means no.** A save over a preset that exists is refused, writes
  nothing, and comes back `ALREADY_EXISTS`; the page renders that refusal as its
  replace prompt, and only an explicit second save goes through. The check and
  the write ask the same function which file a name means, so a confirmation
  cannot end up protecting a different file from the one it overwrites.
- **The browser decides nothing.** The highlight moves when the engine says the
  sound changed, not when the row is clicked; the save form closes when the save
  is confirmed, not when the button is pressed. Both can be refused.
- **Searching and filtering never reach the engine.** They narrow a list the page
  already holds — a round trip per keystroke would be a round trip to compute
  something a microsecond of filtering answers. The search matches name, author,
  category and bank, because all four are things somebody half-remembers about a
  sound they are looking for.
- **Which preset is loaded is derived, not remembered.** The bridge keeps the
  *file* and turns it into an id by looking it up in the current index, because a
  scan renumbers. Nothing sends the file anywhere.
- **A preset load is a state load.** It replaces the whole tree exactly as a host
  project load does, so both paths now call one method on the processor, which
  rebuilds the MIDI table from what arrived and bumps the reload counter. The
  bridge marks its own loads, so a reload it did not cause is recognised as the
  host opening a project — at which point it forgets which preset was loaded.
- **What the scan could not read is shown under the list**, in words, beside the
  count: "2 files could not be read as a preset". A library quietly two shorter
  than the folder is not something a user can act on (CLAUDE.md §33).
- **The empty list says which empty it is.** Not scanned yet, no presets at all,
  no folders at all, or nothing matching the search — four situations that want
  four sentences, where a single "No presets" would be wrong three times in four.

**Two defects found by driving it, both in the seam rather than in a component:**

- **The row the user had just saved was not the row the browser highlighted.**
  Saving writes a file, which makes the index stale by definition, so the status
  sent back alongside the save could only say "loaded: 0" — the id does not
  become knowable until the scan that assigns it finishes. A finished scan now
  pushes the status alongside the index.
- **"Loaded Glass Bell." survived about a fifth of a second.** A preset load
  bumps the reload counter, the editor answers that by resending the whole state
  snapshot, and the page announced the parameter count on every snapshot —
  wiping the message the user had just been given. That line is a *connection*
  message and is now said once.

**Not present in 9c:** deleting or renaming a preset from the browser. Both are
file operations a file manager already does, and neither is worth a command that
destroys a file on the word of a WebView until there is a reason better than
symmetry. The search text and the filters are not remembered across sessions,
for the same reason panel sizes are not (CLAUDE.md §24.3).

### The sounds it ships with (Phase 9d)

Ten factory presets and the init patch. The browser had nothing in it until now
unless the user had saved something themselves; it opens with a library.

- **They are compiled in, not installed.** A factory library made of files needs
  an installer with administrator rights, or a first-run copy that can be
  deleted and half-deleted. Built-in content is always present, cannot be lost,
  and costs no permissions (ADR-0065). The shared factory folder ADR-0062 added
  still works for anything an installer does place there.
- **A preset is stored as the parameters it changes**, not as a rendered
  document. Everything it does not mention sits at the registry's default. That
  keeps each one reviewable — a diff says "this preset moved the filter to
  400 Hz" rather than four hundred lines of XML with one number different — and
  it stops the library freezing today's defaults into itself.
- **A mistake in the table is a test failure, not a broken preset.** Every
  setting is checked against the registry for a real parameter id, a finite
  value inside the range, a value on a discrete parameter's own step, and no
  parameter set twice in one preset. A checked-in document naming a parameter
  that no longer exists would have lost part of itself in silence.
- **It is still an ordinary preset.** `render` produces exactly the `.rnv` text
  a save would have, stamped by the same function, with the metadata in the same
  child, and it is loaded through the same validator as a file from a stranger.
  Origin changes where the bytes come from and nothing else (CLAUDE.md §29.1).
- **The init patch is the empty case.** Init overrides nothing, so it *is* the
  registry's defaults, and there is no separate "reset everything" path that
  could drift from a fresh instance. A test moves all 227 parameters, loads
  Init, and checks that every one of them comes back. The browser gives it a
  button beside Rescan as well as a row in the list.
- **A preset carries no controller**, so none of the ten touches the bend range
  or the MPE zone — asserted both ways: the strings are not in the document at
  all, and a user's settings survive loading every preset in turn (ADR-0061).
- **Document stamping is now one function.** The schema version, the product and
  the product version were already written in two places; the factory renderer
  would have been a third. `state::stamp` is what all three call.

**What "demonstrates the instrument" means here, because it had to be testable:**
every preset is played a held note through a real processor and has to make a
sound — finite, above −40 dBFS, and below the limit of the format. A factory
library where one preset is inaudible ships broken, and a table of plausible
numbers nobody has played is the single most likely thing to contain one.

Between them the ten cover both oscillators, the sub, the noise generator, all
four wavetables, three of the four filter types and both filters in series, the
amplitude and modulation envelopes, sine and saw LFOs, velocity, key tracking,
the mod wheel and aftertouch as modulation sources, and every one of the six
effects.

| Preset | Category | What it shows |
|---|---|---|
| Init | Basics | The registry's defaults, and nothing else |
| Sub Weight | Bass | The sub oscillator, a compressor and an EQ high pass |
| Rasp Bass | Bass | Gate, diode distortion, compressor and EQ — the whole rack in order |
| Hollow Saw Lead | Lead | Seven-voice unison, envelope 2 on the filter, mod wheel on the wave |
| Wide Stab | Lead | Fourteen voices across two oscillators, both filters in series |
| Slow Bloom | Pad | A 1.2 s attack, an LFO on the wavetable, key tracking |
| Glass Bell | Keys | Two sines a nineteenth apart, the upper one dying first |
| Square Keys | Keys | Aftertouch on the wavetable, two EQ bells |
| Wire Pluck | Pluck | A short envelope pair into a syncopated ping-pong delay |
| Dust Sweep | FX | The noise generator through a resonant band pass, swept by an LFO |

**Twenty-two lines the test deleted.** The first draft of the table restated the
default twenty-two times — `filter1_type` set to low pass in eight presets, where
low pass is already the default, and so on. None of them was wrong; all of them
made the tables lie about what each preset was *for*. The check that refuses
them was written as a lint and turned out to be the most useful thing in the
file.

### Wavetables became resources (Phase 9e)

The last of Phase 9, and the largest: wavetables stopped being constants
compiled into the oscillator and became things that are described, validated,
built, shared and replaced.

- **One road into the engine, through the spectrum.** A table is a list of
  harmonics per frame, and the mipmap is rendered from that. The four built-ins
  are *written* as spectra; a table read from a file is *analysed* into one; and
  from that point the engine cannot tell them apart. That is what extends the
  anti-aliasing guarantee to content Apollo did not write, because a level that
  keeps H harmonics is rendered by summing H harmonics rather than by filtering
  something afterwards (ADR-0066).
- **Phase is kept.** A harmonic is a cosine and a sine coefficient rather than
  an amplitude. Two waveforms with identical harmonic amplitudes and different
  phases are different waveforms — same sound in isolation, different shape,
  different peak, and different behaviour through the distortion, the filter
  drive and the compressor that sit downstream.
- **Apollo has its own Fourier transform.** Sixty lines, because one thing needs
  one and JUCE's lives in a module the engine does not otherwise link. It also
  keeps `apollo_core` free of JUCE. Checked against the definition rather than
  against itself, on noise rather than tones.
- **The four placeholders are gone.** They were linear morphs between two
  classic shapes, and the middle of such a table is a sine with a buzz rather
  than a brighter tone. What replaced them:

| Table | What it is |
|---|---|
| Sweep | Every frame a real saw, each carrying more harmonics than the last, the knee moving exponentially because pitch is |
| Pulse | A square narrowing to a twentieth. The one shape that cannot be reached by blending two others at all |
| Formant | A saw under a resonant peak climbing from the second harmonic to the fortieth, Gaussian on a log-frequency axis |
| Fold | A sine driven into a wavefolder — defined in the time domain and analysed into its spectrum, so it takes the same route a loaded file does |

  **This changes what an existing patch sounds like.** The parameter's range is
  unchanged so nothing needs migrating and no project or preset is invalidated,
  but table 2 is a different table than it was. Apollo is pre-1.0 and
  unreleased, which is the window in which that is cheap (ADR-0032).

- **Built once per process, not once per instrument.** Rendering the four costs
  a couple of hundred milliseconds, and a host may make forty instruments while
  scanning its menu. They are immutable, so every instance reads the same bytes.
- **A slot can be replaced while a note sounds.** The pointer is exchanged
  atomically; the audio thread is told there is something new to take; and the
  replaced table is retired rather than freed, released only after the audio
  thread has begun two blocks.
- **A file is untrusted and a failure is ordinary.** The format is a WAV of
  2048-sample cycles, 1 to 256 of them. Missing, too large, not audio, empty,
  not a whole number of frames, silent and non-finite are each refused by name,
  in a sentence that quotes no path, and **every one of them leaves the slot
  playing Apollo's own table**. The size is checked before the file is opened.
- **Loading is asynchronous**, on the same shape as the preset library: a worker
  reads and builds, an async update delivers to the message thread, and the
  message thread is the only place that touches the slots.

**Three defects, all found by reasoning rather than by a test failing:**

- **The first ownership scheme would have freed a table still in use.** It
  pushed the *new* table onto the retired list, so the next swap freed something
  a block could still be reading. Each slot now owns its loaded table outright,
  and a table reaches the retired list only once no slot names it.
- **A published table would never have played.** Voices keep their table pointer
  until handed another, and the engine only hands them one when a parameter
  changes — which a wavetable being replaced is not. The same staleness made the
  two-block retirement rule unsound. The engine now checks a generation counter
  at the top of every block.
- **Building the tables took 539 ms.** Half of it was waste: a spectrum written
  as a formula rarely contains an exact zero, so the swept table's closed frames
  were summing harmonics at 1e-12 — below what a float sample can represent —
  across the whole frame. Skipping harmonics below the destination's resolution
  took it to 216 ms; sharing the tables across instruments took the second
  instrument to nothing.

**Not present in 9e: a way for the user to choose a wavetable file.** The
pipeline is complete and tested, but nothing in the interface opens it. Doing so
needs a native file chooser run by the backend so that the *user* names the path
and it never crosses the bridge (ADR-0063), which is a feature rather than part
of this one.

## 3. What is NOT implemented

| Phase | Absent |
|---|---|
| 7 | **Complete**, but for an optional spectrum analyser. The transport landed in **7a**, a scope on the output and all five sources in **7b**, the modulator traces, output meter, voice count and wavetable displays in **7c**, and the React/TypeScript migration in **7d** |
| 8 | **Complete.** The rack, the distortion, the delay, the reverb, the gate, the compressor, the equaliser, and the whole-chain validation that needed all six to mean anything. Every one of Phase 8's five exit criteria is closed |
| 9 | **Complete.** The `.rnv` document is written, read, validated, migrated and bounded (ADR-0061); the library on disk is located, scanned on a background thread and saved to atomically (ADR-0062); the browser lists, searches, filters, loads and saves it, asking for a preset by a number the backend assigned and unable to express a path at all (ADR-0063); ten factory sounds plus the init patch are compiled in, each played a note by the suite to prove it makes one (ADR-0065); and wavetables became resources — described as spectra, built once per process, replaceable while a note sounds, and read from a file through a validator that leaves the instrument playing whatever goes wrong (ADR-0066). Absent: a way for the user to *choose* a wavetable file, which needs a native chooser rather than a path across the bridge |
| 10 | **10a, 10b and 10c are done.** The assembled instrument is measured for tuning, distortion, noise, DC, aliasing, transient behaviour and stability (§5c, ADR-0067); fifteen renders — ten of them the factory presets — are pinned against references compiled in beside the tests, so a change that alters what Apollo sounds like fails the build (§5d, ADR-0068); and the benchmark was rebuilt into something that reports how much it should be believed, then used to measure the worst-case *callback* rather than an average, memory, first use, and the worst patch the controls can build with everything running at once (§5b, ADR-0069). Absent: the optimisation 10c identified (10d), and host compatibility (10e), which needs real DAWs |
| 11–12 | Packaging, cross-platform release engineering, release hardening |

**All 227 registered parameters now affect audio** — the whole source section,
four envelopes, four LFOs, both filters, sixteen modulation slots, the MIDI
expression settings, `master_gain`, the rack's six slots, and all six effects in
it. `fx_delay_time` was the last one that did not, inert since Phase 2 and
connected in 8b; for the first time since the registry started growing there is
nothing in it that does nothing.

Phase 8a added eleven, 8b eight, 8c eight, 8d thirteen and 8e forty-four. Six of 8a's are the
rack's slots, which enumerated all six effects from the start, including the ones
not built yet, because a discrete parameter's range is permanent once it ships
(ADR-0054) — a decision that has now paid off twice, with the delay's
note-division parameter fixed the same way at fourteen values and the
equaliser's eight band shapes fixed at eight.

8e's forty-four are the largest single addition since the modulation matrix, and
for the same structural reason: six controls for each of seven bands cannot
arrive a band at a time, because a band added later could not be numbered without
renumbering the ones after it.

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

**Verified by CI against Phase 8e code** (run 34781646589, all four jobs green):
Linux (GCC), macOS (Apple Clang), Windows (MSVC) and the Linux Clang sanitizer
job all configure, build and pass with `APOLLO_WARNINGS_AS_ERRORS=ON`.

The same four jobs were green against Phase 4b code (run 34160411849), where the
macOS job was additionally confirmed to link the VST3 bundle and the standalone
`.app` and to actually execute the suite, rather than passing by building
nothing.

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

**2,273,043 assertions, 0 failures**, across 39 test classes. The table below
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
| Resources | Preset library | The disk, which Apollo does not control. The default locations come from the platform rather than from a hard-coded path, asserted by shape so the test is true on all three; a library with no folders at all scans cleanly and reports *missing* rather than *empty*; a scan finds presets, names them from their metadata and reports the nested folder each sits in as its bank; **a folder holding one preset and four files that only wear the extension lists one and counts four**, while a readme and a picture are ignored rather than counted; factory and user content go through the same reader and are told apart only by where they are; **a preset name is turned into a filename that cannot escape, collide or hide** — separators, the parent-directory token, Windows device names with or without an extension, trailing dots and spaces, and names that leave nothing usable, each checked by value and then as a property over every case; a save is atomic, replaces completely, and leaves no temporary files; **a hostile name cannot write outside the folder it was given**, checked with a sentinel file outside it; a saved preset is found by the next scan and loads back into an instrument; a missing file, an empty file, a folder wearing the extension and a file past the size bound each fail gracefully and read nothing; a tree deeper than the bound is cut off, reports itself truncated and still returns what it found; and a scan runs off the message thread, publishes its index, delivers its callback exactly once, and survives the library being destroyed underneath it. Phase 9c added what the browser asks against: a scan returns **factory first, then by bank, then by name**, numbers its entries from 1, and produces the same numbers for the same tree twice, which is the property an id depends on; an id resolves to exactly one preset, while 0, a negative and one past the end resolve to nothing; and **the file a name would be saved to is the file a save actually writes**, checked over four names including one Windows would silently rename, so a replace prompt cannot protect a different file from the one it overwrites |
| DSP | Fourier transform | The transform every loaded wavetable's spectrum comes from, checked against the definition rather than against itself: a naive four-line transform is obviously correct, and the fast one must agree with it to within 1e-9 at four sizes, **on noise rather than on tones** — a sine is symmetric enough that a transform with a sign error in half its butterflies still reproduces it. A length it cannot do leaves the buffer untouched rather than mangled; a sine analyses as one sine coefficient and nothing else; a saw analyses as 1/k across sixteen harmonics; **two waveforms with the same harmonic amplitudes and different phases stay distinguishable**, which is what makes a loaded table the waveform the user supplied; and an arbitrary asymmetric shape survives being taken apart and summed back together, sample for sample |
| Validation | Instrument validation | The assembled instrument rather than its parts, and the only tests in the suite that play notes and measure the audio (ADR-0067). **Tuning** through the whole pitch path — the note number, the sub oscillator's transposition and the oscillator — at every third note from 24 to 96 and at four sample rates, resolved by parabolic interpolation so the answer is in cents rather than in bins; **THD+N** on the sub oscillator, the only source whose output is meant to be a sine; **exact silence at rest**, with every source at full level, because a synthesiser with no note has nothing to produce and a floor of any size would be a bug; **DC offset** with all four sources running, which is what would catch the pulse table if it stopped dropping its DC term; **inharmonic content** found by scanning every bin that is not near a harmonic rather than by checking frequencies somebody predicted; **the step response**, as a cutoff swept across its whole range between two blocks, where a jump larger than the steady-state one would mean the coefficient change was audible; **exact silence after a release** through a chain with two feedback effects, which is denormal behaviour measured by its consequence; **every registered parameter** driven to both ends and the middle of its range with a note held, staying finite and bounded; and **a minute of the worst patch the controls allow**, measured per ten-second window, because what matters is that nothing compounds rather than how loud 88 dB of requested gain turns out to be |
| Regression | Regression renders | Whether Apollo still sounds like itself (ADR-0068). Fifteen renders — the ten factory presets, a four-note chord, the modulation matrix at a dominating depth, a wide stereo image, the same patch at a tenth of the usual block size, and one at 44.1 kHz — each compared against a reference compiled in beside it. A reference is not a wave file but a description: peak, RMS, mid and side level through sixteen slices of time, and energy in thirty-two logarithmic bands, because bit-exact audio is not something four compilers agree on. The bookkeeping is checked first, so a missing reference reports as itself rather than as a mismatch; a case rendered twice in one process produces identical samples, including the noise generator; **the same patch at 512 samples and at 64 sounds the same**, and came out bit-identical; and — the test that earns the rest — **each part of the fingerprint is shown a change it should catch and has to catch it**, because a fingerprint that always agreed would pass for ever and look exactly like one that worked |
| Resources | Wavetable resources | A disk Apollo does not control, and a table being swapped underneath a sounding note. A well-formed WAV of 2048-sample cycles reads back as its frames, in order, with the first analysing as a sine and the last as something rich; **every way a file can fail is refused by name** — missing, not audio, stopping mid-frame, silent, and past the size bound — each leaving nothing behind and each described in a sentence containing nothing path-like; a table read, analysed and rebuilt correlates above 0.999 with the waveform that went in, per frame; a published table replaces what a slot plays and the built-in comes back exactly, while the other three slots do not move; an empty, null or out-of-range publish is refused rather than silencing an oscillator; **a replaced table is kept until two blocks have passed** and not one, and a publish announces itself to the audio thread exactly once; a load runs without the caller waiting and is delivered once, reporting a file name rather than a path; and **a file that is missing or is not audio leaves the slot playing the built-in**, with a loader destroyed mid-load returning cleanly |
| Resources | Factory presets | The sounds Apollo ships with, and the test that makes storing them as data safe. **Every setting of every preset names a real parameter**, carries a finite value inside that parameter's range, sits on its step if it has one, is not set twice, and is not merely a restatement of the default — each failure naming the preset and the parameter; the library is named, categorised, commented and free of duplicate names, and its metadata survives the bounds the reader applies; **loading Init returns all 227 parameters to their registry defaults** from an instrument whose every parameter had been moved, which is what keeps the init patch from drifting; every preset renders into a document that begins `<?xml`, names the state root, is accepted by the ordinary reader and comes back carrying its own name, category and author; **every preset plays a held note through a real processor and has to make a sound** — finite, above -40 dBFS, and below the limit of the format; none of them carries the bend range or the MPE zone in its document, and loading all ten in turn leaves a user's controller settings where they were; and the built-ins reach the index marked factory, in the Factory bank, each with a unique key, each numbered, and each loadable through the route the bridge actually uses |
| State | Preset document | What a `.rnv` is and what it refuses to be. A preset round-trips the sound *and* its metadata; the file is XML text that begins `<?xml`, names the state root and shows its schema version without a parser, because ADR-0053 chose text as a property rather than a preference; metadata reads out of a document without touching any instrument, which is what an index is built from; a preset with no metadata is valid and common; an absurdly long field is cut rather than trusted or refused, on the way out as well as in; quotes, angle brackets, newlines and accented text survive a round trip; **seven ways a document can be refused each leave three parameters and the loaded preset's name exactly as they were**, and each refusal says something that does not quote the document back; **the preset reader and the host reader refuse the same documents for the same reasons**, which is what keeps them one implementation; a version 1 preset migrates and keeps both its cutoff and its name; something far too large is refused on its size alone, through both the load path and the index path; the loaded preset's name travels into a host project and comes back; **a preset carries no MIDI mappings and none of the four expression parameters**, and loading one leaves this user's mappings live and their MPE zone and bend range where they set them (ADR-0061) |
| State | State serialization | Round trip, schema stamping, empty/malformed/foreign/unsupported rejection, **state preservation on rejection**, migration boundaries, every parameter round-tripped, reload counter |
| UI | UI bridge protocol | Malformed JSON, non-object payloads, versioning, unknown types, ID validation, NaN/Inf/out-of-range, gesture states, size limit, error hygiene. Phase 9c added the preset commands, which are the only ones that can reach a filesystem: a preset is named by an index number and **a string where the id belongs is refused**, as are zero, a negative, `1e30` and anything past the scan bound; a save must carry a name that is not empty and not only spaces, every metadata field is refused one character past its bound rather than truncated, and **an absent overwrite flag defaults to refusing**; the index message carries a name but no path, no filename and no field that could hold one; and a status message bounds a metadata name that came off a disk on the way *out* as well as on the way in |
| UI | Parameter bridge | Snapshot matches APVTS, commands reach APVTS, invalid commands change nothing, external changes propagate, coalescing, detach safety, metadata completeness. Phase 9c added the preset seam, driven against a real library in a temporary folder because every interesting failure is between components rather than inside one: a sound saved through the bridge appears in the index and loads back to the value it was saved at; **a save over an existing preset is refused and the file on disk is byte-for-byte what it was**, until an explicit replacement actually replaces it; eight hostile names — `..\..\evil`, `C:/Windows/evil`, `/etc/evil`, `sub/dir/evil`, `CON`, `..`, `.` — produce nothing outside the user folder, asserted by counting what exists beside it; an id the index does not contain is answered `UNKNOWN_PRESET` and opens no file; a host project load clears the browser's claim that a preset is playing; and a bridge with no library attached answers the preset commands with an error rather than a list that never fills |
| Engine | Voice engine | Pitch accuracy against four reference notes, velocity scaling, release to silence, polyphony, allocation order, deterministic stealing, click-free steal, sustain, pitch bend, voice reuse, extreme input, gain staging across five voicings |
| Audio | MIDI rendering | Sample-accurate event placement, **identical output across nine block sizes**, sustain via CC 64, pitch wheel, all-notes-off, master gain scaling |
| MIDI | MIDI mapping model | Address and range validation, reserved controllers, value scaling including inversion and clamping, the bijection and every replacement case, channel-specific beating omni in both learning orders, removal, capacity, and the publication ring — including a producer that outruns it |
| MIDI | Controller profiles | Every built-in profile checked against the registry — real parameters, assignable controllers, no duplicate controller or parameter inside one profile, unique identifiers; applying fills the table exactly; replace clears and merge keeps; an entry naming an unknown parameter is dropped while a reserved controller is refused, each counted separately; a profile's mappings are ordinary mappings that can be learned over, released and cleared; and every bridge command, including an unknown profile and an unknown mode |
| MIDI | MPE and per-note expression | Zone channel classification at both ends of both zones; manager-versus-member routing; RPN decoding, per-channel independence, the null selection and NRPN cancellation; polyphonic aftertouch pressing one note and not another; channel pressure still pressing everything; per-note bend independent per channel and adding to the wheel; note-off matching its channel; CC 74 as timbre inside a zone and as a MIDI Learn target outside one; the bend range as a control, applied to a held wheel; an MPE Configuration Message reaching the zone through the parameter; and pressure not being inherited by a new note while the bend is |
| MIDI | MIDI Learn | Learn assigns the moved control and disarms; a reserved control is refused and leaves learn armed; the learning message does not itself move the parameter; a mapped control sweeps its parameter; **rendering alone never writes a parameter**; a control being learned does not drive its old destination; removal and clearing stop it; sustain and the mod wheel keep their fixed behaviour; mappings round-trip through save and reload and still drive audio; unknown-parameter entries are dropped; every bridge command, including with no MIDI attached |
| Telemetry | Visualisation transport | An untouched source distinguishable from a silent one; the window read back in order, including across the ring's wrap; a stopped source seen to stop rather than holding its last picture; reset; the silence threshold; triggering, including that a flat line reports itself free-running; decimation that shows alternating samples rather than averaging them to nothing; per-source isolation and unique wire tokens; the processor capturing its own output through `processBlock`; and the bridge sending a frame whose points are finite, inside full scale, and actually a waveform. Phase 7b added the four claims that matter most: that an unwatched instance captures nothing and still sounds; that arming discards the picture the last viewer left behind; that the same note rendered with and without capture comes back **sample for sample identical**, including when the block is long enough for the capture path to split it; that a source at level zero is captured *as silence* rather than left uncaptured, and survives a closed filter that takes the mix away; and that a source's trace grows with the voice pool rather than showing one voice from it. Both frame builders also log and bound their message size, so an encoding change that multiplies the traffic is caught here rather than in a profiler |
| Telemetry | Instrument telemetry | The modulator traces, the meter, the voice count and the wavetable displays, added in 7c. A modulator nothing traces is distinguishable from one sitting still; the trace is the most recent second across the ring's wrap; the meter takes a peak in the block it happened in and gives it up over about a second and a half; peak and RMS disagree about a single spike and agree about a constant tone, which is why there are two of them; a clip is still reported half a second after the samples that caused it and clears itself after the hold; an envelope's trace shows its rise, settles at the sustain level and falls on release, with the stage reported at each point; the two ends of a wavetable draw different waves, so the display is following the position rather than ignoring it; an unrouted LFO does not claim to be running and starts to when something routes it; and the frame that reaches the interface names every modulator exactly once and both oscillators by number rather than by array position |

| DSP | Distortion | The curve is bounded, monotonic and centred, and every mode passes a quiet signal through unchanged — which is what lets the mode be switched without a jump; drive compensation holds a -6 dBFS sine within 4 dB across the whole 0 to 36 dB range on all three curves; a fully dry mix is the input delayed by **exactly** the reported latency, asserted sample for sample rather than approximately, because a dry path that has been through arithmetic is a bug; latency does not move with mode, drive, mix or bypass; oversampling removes 13 to 15 dB of fold-back against the same curves at the base rate, and a musical note at moderate drive stays under -60 dBc; absurd and denormal input produces finite output, and a very hot signal leaves bounded by the curve's own ceiling; and a reset leaves no tail in the filters or the delay line |
| DSP | Effects chain | What only means something with every effect in the rack at once. **All 720 orderings** of six effects render finite and bounded, and every one of them reports the same latency — so latency is a property of the set rather than the arrangement, which is what the host's delay compensation depends on; switching any one of the six out of a full chain measurably changes the output, so none is being swallowed by the five around it; **a chain's tail covers the whole chain ringing out**, measured by finding it still audible at the moment the old longest-in-the-chain rule declared it finished; a full rack at the top of every range — maximum feedback, a 20 s decay, 12 dB of makeup, 12 dB on every EQ band, behind a hard clipper at full drive — stays bounded, turns around and reaches exact silence after a minute; the chain rearranged on **every block** for 240 blocks, with effects leaving and rejoining it while audio flows, stays finite and bounded; and one effect named in all six slots runs once, in the first, sounding sample for sample like the same effect named once |
| DSP | Effects rack | An empty rack is **bit-exactly** transparent and reports no latency or tail; an effect whose phase has not landed leaves its slot empty; the same effect in two slots runs once, in the earlier one; an effect sounds the same wherever in the chain it sits; latency counts what is in the chain whether bypassed or not, and drops only when the effect is removed; a bypassed effect delays the signal and does nothing else; and tail is reported for the active chain only, as the sum along it |
| DSP | Delay | Where the repeat lands, in free time and in synced time — a quarter note at 120 BPM is 500 ms and nothing else, a slow whole note is clamped to the buffer rather than wrapping, a free delay ignores the tempo, and no tempo at all falls back to 120 rather than to silence; each repeat quieter than the last; **thirty seconds at maximum feedback with no input**, asserting the tail is quieter every second, never louder than what went in, and near silence at the end; a dry mix is the input sample for sample and the latency is zero; ping-pong puts the first repeat on the side the sound arrived on and the second on the other; damping keeps taking more top off each pass rather than settling after one; sweeping the time as fast as a control can move produces no discontinuity; a bypassed delay is fed silence so it cannot replay old audio when it comes back; reset leaves no tail; absurd and denormal input stays finite through the feedback path and still settles; and the reported tail covers at least one repeat and is bounded |
| DSP | Reverb | The network's line lengths share no factors, in both modes, and a hall's shortest path is much longer than a room's; size scales the lines and mode changes which lines they are; a dry mix is the input sample for sample and the latency is zero; a burst is still sounding a second later; **a minute at the longest decay with no input** stays under the envelope RT60 describes, never exceeds what went in, and is gone by the end; the decay control measures as RT60, as a slope between two later times and in RMS rather than peak; the tail reaches **exactly zero** rather than grinding on as denormals; damping shortens the tail as well as darkening it; pre-delay is a real gap with nothing in it; width at zero puts the tail in the centre; a bypassed reverb goes on decaying and comes back silent rather than resuming; reset leaves nothing behind; absurd input stays finite and still settles; and the reported tail covers the decay and the pre-delay |
| DSP | Dynamics | The compression curve is the ratio expressed in decibels, checked against the static formula and then against rendered audio — 4 dB over the threshold at 4:1 comes out 1 dB over, to within 0.07 dB; the gain ramp travels 1 - 1/e of the way in the time it is given, at three settings; the compressor as a whole is slower than that and the detector's share of the lag is measured rather than hidden; a signal 30 dB below the threshold passes through untouched and the compressor reports that it is doing nothing; makeup lifts the output by exactly what it says and a mix of zero is the input exactly; a gate opens fully on a loud signal and settles at its range on a quiet one; **hold** takes a gate that moved eleven times in two thirds of a second down to once; range means how far down, and the bottom of its travel is exact silence; one channel getting loud moves the other channel's gain too, which is what stereo linking is for; neither processor adds latency or a tail; and absurd input stays finite and settles to silence afterwards |
| DSP | Equaliser | A fresh equaliser is **bit for bit** its input, and a band switched off or muted is the exact identity; a bell applies exactly the decibels on its dial at its own centre; **the curve the display draws is the curve the filter applies**, checked against rendered audio at fifteen frequencies for every shape; a table of reference settings produces exactly the decibels the frontend's own transcription produces, which is the contract between the two implementations of the cookbook; a wider bandwidth reaches further from the centre; order is instances, so a bell's gain applies that many times and a pass filter loses twelve decibels per octave per instance; a shelf reaches its full gain on its own side and nothing on the other; a notch removes its own frequency and leaves its neighbours alone; a band pass is unity at its centre whatever its width; the four shapes with no gain ignore the gain parameter; bands in series add in decibels; the output trim applies exactly the gain it names; **every design in the whole parameter space has both poles inside the unit circle**, at four sample rates; the same settings sound the same at every sample rate; a band coming back into circuit brings nothing with it; only a band that can ring reports a tail and that ring reaches **exactly zero** rather than grinding on as denormals; absurd input produces finite output; a band travels to a new setting over many blocks rather than arriving in one, measured on the logarithmic axis it actually travels along; sweeping a band across the spectrum keeps the output in range; and an equaliser declares and adds no latency |
| Audio | Effects rack in the processor | The parts that only exist once the rack is wired into a plugin: a new instance has an empty rack, reports zero latency and still sounds; putting the distortion in a slot changes what is heard, measurably and without running away with the level; the latency the rack adds reaches the host and does not change when a bypass is automated; a chain — slot, mode, drive, tone, mix — survives the save/restore a project or a preset puts it through and reports its latency again on prepare; every value a slot can hold produces audio and reports the latency of the effect in it and nothing else, with only the empty slot empty and only the gate — whose default threshold sits above a default note — allowed to silence one; an equaliser band in the chain lifts a note with a shelf under it and takes it down when the shelf is inverted, reporting no latency either way; a band's six settings plus the output trim survive the round trip a preset puts them through; and **a full rack of six, in an order that is not the enum's with every effect dialled away from its defaults, survives that round trip and still sounds afterwards**, reporting both its latency and a tail that covers the delay and the reverb together. The three fully wet tests here — a delay, a synced delay and a reverb — now render past the first repeat or reflection rather than measuring the mix ramp that used to leak dry signal past them (ADR-0060) |

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

### The delay, 2026-09-13

| Checked | Result |
|---|---|
| The delay has a panel of its own | **DELAY** beside **DISTORTION** in the effects rank, carrying the `OFF` chip while nothing in the rack selects it |
| Its controls read correctly | Division as note values — `1/1`, `1/2.`, `1/2`, `1/2T`, `1/4.`, `1/4` — with a quarter note selected by default, and Time `500 ms`, Feedback `35 %`, Damping `8 kHz` beside them, each matching the registry |
| Both ways of asking for a time are visible at once | The Timing switch says which is being read; neither the knob nor the divisions are dimmed or hidden, for the reason ADR-0052 gives |
| The rack offers the delay as a real choice | `DELAY` in the slot list, no longer marked "(soon)" |
| Nothing from the rack is left in Output | The module now carries the master fader alone; `fx_delay_time` moved to the panel of the effect that owns it |

### The reverb, 2026-09-13

| Checked | Result |
|---|---|
| The reverb has a panel of its own | **REVERB** in the effects rank beside the distortion and the delay, carrying the `OFF` chip while nothing in the rack selects it |
| Its controls read correctly | Space `ROOM`/`HALL` with hall selected by default, State `ACTIVE`/`BYPASS`, and Size `50 %`, Decay `2.00 s`, Pre-Delay `20 ms`, Damping `6.00 kHz`, Width `100 %`, Mix `0 %` |
| The decay's unit survives the round trip | The registry carries it in milliseconds, because that is the unit every other time control uses; the frontend formats 2000 ms as `2.00 s`, and the DSP works in seconds where the RT60 arithmetic lives |
| The rack offers the reverb as a real choice | `REVERB` in the slot list, no longer marked "(soon)" |

### The gate and the compressor, 2026-09-13

| Checked | Result |
|---|---|
| Both have panels of their own | **GATE** and **COMPRESSOR** in the effects rank beside the other three, each carrying the `OFF` chip while nothing in the rack selects it |
| The gate reads correctly | State `ACTIVE`/`BYPASS`, Threshold `-60.0 dB`, Attack `1.0 ms`, and Hold, Release and Range beyond them |
| The compressor reads correctly | State `ACTIVE`/`BYPASS`, Threshold `-18.0 dB`, Ratio `2.00`, Attack `10 ms`, Release `120 ms`, Makeup `+0.0 dB`, Mix `100 %` — every default from the registry, each with its unit |
| A ratio is a bare number | It is the one control here with no unit, and the frontend shows it as `2.00` rather than inventing one |
| The rack offers both as real choices | `GATE` and `COMPRESSOR` in the slot list, no longer marked "(soon)"; only `EQ (soon)` remains |

**Not driven by hand, for the fourth sub-phase running:** selecting an effect into
a slot through the dropdown, and hearing it. The environment's synthetic input
reaches the WebView only for the first event after a click in the same batch, and
not reliably even then; the one sequence that worked in 8a has not reproduced
since. Everything the audible check would have shown is asserted through the
processor instead — for the reverb, that it is still answering long after the
note was released, reports a tail covering its decay, and adds no latency.

**Not driven by hand in 8b either, and for the same reason:** selecting the delay
into a slot through the dropdown, and hearing the repeats. Keyboard input reached the
page only immediately after a click in the same batch, and the sequence that
worked for the distortion in 8a would not reproduce for the delay; no MIDI source
was available to play a note into a configured chain either. The identical
interaction — a rack slot's dropdown, the same component and the same code path
with a different option index — *was* driven by hand in 8a, and everything the
audible check would have shown is asserted through the processor in
`Tests/Audio/EffectsIntegrationTests.cpp`: a delay in the chain is still sounding
long after the note is released, reports a tail that covers its repeats, adds no
latency, and keeps repeating with no transport to sync to.

### The equaliser, 2026-09-14

The first sub-phase since 8a whose interface was actually **driven** rather than
only read, because the input problem the last four entries record was solved. The
technique is worth keeping: synthetic input posted at the *desktop* — moving the
real cursor and injecting wheel and button events — reaches whatever window
happens to be in front, which is unreliable and, on a machine someone is using,
rude. Posting `WM_MOUSEWHEEL`, `WM_LBUTTONDOWN`, `WM_MOUSEMOVE` and
`WM_LBUTTONUP` straight to Apollo's `Chrome_RenderWidgetHostHWND` child window
reaches the page directly, and `PrintWindow` with `PW_RENDERFULLCONTENT` captures
the WebView's own surface without the window needing to be in front. Two things
to know: the wheel needs the window focused, and the process doing the posting
must call `SetProcessDPIAware` or it measures a 1920x1080 window as 1280x720 and
captures only its top-left corner — which is what made the layout look broken
before it was.

| Checked | Result |
|---|---|
| Every parameter reaches the page | Footer reads **227 parameters bound · protocol v1**, and the "Unassigned" safety-net module does not appear — so all forty-four new entries have a home in the layout |
| The panel renders | **EQUALISER** on a row of its own, with the band number in its heading, the `OFF` chip while nothing in the rack selects it, and the band strip `1 2 3 4 5 6 7` at the right of the heading |
| The response curve draws | Grid at ±18 dB and at the decade frequencies, a flat line at 0 dB, and seven numbered handles spread evenly along the logarithm from 47 Hz to 8.43 kHz — the registry's own defaults |
| Discrete controls show names | Shape `OFF LP BP HP NOTCH LO SHELF BELL HI SHELF` with **BELL** selected, Slope `12 24 36 48` with **12**, In Circuit `IN`/`MUTE`, State `ACTIVE`/`BYPASS` |
| Defaults match the registry | Band 1 at `47.0 Hz`, `0.0 dB`, `1.00 oct`; the equaliser's Level at `0.0 dB` |
| **Dragging a handle works, and lands where it should** | Band 4's handle dragged up and to the right put the band at **1.60 kHz** and **+10.9 dB** — the frequency the logarithmic axis says that pixel is, and the gain the ±18 dB axis says that row is, both within a pixel's worth |
| The knobs follow the drag | Frequency and Gain updated to those values *from the engine's echo*, not from the page: the drag writes a parameter and the knob reads what comes back |
| The curve follows the band | The flat line became a bell peaking at the handle, drawn from the page's own transcription of the cookbook |
| **The chain survives a restart** | Closed the window normally and relaunched: the bell came back at the same frequency and gain, with handle 4 still at its peak. The band strip reset to band 1, which is correct — which band is on screen is interface state and belongs to nobody's patch |

Two defects were found by looking rather than by testing, and both are fixed:

- **The curve did not stretch to its module.** `.cluster--banner` lays its
  children out in a column and aligns them to the start, so the curve sat at its
  minimum width with most of the panel empty beside it. It now asks for the width
  explicitly, and its height is clamped rather than following the aspect all the
  way up — at full width a constant aspect made it half the height of the screen,
  and a decibel axis spanning ±18 needs nothing like that.
- **A bandwidth read as `+1`.** The `oct` unit had one formatter, written for the
  sub oscillator's octave transposition, which rounds to a whole number and adds a
  sign. Applied to a continuous width that turned 1.0 octaves into "+1" and would
  have shown a half-octave band as "+0". The formatter now reads the parameter's
  own step to tell a discrete transposition from a continuous width, which is the
  engine's description rather than a list of ids kept in the page.

**Not driven by hand:** audio through the equaliser in the standalone. No virtual
MIDI port was running on this machine, so there was no way to play a note into a
configured chain. What that check would have shown is asserted through the
processor in `Tests/Audio/EffectsIntegrationTests.cpp`, which renders a real note
through `processBlock` with the equaliser in a slot: a 12 dB low shelf under the
note lifts its peak from 0.0718 to 0.2845, inverting the shelf takes it to 0.0547,
and the latency stays at zero throughout.

### The full chain, 2026-09-14

The first time a chain has been *built by hand* rather than set through
parameters. 8a managed it once and the four phases after it could not; 8e solved
the input problem (issue 15) and this is the first phase to use it for the thing
those entries kept deferring.

One addition to the technique recorded in 8e: a native `<select>` cannot be
driven reliably with arrow keys under synthetic input — the click that opens the
popup sometimes counts as a keystroke and sometimes does not, so a count of
Downs lands on a different option each time. **Type-ahead is deterministic**:
click the select, send the option's first letter as `WM_CHAR`, then Return.
That is how the compressor got into slot 5 after three attempts with arrow keys
put the equaliser, the reverb and the equaliser there again.

| Checked | Result |
|---|---|
| The rack renders six empty slots | `1` to `6`, each reading `—`, with every slot number dim |
| A slot can be filled from its own dropdown | Slot 1 set to `DISTORTION`; its number lit gold and the **Distortion panel's `OFF` chip disappeared** — the interface and the engine agreeing about the chain |
| A full chain of all six, built by hand | `DISTORTION → DELAY → REVERB → GATE → COMPRESSOR → EQ`, every slot number lit |
| Every effect panel agrees | Distortion, Delay, Reverb, Gate, Compressor and Equaliser all lost their `OFF` chips. Six effects, six live panels |
| The equaliser's state survived from the previous session | Band 4's bell still at 1.60 kHz, drawn on the curve, after the restart between phases |

One defect was found by driving it, and is fixed:

- **A duplicated slot claimed to be in the chain.** Two of the arrow-key attempts
  left the same effect in two slots. The rack resolves that the way it has since
  8a — first occurrence wins, because running one effect object twice would feed
  its own output back into its own state — but the *interface* lit both slot
  numbers, because it only checked whether the named effect existed in this
  build. That is precisely the thing `IMPLEMENTED_EFFECTS` exists to prevent, in
  its own words: "lighting a slot that the rack is going to ignore would be the
  interface telling the user something the instrument does not agree with." The
  page now resolves the chain by the same algorithm the engine uses, and a
  duplicated slot reads **`duplicate`** in orange with its number unlit. Verified
  live: the marker appeared on slot 6, then moved to slot 5 when slot 5 became
  the duplicate, then vanished when the chain held six distinct effects.

**Not driven by hand:** audio through the chain, for the same reason as 8e — no
virtual MIDI port is running on this machine, so there is no way to play a note
into it. Everything the audible check would have shown is asserted through
`processBlock` in `Tests/Audio/EffectsIntegrationTests.cpp`, including a full
rack of six restored from a preset and still sounding.

### The preset document, 2026-09-15

**There is nothing user-facing in 9a**, and saying so is the honest record: this
sub-phase is a document layer with no interface, no file dialogs and no file I/O
at all. The browser is 9c. What the running application *can* show is that the
refactor underneath it broke nothing — 9a split `readState` into the part that
unwraps a host's binary blob and the part that validates a parsed document, and
the standalone restores its own state through exactly that path on every launch.

| Checked | Result |
|---|---|
| The standalone launches and renders | Yes, unchanged |
| Every parameter still reaches the page | Footer reads **227 parameters bound · protocol v1**, and the "Unassigned" module does not appear |
| Host state restores through the refactored reader | The equaliser's band 4 came back at **1.60 kHz / +10.9 dB**, drawn on the curve, from the session two phases ago |
| The effects chain restored | Compressor and Equaliser both live, no `OFF` chips — the six-effect chain built by hand in 8f survived |
| The controller state restored | Masthead reads **MIDI 1**: the learned CC mapping is still there, which is the part 9a now explicitly preserves across a *preset* load and must not have broken for a *project* load |

That last row is the one worth having. The new code captures the MIDI mappings
and the four expression parameters before applying a document and puts them back
afterwards; the risk of getting it wrong is not that presets misbehave — there
are none yet — but that ordinary project loading quietly loses a user's
mappings. It does not.

### The library on disk, 2026-09-15

Like 9a, there is nothing here a user can see — the browser is 9c. What 9b adds
that a test cannot settle is where the library actually resolves to *on a real
machine*, and whether the processor now holding a thread-owning member still
starts, runs and shuts down.

| Checked | Result |
|---|---|
| The locations resolve sensibly on this machine | User `C:\Users\…\AppData\Roaming\Apollo\Presets`, factory `C:\ProgramData\Apollo\Presets` — both from JUCE's special-location lookup, with only the two folder names Apollo's own |
| The standalone still launches with the library in it | Yes: 227 parameters bound, 19 threads, window titled Apollo |
| Host state still restores | MIDI 1 in the masthead and the interface unchanged — the processor gained a member that owns a thread, and nothing about startup moved |
| **Nothing scans by itself** | After a full launch and shutdown, `AppData\Roaming\Apollo\Presets` **had not been created**. Constructing the library touches no disk, which is the claim ADR-0062 makes about hosts that instantiate a plugin dozens of times to build a menu |
| The standalone shuts down cleanly | Closed on request, exit without a hang — the case that matters when a library is destroyed with a scan thread attached |
| The tests leave nothing behind | The scratch tree under the system temporary directory is empty after a full run |

The last two rows are the ones worth having. A background thread owned by a
plugin is the classic source of a shutdown hang, and a test suite that does real
file I/O is the classic source of litter on a developer's disk; both were
checked rather than assumed.

### Reset menus, power switches, fullscreen and resizeable panels, 2026-09-15

Four changes the developer asked for, all of them interface work, all driven by
hand against the running standalone launched from the project root.

| Checked | Result |
|---|---|
| The masthead reads the handle | **APOLLO  @ProdByRnV** |
| The fullscreen button exists | `FULLSCREEN` in the masthead beside `MIDI LEARN` |
| **Fullscreen fills the display** | Window went **1773x1182 → 1923x1122**, which is the display's user area plus the window's own chrome |
| **And returns** | Pressing it again went back to **1773x1182 exactly**, the size it was before |
| Every effect has a power switch on its panel | Distortion, Delay, Reverb, Gate, Compressor and Equaliser all read `ON`/`OFF` where they read `ACTIVE`/`BYPASS` before |
| And one in the rack | Each filled slot carries the same switch under its dropdown; an empty slot keeps the space so the row does not jump |
| **Right-click offers Reset** | A menu appears at the pointer reading **"Reset to 2.00"** — the value named, not just the word |
| It knows when there is nothing to do | On a knob already at its default the item is greyed and says "Reset to -18.0 dB" |
| **Choosing it resets the knob** | Ratio dragged to **4.01**, right-clicked, item chosen → back to **2.00** |
| **Panels resize by a corner handle** | The Compressor panel dragged narrower and taller: its six knobs reflowed from one row to two, a scrollbar appeared, and the handle followed to the new corner |
| Sizes are not remembered | By construction — the size is an inline style the browser writes, so a reload starts from the designed layout, which is what was asked for |
| The build survives the copy being in use | Building with the root `Apollo.exe` running now prints a status line and succeeds, where it previously failed the build with a permission error |

**Two real bugs in the menu, both found by driving it and both fixed:**

- **The menu closed before a click could become a choice.** Its dismiss listener
  is capture-phase, so it ran before the item's own handler and `stopPropagation`
  on the item came too late. It now ignores pointer events that land inside
  itself rather than trying to stop them.
- **The knob swallowed the click.** The menu was rendered as a child of the knob,
  so pressing an item first ran the knob's `pointerdown`, which starts a drag and
  calls `setPointerCapture` — redirecting the pointer-up away from the menu. The
  item highlighted under the cursor and could not be chosen. It is now rendered
  through a portal into the document body, which also takes it out of every
  `overflow` and stacking context on the way — including the panels, which became
  scroll containers the moment they became resizeable.

**Verified by keyboard, not by mouse — and that was the wrong conclusion.** This
paragraph originally recorded that synthetic mouse clicks on the menu "could not
be made to land reliably from this environment" and put it down to the test
harness. It was a real defect: the menu could not be chosen with a mouse by
anybody, and the portal described above did not fix it. See **The reset menu
could not be used with a mouse** below, where it is diagnosed and closed.

**Canvases now redraw when their panel resizes**, not only when the window does.
Each picture sizes its backing store to its element's box, and a panel with its
own drag handle can change that box while the window stands still; the
equaliser's curve is the one that shows it most, because it stretches to its
module.

---

### The preset browser, 2026-09-15

Driven by hand against the running standalone, launched from the project root,
with the developer's real preset library empty at the start and removed again at
the end — nothing here was left behind in it.

| Checked | Result |
|---|---|
| The panel is there, above the signal path | **PRESETS** at the top of the workspace, with search, two filter menus, `SAVE…` and `RESCAN` |
| An empty library says which empty it is | "No preset folders yet. Saving a preset creates one." — not "No presets", which would be true and useless |
| **The save form fills in from the loaded sound** | Name, Author, Category, Bank and Comment, prefilled after a preset is loaded and blank before |
| A save with no name cannot be sent | The SAVE button is greyed until the name has something in it |
| **A save reaches the disk** | `%APPDATA%\Apollo\Presets\Mine\Glass Bell.rnv`, 9,268 bytes — **the bank became a folder**, as ADR-0053 says a bank is |
| And the instrument becomes it | Masthead read **Glass Bell**, footer read "Saved Glass Bell.", and the form closed on the confirmation rather than on the press |
| **A load restores the sound** | Relaunched, clicked the row: Oscillator 1's wavetable went back to `SIN→SQR` from the default `SIN→SAW`, the row turned gold, the masthead read Glass Bell and the footer "Loaded Glass Bell." |
| **A save over an existing preset is refused** | "A preset called Glass Bell is already there." with a red **REPLACE IT** beside it and the plain SAVE gone |
| And an authorised replacement replaces | The file's timestamp moved; the sound in it was the new one |
| Search matches more than the name | Typing `mineaaa` — a *bank* — narrowed two presets to **1 of 2** |
| The category menu filters | Set to `Keys`: the preset whose category is `Keysaaa` dropped out, leaving **1 of 2** |
| **Files that are not presets are counted, not hidden** | Two junk `.rnv` files dropped into the folder, then RESCAN: "**2 files could not be read as a preset**" in orange beside "1 of 2 presets", with the real presets still listed |
| The panel resizes like every other | Same corner handle, same rule; the list scrolls inside it rather than the panel growing to four hundred rows |

**Two defects found by driving it, both fixed and both at a seam:**

- **The row the user had just saved was not the row the browser highlighted.**
  Saving writes a file, so the index is stale by definition and the status sent
  back with the save could only say "loaded: 0". A finished scan now pushes the
  status alongside the index.
- **"Loaded Glass Bell." lasted about a fifth of a second.** A preset load bumps
  the reload counter, the editor answers by resending the whole state snapshot,
  and the page announced its parameter count on every snapshot. That line is a
  *connection* message and is now said once.

### The reset menu could not be used with a mouse, 2026-09-15

**This corrects the entry above it.** The previous sub-phase recorded that
synthetic mouse clicks on the reset menu "could not be made to land" and called
it a limitation of the test harness. It was not. It was a real defect, and the
menu could not be chosen with a mouse by anybody.

Two things were wrong, and one of them was mine:

- **The harness was sending clicks in the wrong coordinate space.** Screenshots
  are of the top-level window; the WebView is a child inset by the title bar and
  border, and a click has to be in *its* client space. Every click was landing
  forty pixels above where it was aimed, which reads as "clicks do not work"
  rather than as an offset. Fixing that made every other control clickable —
  and the menu still was not, which is what turned a suspected harness problem
  into a bug report.
- **A React portal moves the DOM node; it does not move the component.** Events
  still propagate along the *React* tree, so a pointer-down on a menu item went
  on to the control that rendered it — and on a knob that means
  `onPointerDown`, which calls `setPointerCapture`. The knob then owned the
  pointer, the release was delivered to the knob instead of the menu, and no
  click was ever generated. The menu now stops pointer propagation at its own
  root, and commits on pointer-up as well as on click, which is what every menu
  in every operating system does anyway.

This is the same bug the previous sub-phase believed it had fixed by moving the
menu into `document.body`. That change was made on a wrong model of what a
portal does, it changed where the menu was drawn and nothing about where its
events went, and it looked fixed because the only route that was ever tested
afterwards was the keyboard.

| Checked | Result |
|---|---|
| **A real right-click, then a real click, resets the knob** | Oscillator 1 Detune dragged to **36 %**, right-clicked, item clicked → **20 %**, and the menu closed |
| Escape still dismisses | Menu gone, value untouched |
| A click elsewhere still dismisses | Menu gone, value untouched, and the click did not choose anything |
| The keyboard still works | Enter on the focused item resets, as before |

**The lesson, and it is the third time this project has met it:** a component
that is correct on its own can be wrong at the seam with the one that contains
it. It is also a lesson about evidence — "the harness cannot do this" is a claim
that needs testing before it is written down, because once written down it stops
anybody looking.

---

### The factory library, 2026-09-15

Driven by hand against the running standalone, with the developer's own preset
folder **absent** — which is the state a new user is in, and the state in which
the browser had nothing in it before this sub-phase.

| Checked | Result |
|---|---|
| **The browser opens with a library** | Ten rows, with no user preset folder on the machine at all |
| Each is described | Categories read Basics, Bass, Lead, Pad, Keys, Pluck, FX; the author on every row is **ProdByRnV**; the bank is **Factory** |
| Origin is said in a word | Every row ends `factory`, which survives a screenshot with no colour in it |
| The count is right | "10 presets" |
| **Loading one changes the instrument** | Slow Bloom: oscillator 1 went to `TRI→SAW` at **40 %**, oscillator 2 came on at **60 %** and stopped reading OFF, and the wavetable displays redrew to the morphed shapes |
| **Its modulation is live** | The masthead read **MOD 3/16** with the lamp green, and Osc 1 Position's arc turned green — the LFO routed to it, shown on the control it moves |
| The browser agrees with the engine | The Slow Bloom row went gold, the masthead read **Slow Bloom**, the footer read "Loaded Slow Bloom." |
| **The Init button works** | Pressed it: MOD back to **0/16**, oscillator 2 back to OFF, both wavetables back to `SIN→SAW` at 0 %, Init highlighted, "Loaded Init." |
| Init is a row as well as a button | Listed under Basics, loadable like any other |

**Not verified by ear.** Nobody has listened to these ten sounds through
speakers — there is no virtual MIDI port on this machine (§6, issue 16), so the
notes in the test suite are rendered rather than played. What *is* verified is
that each one produces finite, audible, non-clipping audio for a held note, and
that the parameters it claims to set are the parameters the instrument ends up
with. Whether "Glass Bell" sounds like a bell is a judgement the developer
should make with the application open.

---

### The four real wavetables, 2026-09-16

Driven by hand against the running standalone. The loader has no interface yet,
so what is verified here is the content and the engine around it; the file path
is covered by the suite.

| Checked | Result |
|---|---|
| The tables are named for what they are | The wavetable strip reads **SWEEP · PULSE · FORMANT · FOLD**, where it read `SIN→SAW · SIN→SQR · TRI→SAW · SAW→SQR` |
| **Pulse is really pulse width** | Selected PULSE and swept Position to 56 %: the display drew a rectangular wave whose high portion is visibly narrower than its low one. Not a blend of two shapes — every frame is a different waveform |
| **Fold is really a wavefolder** | Selected FOLD at 98 %: several reflections inside one cycle, which is what folding looks like and what nothing in the old placeholder set could produce |
| And Fold is the one that proves the analyser | It is the table defined by drawing samples and taking their transform, so seeing it come out right is the load path working end to end |
| The displays follow the position | The wave redraws continuously while Position is dragged, and the readout under it agrees with the knob |
| Startup is not held up | Ready about nine seconds after launch, which is where it was before this sub-phase; the WebView accounts for nearly all of it |

**A measured regression, caught and fixed before it shipped.** The first version
of the spectral tables took **539 ms** to build, in the plugin's constructor. A
host that instantiates forty instances while scanning its menu would have paid
twenty seconds of that, and the standalone visibly waited for its own interface.
Two changes: harmonics below what a float sample can represent are no longer
summed (539 → 216 ms), and the built-ins are built once for the whole process
and shared, since they are immutable (a second instrument now costs nothing).
Both are asserted by the suite rather than left as a note.

**Not a defect, worth recording:** the standalone restores its previous session's
patch on launch, so a fresh window does not show the registry defaults. That is
`juce::StandalonePluginHolder` doing what it is supposed to; pressing **Init**
shows the defaults.

**Not verified by ear.** No virtual MIDI port on this machine (§6, issue 16), so
the four tables have been seen and measured but not heard. Their spectra are
asserted at four positions each and their aliasing floors are in the suite, but
whether Formant sounds like a vowel is a judgement for the developer.

---

## 5b. CPU measurements

Phase 4's remaining exit criterion. Produced by `ApolloTests --benchmark`, which
is built on every platform but run by no CI job — a CPU figure is a measurement,
not a pass or a fail, so it is recorded here rather than asserted in the suite
(`Tests/Performance/Benchmarks.h`).

**Machine:** the development laptop, Windows 11, MSVC, `RelWithDebInfo`,
48 kHz, 512-sample blocks. Figures are **per cent of one core's real time**;
lower is better, and 100 % means the render exactly keeps up with playback.

> **The sections up to "The measurement environment" were taken before Phase
> 10c, on a harness that drifted.** They are kept because they are the record of
> what was known when each phase closed, and because the prose around them
> explains decisions that were made on their basis. Read them within a column
> and never across one. **The 10c sections at the end of §5b supersede them**,
> and are the figures to compare anything future against.

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

| Rack | 8a, cool machine | 8b, same binary, later | 8c, later still | 8d, cool again | 8e |
|---|---:|---:|---:|---:|---:|
| Empty | 0.062 % | 0.081 % | 0.151 % | 0.029 % | 0.035 % |
| Distortion, bypassed | 0.075 % | 0.098 % | 0.185 % | 0.048 % | 0.056 % |
| Distortion, hard clip at 4x | 1.374 % | 2.880 % | 4.848 % | 1.368 % | 1.664 % |
| Distortion, diode (`exp`) at 4x | 2.152 % | 3.098 % | 4.989 % | 1.516 % | 1.784 % |
| Distortion, soft (`tanh`) at 4x | 2.666 % | 3.538 % | 5.863 % | 1.631 % | 1.875 % |
| Delay, bypassed | — | 0.096 % | 0.156 % | 0.036 % | 0.042 % |
| Delay, stereo with feedback | — | 0.270 % | 0.499 % | 0.103 % | 0.138 % |
| Reverb, bypassed | — | — | 1.283 % | 0.337 % | 0.381 % |
| Reverb, 8-line FDN | — | — | 1.359 % | 0.369 % | 0.441 % |
| Gate, stereo-linked peak | — | — | — | 0.054 % | 0.069 % |
| Compressor, stereo-linked RMS | — | — | — | 0.137 % | 0.171 % |
| Equaliser, all bands transparent | — | — | — | — | 0.037 % |
| Equaliser, 7 bands x1 | — | — | — | — | 0.399 % |
| Equaliser, 7 bands x4 | — | — | — | — | 0.526 % |
| **All six at once** | — | — | — | — | **2.831 %** |

The empty row is the honest baseline: it is the benchmark's own input loop, not
the rack, which does nothing at all when no slot is filled. Against it, in the
same run, the **delay** costs about 0.35 % — a linear stage with two interpolated
reads and four filter poles — and the **reverb** about 1.2 %, which is eight
delay lines, eight dampers, eight allpasses and a Hadamard per sample.

The reverb's two rows are the interesting pair: **bypassed costs almost what
active costs**, because the network keeps running so its tail can decay rather
than freeze (ADR-0057). Removing an effect from the chain is the free option;
bypassing it is a musical gesture, not a CPU one.

The last row is the one nobody could measure until every effect existed, and it
is the headline of Phase 8: **a rack holding all six effects, audibly
configured, costs 2.83 % of one core.** Against the 150 % the heaviest patch
costs at full polyphony (issue 13), the effects are not where Apollo's CPU goes —
which is worth knowing before Phase 10 starts optimising, because it says where
not to look.

The equaliser's three rows say two things. **A transparent equaliser is free** —
0.037 % against an empty rack's 0.035 %, which is a transparent band being skipped
rather than multiplied through, and is what makes one safe to leave in the chain.
And **four times the sections is a third more time, not four times**: 0.35 % net
at one instance against 0.48 % at four, measured at every order in between. A
biquad's state update waits on the previous sample's, so one section per band
leaves most of the processor's execution units idle; the extra sections fill those
slots rather than queueing behind them. Raising the slope is close to free.

**The five columns are the same code**, and the fourth is the proof that the
drift is the machine rather than Apollo: measured on a fresh cool laptop, 8d's
distortion rows land back where 8a's were and the reverb measures a *third* of
what it did in 8c — from the same binary. Two *idle* runs minutes apart had
already disagreed by 40 % during 8c, and in one of them hard clipping measured
more than `tanh`, which cannot be true. So read each column as ratios within
itself and never compare a figure across columns. Phase 10 owns profiling, and
the first thing it needs is a measurement environment that does not drift — the
alternating `measurePair` harness exists for exactly this reason and these
absolute rows do not use it.

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

### The measurement environment (Phase 10c)

Everything above this line was measured before Phase 10c and everything below it
after, and the two are not comparable. That is the point of the phase (ADR-0069).

The old figures drifted: the same binary measured the reverb at 1.36 % and then
at 0.37 %, two idle runs minutes apart disagreed by forty per cent, and in one of
them hard clipping came out more expensive than `tanh`, which cannot be true.
The table had grown a column per phase and a paragraph asking the reader to
remember that only ratios within a column mean anything.

What the report does now:

- **A reference kernel** — a fixed amount of arithmetic, unchanged for the life
  of the project — is timed at the start of every run. Every figure is reported
  raw *and* normalised to a machine on which that kernel takes 3.5 ms. The
  normalised column is the one to compare between runs and between phases.
- **The measuring thread is pinned**, and not to core 0, which on Windows
  services device interrupts.
- **Every row carries its own spread**, and the headline is the median of five
  passes rather than the best of three.
- **The run says whether to believe itself.** If something else was using the
  machine for more than five per cent of the time the reference was timed, the
  report says so at the top in those words.

None of this makes a laptop a measurement instrument. It makes one that says
when it is not being one.

**This run:** reference unit **3.682 ms** (0.950x the reference machine),
typical contention **5.4 %**, worst burst 22.1 % off the fastest. That is just
over the five per cent gate, so this run is flagged unsettled and its absolute
figures carry that caveat. Rows still reproduced to within **3.5 %** between two
consecutive reports.

### Voice engine (Phase 10c)

Normalised figures. The default patch is oscillator 1 alone, which is what
Apollo loads with; the heaviest is both oscillators at 16-voice unison plus the
sub and the noise generator — 34 oscillators per voice, 1088 of them at 32
voices.

| Voices | Default | Heaviest | Default, 4 routings |
|---:|---:|---:|---:|
| 1 | 0.28 % | 5.50 % | 0.57 % |
| 8 | 2.28 % | 43.46 % | 4.89 % |
| 32 | **9.84 %** | **174.7 %** | **16.89 %** |

Cost is close to linear in voices in all three columns — 0.29 %, 5.2 % and
0.53 % per voice — so polyphony scales predictably and the per-voice figure is
the one that matters.

**The heaviest patch still cannot run at full polyphony**, at 175 % of one core.
That is inherent arithmetic rather than a defect; ADR-0029 chose to publish the
limit rather than lower the ceilings, and it is issue 13.

### Effects rack (Phase 10c)

Normalised, stereo, on the finished mix — once per block however many notes are
held.

| Rack | Cost |
|---|---:|
| Empty | 0.035 % |
| Distortion, soft (`tanh`) at 4x | 2.000 % |
| Distortion, diode (`exp`) at 4x | 1.778 % |
| Distortion, hard clip at 4x | 1.666 % |
| Delay, stereo with feedback | 0.139 % |
| Reverb, 8-line FDN | 0.476 % |
| Reverb, **bypassed** | 0.424 % |
| Gate, stereo-linked peak | 0.068 % |
| Compressor, stereo-linked RMS | 0.155 % |
| Equaliser, all bands transparent | 0.034 % |
| Equaliser, 7 bands x1 | 0.357 % |
| Equaliser, 7 bands x4 | 0.533 % |
| **All six at once** | **2.773 %** |

With the harness fixed, two things in this table are now readable that were not.
**Hard clipping is cheaper than `tanh`, which is cheaper than `exp`** — the order
arithmetic says it should be, and the order the drifting harness once reported
backwards. And **a bypassed reverb costs almost what an active one costs**,
because the network keeps running so its tail can decay rather than freeze
(ADR-0057): removing an effect is the free option, bypassing it is a musical
gesture.

A full rack of all six, audibly configured, costs **2.8 % of one core** — against
175 % for the heaviest patch at full polyphony. The effects are not where
Apollo's CPU goes, which is worth knowing before 10d starts optimising.

### Worst-case callback (Phase 10c)

**The measurement this project did not have.** Every figure above is an average
over thousands of blocks, and an average is the wrong statistic for real-time
audio: a synthesiser that averages thirty per cent of its deadline and spends one
block at three hundred does not sound like one using thirty per cent, it sounds
like a click. The deadline is per callback and so is the failure.

Each row is 938 consecutive callbacks through the whole processor, timed
individually, as a percentage of the 10.67 ms a 512-sample block at 48 kHz has
to be filled in. Notes arrive *during* the trace, because the expensive blocks
are the ones where something happens — a voice allocated, a voice stolen, an
envelope changing stage.

| Patch | Median | p99 | p99.9 | Worst |
|---|---:|---:|---:|---:|
| Default, 8 held | 2.84 % | 3.21 % | 4.08 % | 4.19 % |
| Default, 32 held | 4.67 % | 5.82 % | 6.11 % | 6.49 % |
| Default + full rack, 32 held | 6.75 % | 7.28 % | 7.58 % | 7.72 % |
| Heaviest, 8 held | 41.39 % | 44.02 % | 44.73 % | 44.90 % |
| **Everything at once, 32 held + stealing** | **90.37 %** | **102.15 %** | **107.89 %** | **111.66 %** |

The first four rows are the good news and they are the ordinary cases: the worst
single callback is within a fifth of the median, so Apollo's cost is steady
rather than spiky. Nothing in the engine produces an occasional expensive block.

The last row is **the worst patch Apollo's own controls can build**, and nothing
before 10c had measured the combination — every earlier figure measured the
heaviest patch, or a full rack, or the modulation matrix, one at a time. A user
builds them together. It misses the deadline one callback in a hundred, and that
is issue 13 stated in the terms that actually matter.

### First use (Phase 10c)

What somebody waits for, and what a session pays to hold.

| | |
|---|---:|
| Constructing the **first** instrument in a process | **142 ms** |
| Constructing a second | 0.91 ms |
| `prepareToPlay` at 48 kHz, 512 samples | 0.18 ms |
| Rendering all ten factory presets to documents | 4.95 ms |
| Loading all ten factory presets into an instrument | 10.35 ms |
| The process before any instrument exists | 12.15 MB |
| The **first** prepared instance adds | **6.82 MB** |
| The second adds | 2.61 MB |
| Each of the next sixteen adds | 2.57 MB |

**An instrument costs about 2.6 MB**, so a project holding twenty of them costs
around 50 MB — which is not a number anybody needs to worry about.

**The first instrument in a process costs 142 ms and 6.8 MB**, against 0.9 ms
and 2.6 MB for every one after it. That difference is the four band-limited
wavetables, built once and shared (ADR-0066), and it is the whole justification
for sharing them: a host that instantiates a plugin forty times while scanning
its menu would otherwise pay five and a half seconds and a quarter of a gigabyte
instead of 142 ms and 110 MB.

**These four rows were wrong when 10c first published them**, and the reason is
worth keeping. They read 4.17 ms and 4.71 MB, and 10c recorded an open question
because that contradicted ADR-0066. ADR-0066 was right. Two `juce::UnitTest`
subclasses held a `WavetableLibrary` as a **member**, and a unit test object is
constructed at static-initialisation time — so 165 ms of table building happened
before `main()`, and every benchmark in the process measured a library that
already existed. Both are lazy now. The bug cost the suite 165 ms on every
invocation, including `--help`.

### Visualisation capture (Phase 10c)

Measured alternately, so both sides sit under the same drift.

| Voices | Render alone | Render + everything | Capture adds |
|---:|---:|---:|---:|
| 1 | 0.274 % | 0.339 % | 0.062 % |
| 8 | 2.207 % | 2.394 % | 0.178 % |
| 32 | 10.212 % | 10.755 % | 0.516 % |

Still well under one per cent of a core at full polyphony, so PRD §30.1's
requirement — that scopes for every source must not materially reduce polyphony —
continues to hold. An instance whose editor is closed pays none of it (ADR-0048).


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

## 5c. Instrument validation measurements

Phase 10a. Everything above measures a *component*; this measures the assembled
instrument — a MIDI note in, audio out, through the voice engine, the filters,
the amplifier and the rack. A synthesiser can be built entirely from correct
parts and still be out of tune, offset from zero, noisy at rest, or capable of
emitting an infinity (ADR-0067).

Produced by `ApolloTests --category Validation`, at 48 kHz in 512-sample blocks
unless stated. These are **measurements**: the figure is recorded here and the
test bounds it where a regression would matter, not where today's build sits.

| Measured | Result |
|---|---|
| **Tuning**, every third note from 24 to 96 | Worst error **0.744 cents**, at note 27. Bounded at 1 cent |
| Tuning across sample rates | A3 within a cent at 44.1, 48, 88.2 and 96 kHz |
| **THD+N** of the sub oscillator at A3 | **0.0013 %** (−97.6 dB). Bounded at 0.1 % |
| **Noise floor at rest** | **Exactly zero**, with every source at full level and with no note |
| **DC offset**, all four sources at once | **−112.7 dBFS**. Bounded at −60 dBFS |
| **Inharmonic content**, 7-voice unison saw at 1760 Hz | **−121.9 dBc**. Bounded at −60 dBc |
| **Step response**: cutoff swept 20 kHz → 20 Hz between two blocks | Largest sample-to-sample step **0.00009**, against **0.00192** in steady state — the change is quieter than the sound |
| **Silence after release**, chain with delay and reverb | Exact zero **8.41 s** after note-off, against a predicted ~10 s |
| **Every parameter** at both ends and the middle, note held | All finite, all bounded |
| **Worst patch the controls allow**, one minute | Peak per ten seconds: **61.0, 61.5, 60.2, 60.6, 60.2, 60.2 dBFS** — flat, so nothing compounds |

**Why the hostile patch is loud and why that is not the measurement.** It asks
for roughly 88 dB of deliberate gain — seven EQ bells at +18, the EQ trim at
+18, 24 dB of makeup and the master at its +6 — on top of four sources at full
into a 36 dB distortion. The number it produces is the instrument obeying. What
had to be proven is that maximum delay feedback, a twenty-second reverb and two
resonant filters in one chain do not **compound**, and the flat window trace is
that proof.

**Two mistakes made and corrected while writing this, both in the tests rather
than in Apollo:**

- The first hostile-patch assertion bounded the absolute peak, which is a
  statement about the range of the gain controls rather than about stability.
- The first silence test gave a half-second delay at 0.6 feedback thirty seconds
  to reach the 1e-18 flush point, which takes eighty-one repeats — forty
  seconds. It reported a defect that did not exist. The settings are now chosen
  by that arithmetic, and the arithmetic is written down beside them.

**Known item, deliberately not addressed:** four DSP test files still have their
own spectrum helpers, which predate the shared analysis toolkit. They measure
different things with thresholds tuned against their own analysis, so converting
them would be churn in passing tests for no functional gain (CLAUDE.md §41). New
measurement work uses `Tests/Analysis/SignalAnalysis.h`.

---

## 5d. Regression renders

Phase 10b. §5c measures properties of the instrument; this asks a different
question — **is it still the same sound?** Fifteen renders, ten of them the
factory presets, each compared against a reference checked in beside it
(ADR-0068).

Produced by `ApolloTests --category Regression`. A golden is not a wave file: it
is a description of the render — peak, RMS, mid and side level through sixteen
slices of time, and energy in thirty-two logarithmic bands — because bit-exact
audio is not something four compilers agree on, and a changed WAV in a diff tells
a reviewer nothing.

### What the suite can see

A fingerprint that always matched would pass for ever and protect nothing, so
each part of it is shown a change it should catch. These are the **measured**
movements against the sensitivity patch, and the smallest change each part can
resolve — the tolerance is 0.1 dB on levels and 0.2 dB on bands:

| Change | Largest movement | Resolves down to |
|---|---|---|
| Filter cutoff, +1 % | 0.223 dB | about **0.5 %** |
| Wavetable position, +0.5 % | 1.732 dB | about **0.06 %** |
| Envelope release, +1 % | 0.249 dB | about **0.5 %** |
| Output level, +0.05 dB | 0.050 dB | **0.1 dB**, by construction |
| Filter resonance, +1 % | 0.079 dB | about **1.3 %** |
| Unison spread, −1 % | 0.076 dB | about **1.3 %** |

The two orders of magnitude between the most and least sensitive are the point
of measuring rather than assuming. The wavetable position — the parameter most
specific to this instrument — is the one the suite sees most sharply, and a
resonance or a stereo width is the one it sees least.

**The first version of this test failed, and it was right to.** It moved the
cutoff by three per cent and the fingerprint reported 0.019 dB, well under the
tolerance. Two things were wrong and only one was the harness: sixteen bands were
too coarse, at two thirds of an octave each, which is now thirty-two; and the
patch under test sat at the wavetable's default position, which is very nearly a
sine — so there were barely any harmonics for a filter to act on. Both fixed, the
same measurement moves eleven times further.

### What it found on the way

| Observed | Result |
|---|---|
| **Block size** — the same patch at 512 samples and at 64 | **Sample for sample identical.** Asserted as "the fingerprints agree"; the bit-exactness is logged, not asserted |
| **Optimisation** — MSVC Debug against the strict optimised build | **Every case bit-identical.** A property of MSVC's default floating-point mode as much as of Apollo; nothing in the design depends on it |
| **Repeatability** — the same case rendered twice in one process | Identical, including the noise generator, whose seed is reset per voice |
| **Cross-platform spread** | Reported per case by every CI job; see the note below |

### Regenerating

    ApolloTests --goldens > Tests/Regression/GoldenRenders.cpp

Nothing does this automatically and nothing should. When a change to Apollo is
meant to change how it sounds, the goldens are regenerated in the same commit and
the diff is the record of what the change did to the sound. A harness that
rewrote its own references when they stopped matching would be an elaborate way
of asserting nothing.

---

## 6. Known issues

| # | Issue | Severity | Notes |
|---|---|---|---|
| 1 | ~~The editor has never been seen running~~ | **Closed** | Verified 2026-09-08 by launching the standalone. The WebView renders, and the page builds all 25 controls from parameter metadata alone, with every default matching the registry. Finding it running is also what exposed the WebView2 backend defect below. |
| 2 | VST3 has not been loaded in a DAW | Medium | **Partially verified.** The bundle loads as a library and exports `GetPluginFactory`, `InitDll` and `ExitDll`; `moduleinfo.json` declares the correct vendor, version and `Instrument`/`Synth` subcategories. That is not the same as instantiating in a host, which still needs a DAW. |
| 3 | ~~Standalone has not been launched against an audio device~~ | **Closed** | Verified 2026-09-08. Windows Audio, Speakers (Realtek) at 48 kHz / 480 samples, output channels 1+2. A MIDI note sent to a virtual port drove Apollo's output to a measured session peak of **0.1829**, against 0.0000 before and after — read from the Windows audio-session meter, not inferred. |
| 4 | Linux CI job failed twice, both causes fixed and confirmed | Resolved | Kept as a record rather than deleted, because both fixes are load-bearing and neither is obvious from the code. **(a) GTK include paths.** `juce_gui_extra.cpp: fatal error: gtk/gtk.h: No such file or directory`, preceded by `warning: "JUCE_WEB_BROWSER" redefined`. Apollo hand-defined `JUCE_WEB_BROWSER=1` instead of setting JUCE's `NEEDS_WEB_BROWSER`. On Linux `_juce_link_optional_libraries` reads that property to decide *both* the define and whether to link `juce::pkgconfig_JUCE_BROWSER_LINUX_DEPS` — the only source of the GTK/WebKitGTK include paths — so the manual define switched the include on while the path was never supplied. **(b) Runner memory exhaustion.** The build then reached 77%, every in-flight compile was SIGTERMed at one instant with no diagnostic, and the runner reported a shutdown signal. No newer run existed, so `cancel-in-progress` was not responsible. The Linux job compiles all of JUCE twice (plugin + test runner, ADR-0010) and since Phase 2 those units also pull in GTK/WebKit headers. Build parallelism is now capped per platform (Linux 2) and the two halves are built as separate targets. Both fixes are confirmed by the green run recorded in §4. |
| 5 | Symbol visibility still unresolved | Low | ADR-0009 deferred the decision to Phase 1. Plugin targets now exist, so it can be closed. |
| 6 | No allocation/lock detector on the audio thread | Medium | Real-time safety is by construction and review, not enforced by a tool. Carried into Phase 10 and **still open after 10c**: the obvious mechanism, replacing global `operator new`, would miss `juce::HeapBlock`, which calls `malloc` directly and is what every audio buffer in JUCE is built on — so a detector built that way would report "no allocations" while not watching the allocations most likely to happen. Needs a platform allocator hook rather than a language one. |
| 7 | ARM64 unverified | Low | No ARM64 runner in the matrix. Phase 11. |
| 8 | `ROADMAP.md` refers to `UI-BINDINGS.md`; the file is `UI_BINDINGS.md` | Trivial | Not renamed silently; other documents cross-reference it. |
| 9 | JUCE 9.0.x exists upstream | Informational | Apollo pins JUCE 8 because the specification says JUCE 8 (ADR-0002). |
| 10 | ~~`filter_*` parameters are un-indexed while the PRD specifies two filters~~ | **Closed** | Resolved in Phase 5b. They became `filter1_*`, joined by `filter2_*` and `filter_routing`, through a schema version 1 to 2 migration rather than a bare rename — the first real use of the migration path built in Phase 2 (ADR-0032). Done while Apollo is pre-1.0, which is the only window in which it is cheap. |
| 11 | Company name and plugin codes are inferred | Low | `ProdByRnV`, `Prnv`, `Apol`, `com.prodbyrnv.apollo` were inferred from the GitHub organisation. Easy to change now, **permanent once released** — please confirm. |
| 12 | Standalone showed "Navigation to the webpage was canceled" instead of the UI | **Fixed** | Found on 2026-09-08, the first time anyone ran the application. The editor never selected a WebView backend, so JUCE built the legacy Internet Explorer control despite `JUCE_USE_WIN_WEBVIEW2=1` and `NEEDS_WEBVIEW2` — necessary but not sufficient, per JUCE's own documentation. The IE control supports neither the resource provider nor the native integration, so the page could not load. Fixed by naming the backend per platform and by giving WebView2 a writable per-user data folder, which also prevents the same silent fallback in hosts whose program directory is read-only (ADR-0027). |
| 13 | The heaviest patch cannot sustain full polyphony in real time | Medium | Measured in Phase 4c and re-measured properly in 10c: 2 x 16-voice unison plus sub and noise costs **175 % of one core at 32 voices** and 87 % at 16 (§5b). The default patch is unaffected at 9.8 %. **10c restated it in the terms that matter**: the worst patch the controls can build, at full polyphony with a full rack and the modulation matrix running, misses its callback deadline one block in a hundred — p99 102 %, worst 112 %. This is inherent arithmetic — 1088 interpolating oscillators — rather than a defect, so the fix is SIMD and interpolation work in 10d. ADR-0029 records why the ceilings were published rather than lowered. |
| 14 | ~~The MIDI Learn interface has not been driven by hand~~ | **Closed** | Verified 2026-09-10 (§5a). The learn mode, the badges, the tooltips, Escape, a real controller completing a learn, a mapping surviving a clean restart and still driving its parameter, and an MPE Configuration Message reconfiguring Apollo from the MIDI stream were all driven by hand against the running standalone. |
| 15 | ~~Synthetic input does not reach the WebView reliably~~ | **Closed** | Four sub-phases in a row (8b-8d, §5a) recorded that the interface could be read but not driven, because injecting input at the desktop reaches whatever window is in front. Solved in 8e by posting the mouse messages directly to Apollo's `Chrome_RenderWidgetHostHWND` child and capturing with `PrintWindow`/`PW_RENDERFULLCONTENT`, which needs neither the cursor nor the foreground. The driving process must call `SetProcessDPIAware` first, or it measures a 1920x1080 window as 1280x720 and captures only its corner. |
| 16 | No MIDI source on this machine for chain tests | Low | 8e and 8f could both drive the interface but neither could play a note into a configured chain, because no virtual MIDI port was running. Audible behaviour is asserted through `processBlock` in `Tests/Audio/EffectsIntegrationTests.cpp` instead, up to and including a full rack of six restored from a preset and still sounding. Worth having a port available before Phase 9, where a preset is supposed to be recognisable by ear. |
| 18 | ~~The wavetable build cost cannot be observed where ADR-0066 says it happens~~ | **Fixed** | Raised by 10c, found and fixed in the debugging pass after it. ADR-0066 says building the four wavetables takes a couple of hundred milliseconds; the benchmark timed that construction and got **0.01 ms**, in Debug as well as optimised. ADR-0066 was right and the benchmark was blind: `UnisonTests` and `WavetableTests` each held a `WavetableLibrary` as a **class member**, and a `juce::UnitTest` subclass is constructed at static-initialisation time in order to register itself — so **165 ms of table building ran before `main()`**, and every measurement in the process saw a library that already existed. Both are lazy now. Three things improved: the first-instrument figures are real (142 ms and 6.8 MB, against 4.17 ms and 4.71 MB before), every invocation of the test binary stopped paying 165 ms it did not need — including `--help` and a ctest run of one unrelated category — and the contradiction with ADR-0066 is gone. **No product code was affected**: nothing under `Source/` holds a heavy object at file scope, so the plugin never had this. |
| 17 | ~~A duplicated rack slot claimed to be in the chain~~ | **Fixed** | Found in 8f by building a chain by hand and landing the same effect in two slots. The engine has resolved duplicates to first-occurrence-wins since 8a; the interface lit both, because it only checked whether the named effect existed in this build. The page now runs the same resolution the engine does, and a duplicated slot reads `duplicate` with its number unlit. |

---

## 7. Blockers

**None for 10d.** One blocker exists further out and is worth naming now because
it cannot be cleared from this machine:

- **10e needs real DAWs.** Host compatibility — discovery, load and unload,
  automation, state and preset recall, MIDI, variable block sizes, sample-rate
  changes, bypass, transport, latency reporting, offline rendering — cannot be
  verified by any amount of testing here. Nothing in Apollo has ever been loaded
  into a host (issue 2).
- **A virtual MIDI port would help before then.** The ten factory presets and
  the four wavetables are measured but have never been *heard*, because this
  machine has no MIDI source (issue 16).

---

## 8. Recommended next action

Continue with **Phase 10d — optimisation of what 10c identified.**

Unusually for an optimisation phase, there is nothing to guess about. 10c
measured it:

1. **The target is the voice, not the rack.** A full rack of all six effects,
   audibly configured, costs **2.8 % of one core**. The heaviest patch at full
   polyphony costs **175 %**. Optimising effects would be work in the wrong
   place, and the measurement says so rather than intuition (§5b).
2. **The failure is per callback, not on average.** The worst patch the controls
   can build — heaviest voices, modulation matrix and full rack together at full
   polyphony — sits at **90 % of the block deadline in the median and 102 % at
   the 99th percentile**. It is not "a bit slow"; it misses one block in a
   hundred, and a missed block is a click (issue 13).
3. **The arithmetic is 1088 interpolating oscillators.** Two oscillators at
   16-voice unison, plus sub and noise, across 32 voices. ADR-0029 chose to
   publish that ceiling rather than lower it, so the work is to make each
   oscillator cheaper — SIMD across unison voices and a cheaper interpolation
   are the two obvious candidates — rather than to take the ceiling away.
4. **The regression renders are what make it safe.** An optimisation is a change
   that is *supposed* to leave the sound alone. 10b built the machinery that
   checks exactly that, and it is the reason 10d can be attempted with
   confidence rather than by ear (ADR-0068).
5. **Two measurement questions travel with it.** There is still no allocation or
   lock detector on the audio thread (issue 6), and the obvious mechanism does
   not work — replacing global `operator new` would miss `juce::HeapBlock`,
   which calls `malloc` directly and is what every JUCE audio buffer is built
   on.

A spectrum analyser remains the one unticked Visualization item, and both PRD and
ROADMAP mark it optional.

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
