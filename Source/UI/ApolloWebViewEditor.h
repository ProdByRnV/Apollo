#pragma once

/*
    Apollo's editor: a JUCE WebBrowserComponent wired to the parameter bridge.

    This class is deliberately thin. It owns no synthesis state, performs no
    validation of its own, and knows nothing about what any particular parameter
    means. Its entire job is transport:

        WebView  --(JSON string)-->  ParameterBridge::handleMessage
        WebView  <--(JSON string)--  ParameterBridge outbound handler

    All validation happens inside the bridge protocol, which is where the
    untrusted boundary is enforced (UI_BINDINGS.md §14). Keeping that logic out
    of here is what lets it be tested without a browser.

    The frontend itself is Phase 7. Until then the editor serves a small built-in
    page that exercises the bridge end to end, so the contract is demonstrably
    live rather than merely compiled.

    Platform note: JUCE selects the WebView backend per platform. Apollo does not
    hard-code one (CLAUDE.md §6.2, §46); the only platform-specific concern is
    acquiring the Windows WebView2 SDK at build time, handled in
    CMake/ApolloWebView.cmake.
*/

#include <juce_gui_extra/juce_gui_extra.h>

#include <optional>

#include "Audio/ApolloAudioProcessor.h"
#include "UI/ParameterBridge.h"
#include "UI/TelemetryBridge.h"

namespace apollo::ui
{

class ApolloWebViewEditor final : public juce::AudioProcessorEditor,
                                  private juce::Timer
{
public:
    explicit ApolloWebViewEditor (ApolloAudioProcessor& processorToUse);
    ~ApolloWebViewEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    /** Identifier of the event the page emits to send Apollo a message.

        Deliberately an event listener rather than Options::withNativeFunction.
        A native function is only reachable through JUCE's own JavaScript module
        (it is dispatched via an internal "__juce__invoke" event), which would
        oblige every page to import that module. An event listener pairs with
        `window.__JUCE__.backend.emitEvent`, the same public surface the outbound
        direction already uses, so both directions are symmetric and neither
        needs a bundler.
    */
    static constexpr const char* inboundEventId = "apolloCommand";

    /** Identifier of the event the page listens on for outbound messages. */
    static constexpr const char* outboundEventId = "apolloMessage";

    void timerCallback() override;

    /** Serves the built-in placeholder page. Replaced by the built frontend
        bundle in Phase 7.
    */
    [[nodiscard]] std::optional<juce::WebBrowserComponent::Resource>
        provideResource (const juce::String& path) const;

    /** Sends one already-serialized message to the page. Message thread only. */
    void sendToWebView (const juce::String& message);

    ApolloAudioProcessor& processor;
    ParameterBridge bridge;

    /** The visualisation broadcast. Separate from the parameter bridge because
        it is a broadcast rather than a conversation, and runs at its own rate.
    */
    TelemetryBridge telemetry;

    juce::WebBrowserComponent webView;

    /** Last observed value of the processor's state-reload counter, so a preset
        or project load can be detected and answered with a full resynchronisation
        rather than inferred from a burst of individual changes.
    */
    int lastSeenStateReload = 0;

    /** True once the page has loaded and it is safe to emit events to it.
        Before that, emitEventIfBrowserIsVisible has nothing to deliver to.
    */
    bool pageReady = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ApolloWebViewEditor)
};

} // namespace apollo::ui
