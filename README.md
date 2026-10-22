# Apollo

A wavetable synthesizer by ProdByRnV, for Windows, macOS and Linux, as a VST3
plugin and a standalone application. Built in C++ with JUCE 8, with an interface
written in React and shown through the platform's own WebView.

> Apollo adapts to the user's hardware and host environment. No core feature
> depends on a specific DAW, operating system, audio interface, MIDI controller,
> driver or connector.

---

## Status

**Version 0.1.0, approaching a release candidate (roadmap Phase 12). Not
released.** There are no downloads yet. Apollo is built from source, below, and
every CI run also packages it for each platform.

It is a complete instrument: it plays, every one of its 298 parameters affects
the sound, and it is tested, measured and packaged on four platforms. What is
not yet true is in [Docs/KNOWN-LIMITATIONS.md](Docs/KNOWN-LIMITATIONS.md). The
largest gaps are that it has not been tried inside a DAW, that its interface has
only ever been seen on Windows, and that several features the specification
describes (drawn curves) are not built yet. They are
being built before the first release (Phase 13).

---

## What it does

**Sound.** Two wavetable oscillators, each with up to sixteen detuned,
stereo-spread unison voices with a choice of detune curve and blend, plus a sub
oscillator (sine, triangle or square) and a stereo noise generator — white, pink, or one of four built-in attack transients — whose rate can follow the keyboard. Four built-in wavetables — Sweep, Pulse, Formant and Fold — scanned
by position. The tables are band-limited mipmaps, and aliasing is measured, not
assumed: worst case −98.5 dBc. Each oscillator can warp its wave five ways —
sync, asymmetry, pulse width, bend and mirror — with the amount modulatable;
warp aliases more than the plain oscillator, and the figures are published.

**Shaping and movement.** Two filters (low-pass, high-pass, band-pass, notch,
a comb tuned by its cutoff, and a formant filter that sweeps through vowels),
each with drive, a mix and key tracking, in series, in parallel, or split so
that each source chooses its own. Four DAHDSR envelopes with adjustable
curves, shaped by dragging them; four LFOs with seven built-in shapes and one
you draw, which can follow the host's tempo; and a sixteen-slot modulation
matrix, each slot's depth itself modulatable, whose sources include velocity,
key tracking, the mod wheel, pitch bend, aftertouch, MPE timbre, a random source
and four macros, each with a range and a curve.

**Effects.** A rack of six slots in any order: distortion (soft, hard and diode
curves, 4× oversampled), a stereo delay with ping-pong and tempo sync, a
room-and-hall reverb, a noise gate, an RMS compressor, and a seven-band
parametric equaliser that draws its own response curve, with the live spectrum
of the sound going in and coming out behind it.

**Presets.** Nine factory sounds and an init patch, built into the plugin, and
your own presets saved as `.rnv` files and browsed by name, category and bank.

**Playing it.** Any MIDI keyboard or controller. MIDI Learn on any control,
controller profiles, MPE, and a standalone that opens the keyboards it finds and
follows the system's output device.

**Seeing it.** An oscilloscope for each sound source and for the output, a live
trace of every envelope and LFO, the wave each oscillator is reading, and an
output meter. Every control can be dragged, nudged from the keyboard, reset, or
given an exact value by typing it.

---

## Platforms

| | |
|---|---|
| Windows 10 and 11 | x86-64 |
| macOS 11 and later | Apple silicon and Intel, one universal binary |
| Linux | x86-64 and ARM64; tested on Ubuntu 24.04 |

Formats: VST3 and standalone. How much of each platform has been seen working,
rather than built, is in [Docs/PLATFORMS.md](Docs/PLATFORMS.md).

---

## Building

You need CMake 3.22+, a C++20 compiler, Git and **Node.js 22.18 or later**.

```sh
cmake -S . -B build
cmake --build build --config RelWithDebInfo
ctest --test-dir build -C RelWithDebInfo --output-on-failure
```

The first configure fetches the pinned JUCE revision and takes a few minutes.
Every build leaves a copy of the standalone in the project root, so it can be
launched straight away. [Docs/BUILD.md](Docs/BUILD.md) covers prerequisites per
platform, the build options, sanitizer builds and packaging.

---

## Documentation

### Using it

| Document | Contents |
|---|---|
| [Docs/PLATFORMS.md](Docs/PLATFORMS.md) | Supported platforms and formats, and how much of each has been verified |
| [Docs/KNOWN-LIMITATIONS.md](Docs/KNOWN-LIMITATIONS.md) | What Apollo does not do yet, and why |
| [Docs/COMPATIBILITY.md](Docs/COMPATIBILITY.md) | Which presets and projects open in which version of Apollo |

### Specification

| Document | Contents |
|---|---|
| [PRD.md](PRD.md) | Product requirements |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Technical architecture |
| [ROADMAP.md](ROADMAP.md) | Implementation order and phase exit criteria |
| [UI_BINDINGS.md](UI_BINDINGS.md) | The contract between the engine and the interface |
| [CLAUDE.md](CLAUDE.md) | Engineering rules |

### Engineering

| Document | Contents |
|---|---|
| [PROJECT-STATE.md](PROJECT-STATE.md) | The verified current state. **Start here** |
| [Docs/BUILD.md](Docs/BUILD.md) | Building, configurations, options, packaging |
| [Docs/TESTING.md](Docs/TESTING.md) | The test suites, the host harness, regression renders, the interface tests |
| [Docs/DECISIONS.md](Docs/DECISIONS.md) | Every architectural decision, with its reasoning |
| [Docs/CODING-STANDARDS.md](Docs/CODING-STANDARDS.md) | Coding standards and warning policy |
| [Docs/VERSIONING.md](Docs/VERSIONING.md) | Version axes and compatibility rules |
| [Docs/PARAMETER-CONVENTIONS.md](Docs/PARAMETER-CONVENTIONS.md) | Parameter naming and the ID contract |
| [Docs/OVERSAMPLING.md](Docs/OVERSAMPLING.md) | Where Apollo oversamples, and why only there |
| [Docs/REPOSITORY-LAYOUT.md](Docs/REPOSITORY-LAYOUT.md) | The directory structure |

---

## The rule that matters most

The audio thread must stay deterministic and real-time safe: no allocation, no
locks, no file or network I/O, no WebView calls, no logging, no unbounded work,
ever (CLAUDE.md §7).

Everything else in this repository is arranged to make that rule easy to keep.
