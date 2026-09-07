#include "UI/ApolloWebViewEditor.h"

// Named explicitly rather than relied on transitively. MSVC's headers pull most
// of these in for free and libc++ often does too, which is exactly why a missing
// include here compiles on Windows and macOS and fails only on libstdc++.
#include <cstddef>
#include <vector>

namespace apollo::ui
{

namespace
{

constexpr int defaultEditorWidth = 900;
constexpr int defaultEditorHeight = 560;

/** How often the editor checks for a wholesale state reload.

    Deliberately slower than the bridge's own parameter update rate: a preset
    load is a human-scale event, and polling a single atomic four times a second
    costs nothing.
*/
constexpr int stateReloadPollHz = 4;

/*
    The placeholder page.

    Phase 7 replaces this with the built React bundle. It exists now for one
    reason: it makes the bridge contract demonstrably live. It requests the
    parameter metadata and the state snapshot on load, renders a control per
    parameter from that metadata alone — never from hard-coded ranges
    (UI_BINDINGS.md §16) — and sends gesture-wrapped setParameter messages.

    It is also the smallest honest test of the round trip: move a control, and
    the value that comes back is the one the engine actually accepted, not the
    one the page sent.
*/
constexpr const char* placeholderPage = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Apollo</title>
<style>
  :root {
    --bg: #14101c; --panel: #1d1729; --line: #2f2542;
    --text: #e8e4f0; --muted: #9d93b4; --accent: #7c4dff; --active: #3ddc84;
  }
  * { box-sizing: border-box; }
  body { margin: 0; background: var(--bg); color: var(--text);
         font: 13px/1.5 system-ui, -apple-system, "Segoe UI", sans-serif; }
  header { padding: 18px 22px; border-bottom: 1px solid var(--line);
           display: flex; align-items: baseline; gap: 12px; }
  h1 { margin: 0; font-size: 17px; letter-spacing: .14em; text-transform: uppercase; }
  .tag { color: var(--muted); font-size: 11px; }
  main { padding: 18px 22px; display: grid; gap: 12px;
         grid-template-columns: repeat(auto-fill, minmax(280px, 1fr)); }
  .param { background: var(--panel); border: 1px solid var(--line);
           border-radius: 8px; padding: 12px 14px; }
  .row { display: flex; justify-content: space-between; align-items: baseline; gap: 8px; }
  .name { font-weight: 600; }
  .value { color: var(--active); font-variant-numeric: tabular-nums; }
  .id { color: var(--muted); font-size: 11px; font-family: ui-monospace, monospace; }
  input[type=range] { width: 100%; margin: 10px 0 0; accent-color: var(--accent); }
  footer { padding: 12px 22px; border-top: 1px solid var(--line);
           color: var(--muted); font-size: 11px; }
</style>
</head>
<body>
<header>
  <h1>Apollo</h1>
  <span class="tag">parameter bridge &mdash; placeholder UI, Phase 7 replaces this</span>
</header>
<main id="params"></main>
<footer id="status">Connecting&hellip;</footer>
<script>
  const PROTOCOL_VERSION = 1;
  const controls = new Map();
  let metadata = [];

  function send (message) {
    // Paired with Options::withEventListener on the native side. Sent as a JSON
    // string rather than an object so the native side validates exactly the
    // bytes the page produced, with no intermediate var conversion.
    window.__JUCE__.backend.emitEvent('apolloCommand', JSON.stringify(message));
  }

  function formatValue (definition, normalized) {
    const skew = definition.skew || 1;
    const plain = definition.min + (definition.max - definition.min) * Math.pow(normalized, 1 / skew);
    const decimals = definition.step >= 1 ? 0 : 2;
    return plain.toFixed(decimals) + (definition.unit ? ' ' + definition.unit : '');
  }

