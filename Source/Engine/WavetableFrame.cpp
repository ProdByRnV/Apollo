#include "Engine/WavetableFrame.h"

#include "DSP/Oscillators/UnisonOscillator.h"

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

    // The cycle is drawn through an oscillator holding the published warp, so
    // the picture comes from the very mapping the voices play rather than from
    // a second copy of it that could drift (Phase 13b). With no warp it is the
    // table's own frame, point for point what this drew before warp existed:
    // the same continuous frame position the oscillator reads, so the picture
    // shows the blend between two frames rather than the nearest one.
    const auto mode = frame.warpMode >= 0 && frame.warpMode < dsp::UnisonOscillator::numWarpModes
                        ? static_cast<dsp::UnisonOscillator::WarpMode> (frame.warpMode)
                        : dsp::UnisonOscillator::WarpMode::off;

    dsp::UnisonOscillator shaper;
    shaper.setWarp (mode, frame.warpAmount);
    shaper.drawWarpedCycle (table, frame.position, frame.points.data(),
                            static_cast<int> (frame.points.size()));

    frame.valid = true;
}

} // namespace apollo::engine
