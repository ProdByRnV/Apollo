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
were simply never scheduled. **The developer has decided they all belong in the
first release**, so they are now Phase 13 of the roadmap, and each row leaves
this table when its sub-phase lands (ADR-0089).

| Area | Not built | Spec |
|---|---|---|
| **Macros** | A macro is a matrix source but not itself a destination, so the matrix cannot move a macro's knob. *Built in 13e-3: four macros, learnable, each with a range and a curve, stored in presets* (ADR-0098) | CLAUDE.md §15 |
| **Drawable LFO curves** | Seven fixed shapes; no user-drawn curve. Deferred because a drawn curve needs an editor and a place in the preset format (ADR-0034) | PRD §15.2 |
| **Oscillators** | No interpolation quality modes. *Built in 13a: oscillator 1 tuning and a start phase per oscillator* | PRD §8.2 |
| **Noise** | No loadable noise samples (13g). *Built in 13d: pink noise, key tracking, and four built-in transients* | PRD §10 |
| **Envelopes** | Shown as live traces, not edited by dragging them. By default a stolen voice's note starts its envelopes at the level the last note left, so a slow attack is skipped when voices run out; Retrigger set to RESTART gives it the whole attack (13e-2, ADR-0097) | PRD §15.1 |
| **Modulation matrix** | Six effect controls are destinations, not every one: the rest are not smoothed per sample, so a routing would step them. *Built in 13e-4 and 13e-5: each filter's drive, mix and key tracking and six effect controls as destinations, and a via source on every slot that modulates its depth*. Host automation as a source and an envelope follower are carried but unscheduled (ROADMAP "Carried forward") | PRD §16 |
| **Wavetables from files** | The engine reads, validates and plays a wavetable from a WAV file, replacing a slot while a note sounds (ADR-0066), but nothing in the interface can choose a file, and a preset cannot refer to one. A file chooser has to be native rather than a path sent across the bridge (CLAUDE.md §40) | PRD §33 |
| **Spectrum analyser** | Only behind the equaliser's curve, and only while the equaliser is in the rack: there is no analyser of the output on its own. *Built: the equaliser's input and output spectra (ADR-0101)* | PRD §30 |

---

## Decided against

| | Why |
|---|---|
| **The mouse wheel does not turn knobs** | The page is a scrolling column of 122 knobs, so a wheel that turned whatever it passed over would change the sound while the page was being scrolled. Drag, Shift-drag, the arrow keys and typed values cover the precision a wheel would add (ADR-0084) |
| **Arrow keys in the preset list do not load presets** | A load replaces the whole sound. A list that could not be crossed from the keyboard without losing an unsaved edit would be a list that could not be used (ADR-0083) |
| **An ambiguous preset name highlights nothing** | After a restart the list marks the preset the sound names, but only if exactly one preset has that name, author and category. A user copy saved under a factory name gives two candidates, and no highlight is better than the wrong one (ADR-0085) |
| **The warp is not oversampled** | A warp reshapes where the cycle is read, and like FM that spreads the waveform's spectrum without limit. Apollo treats it three ways — a mip level chosen for the warp's fastest read with a margin, polyBLEP and polyBLAMP corrections at its jumps and corners, and DC removal — which took the worst mode at 1 kHz from −25 to −40 dBc. What remains is above the plain oscillator's −60 dBc budget: on the brightest saw at amount 0.6, sync and PWM measure about −54 dBc at middle C and −35 dBc at C7; bend stays under −60 to C6. Oversampling would close the gap and would cost a warped stack three to five times an unwarped one, which the worst patch the controls allow cannot afford (ADR-0090). The measured figures are pinned by the test suite, so a change that makes warp alias more fails the build |
| **A comb filter retuned by a large jump takes a moment to get there** | The comb's delay may move by at most one sample per sample. Its fractional period is read through an allpass, which keeps the comb's loudness the same on every note, and an allpass whose input jumps about inside a feedback loop can gain energy: without the limit, a comb swept end to end every 64 samples reached 3.8 million. So a jump from the comb's lowest tuning to its highest takes up to 50 ms at 48 kHz. A sweep of a few octaves, or anything a modulation source does around a note, never meets the limit and follows the 3 ms glide every retuning has (ADR-0091) |
| **A routing switched while a note sounds is a step** | Moving the filter routing, or a source's input under split, changes which filter a sound passes through between one sample and the next, and a filter it has just reached starts from where that filter was. A crossfade would run both arrangements for its length on every voice, which is two more filter slots per voice for a selector most people set once. The filter a source leaves keeps running, so its resonance rings out rather than stopping (ADR-0093) |
| **No SIMD intrinsics** | Measured: most of what vector code would have gained came from data layout alone, which Apollo took instead (ADR-0073, ADR-0074). It is also why ARM64 needed no porting |

---

## Distribution

| | |
|---|---|
| **No installer** | Each package is a folder to copy into place. Uninstalling is deleting it (Docs/BUILD.md §7) |
| **Not signed for distribution on macOS** | The macOS bundles are signed ad hoc, which is enough to run where they were built. A downloaded copy is refused as coming from an unidentified developer until it is signed with a Developer ID and notarised, which needs an Apple developer account the project does not have |
| **Unsigned on Windows** | A plugin is copied, not installed, so Windows does not object. The standalone may meet a SmartScreen prompt |
| **No Windows on ARM64 build** | No CI runner, and JUCE builds it as a separate `arm64ec` target (ADR-0079) |

---

## For contributors

| | |
|---|---|
| **Real-time safety is not enforced by a tool** | No allocation or lock is made on the audio thread by construction and by review. There is no detector, because the obvious one — replacing `operator new` — would not see `juce::HeapBlock`, which calls `malloc` directly and is what JUCE's audio buffers use (issue 6) |
| **Absolute CPU figures are machine-specific** | The development machine is a 15 W laptop that throttles, so performance work compares two builds in one sitting and trusts the ratio, not an absolute figure (PROJECT-STATE §8, ADR-0072) |
