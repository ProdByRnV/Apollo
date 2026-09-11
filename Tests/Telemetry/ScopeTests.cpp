/*
    Visualisation transport tests.

    The whole point of this subsystem is that one thread writes it and another
    reads it, which is exactly the property a unit test cannot observe directly.
    What a test *can* pin down is everything either side of that boundary: that
    the ring returns the window it was given, in order, across a wrap; that an
    untouched source is distinguishable from a silent one; that a frame is
    aligned to a zero crossing when there is one and honest about free-running
    when there is not; and that the decimation shows the signal rather than an
    average that would flatter it.

    All of it is JUCE-free apart from the assertions, because the transport is.
    The processor-level test at the end is the one that needs a processor: it
    proves that what the audio thread actually writes is what a scope reads back.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <vector>

#include "Audio/ApolloAudioProcessor.h"
#include "Telemetry/ScopeBuffer.h"
#include "Telemetry/ScopeFrame.h"
#include "Telemetry/TelemetryHub.h"
#include "UI/TelemetryBridge.h"

using namespace apollo;

namespace
{

constexpr double testSampleRate = 48000.0;
constexpr int testBlockSize = 512;

/** Fills a buffer with a ramp, so every sample is identifiable by its value. */
[[nodiscard]] std::vector<float> ramp (int count, float start = 0.0f)
{
    std::vector<float> values (static_cast<std::size_t> (count));

    for (int i = 0; i < count; ++i)
        values[static_cast<std::size_t> (i)] = start + static_cast<float> (i);

    return values;
}

/** Writes @p count samples of a sine at @p cyclesPerWindow into the buffer. */
void writeSine (telemetry::ScopeBuffer& buffer, int count, double cyclesOverCount,
                float amplitude = 1.0f)
{
    std::vector<float> values (static_cast<std::size_t> (count));

    for (int i = 0; i < count; ++i)
    {
        const auto phase = 2.0 * juce::MathConstants<double>::pi * cyclesOverCount
                         * static_cast<double> (i) / static_cast<double> (count);

        values[static_cast<std::size_t> (i)] = amplitude * static_cast<float> (std::sin (phase));
    }

    buffer.write (values.data(), count);
}

class ScopeTests final : public juce::UnitTest
{
public:
    ScopeTests()
        : juce::UnitTest ("Visualisation transport", "Telemetry")
    {
    }

    void runTest() override
    {
        testInactiveUntilWritten();
        testWindowRoundTrip();
        testWrapAround();
        testSilenceIsCapturedNotInferred();
        testReset();
        testFrameSilence();
        testFrameTriggering();
        testFrameShowsTheSignal();
        testHubSourcesAreIndependent();
        testProcessorCapturesItsOutput();
        testBridgeSendsWhatWasCaptured();
    }

private:
    void testInactiveUntilWritten()
    {
        beginTest ("A source nothing writes is inactive, not silent");

        telemetry::ScopeBuffer buffer;
        expect (! buffer.isActive());

        std::vector<float> window (static_cast<std::size_t> (telemetry::scopeWindowSamples), -1.0f);
        expect (! buffer.readWindow (window.data(), telemetry::scopeWindowSamples),
                "reading an untouched source must fail rather than return zeros");
        expectWithinAbsoluteError (window[0], -1.0f, 1.0e-6f,
                                   "a failed read must leave the destination alone");

        telemetry::ScopeFrame frame;
        expect (! telemetry::buildScopeFrame (buffer, frame));
        expect (! frame.valid);
        expect (frame.silent, "an invalid frame defaults to silent, never to a stale trace");

        // One sample is enough to make it active.
        const float one = 0.5f;
        buffer.write (&one, 1);
        expect (buffer.isActive());
    }

