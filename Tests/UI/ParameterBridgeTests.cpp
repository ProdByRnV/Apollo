/*
    Parameter bridge tests.

    Covers the synchronisation requirements of UI_BINDINGS.md §20: that the
    initial snapshot matches APVTS, that UI commands reach APVTS, that changes
    originating anywhere else propagate back to the UI, and that gestures map
    onto the host's automation boundaries.

    flushPendingUpdates() is driven directly rather than waiting on the timer, so
    these tests are deterministic and need no running message loop.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

#include "Audio/ApolloAudioProcessor.h"
#include "Parameters/ParameterDefinitions.h"
#include "Parameters/ParameterLayout.h"
#include "UI/ParameterBridge.h"

using namespace apollo;

namespace
{

/** Collects everything the bridge sends, standing in for the WebView. */
struct OutboundRecorder
{
    std::vector<juce::String> messages;

    [[nodiscard]] ui::ParameterBridge::OutboundHandler handler()
    {
        return [this] (const juce::String& message) { messages.push_back (message); };
    }

    [[nodiscard]] int countOfType (const juce::String& type) const
    {
        int count = 0;

        for (const auto& message : messages)
        {
            juce::var parsed;

            if (juce::JSON::parse (message, parsed).wasOk())
                if (auto* object = parsed.getDynamicObject())
                    if (object->getProperty ("type").toString() == type)
                        ++count;
        }

        return count;
    }

    [[nodiscard]] bool containsParameterChange (const juce::String& id) const
    {
        for (const auto& message : messages)
        {
            juce::var parsed;

            if (juce::JSON::parse (message, parsed).wasOk())
                if (auto* object = parsed.getDynamicObject())
                    if (object->getProperty ("type").toString() == "parameterChanged"
                        && object->getProperty ("id").toString() == id)
                        return true;
        }

        return false;
    }
};

class ParameterBridgeTests final : public juce::UnitTest
{
public:
    ParameterBridgeTests()
        : juce::UnitTest ("Parameter bridge", "UI")
    {
    }

    void runTest() override
    {
        testSnapshotMatchesApvts();
        testRequestStateReturnsSnapshot();
        testRequestMetadataReturnsMetadata();
        testSetParameterReachesApvts();
        testInvalidCommandsReturnErrorsAndChangeNothing();
        testExternalChangesPropagateToUi();
        testUpdatesAreCoalesced();
        testDetachedHandlerIsSafe();
        testMarkAllParametersDirty();
        testMetadataDescribesTheRegistry();
    }

private:
    void testSnapshotMatchesApvts()
    {
        beginTest ("The initial snapshot matches APVTS exactly");

        ApolloAudioProcessor processor;
        ui::ParameterBridge bridge (processor.getValueTreeState());

        juce::var parsed;
        expect (juce::JSON::parse (bridge.createStateSnapshot(), parsed).wasOk());

        auto* snapshot = parsed.getDynamicObject();
        expect (snapshot != nullptr);

        if (snapshot == nullptr)
            return;

        auto* parameters = snapshot->getProperty ("parameters").getDynamicObject();
        expect (parameters != nullptr, "the snapshot must carry a parameters object");

        if (parameters == nullptr)
            return;

        for (const auto& definition : params::parameterDefinitions)
        {
            const auto id = params::toJuceString (definition.id);

            expect (parameters->hasProperty (id), id + ": missing from the snapshot");

            const auto* parameter = processor.getValueTreeState().getParameter (id);
            expect (parameter != nullptr);

            if (parameter == nullptr)
                continue;

            expectWithinAbsoluteError (
                static_cast<float> (static_cast<double> (parameters->getProperty (id))),
                parameter->getValue(), 1.0e-5f,
                id + ": snapshot disagrees with APVTS");
        }
    }

