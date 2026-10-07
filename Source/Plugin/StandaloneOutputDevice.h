#pragma once

/*
    Keeping the standalone application on the system's output device.

    Declared without any JUCE in the header so the plugin entry point can call
    it unconditionally; a plugin build does nothing, because the host owns the
    audio device and a plugin that moved it would be moving the whole session.
*/

namespace apollo::standalone
{

/** Starts following the system's default output device, until the user picks
    one themselves.

    Safe to call more than once and from any build. MESSAGE THREAD (ADR-0082).
*/
void startOutputDeviceFollowing();

} // namespace apollo::standalone
