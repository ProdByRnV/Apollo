#pragma once

/*
    What this computer remembers about Apollo's window (18b).

    One thing so far: the interface size. It belongs to the machine and the
    eyes in front of it rather than to the sound, so it is not in the project or
    the preset — a project opened on a laptop should not arrive at the size its
    author chose on a 4K monitor. Every Apollo on the computer shares it, in
    every host and in the standalone.

    Kept as a small file beside the user's presets, read when an editor opens
    and written when the size is chosen. Nothing here is touched by the audio
    thread.
*/

#include <juce_core/juce_core.h>

namespace apollo::ui
{

/** Where the settings live: the per-user application data folder JUCE
    resolves for each platform, under Apollo's own folder (CLAUDE.md §46). */
[[nodiscard]] juce::File defaultInterfaceSettingsFile();

/** @returns the interface size stored in @p file, in per cent, or the default
             when the file is missing, unreadable, larger than a settings file
             could be, not Apollo's, or names a size that is not offered. A
             settings file is never a reason for the window not to open. */
[[nodiscard]] int loadInterfaceScale (const juce::File& file);

/** Stores @p scalePercent in @p file, creating its folder. @returns false, and
    writes nothing, for a size that is not offered or a file that cannot be
    written. */
bool saveInterfaceScale (const juce::File& file, int scalePercent);

} // namespace apollo::ui
