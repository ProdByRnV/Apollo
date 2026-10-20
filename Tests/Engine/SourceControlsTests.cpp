/*
    The source controls Phase 13a added: oscillator 1's tuning, each source's
    start phase, unison phase randomisation, detune curve and blend, and the
    sub oscillator's waveform and tracking (PRD §8, §9, §11).

    Two kinds of test. The unison layout's arithmetic is checked directly, where
    an exact answer is available. Everything else is checked through a real
    processor rendering a real note, because a control that computes the right
    number and never reaches the voice is the failure that matters.

    What every one of these controls must also do — leave the sound exactly as
    it was at its default — is not tested here. The regression renders do it
    (Tests/Regression), against references recorded before the controls existed,
    and they passed unchanged.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "Analysis/SignalAnalysis.h"
#include "Audio/ApolloAudioProcessor.h"
#include "DSP/Unison/UnisonLayout.h"

using namespace apollo;

namespace
{

constexpr double renderRate = 48000.0;
constexpr int blockSize = 512;
constexpr int transformSize = 16384;

void setPlain (ApolloAudioProcessor& processor, const juce::String& id, float plain)
{
    auto* parameter = processor.getValueTreeState().getParameter (id);
    jassert (parameter != nullptr);

    if (parameter != nullptr)
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
}

/** No filter and a flat envelope, so what is measured is the source. */
void makeTransparent (ApolloAudioProcessor& processor)
{
    setPlain (processor, "filter1_type", 0.0f);
    setPlain (processor, "filter2_type", 0.0f);
    setPlain (processor, "env1_attack", 0.0f);
    setPlain (processor, "env1_decay", 1.0f);
    setPlain (processor, "env1_sustain", 1.0f);
    setPlain (processor, "env1_release", 5.0f);
}

/** The sub alone, a sine an octave below the note, through nothing. */
void makeSubOnly (ApolloAudioProcessor& processor)
{
    makeTransparent (processor);
    setPlain (processor, "osc1_level", 0.0f);
    setPlain (processor, "osc2_level", 0.0f);
    setPlain (processor, "noise_level", 0.0f);
    setPlain (processor, "sub_level", 1.0f);
    setPlain (processor, "sub_octave", -1.0f);
}

/** Renders the left channel of one note held from the first sample. */
[[nodiscard]] std::vector<float> renderNote (ApolloAudioProcessor& processor, int note, int blocks)
{
    processor.prepareToPlay (renderRate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    std::vector<float> rendered;
    rendered.reserve (static_cast<std::size_t> (blocks * blockSize));

    for (int block = 0; block < blocks; ++block)
    {
        buffer.clear();
        juce::MidiBuffer midi;

        if (block == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.8f), 0);

        processor.processBlock (buffer, midi);

        const auto* left = buffer.getReadPointer (0);
        rendered.insert (rendered.end(), left, left + blockSize);
    }

    return rendered;
}

/** The loudest frequency in a held note, after its first few blocks. */
[[nodiscard]] double peakFrequencyOf (ApolloAudioProcessor& processor, int note)
{
    const auto rendered = renderNote (processor, note, transformSize / blockSize + 4);
    const std::vector<float> settled (rendered.begin() + 4 * blockSize, rendered.end());

    return analysis::analyse (settled, transformSize, renderRate).peakFrequency();
}

/** The amplitude of @p samples at exactly @p frequencyHz, under a
    Blackman-Harris window.

    Evaluated at the frequency rather than read off the nearest FFT bin,
    because a bin can be half a bin away from a harmonic, and there a
    Blackman-Harris window reads 0.8 dB low — which is what the first version of
    this test measured and called a wrong waveform. */
[[nodiscard]] double amplitudeAt (const std::vector<float>& samples, double frequencyHz) noexcept
{
    const auto size = samples.size();
    const auto twoPi = 2.0 * 3.14159265358979323846;

    double real = 0.0, imaginary = 0.0, windowSum = 0.0;

    for (std::size_t n = 0; n < size; ++n)
    {
        const auto x = twoPi * static_cast<double> (n) / static_cast<double> (size - 1);
        const auto window = 0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2.0 * x)
                          - 0.01168 * std::cos (3.0 * x);

        const auto angle = twoPi * frequencyHz * static_cast<double> (n) / renderRate;
        const auto value = window * static_cast<double> (samples[n]);

        real += value * std::cos (angle);
        imaginary -= value * std::sin (angle);
        windowSum += window;
    }

    return 2.0 * std::sqrt (real * real + imaginary * imaginary) / windowSum;
}

[[nodiscard]] double centsBetween (double measured, double expected) noexcept
{
    return measured > 0.0 && expected > 0.0 ? 1200.0 * std::log2 (measured / expected) : 1200.0;
}

