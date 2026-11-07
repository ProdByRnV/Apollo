#pragma once

/*
    A library of drawn LFO shapes, to use in another patch (Phase 18g).

    A drawn shape belongs to the patch it was drawn in (ADR-0102). This is how
    one leaves it: saved under a name, it is a small file in a folder of its
    own, and any LFO in any patch can load it. Apollo ships a few of its own,
    compiled in, listed first.

    WHAT A SHAPE FILE IS. `<name>.rnvlfo`, a few hundred bytes of XML:

        <APOLLO_LFO_SHAPE version="1">
          <POINT x="0" y="1" bend="0.8"/>
          <POINT x="1" y="-1" bend="0"/>
        </APOLLO_LFO_SHAPE>

    Its name is its file's name, so renaming the file renames the shape, and
    the folder is the library: shapes from somebody else are dropped into it.
    That makes every file in it untrusted. One is read only if it is small,
    is XML of this kind, is a version this build understands, and has at least
    two points that are numbers; its points are then repaired the way a curve
    in a hand-edited project is (LfoCurveStore::repair). A file that fails is
    left out and counted, never half-read.

    THE PAGE NAMES A NUMBER, NEVER A PATH, as it does for presets (ADR-0063):
    the shapes are numbered in the order the last scan listed them, a save
    names the shape and never where it goes, and nothing here deletes a file.

    MESSAGE THREAD. Nothing here is touched by the audio thread: a loaded shape
    reaches it as a curve, through the store every curve goes through.
*/

#include <juce_core/juce_core.h>

#include "DSP/LFO/LfoCurve.h"

#include <optional>
#include <vector>

namespace apollo::resources
{

/** The extension of a shape file. Part of the user's library, so permanent. */
inline constexpr const char* lfoShapeExtension = ".rnvlfo";

/** The longest name a saved shape may have, in characters. */
inline constexpr int maximumLfoShapeNameLength = 40;

/** The most of the user's shape files one scan reads. A library is a folder
    somebody else may have filled; this keeps a scan of it bounded. */
inline constexpr int maximumUserLfoShapes = 512;

/** A file larger than this is not a shape: thirty-two points are a few
    hundred bytes. Checked before it is read. */
inline constexpr juce::int64 maximumLfoShapeFileBytes = 64 * 1024;

struct LfoShape
{
    juce::String name;
    bool builtIn = false;
    dsp::LfoCurve curve {};
};

/** Apollo's own shapes, in the order they are listed. Each is valid as it
    stands, which the tests hold them to. */
[[nodiscard]] const std::vector<LfoShape>& builtInLfoShapes();

/** Where the user's shapes are kept: an "LFO Shapes" folder beside Apollo's
    presets, under the platform's own application-data folder. */
[[nodiscard]] juce::File defaultLfoShapeFolder();

/** A shape file's text for @p curve. */
[[nodiscard]] juce::String writeLfoShapeText (const dsp::LfoCurve& curve);

/** The curve in a shape file's text, or nothing if it is not one. */
[[nodiscard]] std::optional<dsp::LfoCurve> readLfoShapeText (const juce::String& text);

/** The curve in @p file, or nothing if it is missing, too large, unreadable or
    not a shape. */
[[nodiscard]] std::optional<dsp::LfoCurve> readLfoShapeFile (const juce::File& file);

/** The name a shape called @p name is saved under: trimmed, one line, made
    safe as a file name. Empty if nothing usable is left. */
[[nodiscard]] juce::String cleanLfoShapeName (const juce::String& name);

/** The file a shape called @p name occupies in @p folder, or an invalid file
    if nothing usable is left of the name or it would sit outside @p folder. */
[[nodiscard]] juce::File lfoShapeFileFor (const juce::File& folder, const juce::String& name);

enum class LfoShapeSaveResult
{
    ok,
    invalidName,          ///< Nothing usable was left of the name.
    exists,               ///< A shape of that name is there, and replacing it was not asked for.
    couldNotCreateFolder,
    couldNotWrite         ///< The disk refused it; a shape already there survives.
};

class LfoShapeLibrary
{
public:
    /** Points at the default folder. Reads nothing until asked to. */
    LfoShapeLibrary();

    /** Points the library somewhere else: tests, and a library on another
        disk. Empties the list until the next scan. */
    void setFolder (const juce::File& folder);

    [[nodiscard]] juce::File getFolder() const { return folder; }

    /** Lists Apollo's shapes, then the user's, alphabetically, reading every
        shape file in the folder up to the limit. */
    void rescan();

    /** What the last scan listed. */
    [[nodiscard]] const std::vector<LfoShape>& getShapes() const noexcept { return shapes; }

    /** How many files in the folder the last scan could not read as shapes. */
    [[nodiscard]] int getUnreadableCount() const noexcept { return unreadable; }

    /** The shape listed at @p index (from 0), or nullptr. */
    [[nodiscard]] const LfoShape* find (int index) const noexcept;

    /** Saves @p curve as a shape called @p name. Atomic: an interrupted save
        leaves a shape already there as it was. Refuses to replace one unless
        @p overwrite says to. */
    [[nodiscard]] LfoShapeSaveResult save (const juce::String& name, const dsp::LfoCurve& curve, bool overwrite);

    /** Opens the folder in the system's file manager, creating it first. */
    bool revealFolder() const;

private:
    juce::File folder;
    std::vector<LfoShape> shapes;
    int unreadable = 0;
};

} // namespace apollo::resources
