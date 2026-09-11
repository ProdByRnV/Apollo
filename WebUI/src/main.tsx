/*
    Apollo's interface — entry point.

    Two principles run through this frontend, and they survived the migration to
    React unchanged because neither was ever about the framework.

    First, the native side owns the truth. Every range, default, step and skew
    comes from the parameter metadata the engine sends; nothing here states what
    a filter cutoff's range is, because the day someone changes it in
    ParameterDefinitions.h the interface must follow without being edited
    (UI_BINDINGS.md §16). What this code does own is presentation: which module a
    control belongs to, what a discrete value is called, and how a number is
    written out.

    Second, nothing re-renders that does not have to. Under React that is no
    longer achieved by never re-rendering at all — it is achieved by every value
    reaching its control through a store keyed on the parameter's own id, so a
    knob re-renders when its own value moves and at no other time, and by the
    eleven canvases being drawn imperatively rather than through the tree
    (state/parameters.ts, state/telemetry.ts).

    StrictMode is deliberately absent. It double-invokes effects in development
    to surface missing cleanups, which is a good trade in an application that has
    a development mode; this bundle is only ever built for production and served
    from inside a plugin, so it would double-subscribe every telemetry handler
    for no one's benefit.
*/

import { createRoot } from 'react-dom/client';

import { App } from './App';
import './styles/apollo.css';

const container = document.getElementById('root');

if (container === null) {
    throw new Error('Apollo: the page shell is missing its root element.');
}

createRoot(container).render(<App />);
