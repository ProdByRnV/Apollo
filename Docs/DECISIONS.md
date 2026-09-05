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
