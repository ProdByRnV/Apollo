#pragma once

/*
    A preset file: what is in one, how it is written, and how it is read back.

    A user-created preset is a single `.rnv` file holding a single sound
    (ADR-0053). Its contents are the versioned state document Apollo already
    writes into a host project, as **text**, plus the four pieces of metadata a
    project does not need — name, author, category, comment.

    WHY THIS IS A THIN LAYER, AND DELIBERATELY SO. Everything that decides
    whether a document may be loaded — the product check, the schema version,
    the migration path, the rule that a rejected document leaves the current
    sound untouched — belongs to `StateSerialization.h` and is *called* from
    here rather than repeated. Host state and preset state are the same document
    reached two ways, and the moment they are read by two implementations they
    begin to disagree about what is acceptable. So a preset is XML text in and
    `state::readStateXml` out.

    THE METADATA IS A CHILD OF THE STATE TREE, not a wrapper around it. That
    matters for three reasons. The root stays the state tree, so one reader
    serves both paths. A document written before this existed simply has no such
    child and needs no schema bump — exactly the argument ADR-0043 made for the
    MIDI map. And because it lives in the tree, the name of the preset you
    loaded travels into the host project when you save it, which is what lets an
    instrument reopen showing the sound it was on rather than "Init".

    THE EXTENSION IS NOT EVIDENCE. A `.rnv` is whatever is on the user's disk:
    renamed, truncated, half-written, or a photograph. Every read is validated,
    every rejection is reported in words that name no path and quote no
    contents, and the sound already loaded survives it (CLAUDE.md §33).

    MESSAGE THREAD ONLY. Parsing XML allocates and can take milliseconds; none
    of this may run on the audio thread (CLAUDE.md §7.1). This file performs no
    file I/O at all — it converts between a document and text, and Phase 9b owns
    the disk.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <string_view>

#include "State/StateSerialization.h"

namespace apollo::presets
{

/** The extension every preset carries, factory and user alike (ADR-0053). */
inline constexpr const char* fileExtension = ".rnv";

/** The wildcard a file chooser wants. */
inline constexpr const char* fileWildcard = "*.rnv";

/** A preset carries the patch, not the controller.

    A `.rnv` is made to be shared, and two parts of Apollo's state describe the
    machine in front of the user rather than the sound they made:

      - the learned MIDI mappings (`<MIDIMAP>`), which say which knob on a
        particular desk moves which parameter;
      - the four expression parameters — the wheel's bend range and the three
        that describe an MPE zone — which are the only ones in the registry
        marked *not automatable*, for exactly this reason: they describe the
        controller rather than the patch (ADR-0045).

    Both are stripped when a preset is written and preserved when one is read.
    Auditioning a sound must not silently rewire somebody's controller, and
    sending a friend a patch must not send them your keyboard's setup. This is
    CLAUDE.md §28's separation of preset state from the rest, made concrete.

    The exclusion is derived from the registry's own `automatable` flag rather
    than from a list of ids kept here, so a future parameter that describes the
    controller is excluded by saying so once, where the parameter is defined.
*/
[[nodiscard]] bool isExcludedFromPresets (std::string_view parameterId);

/** The element preset metadata is stored in, as a child of the state root.

    Part of the serialized contract: renaming it would orphan the metadata in
    every preset already written.
*/
inline constexpr const char* presetTreeType = "PRESET";

inline constexpr const char* nameProperty = "name";
inline constexpr const char* authorProperty = "author";
inline constexpr const char* categoryProperty = "category";
inline constexpr const char* commentProperty = "comment";

/** Longest each metadata field may be, in characters.

    Not arbitrary caution: these strings come from a file on disk and end up in
    a list the interface draws and a tooltip it shows. A megabyte-long "name"
    is not a name, and truncating on the way in is cheaper than defending every
    place one is displayed. Long enough that no real name, author or comment is
    affected.
*/
inline constexpr int maximumNameLength = 128;
inline constexpr int maximumCommentLength = 1024;

/** Largest document this reader will look at, in bytes.

    A preset is the parameter list and four short strings — a few tens of
    kilobytes. This bound exists so that pointing Apollo at a video file costs a
    size check rather than an attempt to parse it, and it is generous enough
    that no real preset can reach it even after the format grows.
*/
inline constexpr int maximumDocumentBytes = 4 * 1024 * 1024;

/** What a preset says about itself.

    All four are free text and all four are optional: a preset with none of them
    is still a valid sound, and the browser will show it under its filename.
*/
struct Metadata
{
    juce::String name;
    juce::String author;
    juce::String category;
    juce::String comment;

    [[nodiscard]] bool operator== (const Metadata&) const = default;
};

/** @returns @p metadata with every field trimmed and bounded.

    Applied on the way in *and* on the way out, so a document Apollo writes can
    never contain a field it would refuse to read back, and a document written
    by something else cannot smuggle an unbounded string into the interface.
*/
[[nodiscard]] Metadata sanitise (const Metadata& metadata);

/** Reads the metadata currently attached to the live state, or an empty set. */
[[nodiscard]] Metadata getMetadata (juce::AudioProcessorValueTreeState& apvts);

/** Attaches @p metadata to the live state, replacing anything already there.

    Kept separate from `write` so that saving a preset and *becoming* that
    preset are two decisions rather than one: a caller that exports a copy
    without renaming the loaded sound is a caller that does not call this.
*/
void setMetadata (juce::AudioProcessorValueTreeState& apvts, const Metadata& metadata);

/** @returns the complete text of a `.rnv` file for the current state.

    The state is stamped with the schema version, the product and the product
    version exactly as a host save is, then @p metadata is attached and the
    whole document is written as XML text.
*/
[[nodiscard]] juce::String write (juce::AudioProcessorValueTreeState& apvts,
                                  const Metadata& metadata);

/** Validates @p text, migrates it if needed, and applies it.

    The current sound is replaced only if every check passes. Delegates to
    `state::readStateXml`, so a preset is accepted on exactly the terms host
    state is.
*/
[[nodiscard]] state::StateLoadResult read (juce::AudioProcessorValueTreeState& apvts,
                                           const juce::String& text);

/** Reads only the metadata out of @p text, without touching the instrument.

    What an index is built from: the browser needs a name and a category for
    every file in a folder and must not load a sound to find them. Validated the
    same way a full read is — a file that would be refused as a preset has no
    metadata worth listing — but it stops before building a ValueTree of every
    parameter, which is most of the cost.

    @returns the reason the document was refused, or `ok`, in which case
             @p destination holds its sanitised metadata.
*/
[[nodiscard]] state::StateLoadResult readMetadata (const juce::String& text,
                                                   Metadata& destination);

} // namespace apollo::presets