  function render () {
    const host = document.getElementById('params');
    host.innerHTML = '';
    controls.clear();

    for (const definition of metadata) {
      const card = document.createElement('div');
      card.className = 'param';

      const row = document.createElement('div');
      row.className = 'row';
      const name = document.createElement('span');
      name.className = 'name';
      name.textContent = definition.name;
      const value = document.createElement('span');
      value.className = 'value';
      row.append(name, value);

      const id = document.createElement('div');
      id.className = 'id';
      id.textContent = definition.id;

      const slider = document.createElement('input');
      slider.type = 'range';
      slider.min = 0;
      slider.max = 1;
      // Discrete parameters keep their step count; continuous ones are smooth.
      slider.step = definition.type === 'int' && definition.max > definition.min
        ? 1 / (definition.max - definition.min)
        : 0.001;

      slider.addEventListener('pointerdown', () =>
        send({ type: 'gesture', version: PROTOCOL_VERSION, id: definition.id, state: 'begin' }));
      slider.addEventListener('pointerup', () =>
        send({ type: 'gesture', version: PROTOCOL_VERSION, id: definition.id, state: 'end' }));
      slider.addEventListener('input', () =>
        send({ type: 'setParameter', version: PROTOCOL_VERSION,
               id: definition.id, normalizedValue: Number(slider.value) }));

      card.append(row, id, slider);
      host.append(card);
      controls.set(definition.id, { slider, value, definition });
    }
  }

  function applyValue (id, normalized) {
    const control = controls.get(id);
    if (!control) return;
    control.slider.value = normalized;
    control.value.textContent = formatValue(control.definition, normalized);
  }

  function handle (message) {
    switch (message.type) {
      case 'parameterMetadata':
        metadata = message.parameters;
        render();
        send({ type: 'requestState', version: PROTOCOL_VERSION });
        break;
      case 'stateSnapshot':
        for (const [id, value] of Object.entries(message.parameters)) applyValue(id, value);
        document.getElementById('status').textContent =
          metadata.length + ' parameters bound · protocol v' + PROTOCOL_VERSION;
        break;
      case 'parameterChanged':
        applyValue(message.id, message.normalizedValue);
        break;
      case 'error':
        document.getElementById('status').textContent = 'Error: ' + message.message;
        break;
    }
  }

  // Registered by JUCE via Options::withNativeIntegrationEnabled.
  window.__JUCE__.backend.addEventListener('apolloMessage', (payload) => {
    try { handle(typeof payload === 'string' ? JSON.parse(payload) : payload); }
    catch (e) { /* a malformed frame is dropped rather than breaking the UI */ }
  });

  send({ type: 'requestMetadata', version: PROTOCOL_VERSION });
</script>
</body>
</html>
)HTML";

/*
    Selects the WebView backend for the platform Apollo is running on.

    This exists because enabling JUCE's WebView2 support at build time is
    necessary but *not* sufficient. `JUCE_USE_WIN_WEBVIEW2=1` compiles the
    backend in, and `NEEDS_WEBVIEW2` links its loader, but JUCE still constructs
    the legacy Internet Explorer control unless the backend is also selected
    here, at the point the component is created — JUCE's own documentation for
    the flag says so, and `createAndInitPlatformDependentPart` falls through to
    `Platform::Win32WebView` for every backend value but `webview2`.

    The failure that omission produces is quiet and misleading rather than loud.
    Everything builds, links and passes its tests; the IE control simply cannot
    serve a resource provider or the native integration, so it fails to reach
    `https://juce.backend/` and renders "Navigation to the webpage was canceled"
    where the UI should be. It is invisible to the whole test suite, which
    exercises the bridge through its protocol layer with no browser at all.

    Only Windows needs the explicit choice: JUCE's default is already WebKit on
    macOS and WebKitGTK on Linux, which are the right backends there. Apollo
    therefore names a backend per platform rather than assuming one
    (CLAUDE.md §6.2, §46), and the platform-specific part is confined to this
    function.
*/
[[nodiscard]] juce::WebBrowserComponent::Options withPlatformBackend (
    juce::WebBrowserComponent::Options options)
{
   #if JUCE_WINDOWS
    using Backend = juce::WebBrowserComponent::Options::Backend;

    /*
        The user-data folder is chosen explicitly rather than left to WebView2.

        Left unset, WebView2 creates its folder beside the *running executable*.
        For the standalone that is Apollo's own build or install directory, which
        may be read-only; for the VST3 it is the host's program directory, which
        very often is. If that folder cannot be created the environment fails to
        initialise and JUCE silently falls back to the IE control — the same
        broken page as above, appearing only for some users and only in some
        hosts, which is close to undiagnosable from a bug report.

        A per-user application data folder is writable in both cases and is
        where this cache belongs. getSpecialLocation resolves it per platform,
        so no path is hard-coded.
    */
    const auto userData = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                              .getChildFile ("Apollo")
                              .getChildFile ("WebView2");

    userData.createDirectory();

    return options.withBackend (Backend::webview2)
                  .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2 {}
                                               .withUserDataFolder (userData));
   #else
    return options;
   #endif
}