    void testRequestStateReturnsSnapshot()
    {
        beginTest ("requestState is answered with a snapshot");

        ApolloAudioProcessor processor;
        ui::ParameterBridge bridge (processor.getValueTreeState());

        juce::var parsed;
        expect (juce::JSON::parse (bridge.handleMessage (R"({"type":"requestState","version":1})"),
                                   parsed).wasOk());
        expectEquals (parsed.getDynamicObject()->getProperty ("type").toString(),
                      juce::String ("stateSnapshot"));
    }

    void testRequestMetadataReturnsMetadata()
    {
        beginTest ("requestMetadata is answered with the registry description");

        ApolloAudioProcessor processor;
        ui::ParameterBridge bridge (processor.getValueTreeState());

        juce::var parsed;
        expect (juce::JSON::parse (bridge.handleMessage (R"({"type":"requestMetadata","version":1})"),
                                   parsed).wasOk());
        expectEquals (parsed.getDynamicObject()->getProperty ("type").toString(),
                      juce::String ("parameterMetadata"));
    }

    void testSetParameterReachesApvts()
    {
        beginTest ("A validated setParameter reaches APVTS");

        ApolloAudioProcessor processor;
        auto& apvts = processor.getValueTreeState();
        ui::ParameterBridge bridge (apvts);

        const auto reply = bridge.handleMessage (
            R"({"type":"setParameter","version":1,"id":"filter1_cutoff","normalizedValue":0.4})");

        expect (reply.isEmpty(), "a successful set needs no immediate reply");
        expectWithinAbsoluteError (apvts.getParameter ("filter1_cutoff")->getValue(), 0.4f, 1.0e-5f);
    }

    void testInvalidCommandsReturnErrorsAndChangeNothing()
    {
        beginTest ("Invalid commands produce errors and leave APVTS untouched");

        ApolloAudioProcessor processor;
        auto& apvts = processor.getValueTreeState();
        ui::ParameterBridge bridge (apvts);

        const auto before = apvts.getParameter ("filter1_cutoff")->getValue();

        for (const auto* json : {
                 R"({"type":"setParameter","version":1,"id":"filter1_cutoff","normalizedValue":7})",
                 R"({"type":"setParameter","version":2,"id":"filter1_cutoff","normalizedValue":0.1})",
                 R"({"type":"setParameter","version":1,"id":"nope_nope","normalizedValue":0.1})",
                 R"({"type":"evaluate","version":1})",
                 "garbage" })
        {
            juce::var parsed;
            expect (juce::JSON::parse (bridge.handleMessage (json), parsed).wasOk(),
                    juce::String ("error reply must be valid JSON for: ") + json);
            expectEquals (parsed.getDynamicObject()->getProperty ("type").toString(),
                          juce::String ("error"),
                          juce::String ("expected an error reply for: ") + json);
        }

        expectWithinAbsoluteError (apvts.getParameter ("filter1_cutoff")->getValue(), before, 1.0e-6f,
                                   "a rejected command must not change any parameter");
    }

    /** Host automation, preset recall and MIDI all change parameters without the
        UI knowing. Those changes must flow back out (UI_BINDINGS.md §9).
    */
    void testExternalChangesPropagateToUi()
    {
        beginTest ("Changes made outside the UI propagate back to the UI");

        ApolloAudioProcessor processor;
        auto& apvts = processor.getValueTreeState();
        ui::ParameterBridge bridge (apvts);

        OutboundRecorder recorder;
        bridge.setOutboundHandler (recorder.handler());

        // Simulates the host automating a parameter.
        apvts.getParameter ("master_gain")->setValueNotifyingHost (0.2f);

        bridge.flushPendingUpdates();

        expect (recorder.containsParameterChange ("master_gain"),
                "an externally changed parameter must be reported to the UI");
    }

