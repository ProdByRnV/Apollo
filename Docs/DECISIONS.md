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

---

## ADR-0030 — Envelope segments are shaped incrementally, with exact endpoints

**Phase 5a · Accepted**

Each DAHDSR segment travels from the envelope's current level to that segment's
endpoint over exactly the time the user set, along

    level = start + (end - start) * (1 - e^(-k*p)) / (1 - e^(-k))

evaluated **incrementally**: `e^(-k*p)` is one multiply per sample by a ratio
computed once when the segment begins. One `exp()` per segment, none per sample.

Two properties were the point.

**Exact endpoints.** The obvious analog envelope is a one-pole filter chasing a
target, which approaches asymptotically and never arrives. That leaves an attack
that does not quite reach full scale, a sustain slightly below the level the user
dialled, and a release that decays toward a denormal instead of silence — so a
voice needs a threshold to decide it has finished, and the threshold is audible
as a truncated tail. The form above lands on its target exactly, which is why
`Envelope` can report `idle` truthfully and the voice can free itself with
nothing left over. The tests assert the endpoints at every curve setting.

**Exact durations.** The user sets a time, and the segment takes that time.
A one-pole's time constant is not its duration, so "100 ms decay" would mean
something the user has to learn rather than something they set.

The alternative considered was a lookup table of shaping curves. Rejected: it
adds a table, an interpolation, and a resolution question, to approximate a
function that costs one multiply.

Curve tension is **one control per envelope**, not one per stage. PRD §15.1 asks
for adjustable curve tension and lists per-stage curves as a later option; six
controls where one will do is a worse instrument until someone asks.

A change arriving mid-segment retimes that segment from wherever the envelope
currently is, preserving the fraction already travelled. Applying only at the
next segment boundary would make a decay knob feel dead while a decay is audibly
in progress.

**Given up:** the exact shape of a real RC circuit, which the asymptotic form
does reproduce. What is kept is a curve that is indistinguishable by ear across
the useful range, and endpoints that are exact.

---

## ADR-0031 — The steal fade is separate from the amplitude envelope

**Phase 5a · Accepted**

`Voice` multiplies its amplitude envelope by a second, independent linear ramp
that runs only while the voice is being stolen, and freezes the envelope for the
duration of that fade.

Before Phase 5a the two were the same mechanism: one level with a `stealing`
stage that ramped it down faster. That worked while the release was a fixed
50 ms constant. It stops working the moment the release is a user control that
reaches ten seconds, because voice stealing has to free a voice in about two
milliseconds whatever the patch says — a stolen voice that took the musical
release time to disappear would starve the pool exactly when polyphony is under
pressure, which is when stealing happens.

The envelope is deliberately **frozen** rather than advanced during the fade. An
envelope allowed to run could reach `idle` part-way through, dropping the level
to zero in a single sample — a click produced by the very mechanism that exists
to prevent one (ADR-0019).

**Given up:** one number describing a voice's amplitude. There are now two, and
`getEnvelopeLevel()` returns their product.

---

## ADR-0032 — The filter parameters were renamed, with a state migration

**Phase 5b · Accepted**

`filter_cutoff`, `filter_resonance` and `filter_drive` became `filter1_cutoff`,
`filter1_resonance` and `filter1_drive`, joined by a `filter2_*` set and a
`filter_routing` control. Schema version 2 migrates version 1 documents by
renaming the three children in place, carrying their saved values.

The three original IDs predated the second filter the PRD specifies, and
`PROJECT-STATE.md` had carried the question as an open issue since Phase 2 with
"a Phase 5 decision" written against it. The alternative was to keep them and add
`filter2_*` alongside, which would have left the instrument with `filter_cutoff`
sitting next to `filter2_cutoff` in every automation lane and every preset for
the rest of its life — a permanent asymmetry, visible to users, in exchange for
avoiding one migration step.

Apollo is 0.1.0 and unreleased, and `Docs/VERSIONING.md` §2 is explicit that
public contracts are not frozen before 1.0 provided a break is recorded. This is
that window, and it does not come back.

The migration is also the first exercise of a code path built in Phase 2 and
never used: `applyMigrationStep` returned false for everything, and no document
had ever been through it. A migration architecture that has never migrated
anything is a plan, not a mechanism. `Tests/State/StateSerializationTests.cpp`
now drives a real version 1 document through it and checks the values survive,
including the case where the old parameters are absent entirely.

Filter 2 and the routing control are simply missing from a version 1 document,
and the loader supplies their defaults — off, and series — which is exactly the
transparent second filter a patch saved before it existed should get.

**Given up:** version 1 documents now depend on a migration step being correct
rather than on nothing happening at all. That is why the step is tested against a
real document rather than only at its boundaries.

---

## ADR-0033 — Filter drive is deliberately gentle, because its aliasing was measured

**Phase 5b · Accepted**

`filter_drive` reaches 2x gain into a soft clipper and no further. The ceiling is
a measurement rather than a taste.

A nonlinearity folds harmonics back into the audible band, and folding is
instantaneous — what comes back is already in band, so no filter downstream can
separate it from signal. (An earlier comment in `FilterDrive.h` claimed the
filter did remove it. That was wrong, and the measurement is what corrected it.)

`Tests/DSP/FilterTests.cpp` measures fold-back against drive, on a full-scale
tone chosen to be inharmonic with the sample rate so folded partials cannot hide
on harmonics:

| Drive | Gain | Worst fold-back |
|---|---|---|
| 0.00 | 1.0x | -146.5 dBc |
| 0.25 | 1.25x | -57.1 dBc |
| 0.50 | 1.5x | -52.6 dBc |
| 1.00 | 2.0x | -45.1 dBc |

and, before the range was cut, -25.1 dBc at 4x and -19.4 dBc at 16x — worse than
the hard clipper the oversampling tests use as their worst case. A saturator with
no knee at all was measured too, and is barely better: the cliff is the
nonlinearity, not the shape chosen for it.

So the -60 dBc budget the wavetable engine holds itself to cannot be met by any
audible amount of drive at 48 kHz without oversampling. Three responses were
considered:

- **Widen the budget** and ship a hard drive. Rejected: the budget would then
  mean nothing, and -19 dBc of inharmonic content is audible.
- **Oversample the drive.** Correct, and rejected *here* on cost and latency.
  This is a per-voice stage: two channels, two filters, thirty-two voices is up
  to 128 conversions, and each adds 39 samples of latency. Latency that appears
  only when a parameter is turned up is worse for a host than a little aliasing,
  and always-on latency penalises every patch for a feature most do not use.
- **Keep the range small.** Chosen. At 2x the worst case is about -45 dBc, which
  is a colour rather than a defect, and the control does something audible.

