#pragma once

/*
    Everything the interface is told about the instrument that is not a waveform.

    ScopeFrame turns a ring of audio into a picture. This is the second half of
    that job and is deliberately a different file, because almost nothing in it
    is audio: modulator traces, an output meter, a voice count, and the shape of
    the wavetable each oscillator is currently reading. They travel together
    because they are all *the same question* — what is this instrument doing
    right now — asked of things that move at human speed rather than at the
    sample rate.

    AND THEY TRAVEL AT A DIFFERENT RATE FROM THE SCOPES, which is the reason they
    are not simply added to the scope message. A scope is a moving picture and
    needs thirty frames a second or it reads as a slideshow. A meter needle and
    an envelope trace are legible at half that, and halving the rate of the
    larger of the two messages is worth more than the tidiness of having one
    (UI_BINDINGS.md §10.5).

    JUCE-free, like the rest of Telemetry: a frame is built and inspected with no
    host, no device and no browser.
*/

#include "Telemetry/LevelMeter.h"
#include "Telemetry/ModulationTrace.h"
#include "Telemetry/TelemetryHub.h"

#include <array>
#include <cstddef>

namespace apollo::telemetry
{

/** Points in a drawn wavetable frame.

    A wavetable's rendered waveform is a *shape*, not a signal, so this is a
    question of how smooth the curve looks rather than of how much of the audio
    survives: 128 points over one cycle draws every waveform the library holds
    without a visible corner at the resolution one of these is drawn at.
*/
inline constexpr int wavetableFramePoints = 128;

//==============================================================================

/** One modulator's drawable trace. */
struct ModulationFrame
{
    /** The trace, oldest first, over `modulationTraceSeconds`. */
    std::array<float, static_cast<std::size_t> (modulationTracePoints)> points {};

    /** The newest entry, repeated for convenience: the interface wants it as a
        number beside the picture, and re-deriving "the last point" in the page
        would be a second place for the trace's orientation to be got wrong.
    */
    float current = 0.0f;

    /** True once anything has been traced. A modulator nothing traces is not the
        same as one sitting still, and the interface must not draw them alike.
    */
    bool valid = false;

    /** True when the matrix reads this modulator.

        An unrouted LFO is not advanced at all, so its trace is honestly a flat
        line; this is what lets the interface say *why* instead of leaving a
        straight line to look like a fault.
    */
    bool routed = false;

    /** `dsp::EnvelopeStage`'s numeric value, and meaningless for an LFO. */
    int envelopeStage = 0;
};

/** One oscillator's current waveform. */
struct WavetableFrame
{
    /** One cycle of the wave being read, at the position below. */
    std::array<float, static_cast<std::size_t> (wavetableFramePoints)> points {};

    /** Where in the table it was taken from, 0 to 1 — the **effective**
        position, including whatever the matrix is adding to the parameter.
    */
    float position = 0.0f;

    /** Which table, by index into the built-in library. */
    int tableIndex = 0;

    bool valid = false;
};

/** One frame of everything above. */
struct InstrumentFrame
{
    std::array<ModulationFrame, modulatorSourceCount> modulators {};
    std::array<WavetableFrame, wavetableDisplayCount> wavetables {};

    LevelReading meter {};

    int activeVoices = 0;
    int polyphony = 0;
};

/** Reads the hub into a frame.

    MESSAGE THREAD. Fills everything except the wavetable *points*, which need
    the oscillator library and are filled by the engine (Engine/WavetableFrame.h)
    — Telemetry deliberately knows nothing about wavetables beyond the two
    numbers that identify one.

    @returns false if nothing has been captured at all, in which case @p frame is
             left at its default.
*/
bool buildInstrumentFrame (const TelemetryHub& hub, InstrumentFrame& frame);

} // namespace apollo::telemetry
