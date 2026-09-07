# Apollo Decision Log

Architectural decisions that would otherwise be re-litigated, re-derived or
silently reversed. ARCHITECTURE.md §17 and CLAUDE.md §42 require significant
changes to be documented; this is where they go.

Each entry records what was decided, why, and what was given up. A decision can
be superseded — mark it, do not delete it.

---

## ADR-0001 — CMake is the authoritative build system

**Phase 0 · Accepted**

CMake, with no Projucer project checked in. Mandated by CLAUDE.md §31 and
ARCHITECTURE.md §10.

Consequence: every new source file, module, asset and test target must be added
to a `CMakeLists.txt`. A file that exists on disk but not in CMake does not build,
and this is intentional — it keeps the build definition honest.

---

## ADR-0002 — JUCE 8.0.15, pinned by commit and verified

**Phase 0 · Accepted**

`CMake/ApolloDependencies.cmake` pins JUCE to tag `8.0.15`, commit
`91ad83ae34a81e0833b1a2b0866f54846370ae53`.

- The specification targets JUCE 8 (PRD §5, ARCHITECTURE.md §5, CLAUDE.md §6.1).
  8.0.15 is the newest JUCE 8 release at the time of writing.
- **JUCE 9.0.x exists upstream.** Adopting it is a specification change, not a
  build-system decision, so it was deliberately not pinned. Raise it as a product
  decision if wanted; it would require re-validating the WebView integration,
  plugin wrappers and DSP behaviour.
- The clone is shallow (fast configure) but the resolved commit is verified
  against the pin, because tags are mutable server-side. A mismatch fails the
  configure unless `APOLLO_ALLOW_UNPINNED_JUCE=ON`.
- `APOLLO_JUCE_SOURCE_DIR` allows an existing checkout for offline builds and CI
  caching, as a cache variable — never a committed path (CLAUDE.md §31.2).

**Given up:** configure requires network access on first use unless a local
checkout is supplied.

---

## ADR-0003 — C++20

**Phase 0 · Accepted**

C++20 with compiler extensions disabled. Supported by every targeted toolchain
and by JUCE 8, and gives `constexpr` facilities that let convention checks such
as parameter-ID validation run at compile time. Disabling extensions keeps MSVC,
Clang and GCC compiling identical source.

---

## ADR-0004 — `juce::UnitTest` as the test framework

**Phase 0 · Accepted**

CLAUDE.md §32 requires dependencies to be avoided where JUCE or the standard
library suffice. `juce::UnitTest` ships in `juce_core`, which is already a hard
dependency, so it costs nothing extra. Catch2 or GoogleTest would add a second
dependency to pin, license-audit and build on every supported platform.

**Given up:** a smaller assertion vocabulary and no built-in parameterised tests.
Revisit only against a concrete need, and record it here rather than mixing
frameworks. Rationale and usage: [TESTING.md](TESTING.md).

---

## ADR-0005 — Two-tier warning policy

**Phase 0 · Accepted**

`apollo::project_options` (safe, applied everywhere) and `apollo::strict_warnings`
(aggressive, applied only to Apollo-owned libraries). Targets that compile JUCE
module sources use JUCE's recommended warning flags instead.

Applying Apollo's strict set to third-party sources would bury Apollo's own
diagnostics, and noisy warnings get disabled rather than fixed.

This produces a structural rule worth stating on its own: **Apollo code belongs in
libraries, and the plugin/standalone/test targets stay thin shells.** That is the
same boundary ARCHITECTURE.md §5.1 requires between DSP and the plugin wrapper —
here it is enforced by the build rather than by discipline.

`APOLLO_WARNINGS_AS_ERRORS` is off locally and on in CI, so exploration is not
blocked while the mainline stays clean.

---

## ADR-0006 — Default build configuration is `RelWithDebInfo`

**Phase 0 · Accepted**

Single-config generators default to `RelWithDebInfo` rather than `Debug`.
Unoptimised DSP does not run at a realistic real-time cost, so a `Debug` default
would give a misleading impression of performance during everyday work.
`RelWithDebInfo` keeps symbols for debugging. `Debug` remains available and is
what sanitizer builds should use.

---

## ADR-0007 — No fast-math, ever, without evidence

**Phase 0 · Accepted**

