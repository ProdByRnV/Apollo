# Apollo Repository Layout

Three project documents propose a directory structure — CLAUDE.md §44, PRD §45
and ARCHITECTURE.md §10 — and they differ in places. All three describe
themselves as recommendations rather than requirements. This document records the
**reconciled layout** so that the structure does not drift between contributors
or between development sessions.

Directories are created when they receive their first file, not ahead of time.
Empty placeholder trees go stale and start lying about what exists.

---

## 1. Reconciliation

| Question | Decision | Reasoning |
|---|---|---|
| `WebUI/` (CLAUDE.md, PRD) vs `Web/` (ARCHITECTURE.md) | **`WebUI/`** | CLAUDE.md is the agent-directive document and explicitly overrides defaults; PRD agrees, making it 2-to-1. |
| `Assets/` (CLAUDE.md, PRD) vs `Resources/` + `Presets/` (ARCHITECTURE.md) | **`Assets/`**, with `Assets/Presets/` inside it | Keeps every shipped resource under one root; the split adds a top-level directory without adding clarity. |
| Effects under `Source/DSP/` (CLAUDE.md) vs `Source/Effects/` (ARCHITECTURE.md) | **`Source/DSP/<Effect>/`** | CLAUDE.md's DSP breakdown is the more detailed of the two, and effects are DSP. |
| `CMake/` and `Docs/` (ARCHITECTURE.md only) | **Adopted** | CLAUDE.md does not exclude them and both are in use. |
| `Source/Parameters/` (ARCHITECTURE.md) vs `Source/State/` (CLAUDE.md, PRD) | **Both** | They are different concerns: parameter *definitions* versus serialization and preset management. |

If a future requirement conflicts with this table, the later explicit
requirement wins (CLAUDE.md §42) — update this document in the same change.

---

## 2. Layout

```text
Apollo/
├── CMakeLists.txt              Authoritative build entry point
├── CMake/                      Build modules (options, warnings, dependencies)
├── PROJECT-STATE.md            Current, verified implementation state
├── README.md
│
├── CLAUDE.md                   Agent directives and engineering rules
├── PRD.md                      Product requirements
├── ARCHITECTURE.md             Technical architecture
├── ROADMAP.md                  Implementation order
├── UI_BINDINGS.md              Native <-> WebView contract
│
├── Docs/                       Engineering documentation
│   ├── BUILD.md
│   ├── TESTING.md
│   ├── CODING-STANDARDS.md
│   ├── VERSIONING.md
│   ├── PARAMETER-CONVENTIONS.md
│   ├── REPOSITORY-LAYOUT.md
│   ├── OVERSAMPLING.md
│   ├── PLATFORMS.md            What runs where, and how much of it has been seen
│   ├── KNOWN-LIMITATIONS.md    What Apollo does not do yet, and why
│   └── DECISIONS.md
│
├── Source/                     Native C++: three libraries and the target shells
│   ├── ApolloVersion.h.in      Configured by CMake
│   ├── Parameters/             Parameter definitions, IDs, metadata
│   ├── Audio/                  AudioProcessor, standalone device following
│   ├── Engine/                 Voices, the voice engine, source mixing
│   ├── DSP/                    Oscillators, Unison, Noise, Filters,
│   │                           Envelopes, LFO, Modulation, Distortion,
│   │                           Delay, Reverb, Dynamics, EQ, Oversampling,
│   │                           Utilities (FFT, smoothing), and Effects
│   │                           (the rack and the contract every effect
│   │                           obeys — the one directory here that holds
│   │                           no DSP of its own)
│   ├── Telemetry/              Scope rings, modulation traces, the meter
│   ├── State/                  State and preset documents, migration
│   ├── MIDI/                   Mapping, MPE, RPN, MIDI Learn, profiles,
│   │                           device adoption
│   ├── Resources/              Preset library, factory presets,
│   │                           wavetable files and loading
│   ├── UI/                     WebView editor, bridge protocol, bridges
│   └── Plugin/                 VST3 and standalone shells, and the
│                               standalone-only behaviour (MIDI and output
│                               device adoption)
│
├── WebUI/                      React / TypeScript frontend                [Phase 7d]
│   ├── package.json            react, react-dom, esbuild, typescript
│   ├── build.mjs               esbuild bundle; CMake runs it
│   ├── tsconfig.json
│   ├── index.html              the shell; everything else is React
│   └── src/
│       ├── bridge/             the wire contract, as types, and the transport
│       ├── params/             range mapping, formatting, enumeration labels
│       ├── state/              parameter, telemetry, MIDI and modulation stores
│       ├── canvas/             the drawing primitives every picture shares
│       ├── components/         knob, segmented, select, rail, scope, trace, meter
│       ├── modules/            the layout, in signal order
│       └── styles/
│
├── Tests/
│   ├── CMakeLists.txt
│   ├── TestMain.cpp            ApolloTests: the unit and integration suite
│   ├── Foundation/             Build system, conventions, invariants
│   ├── Parameters/  Audio/  Engine/  DSP/  Telemetry/
│   ├── State/  MIDI/  Resources/  UI/
│   │                           Each mirrors the Source/ directory it tests
│   ├── Validation/             The assembled instrument, measured
│   ├── Regression/             Fifteen renders against references
│   ├── Analysis/               Spectral tools the two above share
│   ├── Performance/            The benchmark, and the machine it runs on
│   ├── Host/                   ApolloHostTests: the VST3 loaded through a
│   │                           host, including the reliability soak
│   └── Package/                Readers for PE, Mach-O and ELF binaries
│
└── .github/                    CI: the workflow, and the Linux
                                dependency action it shares
```

**No `Assets/` directory**, though all three source documents proposed one.
Factory presets are compiled into the plugin as the parameters they change
(ADR-0065) and the built-in wavetables as spectra (ADR-0066), so Apollo ships
no loose resources. If a future feature needs files beside the plugin, they go
under `Assets/` as §1 decided.

---

## 3. Rules

1. **Every source file must be listed in a `CMakeLists.txt`.** CMake is the
   authoritative build system; an unlisted file does not exist (CLAUDE.md §31).
2. **Apollo code lives in libraries, not in target shells.** `Source/Plugin/`
   stays a thin wrapper over `apollo_core`, `apollo_engine` and
   `apollo_editor` (ARCHITECTURE.md §20.1). This is what keeps
   the DSP engine independent of the plugin format (ARCHITECTURE.md §5.1) and it
   is also what allows the strict warning set to apply to Apollo code without
   drowning in third-party diagnostics — see [CODING-STANDARDS.md](CODING-STANDARDS.md) §2.
3. **Platform-specific code stays out of DSP and engine code** (CLAUDE.md §3.3,
   §46). In practice there is almost none to isolate: one conditional, in the
   editor, choosing the WebView backend, with everything else that differs by
   platform in CMake. `Source/Platform/` is where an adapter goes if one is ever
   needed; it has not been so far.
4. **No hard-coded absolute or developer-machine paths anywhere**, in source or
   in CMake (CLAUDE.md §31.2). Paths come from CMake targets or cache variables.
5. **Test directories mirror the code they test**, and the `juce::UnitTest`
   category string matches the directory name.
