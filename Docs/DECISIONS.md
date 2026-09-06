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
