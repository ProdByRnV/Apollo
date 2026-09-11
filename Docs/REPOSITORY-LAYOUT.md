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
│   └── DECISIONS.md
│
├── Source/                     Native C++ (apollo_core and the target shells)
│   ├── ApolloVersion.h.in      Configured by CMake
│   ├── Parameters/             Parameter definitions, IDs, metadata
│   ├── Audio/                  AudioProcessor, engine entry points        [Phase 1/3]
│   ├── Engine/                 Voice management, synthesis engine         [Phase 3]
│   ├── DSP/                    Oscillators, Filters, Envelopes, LFO,      [Phase 4+]
│   │                           Modulation, Unison, Distortion, Delay,
│   │                           Reverb, Dynamics, EQ, Oversampling
│   ├── State/                  Serialization, presets, migration          [Phase 2/9]
│   ├── MIDI/                   MIDI handling, MIDI Learn, profiles        [Phase 6]
│   ├── Resources/              Wavetable and resource management          [Phase 9]
│   ├── UI/                     WebView editor and bridge                  [Phase 2/7]
│   ├── Platform/               Platform-specific adapters                 [as needed]
│   ├── Plugin/                 VST3 wrapper shell                         [Phase 1]
│   └── Standalone/             Standalone application shell               [Phase 1]
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
├── Assets/                     Shipped resources                          [Phase 9]
│   ├── Wavetables/
│   ├── Noise/
│   ├── Presets/
│   └── UI/
│
├── Tests/
│   ├── CMakeLists.txt
│   ├── TestMain.cpp
│   ├── Foundation/             Build system, conventions, invariants
│   ├── DSP/                                                              [Phase 3+]
│   ├── State/                                                            [Phase 2]
│   ├── MIDI/                                                             [Phase 6]
│   └── Integration/                                                      [Phase 10]
│
└── .github/workflows/          CI
```

Bracketed phases indicate the roadmap phase that creates the directory.

---

## 3. Rules

1. **Every source file must be listed in a `CMakeLists.txt`.** CMake is the
   authoritative build system; an unlisted file does not exist (CLAUDE.md §31).
2. **Apollo code lives in libraries, not in target shells.** `Source/Plugin/` and
   `Source/Standalone/` stay thin wrappers over `apollo_core`. This is what keeps
   the DSP engine independent of the plugin format (ARCHITECTURE.md §5.1) and it
   is also what allows the strict warning set to apply to Apollo code without
   drowning in third-party diagnostics — see [CODING-STANDARDS.md](CODING-STANDARDS.md) §2.
3. **Platform-specific code is isolated in `Source/Platform/`** behind an
   interface, never inlined into DSP or engine code (CLAUDE.md §3.3, §46).
4. **No hard-coded absolute or developer-machine paths anywhere**, in source or
   in CMake (CLAUDE.md §31.2). Paths come from CMake targets or cache variables.
5. **Test directories mirror the code they test**, and the `juce::UnitTest`
   category string matches the directory name.
