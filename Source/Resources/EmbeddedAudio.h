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

    LOSSLESS AND COMPACT: FLAC, 24 bits. A 256-frame wavetable is under two
    megabytes as text; most are a fraction of that. The audio is scaled to full
    scale before it is quantised, which uses all 24 bits and means a floating-
    point file louder than 1 cannot clip. Nothing that plays changes, because
    the table builder normalises every frame and a noise sample is normalised
    to its peak.

    UNTRUSTED ON THE WAY IN. A document is whatever a host or a file supplied,
    so the text is bounded before it is decoded, and decoding produces only a
    reader: what is read from it is held to exactly the rules a file on disk is
    (readWavetable, readNoiseSample), by the same code.

    NOT FOR THE AUDIO THREAD. Encoding and decoding allocate and take
    milliseconds; Apollo does both on its loader thread.
*/

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>
#include <vector>

namespace apollo::resources
{

/** The encoding's version, written beside the audio so that a later one can be
    told apart rather than misread. */
inline constexpr int embeddedAudioFormatVersion = 1;

/** Longest embedded audio text accepted, in characters. A ten-second stereo
    noise sample is about four million; this allows half as much again. */
inline constexpr int maximumEmbeddedAudioChars = 6 * 1024 * 1024;

/** @returns @p channels — equal lengths, one or two of them — at @p rate, as
    text: 24-bit FLAC in base64. Empty if there is nothing to encode or the
    encoder refused it.
*/
[[nodiscard]] juce::String encodeAudio (const std::vector<std::vector<double>>& channels, double rate);

/** @returns a reader over audio encoded by encodeAudio, or null if @p text is
    too long, not base64, or not FLAC. The caller validates what it reads.
*/
[[nodiscard]] std::unique_ptr<juce::AudioFormatReader> decodeAudio (const juce::String& text);

} // namespace apollo::resources
