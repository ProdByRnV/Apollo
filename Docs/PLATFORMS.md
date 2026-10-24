# Supported Platforms and Formats

What Apollo runs on, and how much of that has been seen working rather than
built. The second part matters as much as the first: a platform can be built,
tested and packaged in CI and still never have had a person look at it.

Status as of version 1.0.0, the release candidate (Phase 14). Apollo has not
been released yet.

---

## Formats

| Format | Status |
|---|---|
| **VST3** | Built on every platform below. The primary format |
| **Standalone application** | Built on every platform below |
| AU, CLAP, AAX | Not built. The engine does not depend on the plugin wrapper, so they are possible later (ARCHITECTURE.md §5) |

---

## Operating systems and processors

| Platform | Minimum | Processors | Package |
|---|---|---|---|
| **Windows** | Windows 10 or 11, with the WebView2 runtime (part of Microsoft Edge, already on almost every machine) | x86-64 | `Apollo-<version>-windows-x86_64.zip` |
| **macOS** | macOS 11 Big Sur | Apple silicon and Intel, in one universal binary | `Apollo-<version>-macos-universal.zip` |
| **Linux** | A desktop with X11, GTK 3 and WebKitGTK; tested on Ubuntu 24.04 | x86-64 and ARM64 | `Apollo-<version>-linux-x86_64.zip`, `Apollo-<version>-linux-arm64.zip` |

Each package is a folder to copy: the plugin, the standalone and an
`INSTALL.txt` that says where the plugin goes. There is no installer
(Docs/BUILD.md §7). On Windows nothing else needs installing — the Visual C++
runtime is linked in (ADR-0076). On Linux the libraries Apollo opens at run time
are listed in `INSTALL.txt`, with the packages that provide them on Debian and
Ubuntu, Fedora and Arch (ADR-0078).

**Not supported:** Windows on ARM64. It has never been built: there is no CI
runner for it, and JUCE targets it as `arm64ec`, which is a different target
rather than a rebuild (ADR-0079).

---

## How much has been verified

Every row of this table is something that was done, not something expected to
work.

| | Windows x86-64 | macOS (universal) | Linux x86-64 | Linux ARM64 |
|---|---|---|---|---|
| Builds with warnings as errors | ✅ CI | ✅ CI | ✅ CI | ✅ CI |
| The whole test suite passes | ✅ CI and by hand | ✅ CI | ✅ CI, plus sanitizers | ✅ CI |
| The packaged plugin loads and plays through a VST3 host | ✅ CI | ✅ CI | ✅ CI | ✅ CI |
| Regression renders match the references | ✅ | ✅ | ✅ | ✅ to 0.0005 dB |
| The interface has been seen on screen | ✅ | ❌ never | ❌ never | ❌ never |
| Played by hand with a MIDI keyboard | ✅ two keyboards | ❌ | ❌ | ❌ |
| Inside a DAW | ✅ FL Studio, by the developer | ❌ | ❌ | ❌ |

"Through a VST3 host" means the host harness (Docs/TESTING.md §8): the packaged
bundle loaded through JUCE's VST3 hosting implementation and driven as a DAW
drives it — automation, state save and recall, MIDI, bypass, sample-rate and
block-size changes, latency. It checks the plugin against the VST3 interfaces.
It is not a DAW.

**macOS and Linux have never been seen.** CI builds, signs, loads and plays them,
but has no screen. Whether the interface renders under WKWebView and WebKitGTK,
and how it looks on a Retina display, is unknown until somebody opens it on
those machines.

---

## Hosts

Apollo is written against the VST3 standard rather than against any one DAW
(CLAUDE.md §5.2), and is tested against the standard on every platform. One DAW
has been tried: the developer loaded it in **FL Studio** on Windows, played it
and made beats with the factory presets (2026-10-24, ADR-0109). No other DAW has
been tried.

Found by the host harness and fixed, so they will not be the first things a DAW
reports (ADR-0075): every integer parameter had been published as continuous; a
host asking for mono output was refused; and a note or sustain pedal released
while the host had Apollo bypassed never ended.

---

## Audio

| | |
|---|---|
| Sample rates | 44.1 to 192 kHz, measured: tuning, filters, oscillators and effects are validated across the range, and a running instrument survives repeated rate changes (12a) |
| Block sizes | Any. The same notes render identically at every block size tested, and a host that sends a block longer than it promised is handled rather than skipped (ADR-0080) |
| Output | Stereo or mono |
| Latency | None, unless the distortion is in the effects rack. Its 4× oversampling adds a few samples, which Apollo reports to the host so it can compensate |
| Audio and MIDI devices (standalone) | Whatever the operating system offers. The standalone opens every MIDI input it finds and follows the system's default output device until the user picks another (ADR-0081, ADR-0082) |
