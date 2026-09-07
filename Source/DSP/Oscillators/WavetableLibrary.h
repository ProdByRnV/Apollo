#pragma once

/*
    The built-in wavetables.

    BAND-LIMITING STRATEGY. Each mip level is generated **additively**: the
    waveform is defined as a set of harmonic amplitudes, and a level is
    synthesised by summing only the harmonics it is allowed to keep. The result
    is band-limited exactly, by construction, with no filter design, no
    transition band and no ringing — the harmonics above the limit were never
    present rather than attenuated.

    That works because Apollo's built-in shapes have closed-form harmonic
    series. Arbitrary user-supplied tables cannot be generated this way; they
    have to be analysed and filtered instead, which belongs with the resource
    loader in Phase 9. `Wavetable` stores finished mipmaps either way, so that
    path can be added without touching playback.

    Tables are built once, at construction, off the audio thread. They do not
    depend on sample rate — the mipmap is indexed by harmonic count, and the
    sample rate only affects which level is chosen — so nothing needs rebuilding
    when the host changes rate.

    The four tables below are **placeholder factory content**: mathematically
    defined shapes chosen so the engine can be exercised and measured. Designed
    factory wavetables are Phase 9 content work, not Phase 4 engine work.
*/

#include "DSP/Oscillators/Wavetable.h"

#include <array>

namespace apollo::dsp
{

class WavetableLibrary
{
public:
    /** Number of built-in tables. Matches the `osc1_wavetable` parameter range
        documented in UI_BINDINGS.md §3 (discrete, 0-3).
    */
    static constexpr int numTables = 4;

    /** Frames per built-in table.

        The format supports up to 256 (PRD §8.2); the built-ins use 16 because
        that is enough to scan audibly and keeps both memory and the one-off
        generation cost small.
    */
    static constexpr int framesPerTable = 16;

    /** Builds every table. Allocates; never call from the audio thread. */
    WavetableLibrary();

    /** @returns the table at @p index, clamped into range.

        Never returns null: an out-of-range selection yields a real table rather
        than silence, so a bad parameter value cannot mute the instrument.
    */
    [[nodiscard]] const Wavetable& getTable (int index) const noexcept;

private:
    std::array<Wavetable, static_cast<std::size_t> (numTables)> tables;
};

} // namespace apollo::dsp
