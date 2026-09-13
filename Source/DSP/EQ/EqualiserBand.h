#pragma once

/*
    One band of the equaliser: a shape, a place to put it, and how many times.

    WHAT A BAND IS. A type, a centre frequency, a gain, a bandwidth and an
    order — and the order is what makes this more than a biquad with a struct
    around it. Fruity Parametric EQ 2, the reference (ADR-0059), describes its
    slope control as "the number of instances of the filter", so that is exactly
    what it is here: order N is N identical sections in series. For a low or
    high pass that is 12 dB per octave per instance, giving the 12, 24, 36 and
    48 dB/octave the reference offers. For a bell or a shelf it means the gain
    applies N times over, so a +6 dB bell at order 3 is +18 dB at its centre.

    That last consequence is deliberate and is not a bug to be normalised away.
    An EQ where raising the slope quietly divided the gain to compensate would
    be an EQ whose displayed gain is not the gain, and the roadmap's requirement
    is *predictable* gain behaviour rather than constant gain behaviour. Cascading
    also narrows the audible width — N sections each down 3 dB at the band edge
    are 3N dB down together — which is the other half of what a steeper slope
    means.

    BANDWIDTH, NOT Q. The control is the width of the band in octaves, because
    that is what the reference offers and because it is the half of the
    bandwidth/Q pair that a musician can hear: one octave is one octave wherever
    the band sits, whereas the Q that produces it is a different number at 50 Hz
    than at 5 kHz. `rbj::qForBandwidth` is where the two are reconciled. Wider is
    a larger number, which is also the direction the reference's control moves.

    A TRANSPARENT BAND IS SKIPPED, NOT PROCESSED. A band that is off, muted, or a
    gain shape sitting at exactly 0 dB is mathematically the identity — the
    cookbook's numerator becomes its denominator term for term — so it is
    bypassed entirely rather than multiplied through. An equaliser with nothing
    dialled in therefore costs nothing and is bit-transparent, which is what
    makes it safe to leave in the chain.

    WHY THE STATE IS CLEARED ON THE WAY BACK IN. A skipped band keeps whatever
    was in its sections when it stopped. Letting that back in when the band is
    switched on again pours seconds-old audio into the signal — the same bug the
    reverb's bypass had in 8c — so a band that becomes active again starts from
    silence.

    SMOOTHING IS PER BLOCK, IN THE UNITS THE EAR USES. Frequency is smoothed in
    the log domain and gain in decibels, so a swept band travels evenly rather
    than rushing through the bottom of its range. The step happens once per
    block rather than per sample: redesigning a biquad costs trigonometry, and
    at a few milliseconds per block an exponential approach to the target is
    inaudible. Coefficients are never interpolated — only the settings they are
    designed from are — because an interpolated coefficient set is not
    necessarily a stable filter, while a filter designed from an intermediate
    frequency always is.

    REAL-TIME CONTRACT: `prepare` runs while audio is stopped. Everything else
    is audio-thread callable and allocates nothing (CLAUDE.md §7).
*/

#include "DSP/EQ/Biquad.h"
#include "DSP/Effects/AudioEffect.h"

#include <array>

namespace apollo::dsp
{

class EqualiserBand
{
public:
    /** The shapes a band can take, in the order Fruity Parametric EQ 2 lists
        them, with `off` first.

        Enumerated in full and fixed from the start for the reason every discrete
        parameter in Apollo is: the value is stored normalised in automation
        lanes and presets, so a range that grew later would silently remap every
        one of them (Docs/PARAMETER-CONVENTIONS.md §1).
    */
    enum class Type
    {
        off = 0,    ///< The band does nothing and costs nothing.
        lowPass,    ///< Passes below the frequency; 12 dB/octave per instance.
        bandPass,   ///< Passes a band around it, unity at the centre.
        highPass,   ///< Passes above the frequency.
        notch,      ///< Removes a band around it, unity elsewhere.
        lowShelf,   ///< Lifts or drops everything below the frequency.
        peaking,    ///< A bell around the frequency. The default.
        highShelf   ///< Lifts or drops everything above the frequency.
    };

    /** Number of values `Type` has, including `off`. */
    static constexpr int typeCount = 8;

    /** Most instances of the shape a band will place in series.

        Four, matching the reference's four slope settings. The sections are
        allocated for the maximum whether or not they are used, so changing the
        order never allocates.
    */
    static constexpr int maxOrder = 4;