    void testWindowRoundTrip()
    {
        beginTest ("The window read back is the window written, in order");

        telemetry::ScopeBuffer buffer;

        constexpr int written = telemetry::scopeWindowSamples + telemetry::ScopeBuffer::readMargin;
        const auto values = ramp (written);
        buffer.write (values.data(), written);

        std::vector<float> window (static_cast<std::size_t> (telemetry::scopeWindowSamples));
        expect (buffer.readWindow (window.data(), telemetry::scopeWindowSamples));

        // The window ends a margin behind the writer, so it is the first
        // `scopeWindowSamples` of the ramp rather than the last.
        for (int i = 0; i < telemetry::scopeWindowSamples; ++i)
            expectWithinAbsoluteError (window[static_cast<std::size_t> (i)],
                                       static_cast<float> (i), 1.0e-6f,
                                       "sample " + juce::String (i) + " is out of place");
    }

    void testWrapAround()
    {
        beginTest ("A window spanning the ring's wrap comes back contiguous");

        telemetry::ScopeBuffer buffer;

        // Fill the ring nearly twice, so the newest window certainly straddles
        // the wrap. Written in blocks, the way the audio thread writes it.
        constexpr int total = telemetry::scopeBufferSize * 2 - 777;
        constexpr int block = 333;

        float next = 0.0f;

        for (int written = 0; written < total; written += block)
        {
            const auto count = juce::jmin (block, total - written);
            const auto values = ramp (count, next);
            buffer.write (values.data(), count);
            next += static_cast<float> (count);
        }

        std::vector<float> window (static_cast<std::size_t> (telemetry::scopeWindowSamples));
        expect (buffer.readWindow (window.data(), telemetry::scopeWindowSamples));

        // Whatever the window starts at, it must be consecutive throughout —
        // which is the only thing a wrap could break.
        for (int i = 1; i < telemetry::scopeWindowSamples; ++i)
            expectWithinAbsoluteError (window[static_cast<std::size_t> (i)]
                                           - window[static_cast<std::size_t> (i - 1)],
                                       1.0f, 1.0e-3f,
                                       "the window is discontinuous at sample " + juce::String (i));

        const auto expectedFirst = static_cast<float> (
            total - telemetry::ScopeBuffer::readMargin - telemetry::scopeWindowSamples);

        expectWithinAbsoluteError (window[0], expectedFirst, 1.0e-3f,
                                   "the window must end a margin behind the writer");
    }

    void testSilenceIsCapturedNotInferred()
    {
        beginTest ("A source that goes quiet is seen to go quiet");

        telemetry::ScopeBuffer buffer;

        writeSine (buffer, telemetry::scopeBufferSize, 64.0);

        telemetry::ScopeFrame frame;
        expect (telemetry::buildScopeFrame (buffer, frame));
        expect (! frame.silent, "a sine at full scale is not silent");

        // Now it stops. The capture keeps running, writing the silence the
        // source is now producing, and the scope must follow it down rather than
        // hold the last picture (CLAUDE.md §26.1).
        buffer.writeSilence (telemetry::scopeBufferSize);

        expect (telemetry::buildScopeFrame (buffer, frame));
        expect (frame.silent, "the scope is still showing a stopped source as sounding");
        expect (frame.valid, "and it must still be a valid source, not an absent one");
        expectWithinAbsoluteError (frame.peak, 0.0f, 1.0e-9f);
    }

    void testReset()
    {
        beginTest ("Reset returns a source to untouched");

        telemetry::ScopeBuffer buffer;
        writeSine (buffer, telemetry::scopeBufferSize, 32.0);
        expect (buffer.isActive());

        buffer.reset();

        expect (! buffer.isActive(), "a reset source must read as never written");

        std::vector<float> window (static_cast<std::size_t> (telemetry::scopeWindowSamples));
        expect (! buffer.readWindow (window.data(), telemetry::scopeWindowSamples));
    }

