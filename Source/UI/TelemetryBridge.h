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
    involvement in visualisation is writing samples into a preallocated ring
    (Telemetry/ScopeBuffer.h); it is never asked for a frame, never waits, and
    does not know whether anything is watching (CLAUDE.md §7.1, §26.3).

    The timer runs only while a handler is attached, so a plugin with its editor
    closed builds no frames and serializes no JSON — the capture keeps running,
    because it costs a linear pass the audio thread was going to make anyway, but
    nothing downstream of it does.
*/

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

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

    explicit TelemetryBridge (telemetry::TelemetryHub& hubToUse);
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

    /** Frames per second sent to the interface.

        A scope is a moving picture, so this is a *frame rate* rather than the
        display-refresh compromise the parameter bridge makes: below about 25 Hz
        a trace reads as a slideshow. 30 Hz is comfortably above that and divides
        evenly into every common display rate, so frames land regularly rather
        than beating against the compositor.
    */
    static constexpr int frameRateHz = 30;

private:
    void timerCallback() override;

    telemetry::TelemetryHub& hub;

    /** Built in place every tick rather than allocated, because this runs thirty
        times a second for the life of the editor.
    */
    std::array<telemetry::ScopeFrame, telemetry::scopeSourceCount> frames;

    OutboundHandler outboundHandler;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TelemetryBridge)
};

} // namespace apollo::ui
