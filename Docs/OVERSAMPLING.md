# Oversampling in Apollo

> Where oversampling is applied, where it is deliberately **not**, and why.
> Written in Phase 4c, before the first nonlinear stage exists, so that the
> decision is a recorded one rather than something inferred later from the code.

---

## 1. What oversampling is for

A **nonlinear** function generates frequencies its input did not contain. Feed a
3.3 kHz sine into a hard clipper at 48 kHz and it produces odd harmonics at
9.9 kHz, 16.5 kHz, 23.1 kHz, 29.7 kHz and upwards. Everything above 24 kHz has
nowhere to go: it folds back down and lands at an inharmonic frequency, where no
filter later in the chain can remove it, because by then it is indistinguishable
from signal.

Running the nonlinearity at a higher rate moves the fold-back point up, and the
downsampling filter removes what is above the original Nyquist frequency *before*
the rate drops. Measured in `Tests/DSP/OversamplingTests.cpp`, on a hard clipper:

| Setting | Worst non-harmonic content |
|---|---|
| No oversampling | -35.8 dBc |
| 2x | -45.3 dBc |
| 4x | -62.1 dBc |

That is the entire justification. It is a measurement, and the test fails if 4x
stops improving on the unprocessed case by at least 10 dB.

---

## 2. Where Apollo oversamples

Nothing yet, because Apollo has no nonlinear stage yet. The infrastructure exists
ahead of the stages that need it, which is what Phase 4c is for.

The stages that **will** use it, and the factor each is expected to want:

| Stage | Phase | Expected factor | Why |
|---|---|---|---|
| Distortion / waveshaping | 8 | 4x | The reason this exists. Hard clipping is the worst case measured above |
| Filter drive (`filter_drive`) | 5 | 2x | A saturating filter input is nonlinear, but usually gentler than a clipper |
| Aggressive oscillator warp | later | 2x, if measured | Warp reshapes a band-limited table and can break its band-limiting |
| Compressor / gate gain computer | 8 | none | See below |

Each of those is a decision to be **re-measured when the stage is built**, not a
promise. The table records the expectation so a later phase starts from a
position rather than from nothing.

---

## 3. Where Apollo deliberately does not oversample

This section matters more than the one above. Oversampling everything is the easy
mistake: it costs CPU and latency on every stage that gains nothing from it
(CLAUDE.md §23, ARCHITECTURE.md §2.3).

### The oscillators — already band-limited at the source

Apollo's wavetable oscillators are **not** oversampled and should not be. Their
anti-aliasing is structural: each waveform is stored as an 11-level mipmap whose
levels are generated additively, so a level only ever contains harmonics that fit
below Nyquist at the pitch it is selected for (ADR-0020). There is no aliasing to
remove, and the measurements agree — worst case -98.5 dBc across the pitch range,
against a -60 dBc budget.

Oversampling them would multiply the engine's single largest cost, which
§5 shows is already the dominant one, in exchange for nothing.

### The sub oscillator and the noise generator

The sub is a single-harmonic sine: it cannot alias at any pitch or octave
transposition. White noise generated at the sample rate has no content above
Nyquist to fold. Neither needs it.

### Every linear stage

Gain, pan, balance, mixing, summing, delay lines, EQ and filters in their linear
region all produce no new frequencies. Oversampling them changes nothing except
the CPU bill.

### Dynamics

A compressor's *gain computer* is nonlinear, but it operates on an envelope that
is already heavily smoothed, and the gain it applies changes slowly. The
distortion it produces sits far below the level that would justify the cost.
Revisit only if measurement shows otherwise.

---

## 4. The implementation

`Source/DSP/Oversampling/` holds a JUCE-free, real-time-safe implementation
(ADR-0028 records why Apollo does not use `juce::dsp::Oversampling`).

- **Linear phase**, from Kaiser-windowed halfband FIRs. Chosen over the cheaper
  IIR allpass cascade because a nonlinear stage is normally mixed against its dry
  signal, and a phase-rotated wet path combs rather than blends.
- **Polyphase**: half a halfband's coefficients are exactly zero and are never
  multiplied.
- **Whole-sample latency**, so a dry path can be aligned exactly. The two
  cascaded stages are designed with opposite centre-tap parity specifically to
  make the 4x figure land on a whole number.

Measured properties, from the test suite:

| | 2x | 4x |
|---|---|---|
| Latency (round trip) | 39 samples, 0.81 ms at 48 kHz | 59 samples, 1.23 ms |
| Passband ripple | ±0.0001 dB to 20 kHz | as above, per stage |
| Stopband | -100.7 dB | as above, per stage |

A stage that mixes dry against wet **must** delay its dry path by
`getLatencySamples()`. That is the one contract this class places on its callers.

---

## 5. What it costs

Per channel, including a `tanh` drive, as a percentage of real time
(see §5 of PROJECT-STATE.md for the machine):

| Setting | Cost |
|---|---|
| Bypass | 0.034 % |
| 2x | 0.287 % |
| 4x | 0.809 % |

A stereo 4x-oversampled distortion therefore costs roughly 1.6 % of one core —
small next to the voice engine, and the reason the default for a distortion stage
can reasonably be 4x rather than 2x.

`Factor::none` is a real setting, not a special case for callers to branch
around: it passes the signal through, reports zero latency, and lets a quality
control switch oversampling off without the surrounding code changing shape.
