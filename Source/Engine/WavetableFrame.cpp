#include "Engine/WavetableFrame.h"

namespace apollo::engine
{

void fillWavetableFrame (const dsp::WavetableLibrary& library, telemetry::WavetableFrame& frame)
{
    // MESSAGE THREAD.
    const auto& table = library.getTable (frame.tableIndex);

    if (table.isEmpty())
    {
        frame.valid = false;
        return;
    }

    const auto lastFrame = static_cast<double> (table.getNumFrames() - 1);
    const auto clampedPosition = frame.position < 0.0f ? 0.0f
                               : (frame.position > 1.0f ? 1.0f : frame.position);

    // The same continuous frame position the oscillator itself reads, so the
    // picture shows the blend between two frames rather than the nearest one.
    const auto framePosition = static_cast<double> (clampedPosition) * lastFrame;

    constexpr auto points = telemetry::wavetableFramePoints;

    for (int i = 0; i < points; ++i)
    {
        const auto phase = static_cast<double> (i) / static_cast<double> (points);

        frame.points[static_cast<std::size_t> (i)]
            = table.getSampleAtPosition (0, framePosition, phase);
    }

    frame.valid = true;
}

} // namespace apollo::engine
