#include "UI/TelemetryBridge.h"

#include <utility>

namespace apollo::ui
{

TelemetryBridge::TelemetryBridge (telemetry::TelemetryHub& hubToUse)
    : hub (hubToUse)
{
}

TelemetryBridge::~TelemetryBridge()
{
    stopTimer();

    // Whatever happens to this object, the audio thread stops paying for it.
    hub.setCapturing (false);
}

void TelemetryBridge::setOutboundHandler (OutboundHandler handler)
{
    outboundHandler = std::move (handler);

    const auto watching = static_cast<bool> (outboundHandler);

    // The timer follows the handler rather than running regardless. With no
    // editor attached there is nobody to draw a frame, and building one anyway
    // would be thirty JSON documents a second serialized into nothing.
    if (watching)
        startTimerHz (frameRateHz);
    else
        stopTimer();

    // And so does the capture itself. This is the one switch the audio thread
    // reads, and it is what makes an instance with its editor closed cost
    // nothing at all for visualisation rather than merely wasting it.
    hub.setCapturing (watching);
}

void TelemetryBridge::timerCallback()
{
    sendFrames();
}

juce::String TelemetryBridge::createScopeFrames()
{
    // MESSAGE THREAD.
    for (std::size_t i = 0; i < frames.size(); ++i)
        (void) telemetry::buildScopeFrame (hub.scope (static_cast<telemetry::ScopeSource> (i)),
                                           frames[i]);

    return makeScopeFramesMessage (frames);
}

void TelemetryBridge::sendFrames()
{
    if (! outboundHandler)
        return;

    outboundHandler (createScopeFrames());
}

} // namespace apollo::ui