A drive worth calling distortion belongs to the FX rack in Phase 8, where it is
one stage on a bus rather than 128 in the voices, and where the Phase 4c
oversampler can carry it with a single, constant, reported latency. That is what
`Docs/OVERSAMPLING.md` now says against the filter-drive row, replacing the "2x
expected" it was written with in Phase 4c — the re-measurement clause in that
document doing exactly what it was there for.

**Given up:** a screaming filter. Apollo's filter drives; it does not distort.

---

## ADR-0034 — LFOs run at audio rate, and are not band-limited

**Phase 5c · Accepted**

`Lfo` produces a value every sample, and its shapes are exact rather than
band-limited. Both are the opposite of what the wavetable oscillators do, and
both are deliberate.

**Audio rate, not control rate.** The cheap alternative is one value per block,
which is what many synthesisers do for modulation. Apollo does not, for two
reasons. A block-rate LFO moving a filter cutoff produces a staircase, and the
Phase 5 exit criteria ask for behaviour "free from zipper artifacts" — a
staircase is exactly that artefact. And Apollo's LFOs reach 400 Hz, which is
well inside the audio range and useful there; a control-rate LFO at 400 Hz with
a 512-sample block would be sampling its own shape about twice per cycle.

The cost was measured before the decision rather than assumed
(`ApolloTests --benchmark`, PROJECT-STATE.md §5b): a single LFO costs 0.014 % of
a core per sample for the cheap shapes and 0.034 % for the sine. Four LFOs across
32 voices is 128 instances, so between 1.7 % and 4.4 % — comparable to the entire
default voice engine, and affordable.

The sine is two and a half times the cost of every other shape, because it calls
`std::sin` per sample. If the modulation matrix pushes the total somewhere
uncomfortable, that is the first thing to replace with a polynomial or a small
table, and the benchmark will say whether it was worth it.

**Not band-limited.** A wavetable oscillator must be, because it is heard, and
Apollo goes to considerable lengths there (ADR-0020). An LFO is not heard: it
moves a control. Band-limiting a square-wave LFO would round off exactly the edge
that makes it useful as a switch, in exchange for suppressing harmonics that
nothing will ever listen to. So a square is exactly ±1, and the tests assert that
it has no intermediate values at all.

The smoothing control exists for the case where those edges *are* a problem —
a square or a sample-and-hold stepping a filter cutoff clicks — and it is a slew
on the output, which is the right tool because it is adjustable and can be turned
off. Band-limiting would apply that rounding always, whether it helped or not.

**`step` is a staircase, not a sequence.** PRD §15.2 lists "Step" among the
shapes and also lists custom drawable curves separately. What is implemented is
the ramp quantised into equal steps, which is well defined and needs nothing from
the user. A drawable step sequence is a resource the user creates, so it needs
the editor that draws it (Phase 7) and the preset format that stores it
(Phase 9), and inventing a default pattern here would be inventing content.

**Given up:** the CPU a control-rate LFO would have saved, and the option of
running LFOs so fast they alias — which the 400 Hz ceiling forecloses.

---

## ADR-0035 — Modulation is evaluated on a 16-sample control block

**Phase 5d · Accepted**

A voice re-evaluates its modulation every 16 samples rather than every sample —
a 3 kHz modulation rate at 48 kHz — and pushes the result into destinations that
are themselves smoothed per sample.

Per-sample evaluation is the obvious thing and is not affordable. A modulated
cutoff needs a `tan` to re-resolve the filter coefficients and a modulated pitch
needs a `pow`; doing either per sample per voice multiplies the most expensive
part of the engine by the sample rate. PROJECT-STATE.md §5b already showed the
voice engine dominating Apollo's cost before the matrix existed.

The risk of a control block is a staircase, and the Phase 5 exit criteria
explicitly forbid one. Three things keep it away, and the third is measured:

- **3 kHz is far above where stepping is audible.** The artefact people mean by
  "zipper" comes from updating once per *buffer* — roughly 94 Hz at a 512-sample
  block, which is squarely in the audible range. 16 samples is thirty-two times
  finer.
- **The fastest LFO is still oversampled.** Apollo's LFOs top out at 400 Hz, so
  even the worst case is sampled seven and a half times per cycle.
- **The destinations that would step are smoothed anyway.** Levels and balances
  go through the same per-sample smoothers the parameters use, so they
  interpolate between updates rather than jumping to them.

`ModulationTests` measures it rather than arguing it: an LFO sweeping a resonant
cutoff produces a largest sample-to-sample step of 0.002167, against 0.001292 for
the identical patch with the modulation switched off. A staircase would be orders
of magnitude larger, not 1.7 times.

The cost, measured: a voice with four routings costs 0.391 % of a core against
0.223 % unmodulated, so 12.5 % at full polyphony. A voice with **no** active slot
skips the whole evaluation and costs exactly nothing, which is why the
unmodulated figures are unchanged from Phase 5b.

**Given up:** sample-exact modulation. Nothing in Apollo can currently hear the
difference, and if something later can — an audio-rate FM destination, say — the
block size is one constant.

---

## ADR-0036 — Amplitude modulation attenuates and never boosts

**Phase 5d · Accepted**

A routing to `ModDestination::amplitude` multiplies the voice by
`clamp (1 + offset, 0, 1)`. A positive offset therefore does nothing once the
voice is already at full level.

The alternative is to let it reach 2, which is what an unclamped offset would
give. That would put every gain-staging measurement Apollo has out by up to 6 dB
the moment a user drew a tremolo, and the headroom guarantee (ADR-0025) is stated
for a voice at full level. A guarantee that any routing can silently double is
not a guarantee.

It also costs nothing musically. A tremolo *is* an attenuation — it is the
quiet parts that make it audible — and a patch that wants more level raises the
level control and modulates downward from it, which is the same sound with the
peak in a known place.

This is the same line ADR-0024 drew for the unison spread and ADR-0025 for
stacked sources: where a control could push past the level the engine was
measured for, the engine keeps the ceiling and the documentation says so.

**Given up:** modulation that can make a voice louder than its own level control.

---

## ADR-0037 — The matrix's repetitive parameters are generated, not typed

**Phase 5d · Accepted**

Envelopes 2-4, the four LFOs and the sixteen routing slots are 101 parameter
definitions, and they were produced by a script rather than typed
(`genparams.sh`, kept with the session scratch rather than in the repository —
the generated text is the artefact, and it lives in the registry where it can be
read).

101 near-identical entries typed by hand invites exactly one of them to differ by
a digit — a skew, a default, a range — and a parameter's range is a permanent
part of the automation and preset contract (Docs/PARAMETER-CONVENTIONS.md §1). A
wrong digit in slot 11's depth would not fail a build or a test; it would be
discovered by a user whose eleventh routing behaved differently from the other
fifteen.

