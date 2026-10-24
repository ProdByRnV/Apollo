# Preset and Project Compatibility

What happens when a preset or a DAW project made with one version of Apollo is
opened in another, and the rules a change to Apollo must follow to keep that
true. Each promise below names the test that holds it to its word. A promise
without one is marked as such.

[VERSIONING.md](VERSIONING.md) defines the version numbers this page refers to.

---

## 1. The promise

A preset (`.rnv`) and a DAW project store the same thing: a versioned state
document, read by the same validator (ADR-0061). So everything here applies to
both.

| You open… | In… | What happens | Held by |
|---|---|---|---|
| A preset or project from **Apollo 0.1.0 or later** | A later Apollo | It opens and sounds as it was saved. Settings that did not exist yet take their defaults, and a new setting's default is the one that reproduces the old sound (§3) | A preset 0.1.0 wrote, frozen in the test suite and read by every build (`State compatibility`) |
| One from a **later Apollo**, same schema | An earlier Apollo | It opens. Settings this version does not have are ignored, so the sound may differ if it used them | `State compatibility`: an unknown parameter is ignored and the rest loads |
| One from a later Apollo with a **newer schema** | An earlier Apollo | Refused: "The state data was saved by an incompatible version of Apollo." **The sound already loaded is untouched** | `State serialization`: unsupported versions are refused; refusal changes nothing |
| A file that is **not Apollo's**, or is damaged | Any | Refused, with the current sound untouched. A `.rnv` extension proves nothing about the contents | `State serialization`, `Preset document`, and the reliability suite's stream of damaged documents (12a) |

**A preset is a complete sound.** Loading one sets every setting. Whatever the
document does not mention returns to its default rather than keeping what was
there before, so the same preset always sounds the same, whatever was loaded
before it. *Held by `State compatibility`: every parameter is moved away from
its default, a one-parameter document is loaded, and all the others must be
back at their defaults.*

**Drawn LFO shapes follow the same rule (13f-1).** A curve is not a parameter;
it is stored as an `<LFOCURVES>` element beside the parameters, and only for
LFOs whose curve differs from the default. A document without one, which is
every document written before 13f-1, loads with every LFO on the default curve
rather than keeping whatever was drawn before. An earlier Apollo reading a
document with curves ignores the element. An LFO set to the drawn shape should
then fall back to that version's last shape, step, because the shape's number is
past the end of its range. That is how the parameter clamps; it has not been
tried against an older build. No schema change was needed (ADR-0102). *Held by
`Drawn LFO curves in the instrument`.*

**The user's own content follows it too (13g).** A loaded wavetable or noise
sample is stored as a `<CONTENT>` element of lossless audio beside the
parameters, and only while something is loaded. A document without one, which
is every document written before 13g, loads with every slot empty: an
oscillator on USER plays the first built-in table and SAMPLE plays white noise,
so nothing goes quiet. An earlier Apollo ignores the element; USER and SAMPLE
are past the end of its ranges, so they should clamp to FOLD and CHIFF, which
has not been tried against an older build. No schema change was needed
(ADR-0104). *Held by `User content`.*

**Two things a preset deliberately does not carry**, because they describe your
setup rather than the sound (ADR-0061):

- your **MIDI Learn assignments**;
- the **pitch-bend range** and **MPE zone** settings.

A DAW project carries both. Loading a preset leaves yours as they were.

---

## 2. Automation and the DAW

A DAW stores automation against a plugin's parameters, and a project against the
plugin's identity. Both are permanent once released.