Fast-math style optimisations are not enabled. Apollo depends on IEEE-754
semantics for the NaN/Inf guards and denormal handling required by CLAUDE.md
§34.2 and §37; fast-math permits the compiler to assume those values never occur
and delete the guards, turning a defensive check into dead code.

Revisit only with measured evidence during Phase 10 performance work, never as a
default.

---

## ADR-0008 — Reconciled repository layout

**Phase 0 · Accepted**

CLAUDE.md §44, PRD §45 and ARCHITECTURE.md §10 propose slightly different
structures. The reconciliation, and the reasoning per conflict, is in
[REPOSITORY-LAYOUT.md](REPOSITORY-LAYOUT.md) §1.

Directories are created when they receive their first file rather than
pre-created as empty placeholders, because an empty tree stops reflecting what
actually exists and then misleads.

---

## ADR-0009 — Symbol visibility deferred to Phase 1

**Phase 0 · Accepted**

Hidden default symbol visibility is the right end state for plugin bundles, but
it interacts with the plugin format's exported entry points and cannot be
meaningfully validated without a real VST3/standalone build. Setting it now would
be an unverified change to link behaviour, so the toolchain default is retained
and the decision moves to Phase 1, where it can be tested.

---

## ADR-0010 — The engine is an INTERFACE library, not a static library

**Phase 1 · Accepted**

`apollo_engine` is a CMake INTERFACE library that propagates Apollo's
JUCE-dependent sources to each final target, rather than a STATIC library that
compiles them once.

JUCE modules add their sources to whichever target links them. A STATIC library
linking `juce_audio_processors` would compile the JUCE module sources into
itself, and the plugin target would compile them again, producing duplicate
symbols at link time. An INTERFACE library sidesteps that entirely — it is the
same mechanism JUCE uses for its own modules.

**Given up:** the engine sources are compiled once per final target (the plugin
and the test runner) rather than shared. That cost is small and buys a build
that cannot silently diverge between the two.

`apollo_core` remains STATIC and JUCE-free, so pure logic is still compiled once
and held to the strict warning set.

---

## ADR-0011 — The bridge protocol is independent of the WebView

**Phase 2 · Accepted**

Message parsing, validation and serialization live in `Source/UI/BridgeProtocol.*`
and depend on neither APVTS nor any WebView. `ParameterBridge` connects that
layer to APVTS; `ApolloWebViewEditor` is pure transport on top of both.

The WebView is an untrusted input boundary (UI_BINDINGS.md §14), so its whole
validation surface — malformed JSON, wrong protocol versions, hostile numeric
values, oversized payloads, unknown parameters — needs exhaustive testing. Behind
a browser that testing is slow, flaky and partly manual. In front of it, it is
ordinary unit testing, and the suite runs headless in CI with no browser
dependency at all.

---

## ADR-0012 — Out-of-range parameter values are rejected, not clamped

**Phase 2 · Accepted**

UI_BINDINGS.md §6 permits either clamping or rejecting a value outside `[0, 1]`.
Apollo rejects, with `INVALID_PARAMETER_VALUE`.

A normalised value outside `[0, 1]` is a frontend defect. Clamping hides it while
leaving the UI and the engine disagreeing about what was set — the class of bug
that surfaces later as "the knob does not match the sound". Rejecting surfaces it
at the point of failure; clamping before sending is the frontend's job.

Clamping to each parameter's own range still happens downstream, where the
definition is authoritative.

---

## ADR-0013 — The audio thread only sets a flag; the message thread does the work

**Phase 2 · Accepted**

Host automation invokes the APVTS listener on the **audio thread**. Serializing
JSON or calling a WebView there would allocate and block, violating the real-time
contract outright (CLAUDE.md §7.1).

So `ParameterBridge::parameterChanged` does one thing: sets a lock-free atomic
flag in a fixed-size array. A 30 Hz message-thread timer coalesces those flags
into outbound messages.

Coalescing is not only a performance measure. A parameter swept by automation
changes far faster than any display can show, so forwarding every change would
flood the WebView with values no one can see. One update per parameter per frame
carries exactly the information a UI can use.

---

## ADR-0014 — State schema version is read from XML, not from the parsed ValueTree

**Phase 2 · Accepted**

