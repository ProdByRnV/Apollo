# Changelog

What changed in each release of Apollo, for the people who use it. How it was
built and why is in [Docs/DECISIONS.md](https://github.com/ProdByRnV/Apollo/blob/main/Docs/DECISIONS.md).

---

## 1.4.0 — 2026-11-08

Undo; a size for the interface; three new ways to modulate — by loudness, a
macro by anything, and each stage of an envelope bent on its own; drawn LFO
shapes kept to use again; an analyser of the output; one command that installs
Apollo on Windows; and three things that could click at small buffer sizes,
fixed. Everything 1.0 to 1.3 saved opens and sounds the same.

### What is new

- **Undo and redo.** **Ctrl+Z** (**⌘Z** on a Mac) takes back the last thing you
  did in Apollo — a knob, a routing, a drawn LFO curve, a macro's name, a
  wavetable or noise sample, or a preset you loaded — and **Ctrl+Shift+Z** or
  **Ctrl+Y** puts it back. **UNDO** and **REDO** at the top of the window do the
  same, and their tooltips name what they would undo. Up to 100 steps, kept
  while the window is closed; opening a project starts afresh. MIDI
  assignments are not undone.
- **Make the interface larger or smaller.** **SIZE** at the top of the window
  draws everything from 75 % to 200 %: smaller to fit more on a laptop, larger
  on a big monitor. In a DAW the plugin's window grows or shrinks with it. If
  the window is too small for the size you chose, Apollo draws it as large as
  fits and says so when you point at **SIZE**. Apollo remembers the size on
  your computer; it is not saved in projects or presets.
- **Modulate by loudness: the envelope follower.** **FOLLOW**, a new source at
  the end of the row of sources and in the matrix, is how loud Apollo is,
  measured after the voices and before the effects. Drag it onto any control:
  onto the reverb's or delay's mix at a negative depth, and the effect ducks
  while you play and opens up in the gaps; onto a filter's cutoff, and the sound
  brightens as it gets louder. **Attack**, **Release** and **Gain** are in the
  new **Follower** panel on the Modulation tab, with a live trace of what it is
  doing.
- **Modulate a macro.** Drag a source onto a macro's **Amount** knob, and it
  moves the macro, and the macro moves everything it is routed to: one LFO on
  Macro 1 sweeps the filter, the wavetable and the reverb together. The macro's
  range and curve still hold, so it never goes past the limits you set. The
  matrix lists the macros as destinations by the names you gave them.
- **Keep the LFO shapes you draw.** On an LFO set to **DRAWN**, **Save…** keeps
  the shape under a name, and **Load a shape…** puts it into any LFO in any
  patch. Six shapes of Apollo's own come with it: Pluck, Swell, Pump, Bounce,
  Stairs and Trance Gate. **Folder** opens where your shapes are kept, to back
  them up, share them or add someone else's. Undo takes a loaded shape back.
- **Bend each stage of an envelope on its own.** The attack, decay and release
  each have a bend now: drag a stage's diamond in the envelope's picture, or
  turn **A Bend**, **D Bend** or **R Bend**, and only that stage changes — a
  snappy attack with a slow, soft release, say. **Curve** still bends all three
  together, and the bends add to it. Your sounds are unchanged until you bend
  something.
- **See where the sound's energy is.** The **Analyser** on the **MIDI & Output**
  tab shows the spectrum of everything Apollo plays, live, with a slowly falling
  line of recent peaks. Point at it to read the frequency, the nearest note and
  the level there: to find a bass's fundamental, a harsh resonance, or what a
  filter is doing. Before, a spectrum was only drawn behind the equaliser, and
  only while the equaliser was in use.
- **One command installs Apollo on Windows.** Open PowerShell, paste
  `irm https://raw.githubusercontent.com/ProdByRnV/Apollo/main/Tools/get-apollo.ps1 | iex`
  and press Enter. It downloads Apollo, checks it, installs the application and
  the plugin, and opens Apollo, with no "Windows protected your PC" warning.
  Windows asks once for permission to put the plugin where every DAW looks.
  Run it again to update.
- **The Mac and Linux commands on Apollo's page are whole again.** Since 1.3.0
  they showed on GitHub cut off before `| bash`, so copying them did nothing.
- **Open your presets folder.** **OPEN FOLDER** beside **PRESETS** opens the
  folder your presets are saved in, to back them up or add presets someone made.
  Press **RESCAN** to see what you added.

### What is fixed

Three things that could make a small DAW buffer click, found by a new check
that watches everything Apollo does while it makes sound:

- **SysEx in a MIDI track.** Apollo ignores SysEx, but reserved memory for it
  first, in the middle of making sound. It no longer does.
- **Adding or removing an effect while playing.** Telling the DAW that the
  effects' delay had changed briefly paused the sound engine on Mac and Linux.
  The DAW is now told without pausing it.
- **DAW automation with Apollo's window open.** Each automated value reserved
  memory in the middle of making sound. It no longer does.

And one on a Mac:

- **Choosing an audio input in the standalone.** macOS closes an application
  that opens an input without saying why it wants one, and Apollo did not say.
  macOS now asks you first. Apollo needs no input to play, and opens none
  unless you choose one in **Options → Audio/MIDI Settings…**.

### Two things that move

Projects, presets and MIDI assignments from earlier versions open and sound the
same. The exception is DAW automation that **switches what a modulation slot
uses**:

- **its source or via**: the follower made that list one longer, so a lane
  recorded with 1.3.0 or earlier picks a different source from Key Track
  onwards;
- **its destination**: the macros made that list four longer, so a lane
  recorded with 1.3.0 or earlier picks a different destination from Osc 1
  Position onwards.

Automating which source or destination a slot uses is rare; if you did, choose
it again in the matrix. Automation of a slot's depth, and of every other
control, is unaffected.

### What is in Apollo

- **Sound.** Two wavetable oscillators with up to sixteen unison voices each,
  nine built-in tables, five warp modes, and your own tables from a library or
  any file; a sub oscillator; a noise generator with white, pink, four attack
  transients, or a sample of your own.
- **Shaping.** Two filters — low-pass, high-pass, band-pass, notch, comb and
  formant — with drive, mix and key tracking, in series, in parallel, or split
  per source.
- **Movement.** Four envelopes you can shape by dragging, with a bend for each
  stage; four LFOs with seven shapes and one you draw, kept in a library of
  shapes; tempo sync; an envelope follower; four macros you can name and
  modulate; and a sixteen-slot modulation matrix. Drag a source onto a control
  to modulate it.
- **Effects.** Distortion, delay, reverb, noise gate, compressor and a
  seven-band equaliser with a live spectrum, in any order.
- **Presets.** Nine factory sounds and an init patch built in, and your own
  saved as `.rnv` files, which you can export to send to someone and import
  when someone sends you one. Your wavetables and samples travel inside the
  preset.
- **Playing.** Any MIDI keyboard or controller: MIDI Learn on every control,
  controller profiles you can save and share, MPE, and program changes. In the
  standalone, the computer keyboard and the piano strip as well.
- **Working.** Undo and redo for everything you do in the window, an interface
  from 75 % to 200 %, an analyser of the output, and a scope for every source.

### What you need

- **Windows** 10 or 11, 64-bit. The interface uses Microsoft's WebView2, which
  is already on almost every Windows machine.
- **macOS** 11 or later, Apple silicon or Intel.
- **Linux** on x86-64 or ARM64, with X11, GTK 3 and WebKitGTK.
- A host that loads VST3 plugins, or none: the standalone plays on its own.
  **Logic Pro and GarageBand cannot load Apollo**, as they take only Audio
  Units.

### Installing

Open **PowerShell** on Windows, or **Terminal** on a Mac or Linux, paste the
line for your system and press Enter. It downloads Apollo, checks it, installs
the application and the plugin, and opens Apollo — past "Windows protected
your PC" on Windows and the "unidentified developer" block on a Mac:

- Windows: `irm https://raw.githubusercontent.com/ProdByRnV/Apollo/main/Tools/get-apollo.ps1 | iex`
- Mac: `curl -fsSL https://raw.githubusercontent.com/ProdByRnV/Apollo/main/Tools/get-apollo.sh | bash`
- Linux: `wget -qO- https://raw.githubusercontent.com/ProdByRnV/Apollo/main/Tools/get-apollo.sh | bash`

On Windows it asks once for permission to put the plugin in the folder every
DAW looks in.

**By hand**, download the zip for your system from the assets below. It holds
the application, the plugin in a `VST3` folder, and `INSTALL.txt`, which says
where the plugin goes. There is no installer: you copy the plugin into your
VST3 folder, and uninstalling is deleting it. The step-by-step guide, with what
to do in FL Studio, Ableton Live and Reaper, is
[Getting started](https://github.com/ProdByRnV/Apollo#getting-started) in the
README.

**Updating:** close your DAW and the standalone, then run the same command
again, or copy the new plugin and standalone over the old ones. Your presets and
projects carry over, and sound the same, apart from the automation described
under "Two things that move".

`SHA256SUMS.txt` holds each download's checksum, for anyone who wants to check
what they received.

### Known limitations

- Tried in one DAW, FL Studio on Windows. Other hosts are expected to work, as
  the plugin follows the VST3 standard and is tested against it on every
  platform, but have not been tried.
- New in this version and not yet tried inside a DAW: the plugin's window
  following **SIZE**, and **Ctrl+Z** — a DAW may take that key for its own undo.
  The **UNDO** and **REDO** buttons work either way.
- The interface has only been seen on Windows. On macOS and Linux it is built
  and tested, but nobody has looked at it yet. If the typing keys do nothing on
  a Mac, click once inside the window and try again, and please say so.
- The window takes several seconds to show the interface when it opens.
- Neither the macOS nor the Windows downloads are signed with a paid
  certificate. The one command gets past both; a zip downloaded with a browser
  needs the one Terminal line in the guide on a Mac, and may show a SmartScreen
  prompt on Windows.

The full list is [Docs/KNOWN-LIMITATIONS.md](https://github.com/ProdByRnV/Apollo/blob/main/Docs/KNOWN-LIMITATIONS.md).

---

## 1.3.0 — 2026-11-03

A new layout, in tabs, from the first feedback; the standalone opening to fill
the screen; and one command that installs Apollo on a Mac or on Linux.
Everything 1.0, 1.1 and 1.2 saved opens and sounds the same.

### What is new

- **Tabs instead of one long page.** The instrument is in five tabs under the
  presets: **Oscillators**, **Filters**, **Modulation**, **Effects**, and **MIDI &
  Output**. The tabs stay at the top of the window as you scroll.
- **Modulate from any tab.** The modulation sources sit in one row under the
  tabs. Drag one onto a knob; to reach a knob on another tab, hold the source
  over that tab for a moment and it opens, or click the source, click the tab,
  then click the knob.
- **Step through presets.** **‹** and **›** beside the sound's name play the
  previous or next preset, through the list as it is filtered — choose a
  category and step through just that. **HIDE LIST** folds the presets panel to
  one line.
- **Effects that are off fold away.** An effect that is not in the FX rack is
  folded to its name, so the Effects tab shows the ones you are using. **SHOW
  CONTROLS** opens one anyway.
- **Panels no longer resize.** The drag handle in each panel's corner is gone,
  so the layout cannot be pulled out of shape by accident.
- **The standalone opens filling the screen.** **FULLSCREEN** at the top makes
  it smaller, and bigger again. The plugin opens in your DAW as before.
- **One command installs Apollo on a Mac or on Linux.** Paste it into Terminal
  and it downloads Apollo, checks it, installs the application and the plugin,
  gets them past the Mac's "unidentified developer" block, and opens Apollo:

  - Mac: `curl -fsSL https://raw.githubusercontent.com/ProdByRnV/Apollo/main/Tools/get-apollo.sh | bash`
  - Linux: `wget -qO- https://raw.githubusercontent.com/ProdByRnV/Apollo/main/Tools/get-apollo.sh | bash`

  The application is also at the top of the Mac and Linux downloads now,
  rather than in a folder of its own.

### What is fixed

- An **Unassigned** panel with a "Sub Octave Up" knob appeared at the bottom of
  the page in 1.2.0. It was a duplicate of the sub's octave 0 and is gone.

Your sounds are unchanged: this release changes only the interface and how it
is installed.

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

**On a Mac or on Linux**, open Terminal, paste the line for your system and
press Return. It downloads Apollo, checks it, installs the application and the
plugin, and opens Apollo — on a Mac, past the "unidentified developer" block:

- Mac: `curl -fsSL https://raw.githubusercontent.com/ProdByRnV/Apollo/main/Tools/get-apollo.sh | bash`
- Linux: `wget -qO- https://raw.githubusercontent.com/ProdByRnV/Apollo/main/Tools/get-apollo.sh | bash`

**On Windows**, or to do it by hand anywhere, download the zip for your system
from the assets below. It holds the application, the plugin in a `VST3`
folder, and `INSTALL.txt`, which says where the plugin goes. There is no
installer: you copy the plugin into your VST3 folder, and uninstalling is
deleting it. The step-by-step guide, with what to do in FL Studio, Ableton Live
and Reaper, is [Getting started](https://github.com/ProdByRnV/Apollo#getting-started)
in the README.

**Updating:** on a Mac or Linux, run the same command again. On Windows, close
your DAW and the standalone, then copy the new plugin and standalone over the
old ones. Your presets and projects carry over, and sound the same.

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
- macOS downloads are not signed with a Developer ID. The one command above
  gets past that; a zip downloaded with a browser needs the one Terminal line in
  the guide. The Windows standalone may show a SmartScreen prompt the first time.

The full list is [Docs/KNOWN-LIMITATIONS.md](https://github.com/ProdByRnV/Apollo/blob/main/Docs/KNOWN-LIMITATIONS.md).

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
