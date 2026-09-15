#include "Resources/WavetableFile.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace apollo::resources
{

juce::String describe (WavetableLoadResult result)
{
    switch (result)
    {
        case WavetableLoadResult::ok:
            return "The wavetable loaded.";
        case WavetableLoadResult::missing:
            return "That wavetable is not there any more.";
        case WavetableLoadResult::tooLarge:
            return "That file is far larger than a wavetable.";
        case WavetableLoadResult::unreadable:
            return "That file could not be read as audio.";
        case WavetableLoadResult::empty:
            return "That file has no audio in it.";
        case WavetableLoadResult::wrongFrameLength:
            return "A wavetable must be a whole number of 2048-sample frames.";
        case WavetableLoadResult::tooManyFrames:
            return "That wavetable has more frames than Apollo can hold.";
        case WavetableLoadResult::notFinite:
            return "That wavetable contains values that are not numbers.";
        case WavetableLoadResult::silent:
            return "That wavetable is silent, so it has no waveform.";
    }

    return "That file could not be read as a wavetable.";
}

WavetableLoadResult readWavetableFile (const juce::File& file, WavetableFrames& destination)
{
    destination.clear();

    if (! file.existsAsFile())
        return WavetableLoadResult::missing;

    // Asked before the file is opened. A size check is a stat call; handing a
    // video to a decoder is not.
    if (file.getSize() > static_cast<juce::int64> (maximumWavetableBytes))
        return WavetableLoadResult::tooLarge;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    // A reader rather than an exception: JUCE answers with null for anything it
    // cannot decode, which covers the renamed photograph and the truncated
    // header alike.
    const std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (file) };

    if (reader == nullptr || reader->numChannels == 0)
        return WavetableLoadResult::unreadable;

    const auto totalSamples = reader->lengthInSamples;

    if (totalSamples <= 0)
        return WavetableLoadResult::empty;

    if (totalSamples % wavetableFrameSamples != 0)
        return WavetableLoadResult::wrongFrameLength;

    const auto numFrames = static_cast<int> (totalSamples / wavetableFrameSamples);

    if (numFrames > maximumWavetableFrames)
        return WavetableLoadResult::tooManyFrames;

    // The left channel only. A wavetable is one waveform; a second channel is
    // either a copy of the first or a different recording, and neither is a
    // frame of this table.
    juce::AudioBuffer<float> audio (1, static_cast<int> (totalSamples));

    if (! reader->read (&audio, 0, static_cast<int> (totalSamples), 0, true, false))
        return WavetableLoadResult::unreadable;

    const auto* samples = audio.getReadPointer (0);

    auto loudest = 0.0;

    WavetableFrames frames (static_cast<std::size_t> (numFrames));

    for (int frame = 0; frame < numFrames; ++frame)
    {
        auto& destinationFrame = frames[static_cast<std::size_t> (frame)];
        destinationFrame.resize (static_cast<std::size_t> (wavetableFrameSamples));

        for (int i = 0; i < wavetableFrameSamples; ++i)
        {
            const auto value = static_cast<double> (samples[frame * wavetableFrameSamples + i]);

            // Checked here rather than after the whole file is converted: an
            // infinity would otherwise travel through the analyser and reach a
            // table, and a table of NaN is the one thing a voice cannot
            // recover from (CLAUDE.md §34.2).
            if (! std::isfinite (value))
                return WavetableLoadResult::notFinite;

            destinationFrame[static_cast<std::size_t> (i)] = value;
            loudest = std::max (loudest, std::abs (value));
        }
    }

    // A table of silence would normalise to nothing and play as nothing, which
    // is indistinguishable from the instrument being broken.
    if (loudest <= 0.0)
        return WavetableLoadResult::silent;

    destination = std::move (frames);

    return WavetableLoadResult::ok;
}

} // namespace apollo::resources