[[nodiscard]] double noteFrequency (double note) noexcept
{
    return 440.0 * std::pow (2.0, (note - 69.0) / 12.0);
}

[[nodiscard]] double centsOf (const dsp::UnisonLayout& layout, int index) noexcept
{
    return 1200.0 * std::log2 (layout.getFrequencyRatio (index));
}

class SourceControlsTests final : public juce::UnitTest
{
public:
    SourceControlsTests()
        : juce::UnitTest ("Source controls", "Engine")
    {
    }

    void runTest() override
    {
        testDefaultLayoutIsUnchanged();
        testExponentialCurveCrowdsTheCentre();
        testBlendFadesTheOuterVoicesAtConstantEnergy();
        testOscillatorOneTunes();
        testSubFollowsOscillatorOne();
        testSubWaveformsAreTheirShapes();
        testStartPhaseMovesTheStart();
        testPhaseRandomIsReproducibleAndOffByDefault();
    }

private:
    /** The arguments Phase 13a added, at their defaults, build exactly the layout
        the three-argument form always built — the same bits, not merely close. */
    void testDefaultLayoutIsUnchanged()
    {
        beginTest ("A unison layout at the new controls' defaults is bit-identical to the old one");

        for (int count = 1; count <= dsp::UnisonLayout::maxVoices; ++count)
        {
            dsp::UnisonLayout before, after;
            before.update (count, 0.37f, 0.61f);
            after.update (count, 0.37f, 0.61f, dsp::UnisonLayout::DetuneCurve::linear, 1.0f);

            for (int i = 0; i < count; ++i)
            {
                expect (before.getFrequencyRatio (i) == after.getFrequencyRatio (i));
                expect (before.getGainLeft (i) == after.getGainLeft (i));
                expect (before.getGainRight (i) == after.getGainRight (i));
            }
        }

        dsp::UnisonLayout layout;
        layout.update (5, 0.3f, 0.5f, dsp::UnisonLayout::DetuneCurve::exponential, 0.4f);

        expect (layout.matches (5, 0.3f, 0.5f, dsp::UnisonLayout::DetuneCurve::exponential, 0.4f));
        expect (! layout.matches (5, 0.3f, 0.5f, dsp::UnisonLayout::DetuneCurve::linear, 0.4f),
                "a change of curve must rebuild the layout");
        expect (! layout.matches (5, 0.3f, 0.5f, dsp::UnisonLayout::DetuneCurve::exponential, 0.5f),
                "a change of blend must rebuild the layout");
    }

    void testExponentialCurveCrowdsTheCentre()
    {
        beginTest ("The exponential curve keeps the edges and pulls the inner voices in");

        dsp::UnisonLayout linear, exponential;
        linear.update (5, 1.0f, 0.0f);
        exponential.update (5, 1.0f, 0.0f, dsp::UnisonLayout::DetuneCurve::exponential, 1.0f);

        // Five voices sit at -1, -0.5, 0, 0.5 and 1 of the range. Squared, the
        // inner pair moves to a quarter of it; the edges and the centre stay.
        const auto edge = static_cast<double> (dsp::UnisonLayout::maxDetuneCents);

        expectWithinAbsoluteError (centsOf (exponential, 0), -edge, 1.0e-6);
        expectWithinAbsoluteError (centsOf (exponential, 4), edge, 1.0e-6);
        expectWithinAbsoluteError (centsOf (exponential, 2), 0.0, 1.0e-9);

        expectWithinAbsoluteError (centsOf (linear, 3), edge * 0.5, 1.0e-6);
        expectWithinAbsoluteError (centsOf (exponential, 3), edge * 0.25, 1.0e-6,
                                   "an inner voice must sit at the square of its position");
        expectWithinAbsoluteError (centsOf (exponential, 1), -edge * 0.25, 1.0e-6,
                                   "and symmetrically below the pitch");
    }