`readState` reads the schema version from the XML attribute before converting to
a `ValueTree`.

XML carries no type information, so `ValueTree::fromXml` yields every attribute
as a string `var`. Asking such a var `isInt()` always answers no, which would
reject perfectly valid documents Apollo had just written itself. Reading the
typed attribute directly fixes it and states the intent more clearly.

---

## ADR-0015 — The WebView2 SDK is fetched and pinned, not required preinstalled

**Phase 2 · Accepted**

JUCE picks the WebView backend per platform — WKWebView on macOS, WebKitGTK on
Linux — and both come from the system. Windows is the exception: JUCE links
WebView2, whose headers and import library ship in a NuGet package that is absent
on a stock machine and that JUCE does not vendor.

`CMake/ApolloWebView.cmake` fetches that package at a pinned version, exactly as
the build already does for JUCE (CLAUDE.md §32: explicit, pinned, reproducible).
The alternative — telling every developer and every CI runner to install it by
hand — fails the build with a cryptic `FindWebView2` error whenever someone has
not.

Only the build-time SDK is fetched. The WebView2 *runtime* ships with Microsoft
Edge and is already present on effectively every Windows 10/11 machine.

One trap worth recording, because it fails in a way that looks like success:
JUCE's `FindWebView2` globs `${JUCE_WEBVIEW2_PACKAGE_LOCATION}/*Microsoft.Web.WebView2*`
and treats the first match as the package root, so it expects a NuGet *packages
folder*, not the package itself. Extracting the archive directly into the
location makes the glob match the package's own `.nuspec` **file**; `find_path`
is then handed a file as a hint, returns NOTFOUND, and
`find_package_handle_standard_args` still reports "Found" because the composed
include path is a non-empty string. The build then fails much later on a missing
`WebView2.h`. Apollo therefore extracts into a versioned subdirectory and hands
JUCE the parent.

---

## ADR-0016 — The editor is a separate build target from the engine

**Phase 2 · Accepted**

`apollo_editor` is its own INTERFACE library rather than part of `apollo_engine`,
and `APOLLO_WITH_WEBVIEW` is a per-target definition rather than an engine-wide
one.

- **Layering.** The engine must not depend on the UI (ARCHITECTURE.md §5.2).
  Making that a build-level fact rather than a convention means a stray include
  from DSP code fails to link instead of quietly compiling.
- **The test runner is headless.** It exercises the bridge through its protocol
  layer, which needs no browser. Pulling `juce_gui_extra` and the WebView2 SDK
  into the test binary would add platform dependencies and CI packages that buy
  nothing — and did in fact break the build until the split was made.

With the WebView disabled the processor falls back to a generic parameter editor
rather than to no editor at all, so the plugin stays usable on a configuration
where the platform backend is unavailable (CLAUDE.md §33).

---

## ADR-0017 — Engine gain staging is measured, not derived

**Phase 3 · Accepted**

`VoiceEngine::outputGain` is 0.08, chosen from measurement rather than theory,
and every voice starts from its own fixed phase offset rather than from zero.

The textbook figure for summing N voices is 1/sqrt(N), which assumes the voices
are uncorrelated. Apollo's are not, for two separate reasons, and both were found
by a test rather than by reasoning:

1. **Every voice started at phase zero.** That made a chord's attack sum
   *coherently* instead of as root-N. Thirty-two notes at full velocity peaked at
   1.95 — nearly 8 voice-equivalents where 5.7 was predicted. Voices now start at
   distinct fixed offsets (`Voice::setStartPhase`), which decorrelates the attack
   while staying perfectly reproducible; randomised phase would not.

2. **Harmonically related notes reinforce.** Even decorrelated, the sum depends
   on the interval, not just the count. Measured worst cases at full polyphony,
   relative to one voice: octaves 11.4, unison 10.2, whole tones 8.7, fifths 8.4.
   The first test written used a chromatic cluster, which turned out to be the
   *easiest* case and passed while four other voicings clipped.

The gain covers the worst of those with roughly 9% margin, and the test suite
renders all five voicings and asserts the output stays inside full scale.