The registry itself stays what it was: a flat, readable `constexpr` array with
every identifier visible. The alternative — building the array with a
`constexpr` loop — was rejected because the identifiers are string literals, and
generating those at compile time turns a readable list into an exercise.

**Given up:** nothing structural. The generator is a one-off; the committed
registry is the source of truth, and the count assertion in
`ParameterRegistryTests` still has to be updated deliberately.

---

## ADR-0038 — The frontend is a set of real files, embedded at build time

**Phase 6 (UI) · Accepted**

The interface lives outside C++, and CMake compiles it into the binary with
`juce_add_binary_data`; `ApolloWebViewEditor::provideResource` serves it from a
small table of path, resource name and MIME type. When this was decided the
three files were hand-written and lived in `Source/UI/Web`; since Phase 7d they
are the output of the bundler in `WebUI/` (ADR-0050), with the same three names
and the same table. Everything below is unchanged by that: what mattered here
was that the interface is not a string literal.

They were previously a single `constexpr const char*` raw string literal inside
`ApolloWebViewEditor.cpp`. That was the right shape for a placeholder proving
the bridge was live, and the wrong shape for an interface anybody has to work
on: a stylesheet inside a string literal gets no highlighting, no formatter, no
useful diff, and cannot be opened in a browser to check a layout. The interface
is now the most frequently edited part of the project, so the cost was going to
be paid repeatedly.

Embedding rather than installing loose files is a separate choice from moving
them out of C++. A plugin is copied around by users and hosts, and an asset
directory that has to travel beside a `.vst3` is an asset directory that will
eventually not be there. Embedding keeps Apollo one relocatable binary with no
install-time path to resolve (CLAUDE.md §31.2), and keeps the WebView pointed at
a fixed table rather than at the filesystem (UI_BINDINGS.md §14).

Listing the three files in `CMakeLists.txt` is what makes them build inputs:
editing the CSS relinks the plugin. That is the point of putting web assets
through CMake rather than beside it (CLAUDE.md §31).

**Given up:** editing the UI without rebuilding. A live-reload path that reads
from the source tree in debug builds would restore it and is compatible with
this decision; it is not needed yet.

---

## ADR-0039 — One interaction colour, and colour never carries meaning alone

**Phase 6 (UI) · Accepted**

The interface assigns each colour exactly one job, and nothing is coloured for
decoration:

| Colour | Means |
|---|---|
| violet — now **gold** (ADR-0051) | interaction — focus, hover, the value being held |
| green | modulation and activity — something is moving |
| amber — now **orange** (ADR-0051) | caution — a negative depth, approaching a limit |
| red | clipping, danger, error |

This is CLAUDE.md §24.2 made specific enough to hold. The rule that made it
worth writing down is the second half: a state that is shown in colour is also
shown some other way. A modulated knob turns green *and* the routing that moves
it is a highlighted row in the matrix with a number beside it. A source that is
not in the signal path carries an OFF chip in its heading *and* reads zero on its
level *and* says "silent" on its scope. A depth's sign is a fill direction *and*
a signed percentage. Nothing in the interface can only be perceived as a hue
(CLAUDE.md §39).

That OFF chip used to be a dimmed panel, which was the same rule applied badly:
brightness is a difference with no words attached, and it made the two
oscillators look like two different products whenever one of them sat at zero.
The chip says the thing; every panel is now the same shade (ADR-0052).

The accent — violet when this was written, gold since ADR-0051 — is used *as* an
accent: it appears on the control under the hand and almost nowhere else. It is
deliberately not a gradient wash across panels; surfaces are dark metal separated
by hairlines, and depth comes from those rules rather than from shadows, because
a tool that is stared at for six hours should recede and let the values be the
brightest thing on screen.

**Which hue is which is not what this ADR decided**, and that is worth saying
plainly: everything above survived the recolour untouched, because the decision
here is that each colour has exactly one job and that no state is carried by hue
alone. ADR-0051 changed two of the four hues and none of the rules.

**Given up:** using colour for visual interest. Every hue in the interface is
load-bearing, which is a constraint on future UI work rather than a
one-off styling choice.

---

## ADR-0040 — The interface is laid out by signal flow, not by parameter list

**Phase 6 (UI) · Accepted**

Modules appear in the order the audio travels: oscillator 1 and 2, sub, noise,
the two filters, envelopes, LFOs, the modulation matrix, output. Within a module
the choice that defines what the module *is* — the wavetable, the filter type,
the LFO shape — spans the module as a heading, with the knobs it governs
beneath it.

The page it replaced rendered one identical slider per parameter, 139 of them,
in registry order. That is a correct rendering of the metadata and a useless
instrument: the registry's order is a build history, so `env2_delay` sits beside
`filter_routing` for reasons that are true and irrelevant to the person making a
sound.

Two consequences are worth stating because they constrain future work:

- **Four envelopes and four LFOs are behind tab strips.** Sixty controls shown
  at once is a wall; one section with four of everything is how they are used.
- **Any parameter the layout does not place appears in an "Unassigned" module at
  the bottom.** This is a safety net, not a section. Adding a parameter to the
  registry and forgetting to give it a home makes it appear somewhere ugly
  rather than making it invisible and un-editable.

The layout is presentation and lives in `apollo.js`. Ranges, defaults, steps and
skews are still read from the metadata the engine sends and are never restated
there (UI_BINDINGS.md §16); the discrete *labels* — "LP", "S&H", "Mod Wheel" —
are presentation and do live there, applied only when the table length matches
the range the engine reported, so a list that falls out of step with its enum
shows honest numbers instead of confidently wrong words.

**Given up:** a UI that needs no edit when a parameter is added. A new parameter
now needs one line in the layout to sit in the right module, and the Unassigned
module is what makes forgetting that survivable rather than silent.

---

## ADR-0041 — One control drives one parameter, and one parameter has one control

**Phase 6 (MIDI) · Accepted**

`midi::MappingTable` enforces a bijection. Learning a control that a parameter
already has replaces it; learning a control that another parameter already uses
takes it away from that parameter. Both are reported through `AssignResult`, so
the interface can say a mapping was released rather than letting the user find
out later (CLAUDE.md §33).

The obvious alternative is to let one controller drive several parameters. It
was rejected because Apollo already has a better answer to that need, and a
worse duplicate of it would be actively harmful. A control that should move many
things at once is a macro; a macro is a modulation source with sixteen routing
slots, bipolar depths and per-destination scaling behind it (CLAUDE.md §15). A
second, weaker many-to-one mechanism in the MIDI layer would mean two places to
look when a parameter moves unexpectedly, and the MIDI one would have no depth,
no polarity and no visualisation.