| | Promise | Held by |
|---|---|---|
| **Parameter IDs** (`filter1_cutoff`) | Never change meaning, never reused. A changed parameter gets a new ID, and the old one is migrated on load ([VERSIONING.md](VERSIONING.md) §5) | `Parameter identifier conventions`, `Parameter registry` |
| **The numbers a VST3 host stores** | Pinned against a loaded plugin, because a JUCE upgrade could renumber them with every string ID intact (ADR-0075) | `ApolloHostTests` |
| **Stepped parameters** (filter type, rack slot) | The order of their choices never changes, so a preset or project — which stores the choice's number — always means the same choice. A choice added later goes at the end, and that does grow the range: a host's automation of the parameter stores a *normalised* position, which moves when the range grows. That is allowed before 1.0 and recorded each time — Phase 13b appended warp to the matrix destinations (ADR-0090), 13c-1 comb and formant to the filter types (ADR-0091), 13c-3 split to the filter routing (ADR-0093), and 13d-2 four transients to the noise type (ADR-0095), 13e-3 four macros to every matrix slot's source (ADR-0098), 13e-4 six filter controls to every slot's destination (ADR-0099), 13e-5 six effect controls to it (ADR-0100), 13f-1 the drawn shape to every LFO's shapes (ADR-0102), and 13g USER to both oscillators' wavetables and SAMPLE to the noise types (ADR-0104) — and is a breaking change after it. The rack slots listed all six effects from the start to avoid exactly this (ADR-0054) | **No test pins this.** The registry test checks ranges are consistent, not that they are unchanged, so for now it is held by review alone |
| **Controller profile files** (`.rnvprofile`, Phase 13h-3) | Version 1 is read by every later build. A profile naming a parameter the reading build lacks applies the rest and says how many were dropped; one from a later format version is refused by name rather than half-read (ADR-0107) | `Controller profile files` |
| **Program numbers** (Phase 13h-2) | The host's program list is the built-in sounds in their compiled order, and MIDI bank 0 counts the same way. A new built-in is appended, never inserted. Appending still changes the number of programs, which a host storing the Program parameter as a fraction may notice; that is decided when the first built-in is added after release (ADR-0106) | `Program changes`, `ApolloHostTests` |
| **The plugin's identity** (`ProdByRnV`, `Prnv`, `Apol`, `com.prodbyrnv.apollo`) | **Confirmed by the developer** (ADR-0089). From the first release a DAW identifies Apollo in every saved project by them, so they never change after it | — |

---

## 3. Making a change without breaking this

For anyone changing Apollo: what each kind of change costs.

| Change | What it needs |
|---|---|
| **Add a parameter** | No schema change. Choose the default that **reproduces the sound before the parameter existed**, because that default is what every older preset and project will get. Filter 2 is off by default for this reason (5b) |
| **Change a parameter's default** | Changes every document that does not mention it — including every factory preset that does not set it, since those are stored as changes from the defaults (ADR-0065). The regression renders will fail; regenerate them only if the change was meant (Docs/TESTING.md §7) |
| **Rename or remove a parameter, or change what its value means** | A migration step in `State/StateSerialization.cpp`, a schema version increment, and a test. The only one so far is version 1 → 2, which renamed the filter parameters and carried their values across (ADR-0032) |
| **Change the document's layout** | The same: a migration step and a schema increment. Never reinterpret an existing key |
| **Raise the oldest version still read** (`minimumSupportedSchemaVersion`) | Drops every document older than it. Do not do it after 1.0 without a major version (VERSIONING.md §2) |

**The fixtures are the contract.** `Tests/State/CompatibilityFixtures.h` holds
documents earlier builds actually wrote, captured from the running application.
A later build must keep reading them, so a change that breaks one needs a
migration, never an edited fixture. When a version is released, add a fixture
it wrote.

*This was shown to work, not assumed (12d-3).* A loader that kept the user's
values for unmentioned parameters made the defaults test fail on every one of
them. A schema change shipped without a migration made the 0.1.0 preset fail to
load.

---

## 4. From 1.0

Apollo is at 1.0.0, and from here the rules in §3 are promises rather than
practice: VERSIONING.md §2 allowed breaking changes before 1.0, provided they
were recorded, and none was needed. The rules in §3 have been followed from
the start. The single schema change so far, the filter rename in Phase 5b, was
migrated rather than broken, and Phase 13a added thirteen parameters without
moving a single regression render. A preset or project from 0.1.0 or any
later build opens in 1.0.0 and sounds as it did, and the 0.1.0 preset frozen in
the suite is how every build shows it.

---

## 5. What this does not cover

- **The interface and the engine always come from the same build**, so the
  bridge protocol between them (UI_BINDINGS.md §15) never meets a different
  version and has no compatibility promise to keep. It is versioned anyway, so a
  mismatch is refused, not misread.
- **Wavetables loaded from files** are not referred to by presets at all yet
  (Docs/KNOWN-LIMITATIONS.md). When they are, a preset will need a rule for a
  file that has moved.
- **Other DAWs' behaviour.** What a host does with a refused state is the
  host's business. Apollo's part is to refuse it cleanly, keep playing, and say
  why.
