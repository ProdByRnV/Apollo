#pragma once

/*
    Converting a whole recording from one sample rate to another, offline
    (Phase 13g).

    A loaded noise sample is played from a mipmap built at 48 kHz
    (TransientLibrary.h), and a file can be at any rate. So it is converted once,
    on the loader's thread, when it is read.

    BAND-LIMITED, IN BOTH DIRECTIONS. Each output sample is the input convolved
    with a windowed sinc whose cutoff sits just below the lower of the two
    Nyquist frequencies. Going down, that removes what the new rate cannot hold
    before it can fold back as aliasing; going up, it is the ideal interpolator
    and adds nothing above the old Nyquist. A rate that is already right comes
    back untouched, sample for sample.

    The window is Kaiser, beta 8.6, which puts the stopband near -86 dB, and the
    kernel spans 24 zero crossings either side. Both are read from a table
    rather than computed per tap, which is what makes a ten-second file at
    192 kHz a fraction of a second's work rather than several.

    NOT FOR THE AUDIO THREAD. It allocates the output and the table.
*/

#include <vector>

namespace apollo::dsp
{

/** @returns @p input, at @p inputRate, converted to @p outputRate.

    The output has `floor (input.size() * outputRate / inputRate)` samples. A
    rate that is not a positive finite number returns an empty signal; equal
    rates return @p input unchanged.
*/
[[nodiscard]] std::vector<double> resample (const std::vector<double>& input, double inputRate, double outputRate);

} // namespace apollo::dsp
