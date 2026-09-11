/*
    Modulator traces, the output meter, the voice count and the wavetable
    displays.

    Separate from ScopeTests because the subject is different in kind. A scope is
    a window of audio and its tests are about a ring: ordering, wrapping, and a
    reader standing behind a writer. Everything here is a *reading* — a number
    the audio thread published about itself — and the claims worth making are
    about whether those numbers mean what the interface will assume they mean.

    Four of them carry most of the weight, because each is a way this could look
    plausible and be wrong:

      - an envelope's trace follows the envelope, including through a stage the
        settings alone would not predict;
      - the meter rises instantly and falls slowly, which is the entire
        difference between a meter and the scope's peak;
      - a clip is still reported after the sample that caused it has gone;
      - the wavetable display follows the *effective* position — the parameter
        plus whatever the matrix adds — rather than the parameter alone.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <vector>

#include "Audio/ApolloAudioProcessor.h"
#include "DSP/Oscillators/WavetableLibrary.h"
#include "Engine/WavetableFrame.h"
#include "Telemetry/InstrumentFrame.h"
#include "Telemetry/LevelMeter.h"
#include "Telemetry/ModulationTrace.h"
#include "Telemetry/TelemetryHub.h"
#include "UI/TelemetryBridge.h"

using namespace apollo;

namespace
{

constexpr double testSampleRate = 48000.0;
constexpr int testBlockSize = 512;

void runBlocks (ApolloAudioProcessor& processor, int count)
{
    juce::AudioBuffer<float> buffer (2, testBlockSize);

    for (int i = 0; i < count; ++i)
    {
        juce::MidiBuffer empty;
        buffer.clear();
        processor.processBlock (buffer, empty);
    }
}

/** Blocks covering roughly @p seconds of audio. */
[[nodiscard]] int blocksForSeconds (double seconds)
{
    return static_cast<int> (seconds * testSampleRate / static_cast<double> (testBlockSize)) + 1;
}

