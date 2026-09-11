#include "Telemetry/InstrumentFrame.h"

namespace apollo::telemetry
{

bool buildInstrumentFrame (const TelemetryHub& hub, InstrumentFrame& frame)
{
    // MESSAGE THREAD.
    const auto& instrument = hub.snapshot();

    bool anything = false;

    for (std::size_t i = 0; i < modulatorSourceCount; ++i)
    {
        const auto source = static_cast<ModulatorSource> (i);
        auto& modulator = frame.modulators[i];

        modulator.valid = hub.trace (source).read (modulator.points.data());

        if (! modulator.valid)
        {
            modulator = ModulationFrame {};
            continue;
        }

        anything = true;

        // The newest entry is the last one the read produced, because the read
        // returns the ring oldest first.
        modulator.current = modulator.points.back();
        modulator.routed = instrument.routed[i].load (std::memory_order_relaxed);
        modulator.envelopeStage = instrument.envelopeStage[i].load (std::memory_order_relaxed);
    }

    frame.meter = hub.outputMeter().read();
    anything = anything || frame.meter.active;

    frame.activeVoices = instrument.activeVoices.load (std::memory_order_relaxed);
    frame.polyphony = instrument.polyphony.load (std::memory_order_relaxed);

    for (std::size_t i = 0; i < wavetableDisplayCount; ++i)
    {
        auto& wavetable = frame.wavetables[i];

        wavetable.tableIndex = instrument.wavetableIndex[i].load (std::memory_order_relaxed);
        wavetable.position = instrument.wavetablePosition[i].load (std::memory_order_relaxed);

        // Left invalid here. The points are the engine's to fill, and a frame
        // whose points were never filled must not be drawn as a flat wave.
        wavetable.valid = false;
    }

    return anything;
}

} // namespace apollo::telemetry
