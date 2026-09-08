#pragma once

/*
    The saturation in front of a filter.

    Deliberately small, deliberately gentle, and deliberately not the distortion
    effect. `filter_drive` is the colour a filter picks up when it is pushed —
    the thing that makes a resonant sweep sound like an instrument rather than a
    calculation — and it is a different job from the distortion in the FX rack,
    which exists to be heard as an effect. Phase 8 owns that one.

    WHY A CUBIC AND NOT tanh. A hyperbolic tangent is the textbook soft clipper
    and costs a transcendental call per sample. This runs per voice, per channel,
    per filter: at full polyphony that is 128 calls per sample, and the CPU
    measurements in PROJECT-STATE.md §5b show the voice engine is already the
    dominant cost. The cubic below has the same shape through the region that
    matters — a linear stretch either side of zero easing into a flat ceiling —
    for three multiplies and no call at all.

    WHY IT IS NOT OVERSAMPLED, AND WHY ITS RANGE IS SMALL. It is nonlinear, so it
    folds harmonics back into the audible band. An earlier version of this
    comment claimed the filter downstream removes most of that, which is simply
    wrong: folding is instantaneous, so what comes back is already in band and no
    later filter can tell it from signal. The measurement in
    `Tests/DSP/FilterTests.cpp` is what settled the design — see `driveGain`
    below for the numbers, and ADR-0033 for the alternatives that were weighed.
*/

namespace apollo::dsp
{

/** Soft-clips @p input.

    The transfer curve is linear near zero, bends smoothly, and flattens at
    +/- 2/3. Beyond the knee it is exactly flat rather than continuing to grow,
    so no amount of input can produce an unbounded output.
*/
[[nodiscard]] inline float softClip (float input) noexcept
{
    constexpr float ceiling = 2.0f / 3.0f;

    if (input >= 1.0f)
        return ceiling;

    if (input <= -1.0f)
        return -ceiling;

    // x - x^3/3. The derivative at zero is 1, so quiet signals pass through
    // untouched and the drive control does nothing until it is turned up.
    return input - (input * input * input) / 3.0f;
}
/** Gain applied to the signal before the clipper, for a drive amount of 0 to 1.

    Up to 2x, and no further. That ceiling is a measurement, not a preference.

    A full-scale signal driven past the clipper's knee folds harmonics back into
    the audible band, and `Tests/DSP/FilterTests.cpp` measures exactly how much:
    fold-back sits below -140 dBc while the signal stays inside the smooth part
    of the curve, and jumps to about -57 dBc the moment it does not, reaching
    -25 dBc by 4x. A saturator with no knee at all — the smooth, asymptotic
    kind — was measured too and is barely better, so the cliff is the
    nonlinearity itself rather than the shape chosen for it.

    2x keeps the worst case near -45 dBc, which is a colour rather than a
    defect. A drive worth calling distortion needs oversampling, and that is
    Phase 8's, where one FX stage can carry the latency instead of thirty-two
    voices carrying it each (ADR-0033).

    Zero drive gives unity, and the caller skips the stage entirely there rather
    than multiplying by one and clipping something that cannot clip.
*/
[[nodiscard]] inline float driveGain (float amount) noexcept
{
    return 1.0f + amount;
}

/** Level compensation for `driveGain`, so turning drive up changes the tone
    rather than simply making everything louder.

    The clipper's ceiling is 2/3, so restoring 3/2 would put a fully clipped
    signal back at full scale. Drive only reaches 2x, so the compensation only
    needs part of that, and a low setting stays close to unity where the clipper
    is barely doing anything.
*/
[[nodiscard]] inline float driveCompensation (float amount) noexcept
{
    return 1.0f + amount * 0.5f;
}

} // namespace apollo::dsp