    void testFrameSilence()
    {
        beginTest ("The silence threshold sits below anything audible");

        telemetry::ScopeBuffer buffer;
        telemetry::ScopeFrame frame;

        // Just under the threshold: silent.
        writeSine (buffer, telemetry::scopeBufferSize, 64.0,
                   telemetry::scopeSilenceThreshold * 0.5f);
        expect (telemetry::buildScopeFrame (buffer, frame));
        expect (frame.silent);

        // A hundred times the threshold is still -60 dBFS, and must not be
        // mistaken for silence: it is quiet, not absent.
        writeSine (buffer, telemetry::scopeBufferSize, 64.0,
                   telemetry::scopeSilenceThreshold * 100.0f);
        expect (telemetry::buildScopeFrame (buffer, frame));
        expect (! frame.silent, "a quiet source is not a stopped one");
        expect (! frame.triggered == false || frame.triggered,
                "triggering is reported either way, but must not crash on a tiny signal");
    }

    void testFrameTriggering()
    {
        beginTest ("A steady tone stands still; a flat line admits it is free-running");

        telemetry::ScopeBuffer buffer;

        // 128 cycles over the ring is 64 samples per cycle, so the first sample
        // after a rising crossing is sin(2π/64) above zero and no more. That is
        // the bound the trace has to meet, and stating it as a number taken from
        // the signal rather than a round figure is what makes the assertion mean
        // "it starts at the crossing" instead of "it starts somewhere near zero".
        constexpr double samplesPerCycle = 64.0;
        const auto oneSampleStep = static_cast<float> (
            std::sin (2.0 * juce::MathConstants<double>::pi / samplesPerCycle));

        writeSine (buffer, telemetry::scopeBufferSize, 128.0);

        telemetry::ScopeFrame first;
        expect (telemetry::buildScopeFrame (buffer, first));
        expect (first.triggered, "a full-scale sine must give the scope something to lock onto");

        expect (first.points[0] > 0.0f && first.points[0] <= oneSampleStep * 1.05f,
                "a triggered frame starts on the first sample after a rising crossing, not at "
                    + juce::String (first.points[0], 4));
        expect (first.points[1] > first.points[0],
                "and it is a *rising* crossing, so the trace goes up from it");

        // The same signal again, so the newest window is a different stretch of
        // audio holding the same wave. A triggered scope shows the same picture.
        writeSine (buffer, telemetry::scopeBufferSize, 128.0);

        telemetry::ScopeFrame second;
        expect (telemetry::buildScopeFrame (buffer, second));
        expect (second.triggered);

        for (std::size_t i = 0; i < first.points.size(); ++i)
            expectWithinAbsoluteError (second.points[i], first.points[i], 0.05f,
                                       "the trace moved between two identical signals");

        // A silent source is never reported as triggered: there is nothing to
        // lock onto, and saying otherwise would be a claim the interface might
        // act on.
        buffer.writeSilence (telemetry::scopeBufferSize);

        telemetry::ScopeFrame flat;
        expect (telemetry::buildScopeFrame (buffer, flat));
        expect (! flat.triggered);
        expect (flat.silent);
    }

    void testFrameShowsTheSignal()
    {
        beginTest ("Decimation samples the signal rather than averaging it away");

        telemetry::ScopeBuffer buffer;

        // The worst case for an averaging decimator: alternating samples, which
        // average to nothing and must not be drawn as a flat line. This is
        // exactly what an aliasing or clipping artefact looks like, and a scope
        // that smoothed it would hide the thing it exists to reveal.
        std::vector<float> alternating (static_cast<std::size_t> (telemetry::scopeBufferSize));

        for (std::size_t i = 0; i < alternating.size(); ++i)
            alternating[i] = (i % 2 == 0) ? 1.0f : -1.0f;

        buffer.write (alternating.data(), telemetry::scopeBufferSize);

        telemetry::ScopeFrame frame;
        expect (telemetry::buildScopeFrame (buffer, frame));

        expectWithinAbsoluteError (frame.peak, 1.0f, 1.0e-6f,
                                   "the peak must be the signal's, not the average's");
        expect (! frame.silent);

        for (const auto point : frame.points)
            expectWithinAbsoluteError (std::abs (point), 1.0f, 1.0e-6f,
                                       "an averaging decimator would have flattened this");
    }

