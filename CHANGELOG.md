# Changelog

What changed in each release of Apollo, for the people who use it. How it was
built and why is in [Docs/DECISIONS.md](https://github.com/ProdByRnV/Apollo/blob/main/Docs/DECISIONS.md).

---

## 1.2.0 — 2026-11-01

What the first musicians to play Apollo asked for: sounds you can share, more
wavetables and an easier way to choose them, octave 0 on the sub, a low cut on
the reverb, and macros with names. Everything 1.0 and 1.1 saved opens and sounds
the same.

### What is new

- **Share a sound.** **EXPORT…** in the Presets panel saves the sound you are
  playing as a file, to your Desktop or wherever you choose, ready to send.
  **IMPORT…** adds preset files someone sent you; choose several at once if you
  like. Any wavetable or sample of your own travels inside the file. Importing
  never replaces a preset you already have.
- **Sub oscillator at octave 0.** The sub's Octave now offers −2, −1 and 0. At
  0 it plays at the note's own pitch, a sine, triangle or square under the
  oscillators to thicken them.
- **A low cut on the reverb.** Turn up **Low Cut** in the Reverb panel and the
  low end stays out of the reverb, so a bass or a kick keeps its weight without
  the room turning it to mud. At its lowest it is off.
- **Name your macros.** Click a macro's name in the Macros panel and type —
  "Brightness", "Wobble", up to 16 characters — and press Enter. The name is
  shown wherever you pick the macro as a source, and is saved with your project
  and in your presets, so a sound you share says what its macros do. Your DAW
  still lists them as Macro 1 to Macro 4.
- **Every wavetable in one list, and five new ones.** Click the table's name on
  an oscillator to see all of them — Apollo's and your own library together —
  or step through them with **‹** and **›** while you hold a note. **ADD
  WAVETABLE…** plays one from anywhere on your computer. New: **Analog** (sine,
  triangle, square, saw), **Sync**, **Organ**, **Chime** and **Digital**. A
  preset that uses one still opens in Apollo 1.1.

Your sounds are unchanged: the new controls start where the sound was before
they existed, and DAW automation you drew in 1.1.0 plays as it did.

### What is in Apollo

- **Sound.** Two wavetable oscillators with up to sixteen unison voices each,
  nine built-in tables, five warp modes, and your own tables from a library or
  any file; a sub oscillator; a noise generator with white, pink, four attack
  transients, or a sample of your own.
- **Shaping.** Two filters — low-pass, high-pass, band-pass, notch, comb and
  formant — with drive, mix and key tracking, in series, in parallel, or split
  per source.
- **Movement.** Four envelopes you can shape by dragging, four LFOs with seven
  shapes and one you draw, tempo sync, four macros you can name, and a
  sixteen-slot modulation matrix. Drag a source onto a control to modulate it.
- **Effects.** Distortion, delay, reverb, noise gate, compressor and a
  seven-band equaliser with a live spectrum, in any order.
- **Presets.** Nine factory sounds and an init patch built in, and your own
  saved as `.rnv` files, which you can export to send to someone and import
  when someone sends you one. Your wavetables and samples travel inside the
  preset.
- **Playing.** Any MIDI keyboard or controller: MIDI Learn on every control,
  controller profiles you can save and share, MPE, and program changes. In the
  standalone, the computer keyboard and the piano strip as well.

### What you need

- **Windows** 10 or 11, 64-bit. The interface uses Microsoft's WebView2, which
  is already on almost every Windows machine.
- **macOS** 11 or later, Apple silicon or Intel.
- **Linux** on x86-64 or ARM64, with X11, GTK 3 and WebKitGTK.
- A host that loads VST3 plugins, or none: the standalone plays on its own.

### Installing

Download the zip for your system from the assets below. Each holds the plugin,
the standalone, and `INSTALL.txt`, which says where the plugin goes. There is no
installer: you copy the plugin into your VST3 folder, and uninstalling is
deleting it. The step-by-step guide, with what to do in FL Studio, Ableton Live
and Reaper, is [Getting started](https://github.com/ProdByRnV/Apollo#getting-started)
in the README.

**Updating from 1.0 or 1.1:** close your DAW and the standalone, then copy the
new plugin and standalone over the old ones. Your presets and projects carry
over, and sound the same.

`SHA256SUMS.txt` holds each download's checksum, for anyone who wants to check
what they received.

### Known limitations

- Tried in one DAW, FL Studio on Windows. Other hosts are expected to work, as
  the plugin follows the VST3 standard and is tested against it on every
  platform, but have not been tried.
- The interface has only been seen on Windows. On macOS and Linux it is built
  and tested, but nobody has looked at it yet. If the typing keys do nothing on
  a Mac, click once inside the window and try again, and please say so.
- The window takes several seconds to show the interface when it opens.
- macOS downloads are not signed with a Developer ID, so macOS refuses them as
  coming from an unidentified developer until you clear the download mark with
  the one Terminal line in the guide. The Windows standalone may show a
  SmartScreen prompt the first time.

The full list is [Docs/KNOWN-LIMITATIONS.md](https://github.com/ProdByRnV/Apollo/blob/main/Docs/KNOWN-LIMITATIONS.md).

---

## 1.1.0 — 2026-10-31

Apollo now plays without a MIDI keyboard, and a note that could stick no longer
does. Everything 1.0.0 and 1.0.1 saved opens and sounds the same.

### What is new

- **Play the standalone without a MIDI keyboard.** The computer keyboard plays
  Apollo: the middle row of letters, A to ;, are the white keys from middle C,
  and the row above, W to P, the black keys. Z and X move the octave; C and V
  play softer and harder. A piano strip at the bottom of the window shows what
  is playing and can be played with the mouse. Typing in a text box never plays
  notes. Inside a DAW nothing changes: use the DAW's own typing keyboard.

### What is fixed

- **Notes could stick after playing fast.** When every voice was busy and a
  key was let go within a few milliseconds of its note taking a voice from
  another, the note started and never stopped. It could happen with a MIDI
  keyboard or in a DAW, at full polyphony. Fixed: such a note now sounds
  briefly and ends.

### What is in Apollo

- **Sound.** Two wavetable oscillators with up to sixteen unison voices each,
  four built-in tables, five warp modes, and a table of your own on each; a sub
  oscillator; a noise generator with white, pink, four attack transients, or a
  sample of your own.
- **Shaping.** Two filters — low-pass, high-pass, band-pass, notch, comb and
  formant — with drive, mix and key tracking, in series, in parallel, or split
  per source.
- **Movement.** Four envelopes you can shape by dragging, four LFOs with seven
  shapes and one you draw, tempo sync, four macros, and a sixteen-slot
  modulation matrix. Drag a source onto a control to modulate it.
- **Effects.** Distortion, delay, reverb, noise gate, compressor and a
  seven-band equaliser with a live spectrum, in any order.
- **Presets.** Nine factory sounds and an init patch built in, and your own
  saved as `.rnv` files. Your wavetables and samples travel inside the preset.
- **Playing.** Any MIDI keyboard or controller: MIDI Learn on every control,
  controller profiles you can save and share, MPE, and program changes. In the
  standalone, the computer keyboard and the piano strip as well.

### What you need

- **Windows** 10 or 11, 64-bit. The interface uses Microsoft's WebView2, which
  is already on almost every Windows machine.
- **macOS** 11 or later, Apple silicon or Intel.
- **Linux** on x86-64 or ARM64, with X11, GTK 3 and WebKitGTK.
- A host that loads VST3 plugins, or none: the standalone plays on its own.

### Installing

Download the zip for your system from the assets below. Each holds the plugin,
the standalone, and `INSTALL.txt`, which says where the plugin goes. There is no
installer: you copy the plugin into your VST3 folder, and uninstalling is
deleting it. The step-by-step guide, with what to do in FL Studio, Ableton Live
and Reaper, is [Getting started](https://github.com/ProdByRnV/Apollo#getting-started)
in the README.

**Updating from 1.0.0 or 1.0.1:** close your DAW and the standalone, then copy
the new plugin and standalone over the old ones. Your presets and projects carry
over, and sound the same.

`SHA256SUMS.txt` holds each download's checksum, for anyone who wants to check
what they received.

### Known limitations

- Tried in one DAW, FL Studio on Windows. Other hosts are expected to work, as
  the plugin follows the VST3 standard and is tested against it on every
  platform, but have not been tried.
- The interface, the new keyboard and piano strip included, has only been seen
  on Windows. On macOS and Linux it is built and tested, but nobody has looked
  at it yet. If the typing keys do nothing on a Mac, click once inside the
  window and try again, and please say so.
- macOS downloads are not signed with a Developer ID, so macOS refuses them as
  coming from an unidentified developer until you clear the download mark with
  the one Terminal line in the guide. The Windows standalone may show a
  SmartScreen prompt the first time.

The full list is [Docs/KNOWN-LIMITATIONS.md](https://github.com/ProdByRnV/Apollo/blob/main/Docs/KNOWN-LIMITATIONS.md).

---

## 1.0.1 — 2026-10-24

The same Apollo as 1.0.0, in smaller downloads.

### What changed

- **The downloads hold only Apollo.** The 1.0.0 packages also carried about
  65 MB of the development files of JUCE, the framework Apollo is built with:
  folders called `bin`, `include` and `lib` beside Apollo's own. Nothing used
  them and nothing needs them. Each package now holds the plugin, the
  standalone, `INSTALL.txt` and `README.md`, and nothing else.
- **Nothing about the instrument changed.** It sounds and behaves exactly as
  1.0.0, and opens everything 1.0.0 saved.

If you installed 1.0.0, there is nothing you need to do: the plugin and the
standalone you copied are the same. To tidy up, delete the `bin`, `include`
and `lib` folders from wherever you unzipped 1.0.0.

### Installing

Download the zip for your system from the assets below. Each holds the plugin,
the standalone, and `INSTALL.txt`, which says where the plugin goes. The
step-by-step guide, with what to do in FL Studio, Ableton Live and Reaper, is
[Getting started](https://github.com/ProdByRnV/Apollo#getting-started) in the
README. `SHA256SUMS.txt` holds each download's checksum.

What Apollo is and does is in the
[1.0.0 section](#100--2026-10-24) below.

---

## 1.0.0 — 2026-10-24

The first release of Apollo: a wavetable synthesizer by ProdByRnV, as a VST3
plugin and a standalone application, for Windows, macOS and Linux.

### What is in it

- **Sound.** Two wavetable oscillators with up to sixteen unison voices each,
  four built-in tables, five warp modes, and a table of your own on each; a sub
  oscillator; a noise generator with white, pink, four attack transients, or a
  sample of your own.
- **Shaping.** Two filters — low-pass, high-pass, band-pass, notch, comb and
  formant — with drive, mix and key tracking, in series, in parallel, or split
  per source.
- **Movement.** Four envelopes you can shape by dragging, four LFOs with seven
  shapes and one you draw, tempo sync, four macros, and a sixteen-slot
  modulation matrix. Drag a source onto a control to modulate it.
- **Effects.** Distortion, delay, reverb, noise gate, compressor and a
  seven-band equaliser with a live spectrum, in any order.
- **Presets.** Nine factory sounds and an init patch built in, and your own
  saved as `.rnv` files. Your wavetables and samples travel inside the preset.
- **Playing.** Any MIDI keyboard or controller: MIDI Learn on every control,
  controller profiles you can save and share, MPE, and program changes.

### What you need

- **Windows** 10 or 11, 64-bit. The interface uses Microsoft's WebView2, which
  is already on almost every Windows machine.
- **macOS** 11 or later, Apple silicon or Intel.
- **Linux** on x86-64 or ARM64, with X11, GTK 3 and WebKitGTK.
- A host that loads VST3 plugins, or none: the standalone plays on its own.

### Installing

Download the zip for your system from the assets below. Each holds the plugin,
the standalone, and `INSTALL.txt`, which says where the plugin goes. There is no
installer: you copy the plugin into your VST3 folder, and uninstalling is
deleting it. The step-by-step guide, with what to do in FL Studio, Ableton Live
and Reaper, is [Getting started](https://github.com/ProdByRnV/Apollo#getting-started)
in the README.

`SHA256SUMS.txt` holds each download's checksum, for anyone who wants to check
what they received.

### Your work from here on

From 1.0.0, a preset or project you save keeps opening, and keeps sounding the
same, in every later release. Settings added later take the values that leave
your sound unchanged. The rules are in [Docs/COMPATIBILITY.md](https://github.com/ProdByRnV/Apollo/blob/main/Docs/COMPATIBILITY.md).

### Known limitations

- Tried in one DAW, FL Studio on Windows. Other hosts are expected to work, as
  the plugin follows the VST3 standard and is tested against it on every
  platform, but have not been tried.
- The interface has only been seen on Windows. On macOS and Linux it is built
  and tested, but nobody has looked at it yet.
- macOS downloads are not signed with a Developer ID, so macOS refuses them as
  coming from an unidentified developer until you clear the download mark with
  the one Terminal line in the guide. The Windows standalone may show a
  SmartScreen prompt the first time.

The full list is [Docs/KNOWN-LIMITATIONS.md](https://github.com/ProdByRnV/Apollo/blob/main/Docs/KNOWN-LIMITATIONS.md).
