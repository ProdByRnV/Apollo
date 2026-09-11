/*
    Normalised values in, real-world values out.

    Mirrors `juce::NormalisableRange`, which is what the native side applies. The
    two must agree or a knob will show one number while the engine holds another,
    and nothing will report the disagreement.
*/

import type { ParameterDefinition } from '../bridge/protocol';

/** A zero or missing skew would make the mapping undefined rather than linear,
    so it is treated as linear.
*/
function skewOf(definition: ParameterDefinition): number {
    return definition.skew > 0 ? definition.skew : 1;
}

export function toPlain(definition: ParameterDefinition, normalized: number): number {
    const skew = skewOf(definition);
    const t = skew === 1 ? normalized : Math.pow(normalized, 1 / skew);

    let plain = definition.min + (definition.max - definition.min) * t;

    if (definition.step > 0) {
        plain = definition.min
            + Math.round((plain - definition.min) / definition.step) * definition.step;
    }

    return plain;
}

export function toNormalized(definition: ParameterDefinition, plain: number): number {
    const clamped = Math.min(definition.max, Math.max(definition.min, plain));
    const span = definition.max - definition.min;

    if (span <= 0) return 0;

    const t = (clamped - definition.min) / span;
    const skew = skewOf(definition);

    return skew === 1 ? t : Math.pow(t, skew);
}

/** How many discrete positions a stepped parameter has, or 0 if continuous. */
export function stepCount(definition: ParameterDefinition): number {
    if (!(definition.step > 0)) return 0;

    return Math.round((definition.max - definition.min) / definition.step) + 1;
}
