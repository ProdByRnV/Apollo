/*
    The comb and formant filters, and the slot that holds every filter type
    (Phase 13c, PRD §13.1).

    Each claim the headers make is measured here, by running a signal through
    the filter rather than by checking the arithmetic that sets it up:

      - the comb peaks at its cutoff and every multiple, with zeros between,
        and lifts those peaks by the amount CombFilter.h says;
      - it leaves the loudness of a broadband signal alone at every resonance;
      - it stays stable at full resonance at every tuning, while being swept,
        and decays to exactly zero;
      - the formant filter's cutoff chooses the vowel, and its resonance
        narrows it;
      - the slot's four state-variable types are the state-variable filter,
        sample for sample, which is what lets 13c leave every existing sound
        alone.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "Audio/ApolloAudioProcessor.h"
#include "DSP/Filters/CombFilter.h"
#include "DSP/Filters/FormantFilter.h"
#include "DSP/Filters/VoiceFilter.h"

using namespace apollo;
using namespace apollo::dsp;

namespace
{

constexpr double rate = 48000.0;
constexpr double pi = 3.14159265358979323846;

[[nodiscard]] double toDecibels (double gain)
{
    return 20.0 * std::log10 (std::max (gain, 1.0e-9));
}

/** Steady-state gain of @p filter at @p frequencyHz: a sine in, the RMS of
    what comes out after it settles, over the RMS that went in. */
template <typename Filter>
[[nodiscard]] double gainAt (Filter& filter, double frequencyHz, double settleSeconds = 1.0)
{
    const auto settle = static_cast<int> (rate * settleSeconds);
    const auto measure = static_cast<int> (rate * 0.5);

    double sumIn = 0.0, sumOut = 0.0;

    for (int i = 0; i < settle + measure; ++i)
    {
        const auto input = static_cast<float> (std::sin (2.0 * pi * frequencyHz * i / rate));
        const auto output = filter.processSample (input);

        if (i >= settle)
        {
            sumIn += static_cast<double> (input) * static_cast<double> (input);
            sumOut += static_cast<double> (output) * static_cast<double> (output);
        }
    }

    return std::sqrt (sumOut / sumIn);
}

/** A comb, prepared and tuned. */
[[nodiscard]] CombFilter makeComb (float cutoffHz, float q)
{
    CombFilter comb;
    comb.prepare (rate);

    CombCoefficients coefficients;
    coefficients.set (cutoffHz, q, rate);
    comb.setCoefficients (coefficients);

    return comb;
}

[[nodiscard]] FormantFilter makeFormant (float cutoffHz, float q)
{
    FormantFilter formant;

    FormantCoefficients coefficients;
    coefficients.set (cutoffHz, q, rate);
    formant.setCoefficients (coefficients);

    return formant;
}

/** White noise, reproducibly. */
class Noise
{
public:
    float next() noexcept
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<float> (state >> 8) / 8388608.0f - 1.0f;
    }

private:
    std::uint32_t state = 12345u;
};

/** A band-limited saw at @p frequencyHz, summed from its harmonics. */
[[nodiscard]] std::vector<float> sawWave (double frequencyHz, int samples)
{
    std::vector<float> saw (static_cast<std::size_t> (samples), 0.0f);
    const auto harmonics = static_cast<int> (rate * 0.45 / frequencyHz);

    for (int i = 0; i < samples; ++i)
    {
        double value = 0.0;

        for (int k = 1; k <= harmonics; ++k)
            value += std::sin (2.0 * pi * k * frequencyHz * i / rate) / k;

        saw[static_cast<std::size_t> (i)] = static_cast<float> (value * 2.0 / pi);
    }

    return saw;
}

[[nodiscard]] double rmsOf (const std::vector<float>& signal, std::size_t from)
{
    double sum = 0.0;

    for (auto i = from; i < signal.size(); ++i)
        sum += static_cast<double> (signal[i]) * static_cast<double> (signal[i]);

    return std::sqrt (sum / static_cast<double> (signal.size() - from));
}

void setPlain (ApolloAudioProcessor& processor, const juce::String& id, float plain)
{
    auto* parameter = processor.getValueTreeState().getParameter (id);
    jassert (parameter != nullptr);

    if (parameter != nullptr)
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
}