The bijection also makes the *inverse* question answerable, which is what the
interface actually needs: "what drives this knob" has exactly one answer, so a
badge on a control is well defined.

A learned mapping is always omni-channel. The channel a controller happens to be
transmitting on is not something most players know, and a mapping that stopped
working after they changed it — or after the same controller came back on a
different port — would be a defect from where they are standing.
Channel-specific mappings still exist and win over an omni mapping on the same
controller number; they are reachable through `assign()`, which is the path a
controller profile will use. Pointing at a knob is simply not how you ask for
one.

Two controller groups are refused outright. **CC 64** is the sustain pedal,
which Apollo acts on directly; a pedal that stopped sustaining because it was
once waved at a learn button is a bewildering failure. **CC 120-127** are the
channel-mode messages — all-sound-off, reset-all-controllers, mono/poly — which
are commands and carry no continuous value to scale. **CC 1 is deliberately not
refused**: the mod wheel is a modulation source in the matrix rather than a
mapping, and a user who explicitly learns it to a parameter gets both
behaviours, which is what they asked for.

**Given up:** one physical knob controlling several parameters directly. The
matrix and, later, macros are where that belongs.

---

## ADR-0042 — MIDI-controlled parameters are applied on the message thread

**Phase 6 (MIDI) · Accepted**

A control-change message arrives on the audio thread. The mapping lookup happens
there — it is a bounded scan of a preallocated table with no allocation and no
lock — but the *parameter write* does not. The audio thread stores the resulting
normalised value in a per-parameter atomic slot and sets a flag; a 60 Hz
message-thread timer applies those to APVTS.

APVTS cannot be written from the audio thread. `setValueNotifyingHost` notifies
listeners, reaches the host's automation system and, through Apollo's own
parameter bridge, ends at a WebView call — every one of which is forbidden there
(CLAUDE.md §7.1). This is the same shape as ADR-0013, for the same reason and in
the opposite direction.

**One slot per parameter rather than a queue of events.** A controller sweep
sends about a hundred messages a second and only the newest matters, so the slot
coalesces for free. A queue would have to be drained in order, replaying values
no one can hear, and could overflow during a heavy sweep.

**The cost is one tick of latency, under 17 milliseconds.** That was measured
against what it buys rather than assumed to be free: MIDI-mapped parameters are
control gestures, not notes, and 17 ms on a knob is below what a hand resolves.
Note timing is unaffected — notes are still applied sample-accurately inside the
block (Phase 3), and the mod wheel and aftertouch still reach the modulation
matrix on the audio thread, because those are modulation sources rather than
parameter writes. 60 Hz is deliberately twice the UI's 30 Hz refresh, so a
mapped move reaches the parameter before the interface would have drawn it.

**Gestures are bracketed.** The first change opens `beginChangeGesture` and the
parameter closes it after three quiet ticks, so a controller sweep records in a
host as one automation edit and one undo step rather than several hundred.

The table travels the other way — message thread to audio thread — through
`midi::MappingChannel`, a single-producer, single-consumer ring of whole tables.
Whole tables because one is about 1.3 KB and edits happen at human speed, so the
copy is free at the rate it actually occurs and there is no window in which the
audio thread can observe a half-applied change. A ring rather than a double
buffer with a flag because a ring has no case in which the producer overwrites
what the consumer is reading: it refuses to publish when full, and the caller
republishes on its next tick.

**Given up:** sample-accurate MIDI control of parameters. A design that wanted it
would have to move parameter ownership out of APVTS, which would cost host
automation, preset recall and undo — a far worse trade than 17 ms on a knob.

---

## ADR-0043 — MIDI mappings live in the state tree, and needed no schema bump

**Phase 6 (MIDI) · Accepted**

Mappings are stored as a `<MIDIMAP>` child of the APVTS state root, written
after every edit rather than at save time. `AudioProcessorValueTreeState::
copyState` therefore carries them along with the parameters, and Apollo's state
serialization needs to know nothing about MIDI at all.

Writing on every edit rather than on save is deliberate: a host may ask for state
at any moment, without warning, and a mapping that existed only in memory until
someone pressed Ctrl+S would be lost by every host that does not.

**No schema version bump.** `StateSerialization.h` states the rule this follows:
adding an optional field with a safe default is not a change an older reader
misinterprets. A version 2 document written before this phase simply has no
`<MIDIMAP>` child, and the loader produces an empty table — which is exactly
right, because a project saved before MIDI Learn existed had no mappings. An
older Apollo reading a newer document ignores a child it does not recognise.
Bumping the version would have refused those documents outright for no gain.

Mappings are stored by **parameter ID, not registry index**: indices move
whenever the registry grows, and a mapping that silently pointed at a different
parameter after an update would be worse than one that was dropped. Every entry
is re-validated through the same `assign()` the learn path uses, so a
hand-edited document cannot put the table into a state the interface could not.
An entry naming a parameter this build does not have is dropped and the rest of
the document still loads (CLAUDE.md §33).

**Given up:** mappings that follow the controller rather than the project. Many
instruments keep a global MIDI map in a user config file, because the controller
on the desk does not change when the project does. That is the better default
and it needs file I/O, a user library path and a merge rule against project
state — resource work that belongs with Phase 9. Per-project storage is the
correct floor: it satisfies the requirement that mappings survive save and load,
and a global default library layers on top of it without changing this format.

---

## ADR-0044 — Per-note expression is voice state, and one rule routes every channel message

**Phase 6b (MIDI) · Accepted**

A voice records the MIDI channel its note arrived on, and carries its own
`pressure`, `timbre` and per-note bend. One rule then decides what a channel
message reaches (`midi::MpeZone::appliesTo`):

    no zone           every voice — exactly as plain MIDI has always behaved
    manager channel   every voice in the zone
    member channel    only the voices on that channel

That is the whole of MPE. There is no MPE code path in the engine, no second
voice allocator and no per-note controller registry, because MPE *is* ordinary
MIDI with a channel convention, and the only thing Apollo was missing was
somewhere to put the channel. A keyboard that sends everything on channel 1
gets identical behaviour to before: every sounding voice is on channel 1, so
every channel message reaches all of them.

Polyphonic aftertouch needs none of that machinery — it names its own note — so
it addresses voices directly and works with no zone at all. It predates MPE by
thirty years and should not have to be configured like it.