    void testHubSourcesAreIndependent()
    {
        beginTest ("Each source captures only itself");

        telemetry::TelemetryHub hub;

        for (std::size_t i = 0; i < telemetry::scopeSourceCount; ++i)
            expect (! hub.scope (static_cast<telemetry::ScopeSource> (i)).isActive(),
                    "a fresh hub captures nothing");

        writeSine (hub.scope (telemetry::ScopeSource::output), telemetry::scopeBufferSize, 64.0);

        expect (hub.scope (telemetry::ScopeSource::output).isActive());

        for (std::size_t i = 1; i < telemetry::scopeSourceCount; ++i)
            expect (! hub.scope (static_cast<telemetry::ScopeSource> (i)).isActive(),
                    "writing one source must not activate another");

        hub.reset();
        expect (! hub.scope (telemetry::ScopeSource::output).isActive());

        // Every source has a token and a name, and no two tokens are the same:
        // the frontend keys its scopes on them.
        std::vector<juce::String> tokens;

        for (std::size_t i = 0; i < telemetry::scopeSourceCount; ++i)
        {
            const auto source = static_cast<telemetry::ScopeSource> (i);
            const auto token = juce::String (telemetry::toToken (source).data(),
                                             telemetry::toToken (source).size());

            expect (token.isNotEmpty(), "source " + juce::String (static_cast<int> (i))
                                            + " has no wire token");
            expect (! telemetry::describe (source).empty());

            for (const auto& seen : tokens)
                expect (seen != token, "duplicate scope token: " + token);

            tokens.push_back (token);
        }
    }

    void testProcessorCapturesItsOutput()
    {
        beginTest ("The output scope shows what the processor actually produced");

        ApolloAudioProcessor processor;
        processor.setRateAndBufferSizeDetails (testSampleRate, testBlockSize);
        processor.prepareToPlay (testSampleRate, testBlockSize);

        auto& scope = processor.getTelemetry().scope (telemetry::ScopeSource::output);

        expect (! scope.isActive(), "nothing is captured before the first block");

        juce::AudioBuffer<float> buffer (2, testBlockSize);
        juce::MidiBuffer midi;

        // Silence first. The capture runs regardless, so the source becomes
        // active and reads as silent — which is what lets an idle synthesiser
        // draw a flat line rather than nothing at all.
        buffer.clear();
        processor.processBlock (buffer, midi);

        expect (scope.isActive(), "the capture must run even when the engine is idle");

        telemetry::ScopeFrame frame;

        // Enough blocks to fill the window, then a note.
        for (int i = 0; i < telemetry::scopeBufferSize / testBlockSize + 2; ++i)
        {
            buffer.clear();
            juce::MidiBuffer empty;
            processor.processBlock (buffer, empty);
        }

        expect (telemetry::buildScopeFrame (scope, frame));
        expect (frame.silent, "an idle synthesiser reads as silent");

        midi.addEvent (juce::MidiMessage::noteOn (1, 69, 0.9f), 0);
        buffer.clear();
        processor.processBlock (buffer, midi);

        for (int i = 0; i < telemetry::scopeBufferSize / testBlockSize + 2; ++i)
        {
            buffer.clear();
            juce::MidiBuffer empty;
            processor.processBlock (buffer, empty);
        }

        expect (telemetry::buildScopeFrame (scope, frame));
        expect (frame.valid);
        expect (! frame.silent, "a sounding note must reach the output scope");
        expect (frame.peak > 0.01f, "the captured peak is implausibly low: "
                                        + juce::String (frame.peak, 6));
        expect (frame.peak <= 1.0f, "the capture must not exceed full scale");

        // And the capture goes with the audio: a reset leaves no picture of a
        // note that is no longer sounding.
        processor.reset();
        expect (! scope.isActive());
    }