**Given up:** a deliberately quiet instrument — one note peaks near -22 dBFS.
That is the right trade while Apollo has no output stage: headroom is recoverable
with master gain, clipping is not, and ARCHITECTURE.md §3.4 rules out a limiter
that would merely hide the problem. Revisit once the oscillator (Phase 4) and
effects (Phase 8) make the real signal chain measurable.

---

## ADR-0018 — The synthesis engine lives in apollo_core, free of JUCE

**Phase 3 · Accepted**

`Voice` and `VoiceEngine` render into raw `float* const*` buffers and know
nothing of JUCE, MIDI messages, parameters or hosts. The processor translates
`juce::MidiBuffer` into engine calls.

The deciding factor was the warning policy. `apollo_core` is the only library
held to Apollo's strict set (ADR-0005); `apollo_engine` is an INTERFACE library
(ADR-0010), so its sources inherit the consuming target's JUCE-relaxed flags.
Putting the DSP there would have left `-Wconversion` and `-Wdouble-promotion`
switched off over exactly the code they exist to protect — a silent narrowing or
an accidental double promotion inside a per-sample loop.

It also makes the engine testable with no host, no audio device and no message
loop, which is why the voice tests are plain arithmetic against float buffers
and cannot be flaky.

**Given up:** the processor carries the MIDI translation, and a small amount of
duplication exists between the engine's own vocabulary and JUCE's.

---

## ADR-0019 — A stolen voice fades before the new note starts

**Phase 3 · Accepted**

Stealing does not restart the voice immediately. The voice ramps to silence over
2 ms, and the waiting note begins only once the fade reaches zero
(`Voice::steal`, consumed in the envelope).

Restarting a sounding voice in place means cutting a waveform at full amplitude
and resetting its phase — a step discontinuity, which is exactly what a click is.
Phase 3's exit criteria require that note transitions produce no audible
artifact, and a click is measurable: the test asserts the largest sample-to-sample
step across a steal stays far below the amplitude of the signal being stolen.

**Given up:** a stolen note begins about 2 ms late. Inaudible as timing, and the
better trade against an audible click.

---

## ADR-0020 — Wavetables are band-limited by additive synthesis, into a mipmap

**Phase 4a · Accepted**

Each built-in waveform is defined as a harmonic series, and each mip level is
rendered by summing only the harmonics that level is allowed to keep
(`WavetableLibrary`). Level L keeps `1024 >> L` harmonics; the oscillator picks
the most detailed level whose harmonics all stay below Nyquist for the note being
played, once per note rather than per sample.

The alternative — render the full-bandwidth shape once and low-pass it down for
each level — needs a filter design, a transition band and a stopband figure, and
leaves ringing near the cut. Additive synthesis is band-limited *by construction*:
the harmonics above the limit were never generated rather than attenuated, so
there is no filter to specify and nothing to get subtly wrong.

Each level stores only the samples its bandwidth needs, so the whole mipmap costs
about twice the top level rather than eleven times it.

**Given up:** this only works for shapes with a closed-form harmonic series.
Arbitrary user-supplied tables have to be analysed and filtered instead, which
belongs with the resource loader in Phase 9. `Wavetable` stores finished mipmaps
either way, so that path can be added without touching playback.

---

## ADR-0021 — Coarse mip levels keep a 512-sample floor and one shared normalisation

**Phase 4a · Accepted**

Two decisions inside the mipmap, both reversed from the obvious implementation
after measurement.

**A 512-sample floor per frame.** Twice the harmonic count is the
information-theoretic minimum for *storage*, but a poor basis for
*interpolation*: at 2H samples the highest harmonic gets two samples per cycle.
That is harmless at the detailed levels, where the top harmonics are 60 dB down,
and severe at the coarse levels used for high notes, where level 8 holds four
harmonics and its highest is 12 dB down. Measured with a 16-sample floor, a
3520 Hz saw aliased at -35 dBc against -101 dBc an octave below. Raising the
floor to 512 took it to -98.5 dBc, for under a megabyte across all four tables.

**One normalisation factor per frame, taken from the loudest level.** Removing
harmonics does not simply lower a waveform's peak — a partial sum can peak
*higher* than the complete one, because the harmonics that cancelled the
fundamental's crest are the ones removed. Normalising against level 0 let the
reduced levels reach 1.097, measured. Normalising each level independently would
be worse still: it would boost the duller levels and make a note change loudness
as it crossed an octave boundary.

