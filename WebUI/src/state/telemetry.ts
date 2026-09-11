/*
    Where the frames go, and deliberately not into React state.

    There are eleven canvases on this page and frames arrive thirty times a
    second. Routing them through `useState` would mean eleven components
    re-rendering thirty times a second to produce markup that never changes — the
    canvas element is the same element every frame; only the pixels inside it
    differ, and those are drawn imperatively.

    So a frame is delivered to whoever asked for that source, by call, and the
    component that receives it draws. React is responsible for the canvas being
    there and for the caption beside it; the picture is not React's business at
    all. This is the one place in the migration where the idiomatic answer is the
    wrong one, and it is worth saying so out loud: `useState` here would have
    looked cleaner and cost the interface most of its frame budget.

    Captions *are* React state, and they do change at frame rate while something
    is sounding. That is the right split rather than a compromise: a caption is
    one text node, which is exactly what React's diff is cheap at, and keeping it
    in state means the reading beside a picture cannot drift out of step with it.
    A canvas is the opposite — the element never changes and the pixels are not
    something a diff can help with.
*/

import type {
    InstrumentFrameMessage,
    MeterFrame,
    ModulatorFrame,
    ScopeFrame,
    WavetableFrame,
} from '../bridge/protocol';

type Handler<T> = (frame: T) => void;

const scopeHandlers = new Map<string, Set<Handler<ScopeFrame>>>();
const modulatorHandlers = new Map<string, Set<Handler<ModulatorFrame>>>();
const wavetableHandlers = new Map<number, Set<Handler<WavetableFrame>>>();
const meterHandlers = new Set<(meter: MeterFrame, voices: number, polyphony: number) => void>();

function subscribeIn<K, T>(
    registry: Map<K, Set<Handler<T>>>,
    key: K,
    handler: Handler<T>,
): () => void {
    let handlers = registry.get(key);

    if (handlers === undefined) {
        handlers = new Set();
        registry.set(key, handlers);
    }

    handlers.add(handler);

    return () => {
        const current = registry.get(key);
        if (current === undefined) return;

        current.delete(handler);
        if (current.size === 0) registry.delete(key);
    };
}

export function subscribeToScope(source: string, handler: Handler<ScopeFrame>): () => void {
    return subscribeIn(scopeHandlers, source, handler);
}

export function subscribeToModulator(
    source: string,
    handler: Handler<ModulatorFrame>,
): () => void {
    return subscribeIn(modulatorHandlers, source, handler);
}

export function subscribeToWavetable(
    oscillator: number,
    handler: Handler<WavetableFrame>,
): () => void {
    return subscribeIn(wavetableHandlers, oscillator, handler);
}

export function subscribeToMeter(
    handler: (meter: MeterFrame, voices: number, polyphony: number) => void,
): () => void {
    meterHandlers.add(handler);
    return () => { meterHandlers.delete(handler); };
}

//==============================================================================
// Delivery.

export function deliverScopeFrames(frames: ScopeFrame[]): void {
    for (const frame of frames) {
        const handlers = scopeHandlers.get(frame.source);
        if (handlers) for (const handler of handlers) handler(frame);
    }
}

export function deliverInstrumentFrame(message: InstrumentFrameMessage): void {
    for (const frame of message.modulators) {
        const handlers = modulatorHandlers.get(frame.source);
        if (handlers) for (const handler of handlers) handler(frame);
    }

    for (const frame of message.wavetables) {
        const handlers = wavetableHandlers.get(frame.osc);
        if (handlers) for (const handler of handlers) handler(frame);
    }

    for (const handler of meterHandlers) {
        handler(message.meter, message.voices, message.polyphony);
    }
}