**Pressure is not inherited by a new note; bend and timbre are.** Pressure is a
*force*: nobody is applying one at the instant a note begins, and a voice that
started at the previous note's held pressure would be loudly wrong. Bend and
timbre are *positions*, and every MPE controller places a note's pitch on its
member channel before sending the note-on — so a new note adopts what is already
in force, which is what makes an entry slide land in tune.

**Two bend ranges, and they add.** A wheel on the manager channel bends the
whole zone through the wheel's range while each note is also bent through the
member range, which the specification defaults to ±48 semitones. The two are
separate values on the voice and are summed, rather than one overwriting the
other.

**The pitch-bend modulation source is now the wheel position, not the semitone
count divided by two.** The range became a control in this phase, and a source
derived by dividing by it would silently change meaning the moment anyone
widened the range.

**`ModSource::timbre` was appended**, after `random`, because the numeric value
of every entry above it is already inside saved routings. Appending is safe for
saved *state* — APVTS stores denormalised values, so a stored `14` still means
`random`. It is not entirely free for host *automation*: `modNN_source` had to
grow from a maximum of 14 to 15, so an existing automation lane written at full
scale now resolves to `timbre` rather than `random`. That is a real if minor
break, taken deliberately while Apollo is pre-1.0 and for the same reason
ADR-0032 took one: this is the only window in which it is cheap.

**Given up:** per-note routing of *arbitrary* controllers. Only the three
dimensions MPE defines — bend, pressure and CC 74 — are per-note. Any other CC
on a member channel remains a channel-wide control and a MIDI Learn target,
which is what a user who maps one expects.

---

## ADR-0045 — A controller's own configuration messages go through parameters

**Phase 6b (MIDI) · Accepted**

Apollo decodes two Registered Parameter Numbers: RPN 0, pitch-bend sensitivity,
and RPN 6, the MPE Configuration Message. Both arrive on the audio thread, and
neither is applied to the engine there. Each queues a change to the parameter
that already owns the value — `midi_bend_range`, `mpe_zone`, `mpe_members` —
through the same slot-and-flag path a mapped controller uses (ADR-0042). The
engine then reads it back on the next block along with every other parameter.

The alternative, setting the engine directly, would be shorter and wrong. The
zone would then have two owners: a parameter the interface shows and the host
saves, and a hidden engine field a controller had set. They would disagree the
first time a project was reloaded, and the interface would be the one that was
lying. Going through the parameter means an MPE controller plugging itself in is
indistinguishable from the user setting the same control by hand — it is
visible, saved, undoable, and reported to the frontend by the machinery that
already exists.

Supporting MCM at all is what makes MPE work without a manual step: the
specification says a controller announces its own zone, and CLAUDE.md §16.3
says a profile must never be *required*. A synthesiser that needs a switch
flipped before an MPE keyboard does anything has met the letter of "supports
MPE" and not the point of it.

**CC 6, 38 and 98-101 became reserved controllers.** They carry RPN plumbing —
the halves of a parameter number and a data entry — rather than a control value,
and a MIDI Learn mapping on one of them would jerk a parameter every time a
controller introduced itself. This extends ADR-0041's reserved set by exactly
the criterion that set was defined by: Apollo now acts on them.

**Given up:** NRPN, and the rest of the registered parameter numbers. They are
parsed far enough to know they are *not* one of the two Apollo understands —
which matters, because an NRPN selection has to cancel a pending RPN rather
than let its data entry be applied to the wrong parameter — and then ignored.

---

## ADR-0046 — Controller profiles are standards-based, built in, and never required

**Phase 6c (MIDI) · Accepted**

A controller profile is a named list of "this controller number drives that
parameter" entries. Applying one runs every entry through the same
`MidiControlManager::assign` a learned mapping goes through, so a profile is a
*shortcut* and never a mode: nothing behaves differently afterwards, the
bijection still holds, replacements are still reported, and every resulting
mapping can be relearned, released or cleared like any other.

**The built-in profiles name no manufacturer and no product.** Apollo must not
hard-code a specific controller (CLAUDE.md §46), and it does not have to,
because the MIDI specification has already done the work twice over:

- **Sound Controllers, CC 70-79.** Eight of the ten have agreed meanings, and
  they map onto a subtractive synthesiser almost exactly — brightness, harmonic
  intensity, attack, decay, release, vibrato rate. A controller with a knob
  labelled "Brightness" is sending CC 74, whoever made it.
- **General Purpose Controllers, CC 16-19 and 80-83.** Eight numbers the
  specification deliberately leaves undefined, which is exactly what makes them
  the right home for a generic bank of knobs: a controller sending them asserts
  nothing about what they mean.

A list of device names would have been more immediately impressive and worse:
it would be incomplete on the day it shipped, out of date within a year, and it
would tie Apollo's behaviour to somebody else's product decisions.

**Profiles are kept strictly separate from the parameter registry**, which is
the roadmap's own requirement and the right one. The registry describes what
Apollo has and is permanent; a profile describes what somebody's hardware sends
and is disposable. A profile refers to parameters by ID and resolves them when
it is applied, so an entry naming a parameter this build does not have costs one
dropped entry rather than a refusal — and the result says how many were dropped,
because "eight of eight" and "five of eight" are different outcomes the user is
entitled to tell apart.

**Two application modes, both named explicitly in the interface.** Replace
releases everything first, which is what "set my controller up" means when it is
said about a controller that was set up for something else; merge adds to what
is there. Neither is a hidden default the user has to discover.

**Given up:** user-supplied profile files. They are files, and file loading, a
user library path, and the failure modes of both — missing, corrupt, moved —
belong with the resource work in Phase 9 (CLAUDE.md §30). The seam is already
in place: `ControllerProfile` is plain data and `applyProfile` takes one by
reference, so a profile read from disk enters by exactly the same door a
built-in one does.

---

## ADR-0047 — Visualisation is a one-way broadcast, and the scope never flatters the signal

**Phase 7a (UI) · Accepted**

PRD §30.1 asks for a scope on the output and one on every source. Three decisions
shape how that is built, and all three are about keeping the audio thread and the
picture honestly separated.

**The transport is a lock-free ring per source, and the samples are
`std::atomic<float>`.** The audio thread writes and moves an index; the message
thread reads a window ending a margin *behind* that index and tells the writer
nothing. The atomics look heavy-handed and are not: on every architecture Apollo
targets, a relaxed load or store of a four-byte atomic is the instruction a plain
one would have been, so they cost nothing at run time and buy the difference
between "a reader may see a stale sample" and "this is a data race". A scope can
survive a stale sample; it cannot survive undefined behaviour. Reading behind the
writer is what makes even the stale sample rare: the ring holds 170 ms at 48 kHz
and the interface reads every 33 ms.