**Given up:** memory that strict bandwidth accounting says is unnecessary, and a
small amount of headroom relative to per-level normalisation.

---

## ADR-0022 — Phase is wrapped into range before it is scaled to a table index

**Phase 4a · Accepted**

`Wavetable::getSample` wraps phase into `[0, 1)` with `std::floor` and rejects
non-finite phase outright, *before* multiplying by the frame size and converting
to an `int`. The oscillator likewise wraps with `floor` rather than a single
subtraction.

A single subtraction assumes the phase increment is below 1.0. That holds for any
musical frequency but not for an absurd one, and mip selection clamps the *level*,
not the increment: at 1 GHz the increment is over 20000, phase grows without
bound, and converting the result to an `int` is undefined behaviour.

This is recorded because of how it was found. MSVC did not trap it — it produced
usable-looking garbage, and Windows, Linux/GCC and macOS all went green on broken
code. UBSan on Linux/Clang reported it precisely: `2.15456e+09 is outside the
range of representable values of type 'int'`. The sanitizer job is the only
reason this was ever seen.

**Given up:** a `floor` per cycle instead of a compare and a subtract. It is
reached once per cycle rather than once per sample, so the cost is nothing.

---

## ADR-0023 — The unison layout is computed once per block by the engine

**Phase 4b · Accepted**

`UnisonLayout` holds the per-voice detune ratios, pan gains and start phases for
a unison stack. The engine owns one per primary oscillator, rebuilds it only when
the voice count, detune or spread actually moves, and hands voices a pointer.

The layout depends only on those three values — never on pitch, note or voice —
so it is identical for every sounding voice. Computing it per voice would mean up
to 32 voices x 2 oscillators x 16 unison voices of `pow`, `sin` and `cos` per
block; computing it once is a few dozen. It also puts the unison *design* — the
detune distribution, the pan law, the gain normalisation — in one object that can
be asserted on directly, rather than inferred from a render.

Two consequences follow, and both are handled explicitly rather than left to
convention. The layout mutates underneath the oscillators that point at it, so it
carries a generation counter that an oscillator compares against its own cached
value and re-derives its frequencies when it differs — nobody has to remember to
notify anything. And because voices hold pointers into the engine, `VoiceEngine`
is deliberately neither copyable nor movable: a copy would leave those pointers
aimed at the original, which is a dangling read that would surface as
intermittently wrong audio rather than as a crash.

**Given up:** value semantics on the engine, and a test helper that returned one
by value.

---

## ADR-0024 — Unison spread is anchored at unity in the centre; oscillator pan is a balance

**Phase 4b · Accepted**

Two stereo stages, with two different laws.

**Inside the unison stack**, voices are panned by a constant-power law scaled so
that a *centred* voice is unity on both channels, rather than the usual -3 dB.
The anchor matters: Apollo's default patch is a single centred unison voice, and
the engine's gain staging was measured for exactly that configuration
(ADR-0017). The conventional law, anchored at the extremes, would have made every
default patch 3 dB quieter than the measurement it is checked against — and the
Phase 4b test suite asserts sample-exact equality with a plain oscillator, which
that law would fail. Measured after the change: a single default note peaks at
0.0800, the same value as before the source section existed.

**On the oscillator output**, pan is a *balance*: it attenuates the far channel
and leaves the near one alone. By that point the signal is already stereo, built
by the spread, so a second pan would narrow the image the first one created. The
balance law also has the property that no channel gain ever exceeds unity, so no
pan setting can push a voice above the level the gain staging assumes.

**Given up:** a hard-spread unison voice reaches 1.414 on one channel, and a
hard-balanced oscillator is 3 dB quieter summed to mono than a centred one. Both
are the expected behaviour of their respective laws.

---

## ADR-0025 — The headroom guarantee covers the default patch, not every patch

**Phase 4b · Accepted · Extends ADR-0017**

ADR-0017 fixed `VoiceEngine::outputGain` at 0.08 from measurement, and the suite
asserts that maximum polyphony at full velocity stays inside full scale. Phase
4b adds three more sources and 16-voice unison on two oscillators, so the scope
of that guarantee now has to be stated rather than assumed.