/** A held saw on oscillator 1, through filter 1 as configured, as RMS. */
[[nodiscard]] double renderThroughFilter (float type, float cutoff, float q, int note)
{
    ApolloAudioProcessor processor;
    setPlain (processor, "osc1_level", 1.0f);
    setPlain (processor, "osc1_wavetable", 0.0f);
    setPlain (processor, "osc1_position", 1.0f);
    setPlain (processor, "env1_attack", 0.0f);
    setPlain (processor, "env1_sustain", 1.0f);
    setPlain (processor, "filter1_type", type);
    setPlain (processor, "filter1_cutoff", cutoff);
    setPlain (processor, "filter1_resonance", q);

    constexpr int block = 512;
    processor.prepareToPlay (rate, block);

    juce::AudioBuffer<float> buffer (2, block);
    std::vector<float> rendered;

    for (int b = 0; b < 48; ++b)
    {
        buffer.clear();
        juce::MidiBuffer midi;

        if (b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.9f), 0);

        processor.processBlock (buffer, midi);
        rendered.insert (rendered.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + block);
    }

    for (const auto value : rendered)
        if (! std::isfinite (value))
            return -1.0;

    return rmsOf (rendered, rendered.size() / 2);
}

class CombFormantTests final : public juce::UnitTest
{
public:
    CombFormantTests()
        : juce::UnitTest ("Comb and formant filters", "DSP")
    {
    }

    void runTest() override
    {
        testCombPeaksAndZeros();
        testCombPeakHeight();
        testCombKeepsBroadbandLoudness();
        testCombStability();
        testCombDecaysToSilence();
        testCombGlidesAndResets();
        testCombCoefficientsAreClamped();
        testFormantChoosesTheVowel();
        testFormantResonanceNarrows();
        testFormantLoudness();
        testFormantHostileSettings();
        testSlotKeepsTheStateVariableTypes();
        testSlotSwitching();
        testTypesThroughTheProcessor();
    }

private:
    void testCombPeaksAndZeros()
    {
        beginTest ("The comb peaks at its cutoff and every multiple, with zeros half way between");

        // 480 Hz is exactly 100 samples, so nothing here depends on the
        // interpolator; 440 Hz is not, and must land within a few cents.
        for (const auto cutoffHz : { 480.0f, 440.0f })
        {
            const auto cutoff = static_cast<double> (cutoffHz);

            for (const auto multiple : { 1.0, 2.0, 3.0, 5.0 })
            {
                auto comb = makeComb (cutoffHz, 3.0f);
                const auto peak = toDecibels (gainAt (comb, cutoff * multiple));
                auto between = makeComb (cutoffHz, 3.0f);
                const auto zero = toDecibels (gainAt (between, cutoff * (multiple + 0.5)));

                expect (peak > 10.0, "a peak at " + juce::String (cutoff * multiple, 0) + " Hz reads "
                                         + juce::String (peak, 1) + " dB");
                expect (zero < -30.0, "a zero at " + juce::String (cutoff * (multiple + 0.5), 0)
                                          + " Hz reads " + juce::String (zero, 1) + " dB");
            }
        }
    }

    void testCombPeakHeight()
    {
        beginTest ("The comb lifts its peaks by 2 sqrt (Q + 1/2), and more resonance lifts them more");

        auto previous = -100.0;

        for (const auto q : { 0.1f, 0.707f, 2.0f, 5.0f, 10.0f })
        {
            auto comb = makeComb (480.0f, q);
            const auto measured = toDecibels (gainAt (comb, 480.0, 3.0));
            const auto expected = toDecibels (2.0 * std::sqrt (static_cast<double> (q) + 0.5));

            expectWithinAbsoluteError (measured, expected, 0.2,
                                       "the peak at Q " + juce::String (q) + " reads " + juce::String (measured, 2));
            expect (measured > previous, "and rises with resonance");
            previous = measured;
        }
    }

