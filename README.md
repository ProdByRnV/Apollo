# Apollo

A wavetable synthesizer by ProdByRnV, for Windows, macOS and Linux, as a VST3
plugin and a standalone application. Built in C++ with JUCE 8, with an interface
written in React and shown through the platform's own WebView.

> Apollo adapts to the user's hardware and host environment. No core feature
> depends on a specific DAW, operating system, audio interface, MIDI controller,
> driver or connector.

---

## Status

**Version 1.0.1.** Download it from the
[Releases page](https://github.com/ProdByRnV/Apollo/releases/latest); how to
install and use it is just below. What each release contains is in
[CHANGELOG.md](CHANGELOG.md).

It is a complete instrument: it plays, every one of its 305 parameters affects
the sound, and it is tested, measured and packaged on four platforms. It has
been played in FL Studio. What is not yet true is in
[Docs/KNOWN-LIMITATIONS.md](Docs/KNOWN-LIMITATIONS.md); the largest gaps are
that no other DAW has been tried and that its interface has only ever been seen
on Windows.

---

## Getting started

Apollo comes two ways in one download: a **VST3 plugin**, which you load inside a
DAW such as FL Studio, Ableton Live or Reaper, and a **standalone application**,
which runs on its own with no DAW at all. Use either, or both.

### 1. Download

Open the [latest release](https://github.com/ProdByRnV/Apollo/releases/latest)
and, under **Assets**, download the file for your computer. The number after
"Apollo-" is the version:

| Your computer | Download the file ending in |
|---|---|
| Windows 10 or 11 | `-windows-x86_64.zip` |
| Mac, Apple silicon or Intel (macOS 11 or later) | `-macos-universal.zip` |
| Linux on a PC | `-linux-x86_64.zip` |
| Linux on ARM64 | `-linux-arm64.zip` |

Inside each zip are three things: a **`VST3`** folder holding the plugin,
**`Apollo.vst3`**; a **`Standalone`** folder holding the application; and
**`INSTALL.txt`**, which has the same instructions as this page in more detail.

### 2. Install

<details open>
<summary><b>Windows</b></summary>

1. **Unblock the download.** Right-click the zip, choose **Properties**, tick
   **Unblock** at the bottom if it is there, and press **OK**. Windows marks
   files from the internet, and this stops it questioning every file inside.
2. **Extract it.** Right-click the zip and choose **Extract All…**.
3. **Install the plugin.** Open the extracted `VST3` folder and copy the whole
   **`Apollo.vst3`** folder (the folder, not a file inside it) into
   `C:\Program Files\Common Files\VST3`. Windows asks for administrator
   permission; allow it.
4. **Install the standalone.** Copy `Standalone\Apollo.exe` wherever you keep
   programs — your Desktop is fine — and double-click it to open Apollo. If
   Windows shows "Windows protected your PC", choose **More info**, then
   **Run anyway**. This appears because Apollo is not signed with a paid
   certificate, not because anything is wrong with it.

If Apollo's window opens empty, install **Microsoft Edge WebView2 Runtime** from
Microsoft's website and open Apollo again. Windows 11 already has it.

</details>

<details>
<summary><b>macOS</b></summary>

1. **Extract it.** Double-click the zip.
2. **Install the plugin.** In Finder, choose **Go → Go to Folder…**, type
   `~/Library/Audio/Plug-Ins` and press Return. If there is no folder called
   `VST3` there, make one (**File → New Folder**). Copy **`Apollo.vst3`** from
   the extracted `VST3` folder into `VST3`.
3. **Install the standalone.** Copy `Standalone/Apollo.app` into your
   **Applications** folder.
4. **Let macOS open them.** Apollo is not signed with an Apple Developer ID, so
   macOS blocks it the first time as coming from an unidentified developer.
   Open **Terminal** and paste this line, then press Return:

   ```sh
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Apollo.vst3 /Applications/Apollo.app
   ```

   It removes the "downloaded from the internet" mark from Apollo and nothing
   else. Alternatively, open the app once, then go to **System Settings →
   Privacy & Security** and press **Open Anyway**; that unblocks the app but not
   the plugin, which is why the Terminal line is the dependable way.

</details>

<details>
<summary><b>Linux</b></summary>

1. **Install the libraries Apollo's interface needs.** On Debian and Ubuntu:

   ```sh
   sudo apt install libx11-6 libxext6 libxcursor1 libxinerama1 libxrender1 \
                    libxrandr2 libfreetype6 libfontconfig1 libasound2 \
                    libgtk-3-0 libwebkit2gtk-4.1-0
   ```

   `INSTALL.txt` has the Fedora and Arch equivalents.
2. **Extract it:** `unzip Apollo-*-linux-*.zip -d Apollo`
3. **Install the plugin:** `mkdir -p ~/.vst3 && cp -r Apollo/VST3/Apollo.vst3 ~/.vst3/`
4. **Run the standalone:** `chmod +x Apollo/Standalone/Apollo && Apollo/Standalone/Apollo`

</details>

### 3. Open it in your DAW

DAWs look for new plugins when they scan, so after installing:

- **FL Studio:** **Options → Manage plugins → Find installed plugins**. Apollo
  appears under **Generators**; tick its star to make it a favourite. Add it from
  the **+** at the bottom of the Channel Rack.
- **Ableton Live:** **Settings → Plug-Ins**, turn on **Use VST3 Plug-In
  System Folders**, and press **Rescan**. Apollo appears in the browser under
  **Plug-Ins**; drag it onto a MIDI track.
- **Reaper:** **Options → Preferences → Plug-ins → VST**, press **Re-scan**,
  then **Insert → Virtual instrument on new track** and choose Apollo.
- **Any other DAW:** rescan your plugins and look for **Apollo by ProdByRnV**,
  an instrument.

Then play it from your MIDI keyboard or your DAW's piano roll.

### 4. Or use the standalone

Open the Apollo application. It finds any MIDI keyboard plugged in and plays
through your computer's speakers or headphones straight away. To choose a
different audio interface, sample rate or buffer size, press **Options** at the
top of the window and choose **Audio/MIDI Settings…**.

The standalone has no on-screen keyboard, so you need a MIDI keyboard or
controller to play it. Without one, use the plugin in a DAW.

### 5. Your first sounds

- **Choose a preset.** The **Presets** panel at the top lists Apollo's ten
  built-in sounds. Click one, then play. **INIT** returns to a plain starting
  sound.
- **Turn the knobs.** Drag a knob up or down. Hold **Shift** for fine
  adjustment, **double-click** to reset it, or **right-click** to reset it or
  type an exact value.
- **Make it move.** Drag a green source — an **LFO**, an **ENV**, a macro, or
  any source in the **Modulation Matrix** — onto a knob outlined in gold. A small
  tag appears on the knob; drag the tag up or down to set how far the source
  moves it.
- **Use your controller's knobs.** Press **MIDI LEARN**, click a control in
  Apollo, then move a knob or fader on your controller. They are now linked.
  Press **MIDI LEARN** again to finish.
- **Save your sound.** Press **SAVE…** in the Presets panel and give it a name.
  Your presets are kept in your own settings folder (`%APPDATA%\Apollo\Presets`
  on Windows), so updating Apollo never touches them.
- **Bring your own sounds.** Choose **USER** on an oscillator and press
  **OPEN FILE…** to play a wavetable of your own, or choose **SAMPLE** on the
  noise generator for a sample of your own.

### Updating and uninstalling

To **update**, close your DAW and the standalone, then repeat step 2 with the
new download, replacing the old files. Your presets and projects carry over.

To **uninstall**, delete `Apollo.vst3` from your VST3 folder and delete the
standalone application.

### If something goes wrong

| What you see | What to do |
|---|---|
| Your DAW does not list Apollo | Check that the folder you copied is called `Apollo.vst3` and sits directly in the VST3 folder, then rescan. A 32-bit DAW cannot load Apollo |
| Apollo's window is blank (Windows) | Install Microsoft Edge WebView2 Runtime, then reopen Apollo |
| macOS says Apollo is damaged or cannot be opened | Run the Terminal line in step 2 |
| The standalone makes no sound | **Options → Audio/MIDI Settings…**: choose your output device, and tick your keyboard under MIDI inputs |
| An update did not change anything | Close the DAW before replacing `Apollo.vst3`; a DAW with Apollo open keeps the old copy in use |

Anything else: open an issue on this repository and say what you did and what
you saw.

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
and four macros, each with a range and a curve. Drag a source onto a control to
modulate it, and drag the tag that appears to set how far.

**Your own content.** Each oscillator can play a wavetable of your own, and the
noise generator a sample of your own, played once or looped. Choose them from
your library — folders Apollo scans, with subfolders as banks — or from anywhere
through the system's file dialog. Whatever you load travels inside the project
or preset, as lossless audio, so moving or deleting the file changes nothing.

**Effects.** A rack of six slots in any order: distortion (soft, hard and diode
curves, 4× oversampled), a stereo delay with ping-pong, tempo sync, its own
time on each side if you want one, and a wobble on the repeats, a
room-and-hall reverb, a noise gate, an RMS compressor, and a seven-band
parametric equaliser that draws its own response curve, with the live spectrum
of the sound going in and coming out behind it.

**Presets.** Nine factory sounds and an init patch, built into the plugin, and
your own presets saved as `.rnv` files and browsed by name, category and bank.

**Playing it.** Any MIDI keyboard or controller. MIDI Learn on any control,
a table of every assignment to edit, controller profiles you can save, export
and import, MPE, program changes that choose a preset, and a standalone
that opens the keyboards it finds and follows the system's output device.

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
