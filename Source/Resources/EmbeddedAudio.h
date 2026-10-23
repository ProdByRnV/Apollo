#pragma once

/*
    Audio carried inside a document: a project or a preset (Phase 13g,
    ADR-0104).

    A patch that uses the user's own wavetable or noise sample carries that
    audio with it, rather than a path to it. A path is only as good as the disk
    it names: it breaks when the file is moved, renamed or deleted, when the
    project is opened on another computer, and when a preset is sent to someone
    else. Carried audio breaks in none of those cases, which is what PRD §33
    asks for and what every synthesiser that loads user tables ends up doing.

    LOSSLESS AT 24 BITS, IN A FORMAT OF APOLLO'S OWN. Each sample is quantised
    to a 24-bit integer — after the audio is scaled to full scale, which uses
    every bit and means a floating-point file louder than 1 cannot clip. Each
    channel is then predicted from its own previous samples, by whichever of
    "nothing", "the last sample" and "the line through the last two" leaves
    the smallest residuals; the residuals are written as zigzag variable-length
    integers and compressed with zlib. A smooth wavetable shrinks to a fraction
    of its size; noise, which nothing compresses, stays about the size of
    24-bit PCM. Nothing that plays changes, because the table builder
    normalises every frame and a noise sample is normalised to its peak.

    The first build of this used FLAC, through JUCE's bundled libFLAC. The
    sanitizer build found undefined behaviour in libFLAC's encoder on the path
    every load takes, and no setting JUCE exposes avoids it — so the format was
    replaced before anything shipped. This one is small enough to read in full
    and is tested against hostile input.

        "APAU"  magic
        u8      version (2)
        u8      channels, 1 or 2
        u32     sample rate, little-endian
        u32     frames per channel, little-endian
        u8 * channels   prediction order of each channel, 0 to 2
        ...     zlib: each channel's residuals in turn, zigzag LEB128

    then the whole as base64.

    UNTRUSTED ON THE WAY IN. A document is whatever a host or a file supplied.
    The text is bounded before it is decoded, the header before anything is
    inflated, the inflated stream as it is read, and every integer as it is
    rebuilt. Decoding produces only a reader: what is read from it is held to
    exactly the rules a file on disk is (readWavetable, readNoiseSample), by the
    same code.

    NOT FOR THE AUDIO THREAD. Encoding and decoding allocate and take
    milliseconds; Apollo does both on its loader thread.
*/

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>
#include <vector>

namespace apollo::resources
{

/** The encoding's version, written in the header and beside the audio, so a
    later one can be told apart rather than misread. 1 was FLAC, and is
    refused. */
inline constexpr int embeddedAudioFormatVersion = 2;

/** Longest embedded audio text accepted, in characters. A ten-second stereo
    noise sample is about four million; this allows half as much again. */
inline constexpr int maximumEmbeddedAudioChars = 6 * 1024 * 1024;

/** Most frames per channel a document's audio may declare: a 256-frame
    wavetable, which is more than ten seconds at 48 kHz. */
inline constexpr int maximumEmbeddedAudioFrames = 256 * 2048;

/** @returns @p channels — equal lengths, one or two of them — at @p rate, as
    text. Empty if there is nothing to encode.
*/
[[nodiscard]] juce::String encodeAudio (const std::vector<std::vector<double>>& channels, double rate);

/** @returns a reader over audio encoded by encodeAudio, or null if @p text is
    too long, not base64, not this format, a version this build does not read,
    or damaged anywhere. The caller validates what it reads.
*/
[[nodiscard]] std::unique_ptr<juce::AudioFormatReader> decodeAudio (const juce::String& text);

} // namespace apollo::resources