    /** A swept parameter changes far faster than any display can show. The
        bridge must coalesce, not forward every intermediate value.
    */
    void testUpdatesAreCoalesced()
    {
        beginTest ("Rapid changes are coalesced into one update per flush");

        ApolloAudioProcessor processor;
        auto& apvts = processor.getValueTreeState();
        ui::ParameterBridge bridge (apvts);

        OutboundRecorder recorder;
        bridge.setOutboundHandler (recorder.handler());

        auto* parameter = apvts.getParameter ("filter1_cutoff");

        for (int i = 0; i < 200; ++i)
            parameter->setValueNotifyingHost (static_cast<float> (i) / 200.0f);

        bridge.flushPendingUpdates();

        expectEquals (recorder.countOfType ("parameterChanged"), 1,
                      "200 changes between flushes must coalesce into one message");

        // And the value reported is the latest, not the first.
        juce::var parsed;
        juce::JSON::parse (recorder.messages.front(), parsed);
        expectWithinAbsoluteError (
            static_cast<float> (static_cast<double> (
                parsed.getDynamicObject()->getProperty ("normalizedValue"))),
            parameter->getValue(), 1.0e-5f,
            "the coalesced update must carry the current value");

        // Nothing further is sent until something changes again.
        recorder.messages.clear();
        bridge.flushPendingUpdates();
        expectEquals (recorder.countOfType ("parameterChanged"), 0,
                      "a flush with no changes must send nothing");
    }

    /** The editor can close at any time; the bridge outlives it. */
    void testDetachedHandlerIsSafe()
    {
        beginTest ("Flushing with no handler attached is safe");

        ApolloAudioProcessor processor;
        auto& apvts = processor.getValueTreeState();
        ui::ParameterBridge bridge (apvts);

        OutboundRecorder recorder;
        bridge.setOutboundHandler (recorder.handler());
        bridge.setOutboundHandler ({});

        apvts.getParameter ("master_gain")->setValueNotifyingHost (0.4f);
        bridge.flushPendingUpdates();

        expect (recorder.messages.empty(), "a detached handler must receive nothing");

        // Reattaching must not replay stale changes as if they were new.
        bridge.setOutboundHandler (recorder.handler());
        bridge.flushPendingUpdates();
        expect (recorder.messages.empty(), "stale flags must not be replayed on reattach");
    }

    void testMarkAllParametersDirty()
    {
        beginTest ("Marking everything dirty reports every parameter once");

        ApolloAudioProcessor processor;
        ui::ParameterBridge bridge (processor.getValueTreeState());

        OutboundRecorder recorder;
        bridge.setOutboundHandler (recorder.handler());

        bridge.markAllParametersDirty();
        bridge.flushPendingUpdates();

        expectEquals (recorder.countOfType ("parameterChanged"),
                      static_cast<int> (params::parameterCount()),
                      "every parameter must be reported exactly once");
    }

    void testMetadataDescribesTheRegistry()
    {
        beginTest ("Parameter metadata describes every registered parameter");

        ApolloAudioProcessor processor;
        ui::ParameterBridge bridge (processor.getValueTreeState());

        juce::var parsed;
        expect (juce::JSON::parse (bridge.createParameterMetadata(), parsed).wasOk());

        auto* object = parsed.getDynamicObject();
        expect (object != nullptr);

        if (object == nullptr)
            return;

        const auto* entries = object->getProperty ("parameters").getArray();
        expect (entries != nullptr, "metadata must carry an array of parameters");

        if (entries == nullptr)
            return;

        expectEquals (entries->size(), static_cast<int> (params::parameterCount()));

        // Each entry must carry everything a generic control needs, so the
        // frontend never hard-codes a range (UI_BINDINGS.md §16).
        for (const auto& entry : *entries)
        {
            auto* fields = entry.getDynamicObject();
            expect (fields != nullptr);

            if (fields == nullptr)
                continue;

            for (const auto* required : { "id", "name", "type", "unit", "min", "max",
                                          "default", "skew", "step", "automatable",
                                          "modulatable", "smoothed" })
                expect (fields->hasProperty (required),
                        juce::String ("metadata entry is missing '") + required + "'");
        }
    }
};

ParameterBridgeTests parameterBridgeTests;

} // namespace