    void testCombKeepsBroadbandLoudness()
    {
        beginTest ("The comb leaves a broadband signal's loudness alone at every resonance");

        for (const auto cutoff : { 480.0f, 437.3f })
        {
            for (const auto q : { 0.1f, 0.707f, 3.0f, 10.0f })
            {
                auto comb = makeComb (cutoff, q);
                Noise noise;

                double sumIn = 0.0, sumOut = 0.0;

                for (int i = 0; i < static_cast<int> (rate * 10.0); ++i)
                {
                    const auto input = noise.next();
                    const auto output = comb.processSample (input);

                    if (i > static_cast<int> (rate))
                    {
                        sumIn += static_cast<double> (input) * static_cast<double> (input);
                        sumOut += static_cast<double> (output) * static_cast<double> (output);
                    }
                }

                // Whole or fractional period alike: the allpass interpolator
                // passes every frequency at unity. The cubic it replaced lost
                // 2.9 dB here on the fractional period at Q 10.
                const auto change = 10.0 * std::log10 (sumOut / sumIn);

                expectWithinAbsoluteError (change, 0.0, 0.25,
                                           "noise through a comb at " + juce::String (cutoff, 1) + " Hz, Q "
                                               + juce::String (q) + " changes level by "
                                               + juce::String (change, 2) + " dB");
            }
        }
    }

    void testCombStability()
    {
        beginTest ("The comb stays bounded at full resonance at every tuning, and while swept");

        // Loud noise into the most feedback any setting allows, from the lowest
        // tuning to the highest, and past both ends.
        for (const auto cutoff : { 10.0f, 20.0f, 55.0f, 1000.0f, 9000.0f, 16000.0f, 30000.0f })
        {
            auto comb = makeComb (cutoff, 1000.0f);
            Noise noise;

            float worst = 0.0f;
            auto finite = true;

            for (int i = 0; i < static_cast<int> (rate * 5.0); ++i)
            {
                const auto output = comb.processSample (noise.next());
                finite = finite && std::isfinite (output);
                worst = std::max (worst, std::abs (output));
            }

            expect (finite && worst < 40.0f, "a comb at " + juce::String (cutoff) + " Hz peaked at "
                                                 + juce::String (worst));
        }

        // Swept from top to bottom and back every 64 samples, for ten seconds:
        // the glide must keep a delay that moves this fast from running away.
        auto comb = makeComb (1000.0f, 10.0f);
        Noise noise;
        float worst = 0.0f;
        auto finite = true;

        for (int i = 0; i < static_cast<int> (rate * 10.0); ++i)
        {
            if (i % 64 == 0)
            {
                CombCoefficients coefficients;
                coefficients.set ((i / 64) % 2 == 0 ? 20.0f : 16000.0f, 10.0f, rate);
                comb.setCoefficients (coefficients);
            }

            const auto output = comb.processSample (noise.next());
            finite = finite && std::isfinite (output);
            worst = std::max (worst, std::abs (output));
        }

        logMessage ("    swept end to end every 64 samples: peak " + juce::String (worst, 2));
        expect (finite && worst < 40.0f, "a comb swept end to end peaked at " + juce::String (worst));

        // And retuned to a random cutoff every 16 samples, which is no musical
        // gesture at all and the hardest thing the delay can be asked to do.
        juce::Random random (7);
        auto chaotic = makeComb (1000.0f, 10.0f);
        worst = 0.0f;

        for (int i = 0; i < static_cast<int> (rate * 10.0); ++i)
        {
            if (i % 16 == 0)
            {
                CombCoefficients coefficients;
                coefficients.set (20.0f * std::pow (800.0f, random.nextFloat()), 10.0f, rate);
                chaotic.setCoefficients (coefficients);
            }

            const auto output = chaotic.processSample (noise.next());
            finite = finite && std::isfinite (output);
            worst = std::max (worst, std::abs (output));
        }

        logMessage ("    retuned at random every 16 samples: peak " + juce::String (worst, 2));
        expect (finite && worst < 40.0f, "a comb retuned at random peaked at " + juce::String (worst));
    }

    void testCombDecaysToSilence()
    {
        beginTest ("A ringing comb decays to exactly zero rather than to denormals");

        auto comb = makeComb (1000.0f, 10.0f);
        Noise noise;

        for (int i = 0; i < 4800; ++i)
            (void) comb.processSample (noise.next());

        auto last = 1.0f;

        for (int i = 0; i < static_cast<int> (rate * 5.0); ++i)
            last = comb.processSample (0.0f);

        expect (last == 0.0f, "five seconds after the input stopped the comb reads " + juce::String (last));
    }

