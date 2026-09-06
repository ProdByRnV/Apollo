#pragma once

/*
    Connects the validated bridge protocol to the APVTS parameter system.

    Threading is the whole design problem here.

    Host automation calls the APVTS listener from the *audio thread*. Serializing
    JSON or calling into a WebView there would allocate, block and violate the
    real-time contract outright (UI_BINDINGS.md §19, CLAUDE.md §7.1). So the
    audio thread does the cheapest possible thing — sets a lock-free flag — and a
    message-thread timer coalesces those flags into outbound messages at a
    display-appropriate rate (UI_BINDINGS.md §5, §12).

    That coalescing is not only a performance measure: a parameter swept by
    automation changes far faster than any display can show, and forwarding every
    change would flood the WebView with values no one can see.

    Native/APVTS state stays authoritative throughout. The frontend's values are
    a cache that this class keeps in sync (UI_BINDINGS.md §1).
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters/ParameterDefinitions.h"
#include "UI/BridgeProtocol.h"

#include <array>
#include <atomic>
#include <functional>

namespace apollo::ui
{

class ParameterBridge final : private juce::AudioProcessorValueTreeState::Listener,
                              private juce::Timer
{
public:
    /** Sends one message to the frontend. Always invoked on the message thread. */
    using OutboundHandler = std::function<void (const juce::String&)>;

    explicit ParameterBridge (juce::AudioProcessorValueTreeState& stateToUse);
    ~ParameterBridge() override;

    /** Sets the sink for outbound messages. Pass {} to detach.

        Detaching is what makes editor teardown safe: the bridge outlives no
        WebView, and a closed editor simply stops receiving.
    */
    void setOutboundHandler (OutboundHandler handler);

    /** Handles one inbound message from the frontend.

        @returns the message to send in reply, or an empty string if there is
                 nothing to reply with. Errors are returned as structured error
                 messages rather than thrown.
    */
    [[nodiscard]] juce::String handleMessage (const juce::String& json);

    /** @returns the authoritative snapshot of every registered parameter. */
    [[nodiscard]] juce::String createStateSnapshot() const;

    /** @returns the parameter registry described for the frontend. */
    [[nodiscard]] juce::String createParameterMetadata() const;

    /** Emits a parameterChanged message for every parameter marked dirty since
        the last flush.

        Called by the timer. Exposed so tests can drive it deterministically
        instead of waiting on a real message loop.
    */
    void flushPendingUpdates();

    /** Marks every parameter dirty, so the next flush reports all of them.

        Used after a preset or project load, where many parameters change at once
        and the UI needs to be told about all of them.
    */
    void markAllParametersDirty();

    /** Update rate for coalesced parameter changes, in hertz.

        30 Hz is comfortably above the rate at which a control reads as smooth
        and well below the rate at which automation actually changes values.
    */
    static constexpr int updateRateHz = 30;

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void timerCallback() override;

    /** Applies a validated command. @returns the reply, or an empty string. */
    [[nodiscard]] juce::String applyCommand (const BridgeCommand& command);

    [[nodiscard]] static int indexOfParameter (const juce::String& parameterID);

    juce::AudioProcessorValueTreeState& apvts;

    OutboundHandler outboundHandler;

    /** One flag per registered parameter, set from whichever thread changed the
        parameter and cleared by the message thread. Lock-free and fixed size: no
        allocation, and nothing for the audio thread to contend on.
    */
    std::array<std::atomic<bool>, params::parameterCount()> dirtyFlags {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterBridge)
};

} // namespace apollo::ui
