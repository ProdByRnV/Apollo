#pragma once

/*
    Opening the MIDI inputs a standalone Apollo finds.

    Declared without any JUCE in the header so the plugin entry point can call
    it unconditionally; the implementation is empty in every build that is not
    the standalone application, because a plugin is handed its MIDI by the host
    and must never open a device for itself.
*/

namespace apollo::standalone
{

/** Starts watching for MIDI inputs and opening the ones Apollo has not seen.

    Safe to call more than once and from any build: everything but the
    standalone does nothing. MESSAGE THREAD (ADR-0081).
*/
void startMidiDeviceAdoption();

} // namespace apollo::standalone
