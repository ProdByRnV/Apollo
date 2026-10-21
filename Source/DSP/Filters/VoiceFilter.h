#pragma once

/*
    One of a voice's filter slots: every type the `filterN_type` parameter can
    choose, behind one interface (PRD §13.1).

    The state-variable filter supplies low-pass, high-pass, band-pass and notch,
    as it always has. Phase 13c added the comb and the formant filter, which are
    different structures — a delay line, and three band-passes in parallel — so
    they live beside it rather than inside it. The state-variable filter is also
    used by the delay and the distortion, and has no business carrying a delay
    line for them.

    COEFFICIENTS ARE SEPARATE FROM STATE, as they were for the state-variable
    filter alone: the engine resolves one set per slot and every voice shares it,
    and a modulated voice resolves its own. A set only computes what its type
    needs, so the four original types cost exactly the one `tan` they always
    did.

    SWITCHING TYPE. The state-variable types share their integrators, and a
    switch between them keeps them, as it always has. Switching into the comb or
    the formant filter, or back to the state-variable filter from one of them,
    clears the structure being switched to: it has not been running, so whatever
    it holds is a note from some time ago.

    REAL-TIME CONTRACT: `prepare` allocates and is not real-time safe; every
    other function is callable from the audio thread.
*/

#include "DSP/Filters/CombFilter.h"
#include "DSP/Filters/FormantFilter.h"
#include "DSP/Filters/StateVariableFilter.h"

namespace apollo::dsp
{

/** The values are the `filterN_type` parameter's, and permanent: a preset
    stores the number. A type added later goes at the end. */
enum class FilterType
{
    off = 0,
    lowpass = 1,
    highpass = 2,
    bandpass = 3,
    notch = 4,
    comb = 5,
    formant = 6
};

inline constexpr int numFilterTypes = 7;

/** What a cutoff and a resonance resolve to, for whichever type. */
struct VoiceFilterCoefficients
{
    /** Resolves only what @p type uses; the rest keep their defaults, so two
        sets for the same type and settings compare equal. */
    void set (FilterType type, float cutoffHz, float q, double sampleRate) noexcept;

    [[nodiscard]] bool operator== (const VoiceFilterCoefficients&) const = default;

    SvfCoefficients svf;
    CombCoefficients comb;
    FormantCoefficients formant;
};

class VoiceFilter
{
public:
    /** Allocates the comb's delay line. NOT real-time safe. */
    void prepare (double sampleRate);

    /** Clears every structure. Leaves type and coefficients alone. */
    void reset() noexcept;

    void setType (FilterType newType) noexcept;
    void setCoefficients (const VoiceFilterCoefficients& newCoefficients) noexcept;

    [[nodiscard]] FilterType getType() const noexcept { return type; }

    /** Filters one sample. `off` returns the input unchanged. */
    [[nodiscard]] float processSample (float input) noexcept;

private:
    FilterType type = FilterType::off;

    StateVariableFilter svf;
    CombFilter comb;
    FormantFilter formant;
};

} // namespace apollo::dsp
