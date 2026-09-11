#pragma once

/*
    Turning a captured window into something worth drawing.

    Three things happen between the ring and the picture, and all three happen on
    the message thread because none of them belongs anywhere near the audio one.

    **Triggering.** A scope that simply drew the newest window would show a
    waveform sliding sideways at whatever rate the note's frequency and the
    refresh rate happened to beat at. The frame is therefore aligned to the most
    recent upward zero crossing, so a steady note stands still. If the window
    holds no crossing — noise near silence, or a signal with a large offset — the
    frame is taken from the end of the window and the scope free-runs, which is
    the honest thing to show rather than a picture invented to look stable.

    **Decimation.** The window holds far more samples than a scope has pixels.
    Each output point is the sample at its position rather than an average of the
    span: averaging would quietly turn an aliased or clipped waveform into a
    smooth one, and a scope whose job is to reveal exactly those things must not
    flatter the signal it is drawing.

    **Silence.** A source that has stopped must read as stopped rather than
    holding its last picture (CLAUDE.md §26.1), so the peak is measured and
    reported alongside the points.

    JUCE-free, so all of it is testable against a hand-built waveform with no
    host, no device and no browser.
*/

#include "Telemetry/ScopeBuffer.h"

#include <array>
#include <cstddef>

namespace apollo::telemetry
{

/** One source's drawable frame. */
struct ScopeFrame
{
    /** The trace, -1 to +1, oldest first. Only meaningful when `valid`. */
    std::array<float, static_cast<std::size_t> (scopeFramePoints)> points {};

    /** Largest absolute sample in the window the frame was taken from.

        Reported as well as used, because it is the scope's own level reading and
        the interface would otherwise have to re-derive it from the decimated
        points — which, having thrown most of the window away, would be wrong.
    */
    float peak = 0.0f;

    /** True if the source has ever been written. A source that does not run in
        this build is not the same as one that is quiet, and the interface must
        not draw them alike.
    */
    bool valid = false;

    /** True if the window's peak is below `scopeSilenceThreshold`. */
    bool silent = true;

    /** True if the frame was aligned to a zero crossing. False means the scope
        is free-running, which is worth knowing when a trace will not settle.
    */
    bool triggered = false;
};

/** Samples between one drawn point and the next.

    The sweep the frame covers is `scopeFramePoints * scopeFrameStride` samples —
    768, or 16 ms at 48 kHz, which is two or three cycles of a note in the middle
    of the keyboard. Long enough to read as a waveform, short enough not to be a
    smear.
*/
inline constexpr int scopeFrameStride = 4;

/** The sweep a frame covers, in samples. */
inline constexpr int scopeFrameSpan = scopeFramePoints * scopeFrameStride;

/** How many samples a frame is chosen from.

    Deliberately longer than the sweep, and that difference is the whole trigger
    budget: the frame may start anywhere in the first `scopeWindowSamples -
    scopeFrameSpan` samples, so that is how far back a search for a zero crossing
    can reach. 1280 samples is 26 ms, which holds a full cycle of anything above
    about 38 Hz — below the lowest note anyone will look at a scope for.

    Getting this wrong is quiet rather than loud: a window exactly as long as the
    sweep leaves nowhere to trigger, every frame free-runs, and the only symptom
    is a trace that will not stand still.
*/
inline constexpr int scopeWindowSamples = 2048;

static_assert (scopeWindowSamples > scopeFrameSpan,
               "the window must be longer than the sweep, or there is nowhere to trigger");

static_assert (scopeWindowSamples + ScopeBuffer::readMargin <= scopeBufferSize,
               "the window and its read margin must fit inside the ring");

/** Builds a drawable frame from a source's most recent window.

    MESSAGE THREAD. @returns false if the source has never been written, in
    which case @p frame is left at its default — invalid and silent.
*/
bool buildScopeFrame (const ScopeBuffer& buffer, ScopeFrame& frame);

} // namespace apollo::telemetry