    void testBlendFadesTheOuterVoicesAtConstantEnergy()
    {
        beginTest ("The blend fades the outer voices without changing the stack's energy");

        const auto energyOf = [] (const dsp::UnisonLayout& layout)
        {
            double energy = 0.0;

            for (int i = 0; i < layout.getCount(); ++i)
            {
                const auto left = static_cast<double> (layout.getGainLeft (i));
                const auto right = static_cast<double> (layout.getGainRight (i));

                energy += left * left + right * right;
            }

            return energy;
        };

        dsp::UnisonLayout full;
        full.update (7, 0.3f, 0.0f);
        const auto reference = energyOf (full);

        for (const auto blend : { 0.0f, 0.25f, 0.5f, 0.75f })
        {
            dsp::UnisonLayout layout;
            layout.update (7, 0.3f, 0.0f, dsp::UnisonLayout::DetuneCurve::linear, blend);

            expectWithinAbsoluteError (energyOf (layout), reference, 1.0e-5,
                                       "blend " + juce::String (blend) + " changed the stack's energy");

            // Seven voices: index 3 is the centre, the rest are outer.
            expectWithinAbsoluteError (layout.getGainLeft (0) / layout.getGainLeft (3), blend, 1.0e-5f,
                                       "an outer voice must sit at the blend relative to the centre");
        }

        dsp::UnisonLayout silent;
        silent.update (7, 0.3f, 0.0f, dsp::UnisonLayout::DetuneCurve::linear, 0.0f);
        expect (silent.getGainLeft (0) == 0.0f && silent.getGainLeft (6) == 0.0f,
                "at a blend of zero only the centre is heard");
        expect (silent.getGainLeft (3) > 0.0f);

        // An even stack has no voice on the pitch, so its centre is the pair
        // either side of it.
        dsp::UnisonLayout even;
        even.update (6, 0.3f, 0.0f, dsp::UnisonLayout::DetuneCurve::linear, 0.0f);
        expect (even.getGainLeft (2) > 0.0f && even.getGainLeft (3) > 0.0f
                    && even.getGainLeft (0) == 0.0f && even.getGainLeft (5) == 0.0f,
                "an even stack's centre is its middle pair");
    }

    void testOscillatorOneTunes()
    {
        beginTest ("Oscillator 1 has coarse and fine tuning, as oscillator 2 always had");

        const auto measure = [] (float semitones, float cents)
        {
            ApolloAudioProcessor processor;
            makeTransparent (processor);
            setPlain (processor, "osc1_semitones", semitones);
            setPlain (processor, "osc1_fine", cents);

            return peakFrequencyOf (processor, 57);
        };

        const auto untuned = measure (0.0f, 0.0f);
        expect (std::abs (centsBetween (untuned, noteFrequency (57.0))) < 2.0,
                "untuned, oscillator 1 plays the note: " + juce::String (untuned, 2) + " Hz");

        const auto fifth = measure (7.0f, 0.0f);
        expect (std::abs (centsBetween (fifth, noteFrequency (64.0))) < 2.0,
                "+7 semitones must be a fifth up: " + juce::String (fifth, 2) + " Hz");

        const auto quarterTone = measure (0.0f, 50.0f);
        expect (std::abs (centsBetween (quarterTone, noteFrequency (57.5))) < 2.0,
                "+50 cents must be a quarter tone up: " + juce::String (quarterTone, 2) + " Hz");
    }

    void testSubFollowsOscillatorOne()
    {
        beginTest ("The sub follows the note, or oscillator 1 when told to");

        const auto measure = [] (float tracking)
        {
            ApolloAudioProcessor processor;
            makeSubOnly (processor);
            setPlain (processor, "osc1_semitones", 7.0f);
            setPlain (processor, "sub_tracking", tracking);

            return peakFrequencyOf (processor, 69);
        };

        const auto followsNote = measure (0.0f);
        expect (std::abs (centsBetween (followsNote, 220.0)) < 1.0,
                "tracking the note, the sub is an octave below it, whatever oscillator 1 does: "
                    + juce::String (followsNote, 2) + " Hz");

        const auto followsOscillator = measure (1.0f);
        expect (std::abs (centsBetween (followsOscillator, noteFrequency (69.0 + 7.0) * 0.5)) < 1.0,
                "tracking oscillator 1, the sub is an octave below its transposed pitch: "
                    + juce::String (followsOscillator, 2) + " Hz");
    }

