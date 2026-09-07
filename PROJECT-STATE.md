# Apollo — Project State

> **Purpose:** the authoritative, verified record of what actually exists in this
> repository. The specification documents describe what Apollo *should* become;
> this file describes what it *is*. Nothing is recorded as complete here unless it
> was built and run.
>
> Update this file at the end of every roadmap step.

**Last verified:** 2026-09-07
**Apollo version:** 0.1.0

---

## 1. Current position

| | |
|---|---|
| **Phase** | Phase 4 — Wavetable Oscillator System |
| **Status** | **4a and 4b complete; 4c open** |
| **Milestone** | M4 — Synthesis Core |
| **Next step** | Phase 4c — oversampling infrastructure and CPU measurement |

**Apollo is a wavetable synthesizer.** Two band-limited wavetable oscillators,
each with up to 16 detuned and stereo-spread unison voices, plus a sine sub and
a stereo noise generator, mixed with per-source level and balance and played
polyphonically through the VST3 and standalone builds. Aliasing is measured, not
asserted: worst case -98.5 dBc against a -60 dBc budget.

Still placeholders: the amplitude envelope is a linear attack/release (the four
DAHDSR envelopes are Phase 5), and the four built-in wavetables are
mathematically defined morphs rather than designed factory content (Phase 9).

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
  serves a built-in placeholder page that builds its controls **from the
  parameter metadata alone**, never from hard-coded ranges, and detects preset or
  project loads to resynchronise wholesale. The React frontend that replaces that
  page is Phase 7.

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

The amplitude envelope remains a linear attack/release, a deliberate placeholder
for the DAHDSR envelopes in Phase 5.

---

## 3. What is NOT implemented

| Phase | Absent |
|---|---|
| 4c | Oversampling infrastructure for nonlinear stages; CPU cost measured across polyphony levels |
| 5 | Filters, envelopes, LFOs, modulation matrix |
| 6 | MIDI Learn, controller profiles, aftertouch, MPE (basic note/CC handling landed in Phase 3) |
| 7 | The `WebUI/` React frontend, visualizers, telemetry |
| 8 | Every effect and the FX rack |
| 9 | Presets, wavetable resources, resource packaging |
| 10–12 | DSP validation, profiling, host testing, packaging, release hardening |

**20 of the 25 registered parameters now affect audio** — the whole source
section plus `master_gain`. The remaining five (`filter_cutoff`,
`filter_resonance`, `filter_drive`, `fx_distortion_mix`, `fx_delay_time`) are
exposed to hosts and to the UI but have nothing to act on until the filter and
effect stages exist in Phases 5 and 8.

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

**206,757 assertions, 0 failures**, across 12 test classes:

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
| 10 | `filter_*` parameters are un-indexed while the PRD specifies two filters | Low | A Phase 5 decision. **These IDs have now been written into the registry**, so changing them is a migration, not a rename (Docs/PARAMETER-CONVENTIONS.md §1). |
| 11 | Company name and plugin codes are inferred | Low | `ProdByRnV`, `Prnv`, `Apol`, `com.prodbyrnv.apollo` were inferred from the GitHub organisation. Easy to change now, **permanent once released** — please confirm. |
| 12 | Standalone showed "Navigation to the webpage was canceled" instead of the UI | **Fixed** | Found on 2026-09-08, the first time anyone ran the application. The editor never selected a WebView backend, so JUCE built the legacy Internet Explorer control despite `JUCE_USE_WIN_WEBVIEW2=1` and `NEEDS_WEBVIEW2` — necessary but not sufficient, per JUCE's own documentation. The IE control supports neither the resource provider nor the native integration, so the page could not load. Fixed by naming the backend per platform and by giving WebView2 a writable per-user data folder, which also prevents the same silent fallback in hosts whose program directory is read-only (ADR-0027). |

---

## 7. Blockers

**None.** Phase 4c can begin.

---

## 8. Recommended next action

Begin **Phase 4c — nonlinear preparation**, the last of Phase 4. Its scope is
the three unticked `ROADMAP.md` tasks plus the one open exit criterion:

1. Decide where oversampling is required, and record why. Everything Apollo has
   built so far is linear, and a linear stage cannot fold, so this is a decision
   about the distortion, waveshaping and aggressive warp stages still to come
   rather than about anything currently in the signal path.
2. State explicitly which stages are *not* oversampled, so the absence is a
   recorded decision rather than an oversight (CLAUDE.md §23).
3. Build the reusable oversampling infrastructure those stages will use, with
   its own tests, ahead of the first stage that needs it.
4. Measure CPU cost across representative polyphony levels — the one Phase 4
   exit criterion still open. It needs a real measurement on real hardware
   rather than an estimate, and it is the number that will say whether 32 voices
   with 16-voice unison on both oscillators is a configuration Apollo can
   honestly offer.

Still worth doing, both cheap and both carried since Phase 3: close ADR-0009
(symbol visibility) now that plugin targets exist, and open the plugin once to
confirm the editor renders (§6, issues 1-3).

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