std::vector<std::byte> toBytes (const juce::String& text)
{
    const auto utf8 = text.toRawUTF8();
    const auto size = text.getNumBytesAsUTF8();

    std::vector<std::byte> bytes (size);

    for (size_t i = 0; i < size; ++i)
        bytes[i] = static_cast<std::byte> (utf8[i]);

    return bytes;
}

} // namespace

//==============================================================================

ApolloWebViewEditor::ApolloWebViewEditor (ApolloAudioProcessor& processorToUse)
    : juce::AudioProcessorEditor (processorToUse),
      processor (processorToUse),
      bridge (processorToUse.getValueTreeState()),
      webView (withPlatformBackend (juce::WebBrowserComponent::Options {}
                   .withNativeIntegrationEnabled()
                   .withResourceProvider ([this] (const auto& path) { return provideResource (path); })
                   .withEventListener (juce::Identifier (inboundEventId),
                                       [this] (juce::var payload)
                                       {
                                           // Everything arriving here is untrusted text. It goes
                                           // straight to the bridge, which validates before anything
                                           // can reach a parameter. Any reply — a snapshot, metadata,
                                           // or a structured error — is emitted back as an event.
                                           const auto reply = bridge.handleMessage (payload.toString());

                                           if (reply.isNotEmpty())
                                               sendToWebView (reply);
                                       })))
{
    addAndMakeVisible (webView);

    // Outbound messages are produced on the message thread by the bridge's
    // coalescing timer, never by the audio thread.
    bridge.setOutboundHandler ([this] (const juce::String& message) { sendToWebView (message); });

    lastSeenStateReload = processor.getStateReloadCounter();

    setResizable (true, true);
    setResizeLimits (640, 400, 3840, 2160);
    setSize (defaultEditorWidth, defaultEditorHeight);

    webView.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
    pageReady = true;

    startTimerHz (stateReloadPollHz);
}

ApolloWebViewEditor::~ApolloWebViewEditor()
{
    stopTimer();

    // Detach before the WebView is destroyed: the bridge outlives this editor,
    // and an outbound handler capturing a dead `this` would be a use-after-free
    // on the next coalesced flush.
    bridge.setOutboundHandler ({});
}

void ApolloWebViewEditor::paint (juce::Graphics& g)
{
    // Only visible if the WebView fails to render, in which case a flat dark
    // panel is a better outcome than an undefined one (CLAUDE.md §33).
    g.fillAll (juce::Colour (0xff14101c));
}

void ApolloWebViewEditor::resized()
{
    webView.setBounds (getLocalBounds());
}

void ApolloWebViewEditor::timerCallback()
{
    const auto reloadCounter = processor.getStateReloadCounter();

    if (reloadCounter == lastSeenStateReload)
        return;

    lastSeenStateReload = reloadCounter;

    // A preset or project load moves everything at once. Resynchronise the whole
    // UI from authoritative native state rather than trusting what it currently
    // shows (UI_BINDINGS.md §18).
    sendToWebView (bridge.createStateSnapshot());
}

std::optional<juce::WebBrowserComponent::Resource>
    ApolloWebViewEditor::provideResource (const juce::String& path) const
{
    // One page, served for the root request. Any other path is refused rather
    // than mapped onto the filesystem: the WebView must not become a way to read
    // arbitrary files (UI_BINDINGS.md §14).
    if (path != "/" && path != "/index.html")
        return std::nullopt;

    return juce::WebBrowserComponent::Resource { toBytes (juce::String (placeholderPage)),
                                                 "text/html; charset=utf-8" };
}

void ApolloWebViewEditor::sendToWebView (const juce::String& message)
{
    if (! pageReady)
        return;

    webView.emitEventIfBrowserIsVisible (juce::Identifier (outboundEventId), message);
}

} // namespace apollo::ui
