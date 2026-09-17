#pragma once

/*
    What a golden render stores.

    WHY THIS IS NOT A WAV FILE. The obvious implementation of "golden audio
    renders" is to check in the samples and compare them exactly. Apollo cannot
    do that, and the reason is not squeamishness about repository size:

      - **Identical source does not produce identical floats across
        toolchains.** `std::sin`, `std::exp` and `std::tanh` are correctly
        rounded by nobody and differ by an ulp or so between MSVC's CRT, Apple's
        libm and glibc. GCC contracts `a * b + c` into a fused multiply-add by
        default and MSVC does not, which changes the result of a filter's
        difference equation in the last bits. Vectorisation changes the order of
        a summation. Every one of those is legitimate, and a bit-exact golden
        would report all four CI platforms as broken.
      - **A binary golden is unreviewable.** The point of checking a reference
        into version control is that a diff tells you what changed. A changed
        WAV tells you a WAV changed.

    So a golden render here is a **description** of the audio: what it did over
    time, and what it was made of. Roughly what you would write down if you
    played the render and looked at an analyser, at a resolution fine enough
    that a real change to the DSP moves it and a change of compiler does not.

    THE PARTS, AND WHY EACH ONE IS THERE.

      - **Peak and RMS**, over the whole render. Gain staging, in one number
        each. Almost anything that changes loudness moves these.
      - **Mid level per sixteenth of the render.** The envelope of the sound
        through time. This is what catches an attack that got slower, a release
        that got shorter, a delay whose repeats moved, a note that stopped being
        released at all.
      - **Side level per sixteenth.** The same, for the stereo picture. Unison
        spread, pan, ping-pong and reverb width all live here and nowhere else;
        a fingerprint of the mono sum would call a collapsed stereo image
        unchanged.
      - **Sixteen logarithmic bands** over the whole render. The timbre. This is
        what catches a filter that moved, a wavetable that changed shape, an EQ
        band that lost its gain, an oversampler that stopped running — changes
        that can leave the loudness and the envelope exactly where they were.

    THE CHECKSUM IS LOGGED AND NEVER ASSERTED ACROSS BUILDS. It is an exact hash
    of the raw samples, and it is genuinely useful for the one question it can
    answer: whether two renders **in the same process** produced the same
    samples. Compared between two builds it answers a question nobody asked.
*/

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace apollo::regression
{

/** Time resolution of the envelope part of a fingerprint. */
inline constexpr int segmentCount = 16;

/** Frequency resolution of the timbre part of a fingerprint.

    Thirty-two bands between 20 Hz and Nyquist is very nearly a third of an
    octave each, which is the resolution an audio engineer describes a spectrum
    at and is not a coincidence. Sixteen was tried first and was too coarse to
    be worth having: a two-thirds-octave band is wide enough that a filter
    corner can move inside one without the band noticing (ADR-0068).
*/
inline constexpr int bandCount = 32;

/** Everything quieter than this is recorded as this.

    A band at -170 dBFS is denormal dust, and its value is decided by rounding
    rather than by the instrument. Clamping means a silent thing compares equal
    to a silent thing instead of two arbitrary numbers being asked to agree.
*/
inline constexpr float quietFloorDb = -100.0f;

/** Where the band edges start. Below this is folded into the first band. */
inline constexpr double lowestBandHz = 20.0;

/** A rendered sound, described. */
struct Fingerprint
{
    float peakDb = quietFloorDb;
    float rmsDb = quietFloorDb;

    /** Mid (L+R)/2 level in dBFS, one per sixteenth of the render. */
    std::array<float, segmentCount> midDb {};

    /** Side (L-R)/2 level in dBFS, one per sixteenth of the render. */
    std::array<float, segmentCount> sideDb {};

    /** Band levels in dBFS, logarithmically spaced from 20 Hz to Nyquist.

        Scaled so that the bands sum to the mean square of the mid signal, which
        makes them absolute rather than relative and makes the set internally
        checkable.
    */
    std::array<float, bandCount> bandDb {};

    /** An exact hash of the samples. Logged; never compared across builds. */
    std::uint64_t checksum = 0;
};

/** @returns the fingerprint of a stereo render.

    @param buffer      two channels of rendered audio.
    @param sampleRate  what it was rendered at, which sets the band edges.
*/
[[nodiscard]] Fingerprint fingerprintOf (const juce::AudioBuffer<float>& buffer, double sampleRate);

/** One measurement that did not match its golden. */
struct Difference
{
    std::string what;
    float golden = 0.0f;
    float measured = 0.0f;
    float delta = 0.0f;
};

/** How far a measurement may move before it is a regression.

    These are not the differences observed between platforms — those are an
    order of magnitude smaller, and PROJECT-STATE §5d records them. They are set
    where a change would be **audible or meaningful**, so that the suite fails
    for reasons a person would agree with:

      - A tenth of a decibel is at the edge of audibility on a sustained level
        and well under it on a transient. A change to Apollo that moves a
        preset's loudness by more than that is a change somebody should have
        meant.
      - Bands are given twice that. A band is a narrow slice of a spectrum
        estimated from a finite render, so it is the noisiest thing here, and
        0.2 dB in one sixteenth of the spectrum is still far finer than any real
        change to a filter or a wavetable.
*/
struct Tolerances
{
    float levelDb = 0.10f;
    float bandDb = 0.20f;
};

/** @returns every measurement in @p measured that differs from @p golden by
    more than the tolerance allows. Empty means the render is unchanged.
*/
[[nodiscard]] std::vector<Difference> compare (const Fingerprint& golden,
                                              const Fingerprint& measured,
                                              const Tolerances& tolerances = {});

/** @returns the largest difference between the two, in decibels, whatever it
    was in. Used to report how much headroom the tolerances actually have.
*/
[[nodiscard]] float worstDifference (const Fingerprint& golden, const Fingerprint& measured);

} // namespace apollo::regression
