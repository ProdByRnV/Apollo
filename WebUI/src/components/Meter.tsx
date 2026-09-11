/*
    The output meter.

    Peak and RMS together, because either alone misleads: peak cannot tell a
    quiet signal with one spike from a loud one, and RMS cannot tell you that you
    are about to clip. The clip indicator is the first real use of the red token
    (CLAUDE.md §24.2), and it is a word as well as a colour (§39).

    No canvas here — two divs whose heights change — so unlike the scopes this
    one is entirely React. The distinction is not stylistic: a bar is two numbers
    a frame, and a trace is a hundred and ninety-two.
*/

import { useEffect, useState } from 'react';

import type { MeterFrame } from '../bridge/protocol';
import { subscribeToMeter } from '../state/telemetry';

/** Where the bottom of the meter sits. -60 dB is quiet enough to be silence for
    a synthesiser's output and close enough that the useful range is not squeezed
    into the top tenth of the bar.
*/
const FLOOR_DB = -60;

function meterFraction(amplitude: number): number {
    if (!(amplitude > 0)) return 0;

    const db = 20 * Math.log10(amplitude);

    if (db <= FLOOR_DB) return 0;
    if (db >= 0) return 1;

    return 1 - db / FLOOR_DB;
}

interface Reading {
    active: boolean;
    peak: number[];
    rms: number[];
    clipped: boolean;
    voices: number;
    polyphony: number;
}

const initial: Reading = {
    active: false, peak: [0, 0], rms: [0, 0], clipped: false, voices: 0, polyphony: 0,
};

export function Meter(): JSX.Element {
    const [reading, setReading] = useState<Reading>(initial);

    useEffect(() => subscribeToMeter((meter: MeterFrame, voices, polyphony) => {
        setReading({
            active: meter.active,
            peak: meter.peak ?? [0, 0],
            rms: meter.rms ?? [0, 0],
            clipped: meter.clipped ?? false,
            voices,
            polyphony,
        });
    }), []);

    const loudest = Math.max(reading.peak[0] ?? 0, reading.peak[1] ?? 0);

    const caption = !reading.active
        ? 'no signal'
        : (loudest > 0
            ? `${(20 * Math.log10(loudest)).toFixed(1)} dB · ${reading.voices}/${reading.polyphony}`
            : `silent · ${reading.voices}/${reading.polyphony}`);

    return (
        <div className="meter" data-active={reading.active} data-clipped={reading.clipped}>
            <div className="meter__bars">
                {[0, 1].map((channel) => (
                    <div key={channel} className="meter__channel">
                        <div
                            className="meter__rms"
                            style={{ height: `${meterFraction(reading.rms[channel] ?? 0) * 100}%` }}
                        />
                        <div
                            className="meter__peak"
                            style={{ height: `${meterFraction(reading.peak[channel] ?? 0) * 100}%` }}
                        />
                    </div>
                ))}
            </div>

            <div className="meter__caption">
                <span className="meter__reading">{caption}</span>
                <span className="meter__clip">{reading.clipped ? 'CLIP' : ''}</span>
            </div>
        </div>
    );
}