    void testBridgeSendsWhatWasCaptured()
    {
        beginTest ("The frame the interface receives is the audio that was captured");

        ApolloAudioProcessor processor;
        processor.setRateAndBufferSizeDetails (testSampleRate, testBlockSize);
        processor.prepareToPlay (testSampleRate, testBlockSize);

        ui::TelemetryBridge bridge (processor.getTelemetry());

        std::vector<juce::String> sent;
        bridge.setOutboundHandler ([&sent] (const juce::String& message) { sent.push_back (message); });

        // Nothing captured yet, so the message carries no scopes at all rather
        // than six empty ones.
        {
            juce::var parsed;
            expect (juce::JSON::parse (bridge.createScopeFrames(), parsed).wasOk());

            auto* object = parsed.getDynamicObject();
            expect (object != nullptr);

            if (object == nullptr)
                return;

            expectEquals (object->getProperty ("type").toString(), juce::String ("scopeFrames"));

            const auto* scopeList = object->getProperty ("scopes").getArray();
            expect (scopeList != nullptr);
            expectEquals (scopeList != nullptr ? scopeList->size() : -1, 0,
                          "a source nothing has captured must not appear at all");
        }

        // Play something, and render enough to fill the window.
        juce::AudioBuffer<float> buffer (2, testBlockSize);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 57, 0.9f), 0);

        buffer.clear();
        processor.processBlock (buffer, midi);

        for (int i = 0; i < telemetry::scopeBufferSize / testBlockSize + 2; ++i)
        {
            buffer.clear();
            juce::MidiBuffer empty;
            processor.processBlock (buffer, empty);
        }

        bridge.sendFrames();

        expect (! sent.empty(), "the bridge sent nothing at all");

        if (sent.empty())
            return;

        juce::var parsed;
        expect (juce::JSON::parse (sent.back(), parsed).wasOk(),
                "the frame message is not valid JSON");

        auto* object = parsed.getDynamicObject();
        expect (object != nullptr);

        if (object == nullptr)
            return;

        const auto* scopeList = object->getProperty ("scopes").getArray();
        expect (scopeList != nullptr);
        expectEquals (scopeList != nullptr ? scopeList->size() : -1, 1,
                      "only the output is captured in this build");

        if (scopeList == nullptr || scopeList->isEmpty())
            return;

        auto* entry = scopeList->getFirst().getDynamicObject();
        expect (entry != nullptr);

        if (entry == nullptr)
            return;

        expectEquals (entry->getProperty ("source").toString(), juce::String ("output"));
        expect (! static_cast<bool> (entry->getProperty ("silent")),
                "a sounding note must not be reported as silence");

        const auto* points = entry->getProperty ("points").getArray();
        expect (points != nullptr);
        expectEquals (points != nullptr ? points->size() : -1, telemetry::scopeFramePoints);

        if (points == nullptr)
            return;

        // The trace is a waveform, not a straight line and not a constant: at
        // least one point differs from the first, every point is inside full
        // scale, and none of them is a number that cannot be drawn.
        bool varies = false;

        for (const auto& point : *points)
        {
            const auto value = static_cast<double> (point);

            expect (std::isfinite (value), "a non-finite point reached the interface");
            expect (value >= -1.0 && value <= 1.0,
                    "a point outside full scale reached the interface: " + juce::String (value));

            if (std::abs (value - static_cast<double> (points->getFirst())) > 1.0e-4)
                varies = true;
        }

        expect (varies, "the trace is flat, so nothing of the waveform survived the transport");

        // Detaching stops the sending, which is what makes a closed editor cost
        // nothing beyond the capture itself.
        const auto before = sent.size();
        bridge.setOutboundHandler ({});
        bridge.sendFrames();
        expectEquals (sent.size(), before, "a detached bridge must send nothing");
    }
};

ScopeTests scopeTests;

} // namespace