    void testCombGlidesAndResets()
    {
        beginTest ("A retuned comb glides rather than jumps, and a reset leaves nothing behind");

        // A steady tone through a comb retuned by an octave: the largest step
        // between two output samples must stay near what the tone itself makes.
        auto comb = makeComb (220.0f, 2.0f);
        float worstStep = 0.0f;
        float steadyStep = 0.0f;
        float previous = 0.0f;

        for (int i = 0; i < static_cast<int> (rate * 2.0); ++i)
        {
            if (i == static_cast<int> (rate))
            {
                CombCoefficients coefficients;
                coefficients.set (440.0f, 2.0f, rate);
                comb.setCoefficients (coefficients);
            }

            const auto output = comb.processSample (static_cast<float> (std::sin (2.0 * pi * 220.0 * i / rate)));
            const auto step = std::abs (output - previous);
            previous = output;

            if (i > static_cast<int> (rate * 0.5) && i < static_cast<int> (rate))
                steadyStep = std::max (steadyStep, step);
            else if (i >= static_cast<int> (rate))
                worstStep = std::max (worstStep, step);
        }

        // A glide bends the pitch on its way, so the steps grow a little; a
        // delay that jumped would step by the size of the signal itself, about
        // thirty times the steady step here.
        expect (worstStep < steadyStep * 3.0f, "retuning stepped by " + juce::String (worstStep)
                                                   + " against " + juce::String (steadyStep) + " steady");

        comb.reset();
        auto silent = true;

        for (int i = 0; i < 48000; ++i)
            silent = silent && comb.processSample (0.0f) == 0.0f;

        expect (silent, "after a reset, silence in is silence out");

        // Before prepare there is no line, and the input is not lost.
        CombFilter unprepared;
        expect (unprepared.processSample (0.25f) == 0.25f, "an unprepared comb passes its input");
    }

    void testCombCoefficientsAreClamped()
    {
        beginTest ("The comb's coefficients stay inside what the line and the loop can take");

        CombCoefficients coefficients;

        coefficients.set (30000.0f, 0.707f, rate);
        expectWithinAbsoluteError (coefficients.delaySamples, 3.0f, 1.0e-6f, "a cutoff past fs / 3 is held there");

        coefficients.set (std::nanf (""), std::nanf (""), rate);
        expect (std::isfinite (coefficients.delaySamples) && std::isfinite (coefficients.feedback)
                    && std::isfinite (coefficients.gain),
                "NaN settings resolve to finite coefficients");
        expectWithinAbsoluteError (coefficients.delaySamples, 2400.0f, 1.0e-3f, "and NaN tunes to the lowest comb");

        coefficients.set (1000.0f, 1.0e9f, rate);
        expect (coefficients.feedback <= 0.97f, "no resonance reaches more than 0.97 feedback");

        // Coefficients for 192 kHz on a line prepared at 48: read inside it.
        CombFilter comb;
        comb.prepare (rate);
        coefficients.set (20.0f, 1.0f, 192000.0);
        comb.setCoefficients (coefficients);

        auto finite = true;

        for (int i = 0; i < 48000; ++i)
            finite = finite && std::isfinite (comb.processSample (0.5f));

        expect (finite, "coefficients for a higher rate than the line's are held to the line");
    }

