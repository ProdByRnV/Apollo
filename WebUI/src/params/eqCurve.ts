/*
    The equaliser's response curve, computed the way the engine computes it.

    A MIRROR, AND THE ONE PLACE THE PAGE IS ALLOWED TO HOLD DSP ARITHMETIC.
    Everything else on this page is built from metadata and draws what the engine
    sent it. This cannot be: a response curve is a continuous function of
    frequency, and sending it as points would mean the engine redrawing and
    re-serialising a hundred and sixty values every time a knob moves — for a
    picture that depends on nothing but seven bands of settings the page already
    has. So the page computes it, from the same RBJ cookbook formulae
    `Source/DSP/EQ/Biquad.cpp` uses, at the sample rate the engine reports.

    That makes this a transcription, and a transcription can drift from what it
    transcribes. Two things limit that. The formulae themselves are the RBJ
    cookbook, which is a thirty-year-old published standard and is not going to
    be rewritten — what could move is the clamping and the order semantics around
    them, which is why those are transcribed here term for term rather than
    approximated. And `EqualiserBand::magnitudeDbAt` is the authority on the
    other side: `Tests/DSP/EqualiserTests.cpp` pins the exact decibels a table of
    settings must produce, so the engine cannot change underneath this file
    silently. Nothing checks the two automatically — there is no test runner on
    this side of the bridge and adding one for a single file would cost more than
    it saves — so a change to the cookbook in C++ has to be made here as well.

    The alternative — drawing an approximation and accepting that the picture is
    roughly the sound — is what makes an equaliser display untrustworthy, and an
    untrustworthy display is worse than none.
*/

/** Mirrors `apollo::dsp::EqualiserBand::Type`. */
export const EQ_TYPE_OFF = 0;
export const EQ_TYPE_LOW_PASS = 1;
export const EQ_TYPE_BAND_PASS = 2;
export const EQ_TYPE_HIGH_PASS = 3;
export const EQ_TYPE_NOTCH = 4;
export const EQ_TYPE_LOW_SHELF = 5;
export const EQ_TYPE_PEAKING = 6;
export const EQ_TYPE_HIGH_SHELF = 7;

/** Mirrors `apollo::dsp::rbj`. */
const MIN_Q = 0.025;
const MAX_Q = 80;
const MAX_FREQUENCY_FRACTION = 0.49;
const MIN_FREQUENCY = 1;

/** Mirrors `apollo::dsp::EqualiserBand` and `Equaliser`. */
export const EQ_BAND_COUNT = 7;
export const EQ_MAX_ORDER = 4;
export const EQ_MIN_HZ = 20;
export const EQ_MAX_HZ = 20000;
export const EQ_MAX_GAIN_DB = 18;

/** The rate the curve is drawn at before the engine has reported one.

    A number rather than an empty display: the page is opened before the first
    instrument frame arrives, and a curve that appeared a fifteenth of a second
    late would flicker on every reload.
*/
export const EQ_FALLBACK_SAMPLE_RATE = 48000;

export interface BandSettings {
    type: number;
    frequencyHz: number;
    gainDb: number;
    bandwidthOctaves: number;
    order: number;
    muted: boolean;
}

interface Coefficients {
    b0: number;
    b1: number;
    b2: number;
    a1: number;
    a2: number;
}

const IDENTITY: Coefficients = { b0: 1, b1: 0, b2: 0, a1: 0, a2: 0 };

/** True for the shapes whose gain control does something. */
export function eqTypeHasGain(type: number): boolean {
    return type === EQ_TYPE_LOW_SHELF || type === EQ_TYPE_PEAKING || type === EQ_TYPE_HIGH_SHELF;
}

/** Comparisons rather than Math.min/max, so that a NaN falls to the floor
    rather than propagating — the same shape the C++ clamps are written in, and
    for the same reason.
*/
function clampFrequency(hz: number, sampleRate: number): number {
    if (!(sampleRate > 0)) return MIN_FREQUENCY;

    const ceiling = sampleRate * MAX_FREQUENCY_FRACTION;

    if (hz > ceiling) return ceiling;
    if (hz > MIN_FREQUENCY) return hz;

    return MIN_FREQUENCY;
}

function clampBandwidth(octaves: number): number {
    if (octaves > 6) return 6;
    if (octaves > 0.05) return octaves;

    return 0.05;
}

function clampGainDb(gainDb: number): number {
    if (gainDb > EQ_MAX_GAIN_DB) return EQ_MAX_GAIN_DB;
    if (gainDb >= -EQ_MAX_GAIN_DB) return gainDb;
    if (gainDb < -EQ_MAX_GAIN_DB) return -EQ_MAX_GAIN_DB;

    return 0;
}

function clampOrder(order: number): number {
    return Math.min(EQ_MAX_ORDER, Math.max(1, Math.round(order)));
}

/** The cookbook's relation between a width in octaves and a Q, which depends on
    where in the spectrum the band sits.
*/
function qForBandwidth(bandwidthOctaves: number, hz: number, sampleRate: number): number {
    const frequency = clampFrequency(hz, sampleRate);
    const w0 = (2 * Math.PI * frequency) / sampleRate;
    const sinW0 = Math.sin(w0);

    if (!(sinW0 > 0) || !(bandwidthOctaves > 0)) return MAX_Q;

    const alpha = sinW0 * Math.sinh(0.5 * Math.LN2 * bandwidthOctaves * (w0 / sinW0));

    if (!(alpha > 0)) return MAX_Q;

    return Math.min(MAX_Q, Math.max(MIN_Q, sinW0 / (2 * alpha)));
}