It covers the **default patch**: oscillator 1 alone, one unison voice, centred.
Under that patch no voicing, velocity or polyphony reaches full scale, and that
remains asserted.

It does not cover every reachable combination. Four sources at full level with
16-voice unison on both oscillators, at maximum polyphony, peaks at **7.63** —
about 18 dB over full scale. Pricing that in would mean an `outputGain` near
0.010 and a single default note at roughly -40 dBFS: an instrument nobody could
use, defending against a patch nobody would build by accident. Stacked
configurations are instead guaranteed only to stay finite and bounded, which is
asserted separately.

The measurement itself repeats a lesson from ADR-0017: which *voicing* is worst
is not obvious. A single arbitrary voicing measured 1.8 and would have made the
overshoot look like 6 dB; fifths are harmonically locked and reinforce, and only
trying several found them.

**Given up:** a universal no-clip claim. The master gain is the user's control,
and metering arrives with the output stage in Phase 8.

---

## ADR-0026 — A voice's source section is configured by one struct, not by setters

**Phase 4b · Accepted**

`Voice::setSources` takes a `VoiceSourceSettings` and compares it wholesale
against what the voice already holds, discarding the call if nothing moved. The
engine resolves parameters into that struct once per block.

Phase 4b took a voice from one oscillator to four sources with fifteen parameters
between them. A setter each would have meant fifteen calls per voice in the audio
callback and would have grown with every future source — the shape of change that
ends with a parameter silently not reaching the engine because one call was
forgotten. A single struct compared as a whole cannot develop that gap.

It also draws the layer boundary cleanly. The struct holds *resolved* resources —
a wavetable pointer, a unison layout, a tuning in semitones — while the engine's
`SourceParameters` holds what the host and UI express: a table index, a unison
count, a detune amount. Voices therefore never look up a table by index, never
see a normalised parameter, and never need to know a `WavetableLibrary` exists.

**Given up:** two structures that describe the same section, and the discipline
of keeping them in step.

---

## ADR-0027 — The WebView backend is named per platform, not left to JUCE's default

**Phase 4b (verification) · Accepted**

`ApolloWebViewEditor` selects its backend explicitly through a
`withPlatformBackend` helper: `Backend::webview2` on Windows, JUCE's default
elsewhere. It also names a user-data folder under the per-user application data
directory rather than letting WebView2 choose one.

Enabling WebView2 at build time is necessary but **not sufficient**, and nothing
about the failure says so. `JUCE_USE_WIN_WEBVIEW2=1` compiles the backend in and
`NEEDS_WEBVIEW2` links its loader — Apollo had both — but
`createAndInitPlatformDependentPart` still constructs the legacy Internet
Explorer control for every backend value except `webview2`, which is the
documented behaviour of the flag rather than a bug in JUCE.

The consequence was invisible to everything Apollo measures. The project built
clean on four platforms, linked the VST3 and the standalone, and passed 206,757
assertions, while the standalone's entire UI was the sentence "Navigation to the
webpage was canceled": the IE control supports neither the resource provider that
serves the page nor the native integration the bridge is built on, so it could
not reach `https://juce.backend/`. The test suite could not have caught it — it
builds headless with `APOLLO_WITH_WEBVIEW=0` and exercises the bridge through its
protocol layer, where no browser exists. Only launching the application found it.

The user-data folder is part of the same decision. Left unset, WebView2 creates
its folder beside the running executable: Apollo's own directory for the
standalone, and the *host's* program directory for the VST3, which is very often
read-only. A folder it cannot create means the environment fails to initialise
and JUCE falls back to the IE control — the same broken page, but appearing only
for some users in some hosts, which is close to undiagnosable from a bug report.

Naming the backend per platform also satisfies the standing rule against baking
one operating system's WebView into the architecture (CLAUDE.md §6.2, §46). Only
Windows needs the explicit choice; macOS and Linux already default to WebKit and
WebKitGTK. The platform-specific part is confined to one function in the UI
layer.

**Given up:** a compile-time guard in the editor, and the (theoretical) ability
to fall back to the IE control on a Windows machine with no WebView2 runtime.
That runtime ships with Windows 11 and is redistributable for Windows 10, and a
silent fallback to a control that cannot render Apollo's UI is worse than a
visible failure.

---

