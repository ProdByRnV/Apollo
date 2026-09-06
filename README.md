# Apollo

A cross-platform, host-agnostic multi-oscillator wavetable synthesizer built with
C++/JUCE 8 and a WebView frontend.

- **Formats:** VST3 (primary) and standalone
- **Platforms:** Windows, macOS, Linux — x86-64 and ARM64 where the toolchain supports it
- **Backend:** Modern C++ real-time DSP engine, independent of any plugin format
- **Frontend:** HTML/CSS/TypeScript/React through JUCE's platform WebView
- **Build:** CMake (authoritative; Projucer is not used)

> Apollo adapts to the user's hardware and host environment. No core feature
> depends on a specific DAW, operating system, audio interface, MIDI controller,
> driver or connector.

---

## Status

**Early development — roadmap Phase 1 (Build System & Application Foundation) complete.**

The build system, dependency pinning, test framework and engineering conventions
are in place, and Apollo builds as both a VST3 plugin and a standalone
application with a verified, real-time-safe audio path.

**No synthesis code exists yet.** Apollo outputs silence by design; the voice
engine arrives in Phase 3, and the parameter/state/UI-binding layer in Phase 2.

[PROJECT-STATE.md](PROJECT-STATE.md) is the authoritative, verified record of what
is implemented — read it before starting work.

---

## Quick start

```sh
cmake -S . -B build
cmake --build build --config RelWithDebInfo
ctest --test-dir build -C RelWithDebInfo --output-on-failure
```

The first configure clones the pinned JUCE revision and builds JUCE's `juceaide`
helper, which takes a few minutes. See [Docs/BUILD.md](Docs/BUILD.md) for
prerequisites, build configurations, options and sanitizer builds.

---

## Documentation

### Specification

| Document | Contents |
|---|---|
| [PRD.md](PRD.md) | Product requirements |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Technical architecture |
| [ROADMAP.md](ROADMAP.md) | Implementation order and phase exit criteria |
| [UI_BINDINGS.md](UI_BINDINGS.md) | Native ↔ WebView contract |
| [CLAUDE.md](CLAUDE.md) | Engineering rules and agent directives |

### Engineering

| Document | Contents |
|---|---|
| [PROJECT-STATE.md](PROJECT-STATE.md) | Verified current state — **start here** |
| [Docs/BUILD.md](Docs/BUILD.md) | Building, configurations, options |
| [Docs/TESTING.md](Docs/TESTING.md) | Test framework and how to add tests |
| [Docs/CODING-STANDARDS.md](Docs/CODING-STANDARDS.md) | Coding standards and warning policy |
| [Docs/VERSIONING.md](Docs/VERSIONING.md) | Version axes and compatibility rules |
| [Docs/PARAMETER-CONVENTIONS.md](Docs/PARAMETER-CONVENTIONS.md) | Parameter naming and ID contract |
| [Docs/REPOSITORY-LAYOUT.md](Docs/REPOSITORY-LAYOUT.md) | Reconciled directory structure |
| [Docs/DECISIONS.md](Docs/DECISIONS.md) | Architectural decision log |

---

## The rule that matters most

The audio thread must remain deterministic and real-time safe. No allocation, no
locks, no file or network I/O, no WebView calls, no logging, no unbounded work —
ever (CLAUDE.md §7).

Everything else in this repository is arranged to make that rule easy to keep.
