# Apollo Parameter Naming & ID Conventions

A parameter ID is part of two permanent external contracts: **host automation**
(the parameter's identity in a DAW's automation lane) and **serialized state**
(presets and saved projects). ARCHITECTURE.md §6.1 and UI_BINDINGS.md §3 both
require IDs to be stable once released, so their shape is fixed here before a
registry exists and IDs start shipping.

The mechanical rules in §2 are enforced in code by
`Source/Parameters/ParameterId.h` and covered by `Tests/Foundation/ParameterIdTests.cpp`.
The semantic rules in §3 are conventions a compiler cannot check.

---

## 1. The cardinal rule

> **A released parameter ID is permanent.**

It is never renamed, never repurposed, and never given a different range meaning.
Doing so silently corrupts every existing preset and every saved DAW project that
references it.

To change what a parameter means: add a new ID, and migrate the old one during
state load.

Pre-1.0 this rule is relaxed in practice — see [VERSIONING.md](VERSIONING.md) §2 —
but every ID change before 1.0 still requires a deliberate decision, not a rename
in passing.

---

## 2. Mechanical rules (enforced)

An identifier must:

1. Be non-empty and at most **64 characters**.
2. Contain only lower-case ASCII letters, digits and `_`.
3. Begin with a lower-case letter.
4. Not end with `_`.
5. Not contain `__`.
6. Consist of at least **two** `_`-separated segments.
7. Have at least one letter in every segment.

Rule 6 makes an ID self-describing when it appears alone in a preset file or an
automation lane. Rule 7 rejects `osc_1_gain`, where the index has become a
meaningless standalone segment.

The 64-character bound also serves the bridge: UI_BINDINGS.md §14 requires the
native side to bound every string arriving from the WebView, and an oversized ID
can be rejected before any lookup is attempted.

Violations are reported as a `ParameterIdIssue` with a short description that is
safe to surface through the UI bridge — it never echoes the offending value or
any internal detail (UI_BINDINGS.md §13).

---

## 3. Semantic structure (convention)

```text
<domain>[index]_[group_]<name>
```

### Domain

The owning subsystem. An instance index attaches directly to the domain, with no
separator, and is **1-based** to match how the UI presents it.

| Domain | Meaning | Example |
|---|---|---|
| `osc1`, `osc2` | Primary wavetable oscillators | `osc1_wavetable` |
| `sub` | Sub oscillator | `sub_level` |
| `noise` | Noise generator | `noise_level` |
| `filter1`, `filter2` | State-variable filters | `filter1_cutoff` |
| `env1` … `env4` | DAHDSR envelopes | `env1_attack` |
| `lfo1` … `lfo4` | LFOs | `lfo2_rate` |
| `macro1` … | Macro controls | `macro1_value` |
| `fx` | Effects rack; the effect name is the next segment | `fx_delay_time` |
| `master` | Global output stage | `master_gain` |

`filter_cutoff`, `filter_resonance` and `filter_drive` appear un-indexed in
UI_BINDINGS.md §3 because that registry predates the second filter. They are
valid under these rules. Whether they are renamed or kept and joined by
`filter2_*` is a Phase 2 decision that must be recorded in
[DECISIONS.md](DECISIONS.md) — and, once shipped, is governed by §1.

### Name

The parameter itself, in the units a user would recognise: `cutoff`, `resonance`,
`attack`, `level`, `pan`, `mix`, `rate`, `depth`, `amount`, `position`.

Prefer the term the UI shows. An ID that disagrees with its own label is a
support burden for the life of the product.

### Consistency

Use one word for one concept across the whole instrument:

- `level` for a per-source output amount; `gain` only for a global stage.
- `mix` for dry/wet balance, never `blend` or `wet`.
- `amount` for a modulation quantity; `depth` for a routing depth in the matrix.
- `position` for wavetable scanning, matching PRD §8.1.

---

## 4. Display names

The `AudioProcessorParameter` display name is separate from the ID and *may*
change — it is presentation, not identity.

- Title case: `Filter Cutoff`, `Osc 1 Wavetable`.
- Short enough for a host's automation lane, which often truncates aggressively.
- Never contains the raw ID.

---

## 5. Value representation

From UI_BINDINGS.md §4:

- **Normalized `[0, 1]`** is the transport representation across the bridge.
- **Plain values** carry the user-facing unit (Hz, ms, dB, voice count).
- Native parameter definitions are authoritative for the conversion between them.
- **Discrete parameters keep their step count.** An integer control such as
  `osc1_unison` is never treated as an arbitrary continuous float.

---

## 6. Adding a parameter

1. Confirm the ID satisfies §2 and §3.
2. Confirm the ID has never been used before, including by a removed parameter.
3. Register it in the authoritative native registry (Phase 2) — never in the
   frontend, which consumes metadata rather than declaring it (UI_BINDINGS.md §3).
4. Define range, default, unit, step/skew, smoothing and modulation capability.
5. Add or extend a test covering its range and default.
6. If the parameter is part of the documented bridge registry, update
   UI_BINDINGS.md §3 in the same change.
