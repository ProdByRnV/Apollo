#pragma once

/*
    Reading a noise sample off a disk Apollo does not control (Phase 13g,
    PRD §10).

    A noise sample is any short recording: a breath, a crackle, a strike. It is
    played by the noise source from each note's start, once or round and round,
    the way the built-in transients are (NoiseSource.h), so it is turned into
    the same thing they are: stereo at 48 kHz, ready for makeTransient's mipmap.

    WHAT IS ACCEPTED. Anything JUCE's basic formats decode — WAV, AIFF, FLAC,
    Ogg Vorbis — at any rate, up to ten seconds. Mono plays the same on both
    sides; past two channels, the first two are used. A recording at another
    rate is converted to 48 kHz by a band-limited resampler (Resampler.h).

    Ten seconds, because a noise sample is played per note and kept in memory
    six times over at falling bandwidths, and because it is carried inside
    every patch that uses it (ADR-0104). A minute of rain is a recording, not a
    noise source.

    EVERY FILE IS UNTRUSTED, exactly as a wavetable is (WavetableFile.h): the
    size is checked before anything is opened, the length before anything is
    decoded, and infinities and silence are refused by name.
*/

#include <juce_audio_formats/juce_audio_formats.h>

#include <vector>

namespace apollo::resources
{

/** The rate a noise sample is stored and built at: the transients' own. */
inline constexpr double noiseSampleRate = 48000.0;

/** Longest noise sample accepted, in seconds. */
inline constexpr double maximumNoiseSampleSeconds = 10.0;

/** Largest noise sample file this reader will open, in bytes: ten seconds of
    32-bit stereo at 384 kHz is thirty megabytes, so this leaves room for
    metadata without admitting a video. */
inline constexpr juce::int64 maximumNoiseSampleBytes = 64 * 1024 * 1024;

/** Why a noise sample could not be used. */
enum class NoiseSampleLoadResult
{
    ok,
    missing,    ///< Not there, or not a file.
    tooLarge,   ///< Past the size bound, refused without opening.
    unreadable, ///< No decoder, or the decoder refused it.
    empty,      ///< No audio in it at all.
    tooLong,    ///< Longer than a noise sample may be.
    notFinite,  ///< Contains infinities or NaN.
    silent      ///< Every sample is zero.
};

/** @returns a sentence describing @p result, safe to show a user: no path, no
    contents (UI_BINDINGS.md §13). */
[[nodiscard]] juce::String describe (NoiseSampleLoadResult result);

/** A noise sample ready to build: both channels, at `noiseSampleRate`. */
struct NoiseSampleAudio
{
    std::vector<double> left;
    std::vector<double> right;

    [[nodiscard]] double seconds() const noexcept
    {
        return static_cast<double> (left.size()) / noiseSampleRate;
    }
};

/** Reads from @p reader, wherever its bytes come from — a file, or audio
    embedded in a document — and holds it to the same rules either way.

    @returns `ok`, with @p destination holding audio; otherwise the reason, with
             @p destination empty.
*/
[[nodiscard]] NoiseSampleLoadResult readNoiseSample (juce::AudioFormatReader& reader,
                                                     NoiseSampleAudio& destination);

/** Reads @p file into @p destination: the size bound, then readNoiseSample. */
[[nodiscard]] NoiseSampleLoadResult readNoiseSampleFile (const juce::File& file,
                                                         NoiseSampleAudio& destination);

} // namespace apollo::resources
