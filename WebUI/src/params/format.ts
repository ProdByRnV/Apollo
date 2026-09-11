/*
    Writing a number out.

    The unit arrives as an enum precisely so this layer can decide how to spell
    it. 20000 Hz reads as "20.00 kHz" and 1500 ms as "1.50 s", because a
    synthesiser that makes you count zeroes is a synthesiser you misread at 2am.
*/

import type { ParameterDefinition } from '../bridge/protocol';

export function formatPlain(definition: ParameterDefinition, plain: number): string {
    switch (definition.unit) {
        case 'Hz':
            return plain >= 1000
                ? `${(plain / 1000).toFixed(2)} kHz`
                : `${plain.toFixed(plain < 10 ? 2 : 1)} Hz`;

        case 'ms':
            return plain >= 1000
                ? `${(plain / 1000).toFixed(2)} s`
                : `${plain.toFixed(plain < 10 ? 1 : 0)} ms`;

        case 'dB':
            return `${plain > 0 ? '+' : ''}${plain.toFixed(1)} dB`;

        case '%':
            return `${Math.round(plain * 100)} %`;

        case 'voices':
            return String(Math.round(plain));

        case 'st':
            return `${plain > 0 ? '+' : ''}${Math.round(plain)} st`;

        case 'cents':
            return `${plain > 0 ? '+' : ''}${plain.toFixed(0)} c`;

        case 'oct':
            return `${plain > 0 ? '+' : ''}${Math.round(plain)}`;

        default:
            if (definition.step >= 1) return String(Math.round(plain));
            return plain.toFixed(2);
    }
}

/** Amplitude to a decibel reading, with a floor so silence reads as silence
    rather than as minus infinity.
*/
export function formatDecibels(amplitude: number, floorDb = -60): string {
    if (!(amplitude > 0)) return `${floorDb.toFixed(1)} dB`;

    const db = 20 * Math.log10(amplitude);

    return `${db <= floorDb ? floorDb.toFixed(1) : db.toFixed(1)} dB`;
}
