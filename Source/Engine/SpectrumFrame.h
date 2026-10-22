#pragma once

/*
    The spectrum behind the equaliser's curve (CLAUDE.md §22).

    The curve says what the equaliser is set to do. This says what it is doing:
    the spectrum of the signal going into it, and of the signal coming out, drawn
    on the same frequency axis as the curve. Cut 200 Hz and the output falls away
    at 200 Hz while the input stays where it was; the gap between the two *is*
    the equaliser, measured rather than predicted.

    NOT TELEMETRY, for the same reason the wavetable display is not
    (WavetableFrame.h): the capture is telemetry — two rings either side of the
    equaliser, written by the audio thread exactly as a scope's is — but turning
    a window into a spectrum needs DSP's transform, and Telemetry does not depend
    on DSP. So the arithmetic lives here and runs on the message thread, at the
    interface's frame rate, and the audio thread's part is two mono copies of the
    block.

    WHAT A POINT IS. The spectrum is sent as `spectrumPoints` levels on a
    logarithmic axis from 20 Hz to 20 kHz, the axis the curve is drawn on. Where
    a point's share of that axis holds several transform bins — everywhere above
    a few hundred hertz — it takes the loudest of them, so a single harmonic
    reads at its own level rather than being averaged into the bins around it.
    Where the share is narrower than one bin, at the bottom of the axis, the
    level is interpolated between the two nearest, so the lowest octave is a
    curve rather than a staircase.

    THE LEVEL IS dBFS, TILTED. A sine at full scale reads 0 dB at 1 kHz. Away
    from 1 kHz the reading is tilted by `spectrumTiltDbPerOctave`, upwards
    towards the treble, because almost every real sound loses energy as it
    rises: untilted, a saw wave's spectrum is a slope that leaves the top three
    octaves of the display empty, and the top three octaves are where most of an
    equaliser's work is heard. The same tilt is applied to input and output, so
    the difference between them is untouched by it.

    BALLISTICS. Each point rises quickly and falls slowly, smoothed from one
    frame to the next. A spectrum taken thirty times a second and drawn raw
    flickers too fast to read; one smoothed equally both ways lags behind a note
    starting. Rising fast and falling at a steady rate is how every analyser a
    musician has used behaves, and it means a sound that stops is seen to fall
    away over a second or two rather than vanishing or hanging.
*/

#include "DSP/Utilities/Fft.h"
#include "Telemetry/TelemetryHub.h"

#include <array>
#include <complex>
#include <cstddef>
#include <vector>

namespace apollo::engine
{

/** Points across the display, 20 Hz to 20 kHz. About sixteen to the third of an
    octave: finer than the eye resolves at the width the panel is drawn, and few
    enough that a frame of both spectra is under two kilobytes. */
inline constexpr int spectrumPoints = 160;

/** Samples transformed per frame. 85 ms at 48 kHz, which resolves 12 Hz between
    bins — enough to separate the harmonics of a bass note — and is read thirty
    times a second, so each window overlaps the last by most of its length. */
inline constexpr int spectrumTransformSize = 4096;

static_assert (spectrumTransformSize + telemetry::ScopeBuffer::readMargin <= telemetry::scopeBufferSize,
               "the transform's window and its read margin must fit inside the ring");

/** The lowest level a point reports. Silence reads here. */
inline constexpr float spectrumFloorDb = -120.0f;

/** Decibels per octave added above 1 kHz and taken away below it. 4.5 is the
    slope that draws pink noise rising gently and a typical mix roughly flat. */
inline constexpr float spectrumTiltDbPerOctave = 4.5f;

/** How far a point moves towards a louder reading in one frame: most of the
    way, so a note starting is drawn within a frame or two. */
inline constexpr float spectrumAttack = 0.7f;

/** How far a point may fall in one frame, in dB: sixty decibels a second at
    thirty frames. A steady fall rather than a proportion of the distance, which
    would drop a stopped sound off the bottom of the display in a fifth of a
    second and make every flicker of a noisy signal a flicker of the picture. */
inline constexpr float spectrumReleaseDbPerFrame = 2.0f;

/** One frame of the equaliser's display. */
struct SpectrumFrame
{
    using Levels = std::array<float, static_cast<std::size_t> (spectrumPoints)>;

    /** dB per point, at `frequencyOfPoint`. Meaningful only when `active`. */
    Levels input {};
    Levels output {};

    /** True while the equaliser is in the rack and the engine is prepared. An
        inactive frame is not a quiet one: it says there is nothing to draw, and
        the interface draws the curve on its own. */
    bool active = false;

    /** True when neither side heard anything this frame. The levels are still
        sent, falling, so the picture fades rather than vanishing. */
    bool silent = true;
};

/** Builds `SpectrumFrame`s from the hub's two equaliser taps.

    MESSAGE THREAD. Holds the transform's tables and the smoothed levels from the
    frame before, so one instance belongs to one display.
*/
class SpectrumAnalyser
{
public:
    /** Allocates the transform and its buffers, once. */
    SpectrumAnalyser();

    /** Fills @p frame from @p hub. @returns `frame.active`. */
    bool build (const telemetry::TelemetryHub& hub, SpectrumFrame& frame);

    /** Forgets the smoothed history, so the next frame starts from the floor. */
    void reset() noexcept;

    /** The frequency a point is drawn at, in Hz. */
    [[nodiscard]] static double frequencyOfPoint (int point) noexcept;

private:
    /** Transforms one tap into raw levels per point. @returns false if the tap
        has never been written or its window is silent, in which case @p levels
        is filled with the floor. */
    bool analyse (const telemetry::ScopeBuffer& tap, double sampleRate, SpectrumFrame::Levels& levels);

    dsp::PreparedTransform transform;

    std::vector<float> samples;
    std::vector<double> window;
    std::vector<std::complex<double>> bins;

    /** Power per bin, up to and including the Nyquist bin. */
    std::vector<double> power;

    /** dB added to a bin's power to read a full-scale sine as 0 dB, given the
        window's coherent gain. */
    double normalisationDb = 0.0;

    SpectrumFrame::Levels rawInput {};
    SpectrumFrame::Levels rawOutput {};

    SpectrumFrame::Levels smoothedInput {};
    SpectrumFrame::Levels smoothedOutput {};
};

} // namespace apollo::engine
