# Apollo Versioning Conventions

Apollo has several version numbers that change for different reasons and must not
be conflated. Coupling them would force a state migration every time the product
version moved, or hide a breaking schema change behind an unchanged number.

---

## 1. Version axes

| Axis | Source of truth | Changes when | Now |
|---|---|---|---|
| **Product version** | `project(Apollo VERSION …)` in the top-level `CMakeLists.txt` | A release is made | `0.1.0` |
| **Plugin version** | Derived from the product version by the plugin targets | With the product version | `0.1.0` |
| **State / preset schema version** | `currentSchemaVersion` in `Source/State/StateSerialization.h` | The serialized layout changes incompatibly | `2`; reads `1` and `2` |
| **UI bridge protocol version** | `PROTOCOL_VERSION`, on both sides of the bridge | A bridge message shape changes incompatibly | `1` |
| **Parameter IDs** | The parameter registry | *Never* — see below | 244 |

What these promise a user — which presets and projects open in which version —
is [COMPATIBILITY.md](COMPATIBILITY.md).

---

## 2. Product version

Semantic versioning, `MAJOR.MINOR.PATCH`.

- **MAJOR** — incompatible change to a user-facing contract: preset
  compatibility, parameter identity, or supported platforms/formats.
- **MINOR** — backward-compatible functionality.
- **PATCH** — backward-compatible fixes.

Pre-1.0 (`0.x.y`), the public contracts are explicitly *not* frozen. The point at
which parameter IDs and the preset format become permanent is the 1.0 release;
before then, breaking changes are permitted but must still be recorded.

The version is defined in exactly one place — the top-level `CMakeLists.txt` —
and reaches C++ through the generated `ApolloVersion.h`. A test asserts that the
macro form and the `constexpr` form agree.

---

## 3. State and preset schema version

Serialized state carries its own integer schema version, independent of the
product version (CLAUDE.md §29, ARCHITECTURE.md §6.4). PRD §32 and CLAUDE.md §29
both show it as `formatVersion`, starting at `1`.

Rules:

- Increment when the serialized layout changes in a way an older reader would
  misinterpret. Adding an optional field with a safe default is not such a change.
- Never reinterpret an existing key's meaning. Introduce a new key and migrate.
- Every increment ships with an explicit migration path and a test that loads a
  captured file of each previously supported version. The captured files are
  `Tests/State/CompatibilityFixtures.h`, starting with a preset 0.1.0 wrote
  (12d-3). Version 1 predates them and is tested from a document built in code.
- Loading must validate, and must fail without destroying the current state
  (CLAUDE.md §33).

Preset files (`.rnv`, ADR-0053) carry this same schema version rather than a
second one of their own, so a preset written by an older build migrates through
the same code that migrates an older project. A preset's own metadata — name,
author, category, comment — is additive and does not, by itself, move the schema
version.

---

## 4. UI bridge protocol version

Every structured bridge message carries a `version` field (UI_BINDINGS.md §15),
starting at `1`.

- Increment for any incompatible change to message shape or semantics.
- Unsupported versions are rejected cleanly and never reinterpreted as an older
  schema.
- **Bridge protocol compatibility and parameter-ID compatibility are separate
  concerns**; neither implies the other.

---

## 5. Parameter IDs

Parameter IDs are not versioned — they are permanent.

Once an ID ships, it identifies that parameter forever: it is embedded in every
saved DAW project and every preset, and in host automation lanes. Changing or
repurposing one silently corrupts existing user work.

To change a parameter's meaning, add a new ID and migrate the old one during
state load. See [PARAMETER-CONVENTIONS.md](PARAMETER-CONVENTIONS.md).

---

## 6. Dependency pinning

JUCE is pinned to an exact commit in `CMake/ApolloDependencies.cmake`, and the
configure step verifies the resolved commit against that pin. Upgrading JUCE is a
deliberate change: update both the version and the commit, record it in
[DECISIONS.md](DECISIONS.md), and re-run the full test suite on every supported
platform.

---

## 7. Release tagging

Releases are tagged `v<MAJOR>.<MINOR>.<PATCH>` (e.g. `v1.0.0`). The tag must point
at a commit whose `project(... VERSION ...)` matches the tag.
