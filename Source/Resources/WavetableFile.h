#pragma once

/*
    Reading somebody else's wavetable off a disk Apollo does not control.

    THE FORMAT IS A WAV OF SINGLE CYCLES, end to end, which is what every
    wavetable editor and every synthesiser that accepts them already uses. There
    was no case for inventing one: a private format would mean the user
    converting their library before Apollo could read any of it, and the reason
    to read files at all is that people already have files.

    A frame is **2048 samples**, the resolution PRD §8.2 names, and a table is
    1 to 256 of them. Stereo is read as its left channel: a wavetable is a
    waveform, and two waveforms interleaved is a recording rather than a table.

    EVERY FILE IS UNTRUSTED. It can be missing, locked, enormous, empty, silent,
    truncated mid-frame, a photograph with the wrong extension, or full of
    infinities. None of those may crash, and none may reach the audio thread;
    each comes back as a reason with a sentence a user can act on (CLAUDE.md
    §30, §33).

    WHAT THIS DOES NOT DO is band-limit. It produces frames of samples; turning
    those into the harmonics a mipmap is built from belongs to the analyser, and
    the mipmap itself to `WavetableBuilder`. Keeping the three apart is what
    lets the loader be tested against real files without a DSP engine, and the
    builder against spectra without a disk.
*/

#include <juce_audio_formats/juce_audio_formats.h>

#include <vector>

namespace apollo::resources
{

/** Samples in one frame of a wavetable file. */
inline constexpr int wavetableFrameSamples = 2048;

/** Most frames a table may hold (PRD §8.2). */
inline constexpr int maximumWavetableFrames = 256;

/** Largest wavetable file this reader will open, in bytes.

    256 frames of 2048 samples is half a million samples; at 32-bit stereo that
    is four megabytes, and this allows four times that so an unusual bit depth
    or a chunk of metadata does not push a legitimate file over. Checked before
    the file is opened, so pointing Apollo at a video costs a stat call rather
    than an attempt to decode it.
*/
inline constexpr int maximumWavetableBytes = 16 * 1024 * 1024;

/** The extension the browser and the file dialogs look for. */
inline constexpr const char* wavetableFileExtension = ".wav";

/** Why a wavetable file could not be used. */
enum class WavetableLoadResult
{
    ok,
    missing,          ///< Not there, or not a file.
    tooLarge,         ///< Past the size bound, refused without opening.
    unreadable,       ///< No decoder, or the decoder refused it.
    empty,            ///< No audio in it at all.
    wrongFrameLength, ///< Not a whole number of 2048-sample frames.
    tooManyFrames,    ///< More than a wavetable may hold.
    notFinite,        ///< Contains infinities or NaN.
    silent            ///< Every sample is zero, so it has no waveform.
};

/** @returns a sentence describing @p result, safe to show a user.

    Names no path and quotes no contents, for the same reason bridge errors do
    not (UI_BINDINGS.md §13).
*/
[[nodiscard]] juce::String describe (WavetableLoadResult result);

/** One wavetable read from a file: its frames, each `wavetableFrameSamples`
    long, in the order they appeared.
*/
using WavetableFrames = std::vector<std::vector<double>>;

/** Reads @p file into @p destination.

    @returns `ok`, in which case @p destination holds between 1 and
             `maximumWavetableFrames` frames; otherwise the reason, and
             @p destination is left empty.
*/
[[nodiscard]] WavetableLoadResult readWavetableFile (const juce::File& file,
                                                     WavetableFrames& destination);

} // namespace apollo::resources