## ADR-0028 — Apollo implements its own oversampling rather than using juce::dsp

**Phase 4c · Accepted**

`Source/DSP/Oversampling/` contains a linear-phase, polyphase halfband
oversampler written for Apollo, in `apollo_core`, free of JUCE.
`ARCHITECTURE.md` §2.3 says `juce::dsp::Oversampling` *may* be used; this
supersedes that as the implementation choice, and §2.3 has been updated to match.

The deciding factor is the same one that put the engine in `apollo_core` to begin
with (ADR-0018). `apollo_core` is the only library held to Apollo's strict warning
set, and `juce_dsp` cannot be compiled under it. Taking the JUCE class would have
meant either relaxing the warnings over a per-sample DSP loop — exactly the code
`-Wconversion` and `-Wdouble-promotion` exist to protect — or moving the
oversampler out of the DSP layer, away from the nonlinear stages that use it.

Three design decisions inside it are worth recording, because each had a cheaper
alternative that was rejected for a reason:

**Linear phase, not an IIR allpass cascade.** The allpass form is the classic
cheap halfband: fewer operations, and a fraction of the delay. It is also
phase-nonlinear, and a nonlinear stage is normally mixed against its own dry
signal — `fx_distortion_mix` is already a registered parameter. A dry path summed
with a phase-rotated wet path combs rather than blends, and no delay
compensation can fix it. A linear-phase FIR only needs the dry path delayed by a
whole number of samples.

**Whole-sample latency, bought with mismatched tap counts.** A stage contributes
`centre` samples of round-trip delay at its own input rate, so the second stage
of a 4x cascade contributes `centre / 2` base-rate samples. Two identical stages
would put the total on a half sample, which cannot be reported to a host or used
to align a dry path. The stages therefore use 79 and 81 taps — odd and even
centres — and 4x lands on exactly 59 samples.

**79 taps, not 63.** A halfband's transition band straddles Nyquist, and a
shorter filter's passband began rolling off around 19 kHz, inside the audible
band. 79 taps holds the passband flat (±0.0001 dB) past 20 kHz with a -100.7 dB
stopband, for 40 multiplies per converted sample. Measured, not assumed:
`OversamplingTests` reports both figures.

**Given up:** a well-tested third-party implementation, and the 8x factor JUCE
offers. 8x is added when a stage is measured to need it, rather than because the
option exists.

---

## ADR-0029 — Maximum polyphony is not achievable with the heaviest patch

**Phase 4c · Accepted**

Apollo keeps a 32-voice ceiling and a 16-voice unison ceiling, and does not
attempt to keep every combination of the two inside real time. The measurement
that forced the decision (PROJECT-STATE.md §5b):

| Patch | 1 voice | 16 voices | 32 voices |
|---|---|---|---|
| Default (oscillator 1, no unison) | 0.15 % | 2.32 % | 4.86 % |
| Heaviest (2 x 16 unison, sub, noise) | 4.17 % | 82.05 % | **150.33 %** |

The default patch is cheap enough that polyphony is not a consideration: full
32-voice polyphony costs under five per cent of one core. The heaviest patch is
34 oscillators per voice, and at 32 voices that is 1088 oscillators, each doing
four-point interpolation across two wavetable frames. It cannot run in real time
on the development machine, and it is not close.

Three responses were considered:

- **Lower the ceilings** so every combination fits. Rejected: it would take a
  configuration nobody has complained about away from everybody, to defend
  against one that is reached only deliberately.
- **Load-aware voice limiting** — drop voices as CPU rises. Rejected for now: it
  makes the instrument's output depend on the machine and on what else is
  running, which is a worse failure than a user hearing their patch is expensive.
- **Optimise.** The right answer, and it belongs to Phase 10, which owns
  profiling. The cost here is inherent arithmetic rather than a defect, so it
  needs SIMD and interpolation work, not a bug fix.

What is accepted meanwhile is that the ceilings are the *instrument's* limits,
not a real-time guarantee, and the numbers are published rather than implied.
This mirrors ADR-0025, which drew the same line for headroom: the guarantee
covers the default patch, and stacked configurations are documented rather than
defended.

**Given up:** the ability to say "32 voices always work". Apollo can say it for
the default patch, with a measurement behind it.