    /** The user-facing limits, which the registry's ranges mirror.

        `maximumGainDb` is 18 because that is the scale the reference's display
        is drawn to and the labels its fader carries.
    */
    static constexpr float minimumFrequencyHz = 20.0f;
    static constexpr float maximumFrequencyHz = 20000.0f;
    static constexpr float maximumGainDb = 18.0f;
    static constexpr float minimumBandwidthOctaves = 0.05f;
    static constexpr float maximumBandwidthOctaves = 6.0f;

    struct Settings
    {
        Type type = Type::peaking;

        float frequencyHz = 1000.0f;

        /** Decibels of boost or cut. Ignored by the shapes that have no gain —
            low pass, band pass, high pass and notch — exactly as the reference
            disables its fader for those four.
        */
        float gainDb = 0.0f;

        /** Width of the band in octaves. Larger is wider. */
        float bandwidthOctaves = 1.0f;

        /** Instances of the shape in series, 1 to `maxOrder`. */
        int order = 1;

        /** Taken out of circuit while left in the band's place, so that what a
            band is doing can be checked by ear without losing its settings.
        */
        bool muted = false;

        [[nodiscard]] bool operator== (const Settings&) const = default;
    };

    EqualiserBand() = default;

    /** Sizes nothing — there is nothing to size — but fixes the sample rate the
        coefficients are designed for and settles the smoothing at its target.
    */
    void prepare (double sampleRate);

    /** Clears every section's memory without changing the settings. */
    void reset() noexcept;

    /** Sets the target the band smooths towards. Audio-thread safe.

        Type, order and mute take effect on the next block rather than gliding:
        they are discrete choices, and there is no meaningful halfway point
        between a bell and a notch.
    */
    void setSettings (const Settings& newSettings) noexcept;

    [[nodiscard]] const Settings& getSettings() const noexcept { return target; }

    /** True when the band is mathematically the identity and will be skipped.

        Reflects where the smoothing has actually reached, not where it is
        heading: a band whose gain is on its way back to zero is still doing
        something until it arrives.
    */
    [[nodiscard]] bool isTransparent() const noexcept;

    /** Processes in place, smoothing and redesigning once for the block. */
    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    /** The coefficients one section currently holds. For tests and for the
        stability checks that read the pole positions directly.
    */
    [[nodiscard]] const BiquadCoefficients& getCoefficients() const noexcept { return coefficients; }

    /** @returns the gain this band applies at @p hz, in decibels, including the
        order — which is to say, what the curve drawn over the display shows.
    */
    [[nodiscard]] double magnitudeDbAt (double hz) const noexcept;

    /** The same answer computed from settings alone, without a prepared band.

        This is the authority the interface's curve is checked against: the
        frontend redraws the response in TypeScript, and a test can compare its
        arithmetic against this rather than against a picture.
    */
    [[nodiscard]] static double magnitudeDbAt (const Settings& settings,
                                               double sampleRate,
                                               double hz) noexcept;

    /** Coefficients for a set of settings, without a band to hold them. */
    [[nodiscard]] static BiquadCoefficients design (const Settings& settings,
                                                   double sampleRate) noexcept;

    /** True for a shape whose gain control does something. The four that return
        false are the ones whose fader the reference disables.
    */
    [[nodiscard]] static bool hasGain (Type type) noexcept;

    /** How long the band keeps ringing after its input stops, in seconds.

        An estimate rather than a measurement: a resonant pole decays with a time
        constant of Q/(pi*f), and this reports the time that takes to fall far
        enough to be inaudible. Only bands that actually resonate contribute —
        a cut has no ring to wait for — so an equaliser used for cuts reports no
        tail at all.
    */
    [[nodiscard]] double getTailSeconds() const noexcept;

private:
    /** Moves the smoothed settings one block closer to the target.

        @returns true when anything moved, and therefore when the coefficients
                 need redesigning.
    */
    [[nodiscard]] bool advanceSmoothing (int numSamples) noexcept;

    void redesign() noexcept;

    Settings target;

    /** Where the smoothing has reached. Frequency is carried as its logarithm
        because that is the axis it travels evenly along.
    */
    double smoothedLogFrequency = 0.0;
    double smoothedGainDb = 0.0;
    double smoothedBandwidth = 1.0;

    BiquadCoefficients coefficients;

    /** Sections in use this block: the order, or zero while transparent. */
    int activeSections = 0;

    /** Whether the previous block processed anything, so that a band coming back
        into circuit can clear the audio its sections were holding.
    */
    bool wasActive = false;

    double preparedSampleRate = 0.0;

    std::array<std::array<BiquadState, maxOrder>,
               static_cast<std::size_t> (maxEffectChannels)> sections {};
};

} // namespace apollo::dsp
