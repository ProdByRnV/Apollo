#pragma once

/*
    The tables Apollo ships beyond the four the oscillator's switch names
    (Phase 16d, ADR-0116).

    WHY THEY ARE NOT FOUR MORE CHOICES. `oscN_wavetable` was released with five
    values — the four built-ins and USER — and a DAW stores its automation as a
    position along that range. A sixth value would move every one of them
    (ADR-0114). So these tables are played the way a table of the user's own is:
    loaded into the oscillator's own slot, which USER plays, and carried in the
    patch as audio. A preset that uses one opens in an Apollo that has never
    heard of it, and sounds the same.

    WRITTEN AS SPECTRA, like the four (CLAUDE.md §12.3). A table whose shape is
    only meaningful in time — a synced saw, a quantised sine — is drawn as one
    cycle sixteen times longer than a frame and analysed, so that what the
    analysis folds back is sixteen times further below what it keeps.

    Every table is sixteen frames, scanned by Position, as the four are.
*/

#include "DSP/Oscillators/WavetableBuilder.h"

namespace apollo::dsp
{

/** How many there are. Their numbers, from 0, are what the page asks for, and
    are never reused for a different table. */
inline constexpr int numExtraTables = 5;

/** Frames in each. */
inline constexpr int extraTableFrames = 16;

/** "Analog", "Sync", "Organ", "Chime", "Digital", for @p index from 0; an
    empty string for anything else. */
[[nodiscard]] const char* extraTableName (int index) noexcept;

/** @returns table @p index's spectrum, `extraTableFrames` frames of
    `Wavetable::topLevelHarmonics` harmonics; empty for an index out of range.
    Allocates and takes milliseconds: never on the audio thread. */
[[nodiscard]] TableSpectrum makeExtraTable (int index);

} // namespace apollo::dsp
