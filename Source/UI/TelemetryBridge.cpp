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
}

void TelemetryBridge::setOutboundHandler (OutboundHandler handler)
{
    outboundHandler = std::move (handler);

    // The timer follows the handler rather than running regardless. With no
    // editor attached there is nobody to draw a frame, and building one anyway
    // would be thirty JSON documents a second serialized into nothing.
    if (outboundHandler)
        startTimerHz (frameRateHz);
    else
        stopTimer();
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
