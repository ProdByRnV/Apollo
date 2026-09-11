#pragma once

/*
    Pushes visualisation frames to the frontend.

    Separate from ParameterBridge, and deliberately so. That class is a
    *conversation*: the page asks for things and is answered, and every message
    it sends is the authoritative echo of a value the page may also be holding.
    This one is a *broadcast*: nothing is requested, nothing is acknowledged, and
    a dropped frame costs one repaint and is never noticed. Sharing a class
    between the two would mean sharing a rate as well, and 30 Hz of scope traffic
    has nothing to do with the rate at which a knob's value should be echoed.

    THREADING. Everything here is message thread. The audio thread's entire
    involvement in visualisation is reading one relaxed flag and, if it is set,
    writing samples into a preallocated ring (Telemetry/ScopeBuffer.h). It is
    never asked for a frame and never waits for one (CLAUDE.md §7.1, §26.3).

    The timer runs only while a handler is attached, and so does the capture
    behind it: attaching a handler arms every ring, and detaching one disarms
    them. A plugin with its editor closed therefore builds no frames, serializes
    no JSON, and — the part that matters on the audio thread — takes no taps at
    all. That gate exists because the per-source taps are inside the voice loop,
    so their cost rises with polyphony (PRD §30.1); the alternative is every
    instance in a session paying for pictures only one of them can show.
*/

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "DSP/Oscillators/WavetableLibrary.h"
#include "Telemetry/InstrumentFrame.h"
#include "Telemetry/ScopeFrame.h"
#include "Telemetry/TelemetryHub.h"
#include "UI/BridgeProtocol.h"

#include <array>
#include <functional>

namespace apollo::ui
{

class TelemetryBridge final : private juce::Timer
{
public:
    /** Sends one message to the frontend. Always invoked on the message thread. */
    using OutboundHandler = std::function<void (const juce::String&)>;

    TelemetryBridge (telemetry::TelemetryHub& hubToUse,
                     const dsp::WavetableLibrary& libraryToUse);
    ~TelemetryBridge() override;

    /** Sets the sink for outbound frames, and starts or stops the timer with it.

        Pass {} to detach — which is what makes closing the editor stop the work
        rather than merely stop it being seen.
    */
    void setOutboundHandler (OutboundHandler handler);

    /** Builds and sends one frame for every active source.

        Driven by the timer. Exposed so tests can drive it deterministically
        instead of waiting on a real message loop.
    */
    void sendFrames();

    /** @returns the current frames, whether or not a handler is attached. */
    [[nodiscard]] juce::String createScopeFrames();

    /** Builds and sends one instrument frame: the modulator traces, the output
        meter, the voice count and each oscillator's current waveform.

        Driven by the same timer at half the rate. Exposed for the same reason
        `sendFrames` is — so tests can drive it without a message loop.
    */
    void sendInstrumentFrame();

    [[nodiscard]] juce::String createInstrumentFrame();

    /** Frames per second sent to the interface.

        A scope is a moving picture, so this is a *frame rate* rather than the
        display-refresh compromise the parameter bridge makes: below about 25 Hz
        a trace reads as a slideshow. 30 Hz is comfortably above that and divides
        evenly into every common display rate, so frames land regularly rather
        than beating against the compositor.
    */
    static constexpr int frameRateHz = 30;

    /** How many scope frames pass between instrument frames.

        The traces, the meter and the wavetable displays all move at human speed
        rather than at the sample rate, and fifteen frames a second is well above
        what any of them needs to read smoothly. Halving the rate of the second
        message rather than the first is the right way round: a scope below
        twenty-five frames a second reads as a slideshow, and a meter at fifteen
        does not.
    */
    static constexpr int instrumentFrameDivider = 2;

private:
    void timerCallback() override;

    telemetry::TelemetryHub& hub;

    /** The wavetables, read straight rather than captured: they are built once
        and immutable for the life of the instrument, so the message thread may
        read them while the audio thread does (ARCHITECTURE.md §3.1).
    */
    const dsp::WavetableLibrary& library;

    /** Counts scope frames towards the next instrument frame. */
    int framesUntilInstrument = 0;

    telemetry::InstrumentFrame instrumentFrame;

    /** Built in place every tick rather than allocated, because this runs thirty
        times a second for the life of the editor.
    */
    std::array<telemetry::ScopeFrame, telemetry::scopeSourceCount> frames;

    OutboundHandler outboundHandler;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TelemetryBridge)
};

} // namespace apollo::ui