    void testFormantChoosesTheVowel()
    {
        beginTest ("The formant filter's cutoff chooses the vowel");

        expectWithinAbsoluteError (FormantCoefficients::vowelPosition (160.0f), 0.0f, 1.0e-6f);
        expectWithinAbsoluteError (FormantCoefficients::vowelPosition (1280.0f), 2.0f, 1.0e-5f, "A at 1.28 kHz");
        expectWithinAbsoluteError (FormantCoefficients::vowelPosition (10240.0f), 4.0f, 1.0e-5f, "I at 10.24 kHz");
        expectWithinAbsoluteError (FormantCoefficients::vowelPosition (20000.0f), 4.0f, 1.0e-6f);
        expectWithinAbsoluteError (FormantCoefficients::vowelPosition (20.0f), 0.0f, 1.0e-6f);
        expectWithinAbsoluteError (FormantCoefficients::vowelPosition (std::nanf ("")), 0.0f, 1.0e-6f);

        // Each vowel is loudest at its own first formant: swept in sixths of an
        // octave from 100 Hz to 3.2 kHz, the loudest probe is within a quarter
        // of an octave of it.
        struct Case { const char* name; float cutoff; double f1; };

        const Case cases[] {
            { "U", 160.0f, 350.0 },
            { "O", 452.5f, 400.0 },
            { "A", 1280.0f, 650.0 },
            { "E", 3620.4f, 400.0 },
            { "I", 10240.0f, 290.0 },
        };

        for (const auto& c : cases)
        {
            auto loudest = 0.0;
            auto loudestAt = 0.0;

            for (int k = 0; k <= 30; ++k)
            {
                const auto probe = 100.0 * std::pow (2.0, k / 6.0);
                auto formant = makeFormant (c.cutoff, 0.707f);
                const auto gain = gainAt (formant, probe, 0.25);

                if (gain > loudest)
                {
                    loudest = gain;
                    loudestAt = probe;
                }
            }

            const auto octaves = std::abs (std::log2 (loudestAt / c.f1));

            expect (octaves <= 0.25, juce::String (c.name) + " is loudest at " + juce::String (loudestAt, 0)
                                         + " Hz, not near its first formant at " + juce::String (c.f1, 0));
        }

        // And the vowels are different: I's second formant is far above U's,
        // so I passes 1.87 kHz much more strongly.
        auto u = makeFormant (160.0f, 0.707f);
        auto i = makeFormant (10240.0f, 0.707f);
        const auto difference = toDecibels (gainAt (i, 1870.0)) - toDecibels (gainAt (u, 1870.0));
        expect (difference > 10.0, "I passes 1.87 kHz " + juce::String (difference, 1) + " dB more than U");
    }

    void testFormantResonanceNarrows()
    {
        beginTest ("The formant filter's resonance narrows its formants");

        // A's first formant at 650 Hz, and a frequency a third of an octave
        // above it: the gap between them grows with resonance.
        auto previousGap = -100.0;

        for (const auto q : { 0.3f, 0.707f, 3.0f, 10.0f })
        {
            auto peak = makeFormant (1280.0f, q);
            auto off = makeFormant (1280.0f, q);
            const auto gap = toDecibels (gainAt (peak, 650.0)) - toDecibels (gainAt (off, 650.0 * std::pow (2.0, 1.0 / 3.0)));

            expect (gap > previousGap, "at Q " + juce::String (q) + " the formant stands "
                                           + juce::String (gap, 1) + " dB above its shoulder");
            previousGap = gap;
        }
    }

    void testFormantLoudness()
    {
        beginTest ("A saw through the formant filter is about as loud as through no filter");

        const auto saw = sawWave (110.0, static_cast<int> (rate));
        const auto reference = rmsOf (saw, saw.size() / 2);

        for (const auto cutoff : { 160.0f, 400.0f, 1280.0f, 4000.0f, 10240.0f })
        {
            auto formant = makeFormant (cutoff, 0.707f);
            std::vector<float> filtered;

            for (const auto sample : saw)
                filtered.push_back (formant.processSample (sample));

            const auto change = toDecibels (rmsOf (filtered, filtered.size() / 2) / reference);

            logMessage ("    vowel position " + juce::String (FormantCoefficients::vowelPosition (cutoff), 2)
                        + ": " + juce::String (change, 1) + " dB against the saw");

            expect (std::abs (change) < 3.0,
                    "a 110 Hz saw through the vowel at " + juce::String (cutoff) + " Hz changes level by "
                        + juce::String (change, 1) + " dB");
        }
    }

    void testFormantHostileSettings()
    {
        beginTest ("The formant filter stays finite for any setting and any input");

        for (const auto cutoff : { 0.0f, 20.0f, 20000.0f, 1.0e9f, std::nanf ("") })
        {
            for (const auto q : { 0.0f, 0.1f, 10.0f, 1.0e9f, std::nanf ("") })
            {
                auto formant = makeFormant (cutoff, q);
                Noise noise;
                auto finite = true;
                float worst = 0.0f;

                for (int n = 0; n < 48000; ++n)
                {
                    const auto output = formant.processSample (noise.next() * 4.0f);
                    finite = finite && std::isfinite (output);
                    worst = std::max (worst, std::abs (output));
                }

                expect (finite && worst < 100.0f, "cutoff " + juce::String (cutoff) + ", Q " + juce::String (q)
                                                      + " peaked at " + juce::String (worst));
            }
        }
    }

