#pragma once

/*
    Drawing the wave an oscillator is currently reading.

    PRD §30.2 asks the interface to show the current waveform, the wavetable
    position and the scanning behaviour. All three are the same picture: the wave
    *at the position the oscillator is actually reading*, redrawn as that
    position moves. A display of the table's first frame, or of the parameter's
    value rather than the modulated one, would be a picture of the patch rather
    than of the instrument.

    NOT TELEMETRY, AND DELIBERATELY NOT CAPTURED. Nothing here travels through a
    ring, because nothing here is produced by the audio thread: a wavetable is a
    resource that was built once and is immutable for the life of the instrument
    (ARCHITECTURE.md — it is shared unsynchronised between voices precisely
    because of that). So the message thread reads it directly, and the only thing
    the audio thread has to publish is the two numbers that say which frame to
    draw. Rendering 128 points at 15 Hz on the message thread costs less than
    sending them would.

    MIP LEVEL 0, always: the finest table, whatever the note. The mipmap exists
    so that a high note does not alias, and a picture has no pitch — showing a
    band-limited copy would draw a rounder wave than the one the user chose at
    exactly the moment they are choosing it.
*/

#include "DSP/Oscillators/WavetableLibrary.h"
#include "Telemetry/InstrumentFrame.h"

namespace apollo::engine
{

/** Renders the wave at @p frame's table index and position into its points.

    MESSAGE THREAD. Sets `valid`, or clears it if the table is empty or the index
    is out of range — in which case the interface draws nothing rather than a
    flat line, which would claim the oscillator was reading silence.
*/
void fillWavetableFrame (const dsp::WavetableLibrary& library, telemetry::WavetableFrame& frame);

} // namespace apollo::engine