void setParameter (ApolloAudioProcessor& processor, const juce::String& id, float value)
{
    if (auto* parameter = processor.getValueTreeState().getParameter (id))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

/** Feeds a meter a block of a constant magnitude. */
void feed (telemetry::LevelMeter& meter, float magnitude, int numSamples)
{
    std::vector<float> left (static_cast<std::size_t> (numSamples), magnitude);
    std::vector<float> right (static_cast<std::size_t> (numSamples), magnitude);

    const float* channels[] = { left.data(), right.data() };

    meter.process (channels, 2, 0, numSamples);
}

class InstrumentTelemetryTests final : public juce::UnitTest
{
public:
    InstrumentTelemetryTests()
        : juce::UnitTest ("Instrument telemetry", "Telemetry")
    {
    }

    void runTest() override
    {
        testTraceIsInactiveUntilWritten();
        testTraceIsAWindowOfHistory();
        testMeterRisesInstantlyAndFallsSlowly();
        testMeterRmsIsNotPeak();
        testClipOutlivesTheSampleThatCausedIt();
        testEnvelopeTraceFollowsTheEnvelope();
        testWavetableFrameFollowsThePosition();
        testUnroutedModulatorsSaySo();
        testFrameReachesTheInterface();
    }

private:
    void testTraceIsInactiveUntilWritten()
    {
        beginTest ("A modulator nothing traces is inactive, not still");

        telemetry::ModulationTrace trace;
        expect (! trace.isActive());

        std::vector<float> points (static_cast<std::size_t> (telemetry::modulationTracePoints),
                                   -5.0f);

        expect (! trace.read (points.data()),
                "reading an untouched trace must fail rather than return zeros");
        expectWithinAbsoluteError (points[0], -5.0f, 1.0e-6f,
                                   "a failed read must leave the destination alone");

        trace.write (0.25f);
        expect (trace.isActive());

        expect (trace.read (points.data()));
        expectWithinAbsoluteError (points.back(), 0.25f, 1.0e-6f,
                                   "the newest entry must be last");
    }

    void testTraceIsAWindowOfHistory()
    {
        beginTest ("The trace is the last second, oldest first, across the wrap");

        telemetry::ModulationTrace trace;

        // Two and a half windows, so the ring has wrapped and what comes back is
        // the *most recent* window rather than the first one written.
        const auto written = telemetry::modulationTracePoints * 5 / 2;

        for (int i = 0; i < written; ++i)
            trace.write (static_cast<float> (i));

        std::vector<float> points (static_cast<std::size_t> (telemetry::modulationTracePoints));
        expect (trace.read (points.data()));

        for (int i = 0; i < telemetry::modulationTracePoints; ++i)
        {
            const auto expected = static_cast<float> (written - telemetry::modulationTracePoints + i);

            expectWithinAbsoluteError (points[static_cast<std::size_t> (i)], expected, 1.0e-6f,
                                       "entry " + juce::String (i) + " is out of place");
        }
    }

    void testMeterRisesInstantlyAndFallsSlowly()
    {
        beginTest ("The meter takes a peak at once and gives it up slowly");

        telemetry::LevelMeter meter;
        meter.prepare (testSampleRate);

        // One block at half scale, and the peak must already be there: a meter
        // that smoothed its attack would under-report exactly the transients
        // worth reporting.
        feed (meter, 0.5f, testBlockSize);

        auto reading = meter.read();
        expect (reading.active);
        expectWithinAbsoluteError (reading.peak[0], 0.5f, 1.0e-4f,
                                   "the peak must arrive in the block it happened in");

        // Then silence. After a tenth of a second the peak has fallen by about
        // 2 dB, which is 20 dB per second and is still plainly readable.
        feed (meter, 0.0f, static_cast<int> (testSampleRate * 0.1));

        const auto afterShortSilence = meter.read().peak[0];

        expect (afterShortSilence < 0.5f, "the peak must decay at all");
        expect (afterShortSilence > 0.3f,
                "the peak fell far too fast to read: " + juce::String (afterShortSilence, 4));

        // A second and a half of silence takes 30 dB off it, so it is well down
        // but the meter has not simply been reset.
        feed (meter, 0.0f, static_cast<int> (testSampleRate * 1.5));

        const auto afterLongSilence = meter.read().peak[0];

        expect (afterLongSilence < afterShortSilence * 0.2f,
                "a second and a half of silence must take the peak most of the way down");
    }

    void testMeterRmsIsNotPeak()
    {
        beginTest ("RMS and peak disagree, which is why there are two of them");

        telemetry::LevelMeter meter;
        meter.prepare (testSampleRate);

        // A signal that is silent except for one sample at full scale. Peak and
        // RMS must say completely different things about it — if they agreed,
        // one of the two readings would be pointless.
        constexpr int blockSamples = 512;
        std::vector<float> spike (static_cast<std::size_t> (blockSamples), 0.0f);
        spike[0] = 0.9f;

        const float* channels[] = { spike.data(), spike.data() };

        // Long enough for the 300 ms average to settle on the true energy.
        for (int i = 0; i < 200; ++i)
            meter.process (channels, 2, 0, blockSamples);

        const auto reading = meter.read();

        expect (reading.peak[0] > 0.8f,
                "the peak must see the spike: " + juce::String (reading.peak[0], 4));
        expect (reading.rms[0] < 0.1f,
                "the RMS must not: " + juce::String (reading.rms[0], 4));

        // And a steady tone at the same level reads the other way round: for a
        // constant signal the two readings agree, which is the sanity check on
        // the RMS arithmetic itself.
        telemetry::LevelMeter steady;
        steady.prepare (testSampleRate);

        for (int i = 0; i < 200; ++i)
            feed (steady, 0.9f, blockSamples);

        const auto steadyReading = steady.read();

        expectWithinAbsoluteError (steadyReading.rms[0], 0.9f, 0.02f,
                                   "a constant signal's RMS is its own level");
    }

    void testClipOutlivesTheSampleThatCausedIt()
    {
        beginTest ("A clip is still reported after the sample that caused it has gone");

        telemetry::LevelMeter meter;
        meter.prepare (testSampleRate);

        feed (meter, 0.5f, testBlockSize);
        expect (! meter.read().clipped, "half scale is not a clip");

        feed (meter, 1.0f, testBlockSize);
        expect (meter.read().clipped, "full scale is a clip");

        // Half a second later the offending samples are long gone and the
        // indicator is still up, which is the whole point of holding it.
        feed (meter, 0.1f, static_cast<int> (testSampleRate * 0.5));
        expect (meter.read().clipped, "the clip must outlast the block it happened in");

        // And it clears itself, so one clip does not leave the instrument
        // looking broken for ever.
        feed (meter, 0.1f, static_cast<int> (testSampleRate * 1.5));
        expect (! meter.read().clipped, "the hold must expire");
    }

    void testEnvelopeTraceFollowsTheEnvelope()
    {
        beginTest ("The envelope trace is the envelope, not a picture of its settings");

        ApolloAudioProcessor processor;
        processor.setRateAndBufferSizeDetails (testSampleRate, testBlockSize);
        processor.prepareToPlay (testSampleRate, testBlockSize);
        processor.getTelemetry().setCapturing (true);

        auto& hub = processor.getTelemetry();
        auto& trace = hub.trace (telemetry::ModulatorSource::envelope1);

        // A long attack, so the rise happens over many trace entries rather than
        // between two of them.
        setParameter (processor, "env1_attack", 400.0f);
        setParameter (processor, "env1_decay", 400.0f);
        setParameter (processor, "env1_sustain", 0.7f);
        setParameter (processor, "env1_release", 300.0f);

        juce::AudioBuffer<float> buffer (2, testBlockSize);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 57, 0.9f), 0);

        buffer.clear();
        processor.processBlock (buffer, midi);

        // Part-way through the attack.
        runBlocks (processor, blocksForSeconds (0.15));

        std::vector<float> points (static_cast<std::size_t> (telemetry::modulationTracePoints));
        expect (trace.read (points.data()));

        const auto duringAttack = points.back();

        expect (duringAttack > 0.05f && duringAttack < 0.95f,
                "part-way up a 400 ms attack the envelope should be part-way up, not "
                    + juce::String (duringAttack, 4));

        expect (hub.snapshot().envelopeStage[0].load() == 2,
                "the reported stage should be attack");

        // The trace rises, which is the claim: a trace that merely held the
        // current value would be flat and would still have passed everything
        // above.
        expect (points.back() > points.front(),
                "the trace must show the rise, not just the present value");

        // At sustain, and the value is the sustain level rather than whatever
        // the attack left behind.
        runBlocks (processor, blocksForSeconds (1.2));
        expect (trace.read (points.data()));

        expectWithinAbsoluteError (points.back(), 0.7f, 0.05f,
                                   "the envelope should be sitting at its sustain level");
        expect (hub.snapshot().envelopeStage[0].load() == 5, "the stage should be sustain");

        // And the release is *seen*, which the settings alone could not tell you
        // the timing of.
        processor.getVoiceEngine().noteOff (57);
        runBlocks (processor, blocksForSeconds (0.1));
        expect (trace.read (points.data()));

        expect (points.back() < 0.7f, "the release must show as a fall");
        expect (points.back() > 0.0f, "and must not have finished already");
    }

    void testWavetableFrameFollowsThePosition()
    {
        beginTest ("The wavetable display shows the position the oscillator is reading");

        dsp::WavetableLibrary library;

        telemetry::WavetableFrame first;
        first.tableIndex = 0;
        first.position = 0.0f;
        engine::fillWavetableFrame (library, first);

        telemetry::WavetableFrame last;
        last.tableIndex = 0;
        last.position = 1.0f;
        engine::fillWavetableFrame (library, last);

        expect (first.valid && last.valid);

        // The built-in tables are morphs, so the two ends of one are different
        // waves. A display that ignored the position would return the same
        // points for both and pass every other check here.
        double difference = 0.0;

        for (std::size_t i = 0; i < first.points.size(); ++i)
            difference += std::abs (static_cast<double> (first.points[i])
                                    - static_cast<double> (last.points[i]));

        expect (difference > 1.0,
                "the two ends of a wavetable must not draw the same wave: "
                    + juce::String (difference, 4));

        // The drawn wave is a real waveform: bounded, and not a flat line.
        float lowest = 1.0f;
        float highest = -1.0f;

        for (const auto value : first.points)
        {
            expect (std::isfinite (value), "a non-finite sample reached the display");
            lowest = juce::jmin (lowest, value);
            highest = juce::jmax (highest, value);
        }

        expect (highest > 0.1f && lowest < -0.1f,
                "the drawn wave should swing both ways");
        expect (highest <= 1.01f && lowest >= -1.01f, "and stay inside full scale");

        // An out-of-range table is clamped by the library rather than refused,
        // so the display stays valid rather than blanking on a bad index.
        telemetry::WavetableFrame outOfRange;
        outOfRange.tableIndex = 99;
        engine::fillWavetableFrame (library, outOfRange);
        expect (outOfRange.valid, "an out-of-range index must not blank the display");
    }

    void testUnroutedModulatorsSaySo()
    {
        beginTest ("A modulator nothing routes is reported as unrouted");

        ApolloAudioProcessor processor;
        processor.setRateAndBufferSizeDetails (testSampleRate, testBlockSize);
        processor.prepareToPlay (testSampleRate, testBlockSize);
        processor.getTelemetry().setCapturing (true);

        auto& hub = processor.getTelemetry();

        runBlocks (processor, blocksForSeconds (0.2));

        const auto& snapshot = hub.snapshot();

        // Envelope 1 shapes every voice whether or not the matrix reads it, so it
        // is always doing something.
        expect (snapshot.routed[0].load(), "envelope 1 is always in use");

        expect (! snapshot.routed[static_cast<std::size_t> (telemetry::ModulatorSource::lfo1)].load(),
                "an unrouted LFO must not claim to be running");

        // Route LFO 1 to something, and it says so.
        setParameter (processor, "mod01_source",
                      static_cast<float> (static_cast<int> (dsp::ModSource::lfo1)));
        setParameter (processor, "mod01_destination",
                      static_cast<float> (static_cast<int> (dsp::ModDestination::filter1Cutoff)));
        setParameter (processor, "mod01_depth", 0.5f);

        runBlocks (processor, blocksForSeconds (0.2));

        expect (snapshot.routed[static_cast<std::size_t> (telemetry::ModulatorSource::lfo1)].load(),
                "a routed LFO must be reported as routed");
    }

    void testFrameReachesTheInterface()
    {
        beginTest ("The instrument frame the interface receives describes the instrument");

        ApolloAudioProcessor processor;
        processor.setRateAndBufferSizeDetails (testSampleRate, testBlockSize);
        processor.prepareToPlay (testSampleRate, testBlockSize);

        ui::TelemetryBridge bridge (processor.getTelemetry(),
                                    processor.getVoiceEngine().getWavetableLibrary());

        std::vector<juce::String> sent;
        bridge.setOutboundHandler ([&sent] (const juce::String& message) { sent.push_back (message); });

        juce::AudioBuffer<float> buffer (2, testBlockSize);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 57, 0.9f), 0);

        buffer.clear();
        processor.processBlock (buffer, midi);

        runBlocks (processor, blocksForSeconds (0.4));

        bridge.sendInstrumentFrame();
        expect (! sent.empty(), "the bridge sent nothing at all");

        if (sent.empty())
            return;

        // A budget, and a generous one. The frame carries eight traces of 128
        // points and two waveforms of 128 more, which is a bigger message than
        // anything else Apollo sends — worth a number in the log so that a
        // change which quietly multiplies it is noticed here rather than in a
        // profiler.
        const auto bytes = sent.back().getNumBytesAsUTF8();
        logMessage ("instrument frame: " + juce::String (static_cast<int> (bytes)) + " bytes");

        expect (bytes < 12u * 1024u,
                "the instrument frame has grown to " + juce::String (static_cast<int> (bytes))
                    + " bytes");

        juce::var parsed;
        expect (juce::JSON::parse (sent.back(), parsed).wasOk(),
                "the instrument frame is not valid JSON");

        auto* object = parsed.getDynamicObject();
        expect (object != nullptr);

        if (object == nullptr)
            return;

        expectEquals (object->getProperty ("type").toString(), juce::String ("instrumentFrame"));

        // Every modulator, each named exactly once: the page keys its traces on
        // these tokens, so a collision would send two modulators to one canvas.
        const auto* modulators = object->getProperty ("modulators").getArray();
        expect (modulators != nullptr);
        expectEquals (modulators != nullptr ? modulators->size() : -1,
                      static_cast<int> (telemetry::modulatorSourceCount));

        if (modulators == nullptr)
            return;

        for (std::size_t i = 0; i < telemetry::modulatorSourceCount; ++i)
        {
            const auto token = telemetry::toToken (static_cast<telemetry::ModulatorSource> (i));
            const juce::String expected (token.data(), token.size());

            int seen = 0;

            for (const auto& item : *modulators)
                if (auto* entry = item.getDynamicObject())
                    if (entry->getProperty ("source").toString() == expected)
                        ++seen;

            expectEquals (seen, 1, "the frame names " + expected + " other than exactly once");
        }

        // The first entry is envelope 1, and it carries a real trace rather than
        // an empty array.
        if (auto* first = modulators->getFirst().getDynamicObject())
        {
            expectEquals (first->getProperty ("source").toString(), juce::String ("env1"));

            const auto* points = first->getProperty ("points").getArray();
            expect (points != nullptr);
            expectEquals (points != nullptr ? points->size() : -1,
                          telemetry::modulationTracePoints);

            expect (static_cast<double> (first->getProperty ("current")) > 0.0,
                    "a held note's amplitude envelope is not at zero");
        }

        // Both wavetable displays, named by oscillator rather than by position.
        const auto* wavetables = object->getProperty ("wavetables").getArray();
        expect (wavetables != nullptr);
        expectEquals (wavetables != nullptr ? wavetables->size() : -1,
                      static_cast<int> (telemetry::wavetableDisplayCount));

        if (wavetables != nullptr)
        {
            for (const auto& item : *wavetables)
            {
                auto* entry = item.getDynamicObject();
                expect (entry != nullptr);

                if (entry == nullptr)
                    continue;

                const auto oscillator = static_cast<int> (entry->getProperty ("osc"));
                expect (oscillator >= 1 && oscillator <= 2,
                        "a wavetable entry must name its oscillator");

                const auto* points = entry->getProperty ("points").getArray();
                expect (points != nullptr);
                expectEquals (points != nullptr ? points->size() : -1,
                              telemetry::wavetableFramePoints);
            }
        }

        // The meter, reporting the note that is sounding.
        auto* meter = object->getProperty ("meter").getDynamicObject();
        expect (meter != nullptr);

        if (meter != nullptr)
        {
            expect (static_cast<bool> (meter->getProperty ("active")),
                    "the meter must be active once a block has been processed");

            const auto* peak = meter->getProperty ("peak").getArray();
            expect (peak != nullptr);
            expectEquals (peak != nullptr ? peak->size() : -1, telemetry::meterChannels);

            if (peak != nullptr && ! peak->isEmpty())
                expect (static_cast<double> (peak->getFirst()) > 0.0,
                        "a sounding note must reach the meter");
        }

        expectEquals (static_cast<int> (object->getProperty ("voices")), 1,
                      "one note is one voice");
        expect (static_cast<int> (object->getProperty ("polyphony")) > 0);

        // Detaching stops the sending, exactly as it does for the scopes.
        const auto before = sent.size();
        bridge.setOutboundHandler ({});
        bridge.sendInstrumentFrame();
        expectEquals (sent.size(), before, "a detached bridge must send nothing");
    }
};

InstrumentTelemetryTests instrumentTelemetryTests;

} // namespace