    void testSlotKeepsTheStateVariableTypes()
    {
        beginTest ("The slot's four state-variable types are the state-variable filter, sample for sample");

        const std::pair<FilterType, StateVariableFilter::Mode> pairs[] {
            { FilterType::lowpass, StateVariableFilter::Mode::lowpass },
            { FilterType::highpass, StateVariableFilter::Mode::highpass },
            { FilterType::bandpass, StateVariableFilter::Mode::bandpass },
            { FilterType::notch, StateVariableFilter::Mode::notch },
        };

        for (const auto& [type, mode] : pairs)
        {
            VoiceFilter slot;
            slot.prepare (rate);
            slot.setType (type);

            VoiceFilterCoefficients coefficients;
            coefficients.set (type, 900.0f, 4.0f, rate);
            slot.setCoefficients (coefficients);

            StateVariableFilter reference;
            reference.setMode (mode);
            SvfCoefficients svf;
            svf.set (900.0f, 4.0f, rate);
            reference.setCoefficients (svf);

            Noise noise;
            auto identical = true;

            for (int i = 0; i < 48000; ++i)
            {
                const auto input = noise.next();
                identical = identical && slot.processSample (input) == reference.processSample (input);
            }

            expect (identical, "type " + juce::String (static_cast<int> (type)) + " differs");

            // And resolving a state-variable type computes nothing else.
            expect (coefficients.comb == CombCoefficients {} && coefficients.formant == FormantCoefficients {},
                    "a state-variable type resolves only its own coefficients");
        }

        VoiceFilter off;
        expect (off.processSample (0.3f) == 0.3f, "off passes its input");
    }

    void testSlotSwitching()
    {
        beginTest ("Switching into the comb or the formant filter starts it from silence");

        VoiceFilter slot;
        slot.prepare (rate);

        VoiceFilterCoefficients coefficients;
        coefficients.set (FilterType::comb, 500.0f, 10.0f, rate);
        slot.setType (FilterType::comb);
        slot.setCoefficients (coefficients);

        Noise noise;

        for (int i = 0; i < 4800; ++i)
            (void) slot.processSample (noise.next());

        // Away and back: the comb's line must not still hold that noise.
        slot.setType (FilterType::lowpass);
        coefficients.set (FilterType::lowpass, 500.0f, 1.0f, rate);
        slot.setCoefficients (coefficients);
        slot.setType (FilterType::comb);
        coefficients.set (FilterType::comb, 500.0f, 10.0f, rate);
        slot.setCoefficients (coefficients);

        auto silent = true;

        for (int i = 0; i < 4800; ++i)
            silent = silent && slot.processSample (0.0f) == 0.0f;

        expect (silent, "a comb switched back to holds nothing from before");
    }

    void testTypesThroughTheProcessor()
    {
        beginTest ("Comb and formant reach the voice through the type parameter");

        const auto lowpass = renderThroughFilter (1.0f, 20000.0f, 0.707f, 57);
        const auto combTuned = renderThroughFilter (5.0f, 220.0f, 5.0f, 57);
        const auto combBetween = renderThroughFilter (5.0f, 293.3f, 5.0f, 57);
        const auto formant = renderThroughFilter (6.0f, 1280.0f, 0.707f, 57);

        expect (lowpass > 0.0 && combTuned > 0.0 && combBetween > 0.0 && formant > 0.0,
                "every type sounds, and finitely");

        // A comb tuned to the note lifts all of its harmonics; tuned a fourth
        // away, most fall between the teeth.
        expect (combTuned > combBetween * 2.0,
                "a comb tuned to A3 is louder than one a fourth above it: "
                    + juce::String (toDecibels (combTuned / combBetween), 1) + " dB");

        expect (std::abs (toDecibels (formant / lowpass)) > 0.5, "the formant filter changes the sound");
    }
};

CombFormantTests combFormantTests;

} // namespace
