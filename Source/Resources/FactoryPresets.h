#pragma once

/*
    The sounds Apollo ships with, and the one it starts from.

    WHY THESE ARE DATA AND NOT FILES. A factory library shipped as `.rnv` files
    has to get onto the user's disk, and the only ways to do that are an
    installer writing into a shared location — which needs administrator rights,
    which a plugin should not need — or a first-run copy that can be deleted,
    half-deleted, or edited into something that no longer loads. Apollo's
    factory content is compiled in. It is always present, it cannot be lost, and
    it costs no permissions.

    WHAT IS STORED IS NOT THE DOCUMENT. Each preset here is a name and a short
    list of the parameters it changes; everything it does not mention sits at
    the registry's default. That is deliberate and it is the important decision
    in this file:

      - **It is reviewable.** A diff shows "this preset moved the filter to
        400 Hz", not four hundred lines of XML with one number different.
      - **It cannot rot silently.** A checked-in document naming a parameter
        that no longer exists is a preset that quietly loses part of itself. A
        table is checked against the registry by a test, so the same mistake is
        a build failure.
      - **It stays honest about defaults.** A preset that does not mention the
        reverb gets whatever the reverb's default is *today*, which is what
        "this preset is about its filter" should mean.

    THE DOCUMENT IS STILL A DOCUMENT. `render` turns one of these into exactly
    the `.rnv` text a save would have produced, stamped by the same function and
    carrying its metadata in the same child, and loading one goes through the
    same validator as a file off a stranger's disk (CLAUDE.md §29.1). Nothing
    about a preset's origin changes its format or the path it takes into the
    instrument — only where the bytes came from.

    THE INIT PATCH IS THE EMPTY CASE. It overrides nothing, so it *is* the
    registry's defaults, expressed the same way every other preset is. There is
    no separate code path for "reset everything", and therefore no way for the
    init patch to drift from what a fresh instance sounds like.
*/

#include <juce_core/juce_core.h>

#include <array>
#include <span>
#include <string_view>

#include "State/PresetDocument.h"

namespace apollo::resources
{

/** One parameter a factory preset moves away from its default.

    The value is the **plain** one — hertz, milliseconds, decibels, or the
    integer a discrete control uses — so that this table reads the way the
    interface does. Normalising is the document writer's job, not the author's.
*/
struct FactorySetting
{
    std::string_view id;
    float value;
};

/** One sound Apollo ships with. */
struct FactoryPreset
{
    std::string_view name;
    std::string_view category;
    std::string_view comment;

    /** What it changes. Everything absent stays at the registry default. */
    std::span<const FactorySetting> settings;
};

/** Every built-in preset.

    **This is not the order the browser shows.** The index sorts alphabetically
    within a bank, which is how somebody scans a list, and these all share the
    Factory bank — so the browser's order is `Dust Sweep, Glass Bell, Hollow Saw
    Lead, Init, …`, not this one.

    `Init` is first *here*, where the order means something else: it is the
    canonical starting point, it is what `factoryPresets().front()` hands to
    anything that wants the default patch, and it is the entry a reader of this
    file should meet before the ones that are departures from it. The interface
    gives it a button of its own rather than relying on where it lands in a
    list.
*/
[[nodiscard]] std::span<const FactoryPreset> factoryPresets();

/** The author on every built-in preset.

    One name in one place rather than repeated on each entry, because they were
    all made by the same person and a typo on one of them would look like a
    second author in the browser's list.
*/
inline constexpr std::string_view factoryAuthor = "ProdByRnV";

/** The bank every built-in preset reports.

    Factory content has no folders — it has no files — so it needs a bank of its
    own or it would sit at the top of the list mixed in with whatever the user
    has saved loose. "Factory" is what it is.
*/
inline constexpr std::string_view factoryBank = "Factory";

/** @returns the complete `.rnv` text for @p preset.

    Exactly what `presets::write` would have produced for an instrument in that
    state: every registered parameter present at its plain value, the document
    stamped with the schema version and the product, and the metadata in its
    own child. It is fed to the ordinary reader, so a mistake in this table is
    caught by the same validation a corrupt file would meet.
*/
[[nodiscard]] juce::String render (const FactoryPreset& preset);

/** @returns the metadata @p preset will report to the browser. */
[[nodiscard]] presets::Metadata metadataOf (const FactoryPreset& preset);

} // namespace apollo::resources
