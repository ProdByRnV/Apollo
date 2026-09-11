/*
    The only place this page touches the native side.

    Everything else imports `send` and `subscribe` and never sees
    `window.__JUCE__`, which is what makes the page runnable in a plain browser:
    with no backend attached the bridge reports itself unavailable, sends
    nothing, and the interface still builds and says so rather than sitting on
    "Connecting" for ever (CLAUDE.md §33).
*/

import type { InboundMessage, OutboundMessage, ProfileMode } from './protocol';
import { PROTOCOL_VERSION } from './protocol';

/** JUCE's injected object. Declared rather than imported because it is put on
    `window` by the WebView host before the page's own script runs.
*/
interface JuceBackend {
    emitEvent(eventId: string, payload: string): void;
    addEventListener(eventId: string, handler: (payload: unknown) => void): void;
}

declare global {
    interface Window {
        __JUCE__?: { backend?: JuceBackend };
    }
}

const backend = window.__JUCE__?.backend;

export const bridgeAvailable = backend !== undefined;

type Listener = (message: InboundMessage) => void;

const listeners = new Set<Listener>();

if (backend !== undefined) {
    backend.addEventListener('apolloMessage', (payload: unknown) => {
        let message: InboundMessage;

        try {
            message = typeof payload === 'string'
                ? (JSON.parse(payload) as InboundMessage)
                : (payload as InboundMessage);
        } catch {
            // A malformed frame is dropped. The interface staying up with a
            // stale value beats it going blank (CLAUDE.md §33).
            return;
        }

        for (const listener of listeners) listener(message);
    });
}

export function send(message: OutboundMessage): void {
    // Sent as a JSON string rather than an object, so the native side validates
    // exactly the bytes this page produced with no intermediate var conversion.
    backend?.emitEvent('apolloCommand', JSON.stringify(message));
}

export function subscribe(listener: Listener): () => void {
    listeners.add(listener);
    return () => { listeners.delete(listener); };
}

//==============================================================================
// The commands the page sends, named rather than assembled at each call site.

export function setParameter(id: string, normalized: number): void {
    send({ type: 'setParameter', version: PROTOCOL_VERSION, id, normalizedValue: normalized });
}

export function gesture(id: string, state: 'begin' | 'end'): void {
    send({ type: 'gesture', version: PROTOCOL_VERSION, id, state });
}

export function requestMetadata(): void {
    send({ type: 'requestMetadata', version: PROTOCOL_VERSION });
}

export function requestMidiMappings(): void {
    send({ type: 'requestMidiMappings', version: PROTOCOL_VERSION });
}

export function midiLearnBegin(id: string): void {
    send({ type: 'midiLearnBegin', version: PROTOCOL_VERSION, id });
}

export function midiLearnCancel(): void {
    send({ type: 'midiLearnCancel', version: PROTOCOL_VERSION });
}

export function midiMappingRemove(id: string): void {
    send({ type: 'midiMappingRemove', version: PROTOCOL_VERSION, id });
}

export function midiMappingClearAll(): void {
    send({ type: 'midiMappingClearAll', version: PROTOCOL_VERSION });
}

export function requestControllerProfiles(): void {
    send({ type: 'requestControllerProfiles', version: PROTOCOL_VERSION });
}

export function applyControllerProfile(profile: string, mode: ProfileMode): void {
    send({ type: 'applyControllerProfile', version: PROTOCOL_VERSION, profile, mode });
}