**Triggering, decimation and silence all happen on the message thread.** A frame
is aligned to the most recent rising zero crossing so a steady note stands still,
and says so when it could not find one rather than inventing stability. Each
drawn point is *the sample at that position*, not an average of the span:
averaging would turn an aliased or clipped waveform into a smooth one, and a
scope whose job is to reveal exactly those things must not flatter what it draws.
The window's peak is measured so a stopped source reads as stopped instead of
holding its last picture (CLAUDE.md §26.1).

**The frame bridge is separate from the parameter bridge**, because the two are
different kinds of thing. ParameterBridge is a conversation — the page asks and
is answered, and every message is the authoritative echo of a value the page also
holds. This is a broadcast: nothing is requested, nothing is acknowledged, and a
dropped frame costs one repaint. Sharing a class would mean sharing a rate, and
30 Hz of scope traffic has nothing to do with how often a knob's value should be
echoed. The frame timer follows the outbound handler, so a plugin with its editor
closed serializes nothing.

**Measured, as PRD §30.1 requires rather than assumed.** Output capture adds
0.012 % of real time at one voice and 0.28 % at thirty-two — 3 to 7 % of the
render it follows, and flat in voice count because it is one pass over the
finished buffer. Building all six frames costs 0.06 % of real time at 30 Hz, on
the message thread. Scopes do not cost polyphony.

**The trace is drawn 1:1 and is never auto-scaled.** A scope that stretched its
input to fill the canvas would make every signal look the same loudness, and
would hide the one thing a scope is best at showing: that something is too loud.
The consequence is visible and deliberate — Apollo's gain staging is conservative
(ADR-0017: one note peaks near -22 dBFS), so an ordinary note draws a small
trace. The caption carries the peak in decibels so the level is readable as a
number, and a display-scale control belongs with the metering work in 7c rather
than as an invented default here.

**Given up:** a scope that looks impressive on a quiet patch. That is the same
trade ADR-0021 and ADR-0033 made — the measurement is worth more than the
flattering picture.

**A note on where the rings live.** Six of them are about 190 KB, which is more
than a thread's stack is worth, so the hub is held by pointer rather than by
value. The first version made it a by-value member of the processor and overflowed
the stack in a test that had been passing for months — a failure that looked
nothing like its cause. The allocation happens once, at construction, which is
exactly where allocation is permitted (CLAUDE.md §9.2).

---

## ADR-0048 — A source's scope shows the whole pool, before the filter, and only while somebody is watching

**Phase 7b (UI) · Accepted**

The output scope of ADR-0047 was one pass over a buffer that already existed. The
five per-source scopes are not: the signals they show exist only inside the voice
loop, which runs up to thirty-two times a block. Three questions follow, and the
answers are what make the difference between a useful instrument and a slower one.

**Capture is taken once for the whole voice pool, not per voice.** What
"oscillator 1" is producing is what every voice producing it is producing
together — any other reading stops being true the moment a second note is held.
So the engine owns one mono accumulator per tap, each voice adds its own
contribution into it, and the sum is published once. The accumulators are cleared
before each pass, which is what makes a source that stops *seen* to stop: the
zeroes are captured rather than inferred from the absence of a write (CLAUDE.md
§26.1). The alternative — showing whichever voice was asked — would draw a
readable single waveform during a chord and would be a picture of something that
is not happening.

The accumulators are a fixed 512 samples and the voice pool is rendered in chunks
of that length while capturing, because `prepare` is told a sample rate and not a
block length, and a host may hand over a longer block than it promised in any
case. A voice rendered as two consecutive calls produces exactly what it produces
as one: the per-sample state that drives it lives in the voice and carries across
the boundary. A test renders the same note both ways and compares the output
sample for sample.

**The four source taps sit before the filter and the amplifier; the fifth sits
after both.** A source scope is what you watch while choosing a wavetable
position, and an envelope's shape drawn over the waveform would obscure the only
thing being looked at — so the sources are taken at the source, after their own
level and balance and nothing else. `postFilter` is the opposite: the voice's
finished contribution, everything it does to the signal, immediately before it is
added to the mix. Today that makes it the output scope without the master gain,
which is nearly redundant; once the effects rack exists it becomes the dry
instrument against the processed one, which is the comparison it is really for.

The pair is more informative than either alone. Closing the filter takes the
sound away without taking the oscillator away, and the two scopes then disagree —
which is the difference between watching a source and watching the mix, and is
asserted as such in the tests.

**Capture runs only while something is watching, and arming it clears the rings.**
Measured on this machine, the six taps together add 0.05 % of real time at one
voice and 0.76 % at thirty-two. In absolute terms that is small — scopes still do
not cost polyphony, which is what PRD §30.1 asks — but as a fraction of the render
it is 8 to 18 %, and an instance whose editor is closed has no reason to pay any
of it. A session holding twenty instances shows one. So the hub carries a flag
the audio thread reads once per block, the editor sets it when it attaches its
outbound handler and clears it when it detaches, and with it clear the engine
renders exactly the code it ran before any of this existed.

Arming clears every ring first. Between one viewer closing and the next opening,
the rings still hold whatever was sounding at the time, and a scope whose opening
frame is audio from a previous session is precisely the stale trace §26.1 forbids.
Clearing is safe there *because* capture is off: with the flag clear no audio
thread is writing to those rings, so there is no writer to race with.

**One code path, not two.** The noise generator adds into whatever it is given, so
the obvious implementation lets it add straight into the mix when nothing is
watching and through a local when something is. That is identical arithmetic
wherever the multiply and the add stay separate instructions, and one rounding
apart wherever the compiler contracts them into an FMA: adding into the mix
rounds once, going through a local rounds twice. It passed on MSVC and GCC and
failed on Apple Clang by 4.5e-8 across 509 samples. Inaudible — and fixed rather
than tolerated, because "watching a source does not change it" is worth having as
an equality, and an equality is only checkable while there is one path to check.
The local is therefore unconditional and the branch buys only the store.

**Given up:** a scope that keeps working when nobody has asked for one, and the
option of reading a source's trace back from a headless instance without arming it
first. Both are recoverable by setting the flag; neither is worth a permanent tax
on every instance in a project.

---

## ADR-0049 — A modulator is sampled, not captured; and the points travel as integers

**Phase 7c (UI) · Accepted**

Everything Apollo draws so far has been a window of audio read out of a ring.
The four things 7c adds — envelope traces, LFO traces, an output meter and the
wavetable displays — are none of them audio, and treating them as if they were
would have been the easy mistake and the expensive one.