function normalise(
    b0: number, b1: number, b2: number,
    a0: number, a1: number, a2: number,
): Coefficients {
    if (!(a0 > 0) && !(a0 < 0)) return IDENTITY;

    const inverse = 1 / a0;

    return {
        b0: b0 * inverse,
        b1: b1 * inverse,
        b2: b2 * inverse,
        a1: a1 * inverse,
        a2: a2 * inverse,
    };
}

export function designBand(band: BandSettings, sampleRate: number): Coefficients {
    if (!(sampleRate > 0) || band.type === EQ_TYPE_OFF || band.muted) return IDENTITY;

    const frequency = clampFrequency(band.frequencyHz, sampleRate);
    const q = Math.min(
        MAX_Q,
        Math.max(MIN_Q, qForBandwidth(clampBandwidth(band.bandwidthOctaves), frequency, sampleRate)),
    );

    const w0 = (2 * Math.PI * frequency) / sampleRate;
    const cosW0 = Math.cos(w0);
    const alpha = Math.sin(w0) / (2 * q);
    const a = Math.pow(10, clampGainDb(band.gainDb) / 40);

    switch (band.type) {
        case EQ_TYPE_LOW_PASS: {
            const oneMinusCos = 1 - cosW0;
            return normalise(
                0.5 * oneMinusCos, oneMinusCos, 0.5 * oneMinusCos,
                1 + alpha, -2 * cosW0, 1 - alpha,
            );
        }

        case EQ_TYPE_HIGH_PASS: {
            const onePlusCos = 1 + cosW0;
            return normalise(
                0.5 * onePlusCos, -onePlusCos, 0.5 * onePlusCos,
                1 + alpha, -2 * cosW0, 1 - alpha,
            );
        }

        // The constant-peak-gain form, matching the engine: unity at the centre
        // whatever the bandwidth.
        case EQ_TYPE_BAND_PASS:
            return normalise(alpha, 0, -alpha, 1 + alpha, -2 * cosW0, 1 - alpha);

        case EQ_TYPE_NOTCH:
            return normalise(1, -2 * cosW0, 1, 1 + alpha, -2 * cosW0, 1 - alpha);

        case EQ_TYPE_PEAKING:
            return normalise(
                1 + alpha * a, -2 * cosW0, 1 - alpha * a,
                1 + alpha / a, -2 * cosW0, 1 - alpha / a,
            );

        case EQ_TYPE_LOW_SHELF: {
            const aPlus = a + 1;
            const aMinus = a - 1;
            const twoRootA = 2 * Math.sqrt(a) * alpha;

            return normalise(
                a * (aPlus - aMinus * cosW0 + twoRootA),
                2 * a * (aMinus - aPlus * cosW0),
                a * (aPlus - aMinus * cosW0 - twoRootA),
                aPlus + aMinus * cosW0 + twoRootA,
                -2 * (aMinus + aPlus * cosW0),
                aPlus + aMinus * cosW0 - twoRootA,
            );
        }

        case EQ_TYPE_HIGH_SHELF: {
            const aPlus = a + 1;
            const aMinus = a - 1;
            const twoRootA = 2 * Math.sqrt(a) * alpha;

            return normalise(
                a * (aPlus + aMinus * cosW0 + twoRootA),
                -2 * a * (aMinus + aPlus * cosW0),
                a * (aPlus + aMinus * cosW0 - twoRootA),
                aPlus - aMinus * cosW0 + twoRootA,
                2 * (aMinus - aPlus * cosW0),
                aPlus - aMinus * cosW0 - twoRootA,
            );
        }

        default:
            return IDENTITY;
    }
}

/** |H(f)| for one section, evaluated on the unit circle. */
function magnitudeOf(c: Coefficients, hz: number, sampleRate: number): number {
    if (!(sampleRate > 0)) return 1;

    const w = (2 * Math.PI * hz) / sampleRate;

    const cos1 = Math.cos(w);
    const sin1 = Math.sin(w);
    const cos2 = Math.cos(2 * w);
    const sin2 = Math.sin(2 * w);

    const numeratorReal = c.b0 + c.b1 * cos1 + c.b2 * cos2;
    const numeratorImag = c.b1 * sin1 + c.b2 * sin2;

    const denominatorReal = 1 + c.a1 * cos1 + c.a2 * cos2;
    const denominatorImag = c.a1 * sin1 + c.a2 * sin2;

    const numerator = numeratorReal * numeratorReal + numeratorImag * numeratorImag;
    const denominator = denominatorReal * denominatorReal + denominatorImag * denominatorImag;

    if (!(denominator > 0)) return 0;

    return Math.sqrt(numerator / denominator);
}

/** One band's contribution at @p hz, in decibels, including its order.

    The sections are identical, so the response is one section's raised to the
    order — which in decibels is one section's multiplied by it.
*/
export function bandMagnitudeDbAt(band: BandSettings, sampleRate: number, hz: number): number {
    if (band.type === EQ_TYPE_OFF || band.muted) return 0;

    const magnitude = magnitudeOf(designBand(band, sampleRate), hz, sampleRate);
    const order = clampOrder(band.order);

    if (!(magnitude > 0)) return -1000 * order;

    return 20 * Math.log10(magnitude) * order;
}

/** The whole equaliser at @p hz: every band summed, plus the output trim.

    Summing in decibels is exact rather than an approximation — the bands are in
    series, so their linear magnitudes multiply, and a product of magnitudes is a
    sum of decibels.
*/
export function curveDbAt(
    bands: readonly BandSettings[],
    levelDb: number,
    sampleRate: number,
    hz: number,
): number {
    let total = levelDb;

    for (const band of bands) total += bandMagnitudeDbAt(band, sampleRate, hz);

    return total;
}
