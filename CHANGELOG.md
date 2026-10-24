# Changelog

What changed in each release of Apollo, for the people who use it. How it was
built and why is in [Docs/DECISIONS.md](https://github.com/ProdByRnV/Apollo/blob/main/Docs/DECISIONS.md).

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
[1.0.0 release notes](https://github.com/ProdByRnV/Apollo/releases/tag/v1.0.0).

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