    /** Each waveform measured by its harmonics, which is what the name means:
        a sine has none, a triangle has odd ones at 1/k^2, a square odd ones at
        1/k. And each peaks where the sine does, so changing it is not a jump in
        level. */
    void testSubWaveformsAreTheirShapes()
    {
        beginTest ("The sub's sine, triangle and square have the harmonics their names promise");

        struct Shape { float waveform; double third; double fifth; const char* name; };

        const Shape shapes[] {
            { 0.0f, 0.0, 0.0, "sine" },
            { 1.0f, 20.0 * std::log10 (1.0 / 9.0), 20.0 * std::log10 (1.0 / 25.0), "triangle" },
            { 2.0f, 20.0 * std::log10 (1.0 / 3.0), 20.0 * std::log10 (1.0 / 5.0), "square" },
        };

        float sinePeak = 0.0f;

        for (const auto& shape : shapes)
        {
            ApolloAudioProcessor processor;
            makeSubOnly (processor);
            setPlain (processor, "sub_waveform", shape.waveform);

            // Note 69: the sub is at 220 Hz, with its third and fifth well clear
            // of each other and of Nyquist.
            const auto rendered = renderNote (processor, 69, transformSize / blockSize + 4);
            const std::vector<float> settled (rendered.begin() + 4 * blockSize, rendered.end());
            const auto fundamental = amplitudeAt (settled, 220.0);
            const auto relative = [&] (double frequencyHz)
            {
                return 20.0 * std::log10 (std::max (amplitudeAt (settled, frequencyHz), 1.0e-12) / fundamental);
            };

            const auto second = relative (440.0);
            const auto third = relative (660.0);
            const auto fifth = relative (1100.0);

            expect (second < -80.0, juce::String (shape.name) + " must have no even harmonics: "
                                        + juce::String (second, 1) + " dB at the second");

            if (shape.waveform == 0.0f)
            {
                expect (third < -80.0, "a sine has no third harmonic: " + juce::String (third, 1) + " dB");
                sinePeak = analysis::peakOf (settled);
                continue;
            }

            expectWithinAbsoluteError (third, shape.third, 0.1,
                                       juce::String (shape.name) + "'s third harmonic");
            expectWithinAbsoluteError (fifth, shape.fifth, 0.1,
                                       juce::String (shape.name) + "'s fifth harmonic");

            // The builder normalises every frame so its loudest mip level peaks
            // at 1, so no waveform can peak above the sine. The square comes in
            // a little under it, because the level carrying the most Gibbs
            // overshoot is the one scaled to 1 (WavetableLibrary.cpp).
            const auto ratio = analysis::peakOf (settled) / sinePeak;
            logMessage ("    " + juce::String (shape.name) + " peaks at " + juce::String (ratio, 3)
                        + " of the sine at 220 Hz");
            expect (ratio > 0.9f && ratio < 1.001f,
                    juce::String (shape.name) + " peaks at " + juce::String (ratio, 3)
                        + " of the sine, not within 0.9 to 1");
        }
    }

    /** Half a cycle of a sine is its negative, so a note started half a cycle
        later must cancel the note started at zero, sample for sample. That is a
        start phase that reached the voice, not merely a parameter that moved. */
    void testStartPhaseMovesTheStart()
    {
        beginTest ("A start phase of half a cycle starts the sine at its negative");

        const auto renderWithPhase = [] (float phase)
        {
            ApolloAudioProcessor processor;
            makeSubOnly (processor);
            setPlain (processor, "sub_phase", phase);

            return renderNote (processor, 60, 8);
        };

        const auto atZero = renderWithPhase (0.0f);
        const auto atHalf = renderWithPhase (0.5f);

        float worst = 0.0f;

        for (std::size_t i = 0; i < atZero.size(); ++i)
            worst = std::max (worst, std::abs (atZero[i] + atHalf[i]));

        const auto peak = analysis::peakOf (atZero);

        expect (peak > 0.01f, "the note must sound");
        expect (worst < peak * 1.0e-3f,
                "half a cycle later must be the negative: residual "
                    + juce::String (worst / peak, 6) + " of the peak");

        // The primary oscillators reach theirs the same way. Not a sine, so no
        // exact identity, but a moved start must move the samples.
        const auto renderOscillator = [] (float phase)
        {
            ApolloAudioProcessor processor;
            makeTransparent (processor);
            setPlain (processor, "osc1_phase", phase);
            return renderNote (processor, 60, 2);
        };

        expect (renderOscillator (0.0f) != renderOscillator (0.3f),
                "oscillator 1's start phase must reach the voice");
    }

    /** Random, but not unrepeatable: the same notes must render the same way
        every time, or nothing about Apollo's output could be asserted (ADR-0017). */
    void testPhaseRandomIsReproducibleAndOffByDefault()
    {
        beginTest ("Phase randomisation scatters a stack reproducibly, and is off by default");

        const auto renderStack = [] (bool setRandom, float amount)
        {
            ApolloAudioProcessor processor;
            makeTransparent (processor);
            setPlain (processor, "osc1_unison", 7.0f);
            setPlain (processor, "osc1_detune", 0.3f);

            if (setRandom)
                setPlain (processor, "osc1_phase_random", amount);

            return renderNote (processor, 57, 4);
        };

        const auto untouched = renderStack (false, 0.0f);

        expect (renderStack (true, 0.0f) == untouched,
                "a randomisation of zero must render exactly what no randomisation rendered");

        const auto scattered = renderStack (true, 0.8f);

        expect (scattered != untouched, "a randomisation above zero must move the stack's start");
        expect (renderStack (true, 0.8f) == scattered,
                "the same notes must scatter the same way every time they are rendered");
        expect (analysis::allFinite (scattered));
    }
};

SourceControlsTests sourceControlsTests;

} // namespace
