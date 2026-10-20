# Known Limitations

What Apollo does not do, or has not been shown to do, as of version 0.1.0 in
Phase 12. Apollo has not been released.

Every entry is one of four kinds, kept apart because they mean different things:
something **not yet verified** may well work; something **specified but not
built** does not exist yet; something **decided against** was considered and
rejected for a stated reason; and a **distribution** limit is about getting
Apollo onto a machine rather than about Apollo.

[PLATFORMS.md](PLATFORMS.md) has the per-platform picture.
[PROJECT-STATE.md](../PROJECT-STATE.md) §6 has the engineering issue list this
page is drawn from.

---

## Not yet verified

| | What is known instead |
|---|---|
| **Apollo inside a DAW** | The packaged VST3 is loaded and driven through a VST3 host implementation in CI on every platform, which found and fixed four defects (ADR-0075). No DAW has been tried: that is Phase 10e-2, deferred at the developer's direction |
| **The interface on macOS and Linux** | Built, packaged and played through a host in CI, but never seen on a screen. Retina is likewise unverified |
| **A screen reader** | Every panel is a named region with a heading, every control has a name and a value, and keyboard focus is managed (ADR-0083). Nobody has listened to Narrator, VoiceOver or Orca read it |
| **Closing and reopening the editor many times inside a host** | Attaching and detaching the bridge is tested, and the window was opened and closed by hand on Windows. A headless harness cannot open an editor, so repetition inside a host is not tested (12a) |
| **Sound from the speaker on the end of a cable** | The standalone was shown to switch to the system's default output when headphones were plugged in (ADR-0082). That the speaker then played was not confirmed |

---

## Specified but not built

Features the product requirements ([PRD.md](../PRD.md)) or the project
instructions ([CLAUDE.md](../CLAUDE.md)) describe, which Apollo does not have.
None of them was dropped by a recorded decision except where one is cited. They
were simply never scheduled. Whether each belongs before a first release is the
developer's decision, and is open.

| Area | Not built | Spec |
|---|---|---|
| **Warp** | The whole warp engine: asymmetric sync, PWM, bend and mirror, a warp amount per oscillator, and warp as a modulation destination | PRD §8.1, §12 |
| **Macros** | Macro controls, as matrix sources or destinations, and in presets | PRD §17, CLAUDE.md §15 |
| **LFO tempo sync** | An LFO cannot follow the host's tempo. The conversion exists in the LFO code (`tempoSyncedRateHz`) but no parameter selects it, so it is unreachable | PRD §15.2, CLAUDE.md §14.2 |
| **Drawable LFO curves** | Seven fixed shapes; no user-drawn curve. Deferred because a drawn curve needs an editor and a place in the preset format (ADR-0034) | PRD §15.2 |
| **Filter types** | Low-pass, high-pass, band-pass and notch only; no comb or formant | PRD §13.1, CLAUDE.md §13 |
| **Filter parameters** | No per-filter mix and no key tracking. Routing is series or parallel; no split routing | PRD §13 |
| **Oscillators** | No per-oscillator start phase (voice phases are fixed, ADR-0017) and no interpolation quality modes | PRD §8.1, §8.2 |
| **Sub oscillator** | Sine only, an octave or two below; no triangle or square, no phase, no tracking mode | PRD §9 |
| **Noise** | White noise only; no pink noise, no transient samples, no loadable noise, no key tracking | PRD §10 |
| **Unison** | Voice count, detune and spread; no detune curve, phase randomisation or blend | PRD §11 |
| **Envelopes** | Shown as live traces, not edited by dragging them | PRD §15.1 |
| **Modulation matrix** | No effect parameters or filter drive as destinations, and no modulation of a routing's depth. Host automation as a source and an envelope follower are carried but unscheduled (ROADMAP "Carried forward") | PRD §16 |
| **Wavetables from files** | The engine reads, validates and plays a wavetable from a WAV file, replacing a slot while a note sounds (ADR-0066), but nothing in the interface can choose a file, and a preset cannot refer to one. A file chooser has to be native rather than a path sent across the bridge (CLAUDE.md §40) | PRD §33 |
| **Spectrum analyser** | Optional in the PRD and not built | PRD §30 |

---

## Decided against

| | Why |
|---|---|
| **The mouse wheel does not turn knobs** | The page is a scrolling column of 122 knobs, so a wheel that turned whatever it passed over would change the sound while the page was being scrolled. Drag, Shift-drag, the arrow keys and typed values cover the precision a wheel would add (ADR-0084) |
| **Arrow keys in the preset list do not load presets** | A load replaces the whole sound. A list that could not be crossed from the keyboard without losing an unsaved edit would be a list that could not be used (ADR-0083) |
| **An ambiguous preset name highlights nothing** | After a restart the list marks the preset the sound names, but only if exactly one preset has that name, author and category. A user copy saved under a factory name gives two candidates, and no highlight is better than the wrong one (ADR-0085) |
| **No SIMD intrinsics** | Measured: most of what vector code would have gained came from data layout alone, which Apollo took instead (ADR-0073, ADR-0074). It is also why ARM64 needed no porting |

---

## Distribution

| | |
|---|---|
| **No installer** | Each package is a folder to copy into place. Uninstalling is deleting it (Docs/BUILD.md §7) |
| **Not signed for distribution on macOS** | The macOS bundles are signed ad hoc, which is enough to run where they were built. A downloaded copy is refused as coming from an unidentified developer until it is signed with a Developer ID and notarised, which needs an Apple developer account the project does not have |
| **Unsigned on Windows** | A plugin is copied, not installed, so Windows does not object. The standalone may meet a SmartScreen prompt |
| **No Windows on ARM64 build** | No CI runner, and JUCE builds it as a separate `arm64ec` target (ADR-0079) |
| **The plugin's identity is not yet confirmed** | The manufacturer name and plugin codes (`ProdByRnV`, `Prnv`, `Apol`, `com.prodbyrnv.apollo`) were inferred. They can change now and become permanent at the first release, because a DAW identifies a plugin in a saved project by them (PROJECT-STATE §6, issue 11) |

---

## For contributors

| | |
|---|---|
| **Real-time safety is not enforced by a tool** | No allocation or lock is made on the audio thread by construction and by review. There is no detector, because the obvious one — replacing `operator new` — would not see `juce::HeapBlock`, which calls `malloc` directly and is what JUCE's audio buffers use (issue 6) |
| **Absolute CPU figures are machine-specific** | The development machine is a 15 W laptop that throttles, so performance work compares two builds in one sitting and trusts the ratio, not an absolute figure (PROJECT-STATE §8, ADR-0072) |