**A modulator is sampled at the rate it is drawn, not at the rate it is
produced.** CLAUDE.md §26.1 asks for "a live trace of the value each is currently
producing, not a static picture of its shape", and that distinction is the whole
feature: an editable DAHDSR outline with a playhead — which is what every
synthesiser draws, because it is easier — shows what the envelope was
*configured* to do, and the two part company the moment anything is modulated,
retriggered or clamped. It is the outline that is wrong when they disagree.

But a modulator moves at a few hertz. Capturing one at 48 kHz into an 8192-entry
ring and decimating it to 128 points would be a ring three hundred times larger
than the picture taken from it, and would need a decimation rule with all the
attendant arguments about what may be thrown away. So the ring *is* the picture:
128 entries, one every 7.8 ms, holding exactly the second the interface draws.
There is nothing to decimate and therefore nothing to defend. The resolution is
stated rather than hidden — anything faster than 7.8 ms shows as a step, which is
a fact about the picture and not a defect in it.

The entry is taken between capture chunks, which at first looks like a reason to
shorten the chunks until they divide the trace interval. Measurably it is not: at
128 samples the per-chunk overhead cost 1.85 % of real time at thirty-two voices
against 0.76 % at 512. A chunk is instead *shortened*, on the one in three that
needs it, to land exactly on the next trace boundary. The trace keeps an exact
rate and the capture keeps its long chunks.

**Which voice a trace follows has to be chosen, and the choice is the newest
note.** Envelopes and LFOs are per voice. Summing them would be meaningless —
four envelopes added together describe nothing — and averaging would flatten
exactly what is being watched. The most recently started sounding voice is the
one whose envelope you are listening to while you adjust it, which is the only
rule that matches what the person looking at the screen is doing.

**An unrouted modulator is reported, not hidden.** The engine does not advance an
LFO nothing reads, so its trace is honestly a flat line — and a flat line with no
explanation looks like a fault. The frame therefore carries a `routed` flag, the
trace is omitted for a modulator that is not running, and the interface draws the
zero line and the word "unrouted". On the default patch, which routes nothing,
that is seven eighths of the message as well.

**A meter is not a scope's peak.** The scope reports the largest sample in one
43 ms window with no ballistics at all: it flickers, and a transient landing
between two frames is never shown. The meter rises instantly, falls at 20 dB per
second, reports RMS over 300 ms beside the peak because neither alone is enough —
peak cannot tell a quiet signal with one spike from a loud one, and RMS cannot
tell you that you are about to clip — and holds a clip for a second and a half so
that it is seen by someone who was not watching at the time. Clipping is detected
at full scale rather than below it: Apollo's output is float and a sample above
1.0 survives here, but it will not survive whatever converts to integer
downstream, and a meter that stayed quiet about that would be reporting on its
own numeric range rather than on the signal's fate. This is also the first real
use of the red token CLAUDE.md §24.2 reserves, and it is a word as well as a
colour (§39).

**The wavetable display is not telemetry at all.** A wavetable is a resource,
built once and immutable for the life of the instrument — which is precisely why
the voices already share it without synchronisation. So the audio thread
publishes two numbers, the table index and the *effective* position, and the
message thread reads the table directly and renders 128 points from it. Drawing
them costs less than sending them would. The position is the effective one — the
parameter plus whatever the matrix is adding — because a display that sat still
while a modulated oscillator swept would be failing at the one case it exists
for. Mip level 0 always: a picture has no pitch, and showing a band-limited copy
would draw a rounder wave than the user chose at the moment they are choosing it.

**The points travel as integers, and this fixed a defect rather than adding an
optimisation.** ADR-0047 rounded each point to three decimals on the reasoning
that a thousandth of full scale is below a pixel and that rounding would roughly
halve the message. The first half is true. The second was never measured, and is
backwards: `juce::JSON` serialises a double between 0.1 and 1 to sixteen decimal
places, and rounding to three decimals produces a double whose sixteen-place
expansion is 0.1229999999999999 rather than anything short. Worse, JUCE
pretty-prints by default, putting every array element on its own indented line.
One scope frame measured **18,395 bytes**, thirty times a second.

Sending thousandths as integers and asking for one line brings the same frame to
**5,199 bytes** and the instrument frame to **2,765**. Both are asserted as
budgets in the tests and logged, so a change that quietly multiplies them is
noticed here rather than in a profiler.

**Given up:** a modulator trace fast enough to resolve a 5 ms attack, and a
resettable clip indicator. The first would need a ring three hundred times the
size of the picture for a detail below the refresh rate of the screen; the second
would need an inbound command on the parameter bridge, which is a conversation
about parameters and has no business carrying one about a meter.

---

## ADR-0050 — The interface is React and TypeScript, bundled by esbuild from CMake

**Phase 7d (UI) · Accepted**

`apollo.js` had grown to two thousand lines and was the largest single source
file in the project. It built every control, scope, trace, meter and display by
hand from `document.createElement`, kept its state in module-level `Map`s, and
answered "what redraws when this changes" only by being read end to end. That was
the right way to start — no dependencies, no build step, and the parameter
metadata as the single source of truth (ADR-0038) — and it had stopped being the
right way to continue.

**What the migration had to preserve, and did.** Three properties were the whole
value of the old page and every one of them is a property a rewrite loses
quietly:

- *No control invents its own range.* Every knob is still built from the
  metadata the engine sent (UI_BINDINGS.md §16). A component with a hard-coded
  minimum would look right until someone changed `ParameterDefinitions.h`.
- *The accessibility semantics.* `role="slider"` with live `aria-valuenow` and
  `aria-valuetext`, radio semantics on the switches, visible focus, and nothing
  signalled by colour alone (ADR-0039). React makes all of that easier to write
  and easier to forget.
- *Nothing re-renders that does not have to.* This is the one that changed shape.
  The old page achieved it by never re-rendering at all; the new one achieves it
  with stores.

**Values reach controls through stores, not through props.** A parameter store
with **one listener set per id**, read through `useSyncExternalStore`, so a knob
re-renders when its own value moves and at no other time. A `useState` at the top
of the tree would have re-rendered the page on every 30 Hz echo and on all
hundred and forty-three values when a project loads. The same shape, with one
listener set rather than one per id, carries the MIDI mappings — those change a
few times a minute, and per-id machinery for that frequency would be cost without
benefit. Which knobs a live routing lights is a third store whose hook returns a
**boolean**, so a knob re-renders when its own answer flips rather than whenever
any slot moves.

**The eleven canvases are drawn imperatively and are not React's business.**
Frames arrive thirty times a second; routing them through state would mean eleven
components re-rendering to produce markup that never changes, because the canvas
element is the same element every frame and only the pixels differ. So a frame is
delivered by call to whoever subscribed to that source, and the component draws.
This is the one place in the migration where the idiomatic answer is the wrong
one, and it is worth saying out loud: `useState` there would have looked cleaner
and cost the interface most of its frame budget. Captions *are* state — a caption
is one text node, which is exactly what a diff is cheap at.

**esbuild rather than a framework.** The requirement is TypeScript compiled, JSX
transformed, several dozen modules bundled and a stylesheet emitted, and that is
what esbuild does in one dependency. Vite or webpack would add a dev server
Apollo cannot use — the page only ever runs inside the plugin, served from
memory — a plugin ecosystem nothing here needs, and a few hundred transitive
packages to audit for a licence review that is a real obligation (CLAUDE.md §32).
The whole tree is **twelve packages**. esbuild does not type-check, so `tsc
--noEmit` runs first and CMake runs both; without that a type error would reach
the plugin as a perfectly valid bundle that is wrong at run time.

**CMake runs the bundler; the built files are not committed.** The alternative —
checking the bundle in — is a generated file that can silently disagree with its
source: someone edits a component, forgets to rebuild, and the plugin ships the
previous interface with no diff to show for it. The cost is that **building the
plugin now needs Node**. `apollo_core` and `ApolloTests` do not, deliberately:
the test suite has never had a browser dependency and must not acquire one, and
the sanitizer job configures the editor out entirely. The three output names are
fixed rather than hashed, because there is no cache to bust when a page is served
out of a plugin binary and a hashed name would mean regenerating the C++ resource
table on every build.

**The bundle is 170 KB against the old page's 70.** That is React and ReactDOM,
and it is the honest cost of the migration. Next to a VST3 binary it is nothing,
and it buys a page that can be read one component at a time.

**What the migration cost, concretely.** One real regression, found by driving
the running application rather than by any test: the knob's held flag was kept in
a ref, and a ref mutated on pointer-up re-renders nothing, so a control stayed
lit after the hand left it. The hand-built page wrote the attribute directly and
could not have had the problem. It is the exact shape of mistake a declarative
tree invites — state that is *rendered* must live where rendering can see it —
and it is now a `useState` in both draggable controls.

**Given up:** a frontend anyone can edit with a text editor and reload. Changing
the interface now needs Node installed and a build step to run. That is the
price of types and components, it was paid deliberately, and
`-DAPOLLO_ENABLE_WEBVIEW=OFF` remains the escape hatch for a machine without
Node that only wants the engine and its tests.

---

## ADR-0051 — The accent is gold on gunmetal, not violet on graphite

**Post-Phase 7 (UI) · Accepted**

Apollo is named after the Greek god Apollo — god of the sun and of music — and
the interface did not say so. It was deep violet on near-black graphite from
Phase 6 through Phase 7d, because CLAUDE.md §24.1 asked for a "deep purple accent
system" and that line was taken at face value. The developer read it back off the
screen and said it had never been the intent: the accent should be gold, keyed to
`#F7EF8A`, and the ground a dark metallic grey. Under CLAUDE.md §42 the later
explicit requirement wins, and they asked for the written specifications to be
corrected rather than merely noted — so CLAUDE.md, PRD.md, ARCHITECTURE.md and
ROADMAP.md were updated with it.

**The grey is neutral and faintly cool, which is a decision.** Gold against a
warm grey goes muddy: the two are close in hue and end up arguing about
saturation rather than contrasting. Gold against steel rings, and steel is what
brushed metal actually looks like. The surfaces are therefore gunmetal —
`#15181b` through `#313840` — rather than the warm graphite that a gold accent
first suggests.

**Three intensities of gold, and `#F7EF8A` is the bright end.** On a dark ground
the brand colour is where the accent should be loudest: it is the scope and
wavetable trace, the value being held, and every hover. A richer `#d4a72c` does
the quieter work — knob arcs, borders, the filled chip of a selected switch, with
the ground's own colour as that chip's label, because light text on gold is
unreadable at ten pixels and dark text on gold is a struck coin. A deep `#5a4712`
is left for a border or a wash that must not draw the eye.

**Amber became orange.** CLAUDE.md §24.2 gives caution its own colour, and amber
beside a gold accent is the accent wearing a different hat. Orange is far enough
away to be read as a different statement.

**A light theme was built first, and failed for a reason worth recording.** The
first correction said the plugin should not be dark-themed at all, so it was
rebuilt as ivory and gold — marble and sunlight. Two things went wrong. A module
that recedes does so by losing opacity, and a *dark* display losing opacity
against a *light* page does not dim, it turns muddy grey: every silent scope and
every inactive module looked broken. Making the displays light instead fixed that
and created the second problem, which is that a gold trace on parchment is a dark
line on a pale field rather than something that radiates. The developer looked at
it and asked for dark metallic grey, which resolves both: on a dark ground the
opacity treatment works and the trace glows.

**Given up:** nothing structural. ADR-0039's rules — one job per colour, and no
state carried by hue alone — are untouched; only two of the four hues moved, and
the whole change is the token block plus three canvas fallback colours, because
the stylesheet never re-invents a value below its tokens.

---

## ADR-0052 — A module that is off is marked, not dimmed

**Post-Phase 7 (UI) · Accepted**

A module whose source is not in the signal path — a level at zero, a filter set
to Off — used to render at 55 % opacity. The developer looked at the two
oscillators side by side and said the disparity was not acceptable: Oscillator 1
and Oscillator 2 are a matched pair, structurally identical and adjacent, and one
of them being a different brightness read as one being *special* rather than one
being *silent*.

They offered two ways out: give each oscillator an explicit ON/OFF switch that
lights when on, or make both panels the same shade. **The second, with the state
kept**: every panel now renders identically and the module's heading carries a
small OFF chip.

**Why not the switch.** Level already *is* the enable — a source at exactly zero
is skipped by the voice rather than rendered and multiplied by nothing (Voice.cpp
`renderAdding`), so a separate enable would be a second control for a state that
already has one, two new automatable parameters, and a state-schema migration for
presets that do not need it. It would also not have solved the problem as stated:
with oscillator 2 off, its switch would be unlit and oscillator 1's lit, which is
the same disparity in a smaller rectangle.

**Why the chip is better than the dim regardless.** Opacity is a difference with
no words attached: it says "this panel matters less", not "this source is
silent", and a reader has to have another panel to compare it against to notice
at all. ADR-0039 requires that no state be carried by appearance alone, and a
brightness wash is the weakest possible compliance with that. The chip states it
in a word, beside the module's own name, and it is legible on the module by
itself.

**Given up:** the at-a-glance sense of which half of the instrument is live,
which the dim did give. The level readings, the OFF chips and the scopes' own
"silent" captions all still say it; none of them say it as fast. That is the
trade the developer asked for, and it buys a page where two identical modules
look identical.
