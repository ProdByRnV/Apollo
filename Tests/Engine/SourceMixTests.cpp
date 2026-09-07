/*
    Source-section tests.

    Phase 4b turns a voice from one oscillator into four sources with a mixer.
    These cover that mixer as the engine actually presents it: that the default
    patch is unchanged, that each source can be heard and silenced independently,
    that tuning and octave controls land on the pitches they claim, that the
    stereo controls do what their names say, and that the combinations Apollo
    deliberately does *not* guarantee headroom for still stay finite.
*/

#include <juce_core/juce_core.h>

#include <cmath>
#include <limits>
#include <vector>

#include "Engine/VoiceEngine.h"

using namespace apollo::engine;

namespace
{

constexpr double testSampleRate = 48000.0;

/** A preallocated stereo render target with the pointer array the engine
    expects.
*/
class RenderBuffer
{
public:
    RenderBuffer (int numChannels, int numSamples)
        : channels (static_cast<std::size_t> (numChannels),
                    std::vector<float> (static_cast<std::size_t> (numSamples), 0.0f))
    {
        for (auto& channel : channels)
            pointers.push_back (channel.data());
    }

    [[nodiscard]] float* const* data() noexcept { return pointers.data(); }
    [[nodiscard]] int getNumChannels() const noexcept { return static_cast<int> (channels.size()); }
    [[nodiscard]] int getNumSamples() const noexcept { return static_cast<int> (channels[0].size()); }

    [[nodiscard]] const std::vector<float>& channel (int index) const noexcept
    {
        return channels[static_cast<std::size_t> (index)];
    }

    [[nodiscard]] float peak() const noexcept
    {
        float highest = 0.0f;

        for (const auto& c : channels)
            for (const auto sample : c)
                highest = std::max (highest, std::abs (sample));

        return highest;
    }

    /** Root mean square across every channel.

        Preferred over the peak whenever a test is asking how many independent
        sources a signal contains. The peak of a sum of independent sources is a
        tail statistic and swings by tens of percent between runs; the RMS is an
        average and lands within a percent of sqrt(N) every time.
    */
    [[nodiscard]] double rms() const noexcept
    {
        double total = 0.0;
        std::size_t count = 0;

        for (const auto& c : channels)
        {
            for (const auto sample : c)
                total += static_cast<double> (sample) * static_cast<double> (sample);

            count += c.size();
        }

        return count > 0 ? std::sqrt (total / static_cast<double> (count)) : 0.0;
    }

    [[nodiscard]] float peakOfChannel (int index) const noexcept
    {
        float highest = 0.0f;

        for (const auto sample : channel (index))
            highest = std::max (highest, std::abs (sample));

        return highest;
    }

    [[nodiscard]] bool isFinite() const noexcept
    {
        for (const auto& c : channels)
            for (const auto sample : c)
                if (! std::isfinite (sample))
                    return false;

        return true;
    }

    [[nodiscard]] float largestSampleStep() const noexcept
    {
        float largest = 0.0f;

        for (const auto& c : channels)
            for (std::size_t i = 1; i < c.size(); ++i)
                largest = std::max (largest, std::abs (c[i] - c[i - 1]));

        return largest;
    }

private:
    std::vector<std::vector<float>> channels;
    std::vector<float*> pointers;
};

/** @returns the frequency of a rendered tone, estimated from upward zero
    crossings.

    Deliberately the same simple estimator the voice-engine tests use: it is
    accurate to well under a semitone, which is all these tests need, and it has
    no windowing or resolution subtleties to reason about.
*/
[[nodiscard]] double estimateFrequency (const std::vector<float>& samples, double sampleRate)
{
    int crossings = 0;
    int firstCrossing = -1;
    int lastCrossing = -1;

    for (std::size_t i = 1; i < samples.size(); ++i)
    {
        if (samples[i - 1] <= 0.0f && samples[i] > 0.0f)
        {
            if (firstCrossing < 0)
                firstCrossing = static_cast<int> (i);

            lastCrossing = static_cast<int> (i);
            ++crossings;
        }
    }

    if (crossings < 2 || firstCrossing < 0 || lastCrossing <= firstCrossing)
        return 0.0;

    const auto spanSamples = static_cast<double> (lastCrossing - firstCrossing);
    return static_cast<double> (crossings - 1) * sampleRate / spanSamples;
}

class SourceMixTests final : public juce::UnitTest
{
public:
    SourceMixTests()
        : juce::UnitTest ("Source section", "Engine")
    {
    }

    void runTest() override
    {
        testDefaultPatchIsOscillatorOneAlone();
        testSourcesAreIndependentlyAudible();
        testOscillatorTwoTuningTracksItsControls();
        testSubOscillatorPlaysBelowTheNote();
        testPanMovesSignalBetweenChannels();
        testUnisonWidensAndStaysStable();
        testNoiseIsStereoAndVoiceIndependent();
        testLevelChangesAreSmoothed();
        testWavetableSelectionStillWorksThroughTheShortcuts();
        testStackedSourcesStayFinite();
        testExtremeSourceParametersStayFinite();
    }

private:
    static void prepareEngine (VoiceEngine& engine, int polyphony = VoiceEngine::defaultPolyphony)
    {
        engine.prepare (testSampleRate);
        engine.setPolyphony (polyphony);
    }

    static void render (VoiceEngine& engine, RenderBuffer& buffer)
    {
        engine.render (buffer.data(), buffer.getNumChannels(), 0, buffer.getNumSamples());
    }

    void testDefaultPatchIsOscillatorOneAlone()
    {
        beginTest ("The default patch is oscillator 1 alone, centred and mono");

        const SourceParameters parameters;

        expectEquals (parameters.osc1.level, 1.0f, "oscillator 1 must sound by default");
        expectEquals (parameters.osc2.level, 0.0f, "oscillator 2 must be silent by default");
        expectEquals (parameters.subLevel, 0.0f, "the sub must be silent by default");
        expectEquals (parameters.noiseLevel, 0.0f, "noise must be silent by default");
        expectEquals (parameters.osc1.unisonVoices, 1, "unison must be off by default");

        VoiceEngine engine;
        prepareEngine (engine);
        engine.noteOn (69, 1.0f);

        RenderBuffer buffer (2, 4800);
        render (engine, buffer);

        expect (buffer.peak() > 0.05f, "the default patch must produce a signal");

        logMessage ("default patch, one note at full velocity: peak "
                    + juce::String (buffer.peak(), 4));

        // Sample-exact equality between the channels. Adding a stereo source
        // section must not invent width where the patch asks for none — and it
        // is also the property that keeps the Phase 3 headroom measurement valid,
        // because a centred single voice still reaches both channels at unity.
        for (std::size_t i = 0; i < buffer.channel (0).size(); ++i)
            expectEquals (buffer.channel (0)[i], buffer.channel (1)[i]);
    }

    void testSourcesAreIndependentlyAudible()
    {
        beginTest ("Each source can be heard and silenced on its own");

        struct Case
        {
            const char* name;
            SourceParameters parameters;
        };

        SourceParameters osc2Only;
        osc2Only.osc1.level = 0.0f;
        osc2Only.osc2.level = 1.0f;

        SourceParameters subOnly;
        subOnly.osc1.level = 0.0f;
        subOnly.subLevel = 1.0f;

        SourceParameters noiseOnly;
        noiseOnly.osc1.level = 0.0f;
        noiseOnly.noiseLevel = 1.0f;

        const Case cases[] {
            { "oscillator 2", osc2Only },
            { "the sub oscillator", subOnly },
            { "the noise generator", noiseOnly }
        };

        for (const auto& testCase : cases)
        {
            VoiceEngine engine;
            prepareEngine (engine);
            engine.setSourceParameters (testCase.parameters);
            engine.noteOn (60, 1.0f);

            RenderBuffer sounding (2, 4800);
            render (engine, sounding);

            expect (sounding.peak() > 0.02f,
                    juce::String (testCase.name) + " produced no signal when it was the only source");

            // And with every source silent the engine must be digitally silent,
            // not merely quiet.
            VoiceEngine silent;
            prepareEngine (silent);

            SourceParameters allOff = testCase.parameters;
            allOff.osc1.level = 0.0f;
            allOff.osc2.level = 0.0f;
            allOff.subLevel = 0.0f;
            allOff.noiseLevel = 0.0f;

            silent.setSourceParameters (allOff);
            silent.noteOn (60, 1.0f);

            RenderBuffer quiet (2, 4800);
            render (silent, quiet);

            expectEquals (quiet.peak(), 0.0f,
                          "with every source at zero the engine must be silent");
        }
    }

    void testOscillatorTwoTuningTracksItsControls()
    {
        beginTest ("Oscillator 2 transposes by its own tuning");

        struct Case { float semitones; double expectedRatio; const char* name; };

        const Case cases[] {
            { 0.0f, 1.0, "unison with oscillator 1" },
            { 12.0f, 2.0, "an octave up" },
            { -12.0f, 0.5, "an octave down" },
            { 7.0f, 1.4983, "a fifth up" }
        };

        constexpr double a440 = 440.0;

        for (const auto& testCase : cases)
        {
            SourceParameters parameters;
            parameters.osc1.level = 0.0f;
            parameters.osc2.level = 1.0f;
            parameters.osc2.tuneSemitones = testCase.semitones;

            VoiceEngine engine;
            prepareEngine (engine);
            engine.setSourceParameters (parameters);
            engine.noteOn (69, 1.0f);

            RenderBuffer buffer (1, static_cast<int> (testSampleRate));
            render (engine, buffer);

            const auto measured = estimateFrequency (buffer.channel (0), testSampleRate);
            const auto expected = a440 * testCase.expectedRatio;

            // One percent is a sixth of a semitone: tight enough that a wrong
            // interval fails, loose enough that the zero-crossing estimator's
            // own error does not.
            expect (std::abs (measured - expected) < expected * 0.01,
                    juce::String (testCase.name) + ": expected " + juce::String (expected, 1)
                        + " Hz but measured " + juce::String (measured, 1) + " Hz");
        }

        // Fine tuning is expressed in the same semitone quantity, so a hundredth
        // of a semitone is one cent. Half a semitone is large enough for the
        // estimator to resolve unambiguously.
        SourceParameters fine;
        fine.osc1.level = 0.0f;
        fine.osc2.level = 1.0f;
        fine.osc2.tuneSemitones = 0.5f;

        VoiceEngine engine;
        prepareEngine (engine);
        engine.setSourceParameters (fine);
        engine.noteOn (69, 1.0f);

        RenderBuffer buffer (1, static_cast<int> (testSampleRate));
        render (engine, buffer);

        const auto measured = estimateFrequency (buffer.channel (0), testSampleRate);
        const auto expected = a440 * std::pow (2.0, 0.5 / 12.0);

        expect (std::abs (measured - expected) < expected * 0.01,
                "fractional tuning: expected " + juce::String (expected, 1)
                    + " Hz but measured " + juce::String (measured, 1) + " Hz");
    }

    void testSubOscillatorPlaysBelowTheNote()
    {
        beginTest ("The sub oscillator plays whole octaves below the note");

        for (const int octave : { -1, -2 })
        {
            SourceParameters parameters;
            parameters.osc1.level = 0.0f;
            parameters.subLevel = 1.0f;
            parameters.subOctave = octave;

            VoiceEngine engine;
            prepareEngine (engine);
            engine.setSourceParameters (parameters);
            engine.noteOn (69, 1.0f);

            RenderBuffer buffer (1, static_cast<int> (testSampleRate));
            render (engine, buffer);

            const auto measured = estimateFrequency (buffer.channel (0), testSampleRate);
            const auto expected = 440.0 * std::pow (2.0, static_cast<double> (octave));

            expect (std::abs (measured - expected) < expected * 0.01,
                    juce::String (octave) + " octaves: expected " + juce::String (expected, 1)
                        + " Hz but measured " + juce::String (measured, 1) + " Hz");
        }
    }

    void testPanMovesSignalBetweenChannels()
    {
        beginTest ("Oscillator balance moves signal between the channels");

        const auto peaksFor = [this] (float pan)
        {
            SourceParameters parameters;
            parameters.osc1.pan = pan;

            VoiceEngine engine;
            prepareEngine (engine);
            engine.setSourceParameters (parameters);
            engine.noteOn (69, 1.0f);

            RenderBuffer buffer (2, 4800);
            render (engine, buffer);

            return std::pair { buffer.peakOfChannel (0), buffer.peakOfChannel (1) };
        };

        const auto [centreLeft, centreRight] = peaksFor (0.0f);
        const auto [hardLeftL, hardLeftR] = peaksFor (-1.0f);
        const auto [hardRightL, hardRightR] = peaksFor (1.0f);

        expectWithinAbsoluteError (centreLeft, centreRight, 1.0e-6f,
                                   "a centred oscillator must be equal on both channels");

        expectEquals (hardLeftR, 0.0f, "hard left must put nothing on the right channel");
        expectEquals (hardRightL, 0.0f, "hard right must put nothing on the left channel");

        // The near channel keeps its level rather than gaining: no balance
        // setting may push a channel above the level the gain staging assumes.
        expectWithinAbsoluteError (hardLeftL, centreLeft, 1.0e-6f,
                                   "panning must attenuate the far channel, not boost the near one");
        expectWithinAbsoluteError (hardRightR, centreRight, 1.0e-6f);
    }

    void testUnisonWidensAndStaysStable()
    {
        beginTest ("Unison widens the image and stays stable at high voice counts");

        SourceParameters mono;
        mono.osc1.unisonVoices = 1;

        SourceParameters wide;
        wide.osc1.unisonVoices = 16;
        wide.osc1.detune = 0.5f;
        wide.osc1.spread = 1.0f;

        VoiceEngine narrowEngine;
        prepareEngine (narrowEngine);
        narrowEngine.setSourceParameters (mono);
        narrowEngine.noteOn (57, 1.0f);

        RenderBuffer narrow (2, 4800);
        render (narrowEngine, narrow);

        float narrowDifference = 0.0f;

        for (std::size_t i = 0; i < narrow.channel (0).size(); ++i)
            narrowDifference = std::max (narrowDifference,
                                         std::abs (narrow.channel (0)[i] - narrow.channel (1)[i]));

        expectEquals (narrowDifference, 0.0f, "one unison voice must stay mono");

        VoiceEngine wideEngine;
        prepareEngine (wideEngine);
        wideEngine.setSourceParameters (wide);
        wideEngine.noteOn (57, 1.0f);

        RenderBuffer spread (2, 4800);
        render (wideEngine, spread);

        float wideDifference = 0.0f;

        for (std::size_t i = 0; i < spread.channel (0).size(); ++i)
            wideDifference = std::max (wideDifference,
                                       std::abs (spread.channel (0)[i] - spread.channel (1)[i]));

        expect (wideDifference > 0.005f, "sixteen spread unison voices must produce a stereo image");

        // The Phase 4 exit criterion: unison remains stable at high voice
        // counts. Full polyphony, sixteen unison voices on both oscillators,
        // every source at full level — 1024 oscillators sounding at once.
        SourceParameters everything = wide;
        everything.osc2 = wide.osc1;
        everything.osc2.level = 1.0f;
        everything.osc2.tuneSemitones = 7.0f;
        everything.subLevel = 1.0f;
        everything.noiseLevel = 0.5f;

        VoiceEngine loaded;
        prepareEngine (loaded, VoiceEngine::maxPolyphony);
        loaded.setSourceParameters (everything);

        // Spaced by whole tones from a low C, which keeps all 32 notes inside
        // the MIDI range — a wider spacing would run off the end of the keyboard
        // and quietly test 31 voices instead of 32.
        for (int i = 0; i < VoiceEngine::maxPolyphony; ++i)
            loaded.noteOn (36 + i * 2, 1.0f);

        expectEquals (loaded.getActiveVoiceCount(), VoiceEngine::maxPolyphony);

        RenderBuffer heavy (2, 9600);
        render (loaded, heavy);

        expect (heavy.isFinite(),
                "the engine must stay finite at maximum polyphony and maximum unison");
        expect (heavy.peak() > 0.0f, "a fully loaded engine must still produce signal");

        logMessage ("32 voices, 16-voice unison on both oscillators, every source on: peak "
                    + juce::String (heavy.peak(), 3));
    }

    void testNoiseIsStereoAndVoiceIndependent()
    {
        beginTest ("Noise is stereo, and independent between voices");

        SourceParameters parameters;
        parameters.osc1.level = 0.0f;
        parameters.noiseLevel = 1.0f;

        VoiceEngine engine;
        prepareEngine (engine);
        engine.setSourceParameters (parameters);
        engine.noteOn (60, 1.0f);

        RenderBuffer one (2, 9600);
        render (engine, one);

        float difference = 0.0f;

        for (std::size_t i = 0; i < one.channel (0).size(); ++i)
            difference = std::max (difference,
                                   std::abs (one.channel (0)[i] - one.channel (1)[i]));

        expect (difference > 0.001f, "the noise generator must not produce a mono signal");

        // Four voices of noise must sum like four independent sources, not like
        // one source four times as loud. Measured as RMS rather than peak:
        // independent sources sum to sqrt(4) = 2 in RMS and identical ones to
        // exactly 4, and RMS separates those two cleanly, whereas the peak of a
        // sum of independent noise is a tail statistic that swings too widely
        // between runs to assert on.
        VoiceEngine chord;
        prepareEngine (chord);
        chord.setSourceParameters (parameters);

        for (const int note : { 60, 64, 67, 72 })
            chord.noteOn (note, 1.0f);

        RenderBuffer four (2, 9600);
        render (chord, four);

        const auto ratio = four.rms() / one.rms();

        expect (ratio > 1.7 && ratio < 2.4,
                "four voices of noise summed to " + juce::String (ratio, 3)
                    + " times one voice in RMS; independent sources give 2.0 and a "
                      "shared stream would give 4.0");
    }

    void testLevelChangesAreSmoothed()
    {
        beginTest ("A jump in source level ramps rather than steps");

        VoiceEngine engine;
        prepareEngine (engine);

        SourceParameters silent;
        silent.osc1.level = 0.0f;

        engine.setSourceParameters (silent);
        engine.noteOn (69, 1.0f);

        // Let the note reach its sustain, so the amplitude envelope is not the
        // thing being measured.
        RenderBuffer settle (2, 4800);
        render (engine, settle);

        SourceParameters loud;
        loud.osc1.level = 1.0f;

        engine.setSourceParameters (loud);

        RenderBuffer transition (2, 4800);
        render (engine, transition);

        expect (transition.peak() > 0.05f, "the level change must be audible");

        // A 20 ms ramp at 48 kHz moves the gain by 1/960 per sample, so the
        // largest step the signal can take is a small fraction of full scale.
        // An unsmoothed jump would produce a step the size of the waveform
        // itself.
        expect (transition.largestSampleStep() < 0.02f,
                "the level jump stepped by " + juce::String (transition.largestSampleStep(), 4)
                    + ", which is a click");
    }

    void testWavetableSelectionStillWorksThroughTheShortcuts()
    {
        beginTest ("The single-control shortcuts move only their own parameter");

        VoiceEngine engine;
        prepareEngine (engine);

        SourceParameters parameters;
        parameters.osc2.level = 0.5f;
        parameters.noiseLevel = 0.25f;
        engine.setSourceParameters (parameters);

        engine.setWavetableIndex (2);
        engine.setWavetablePosition (0.75f);

        expectEquals (engine.getWavetableIndex(), 2);
        expectWithinAbsoluteError (engine.getWavetablePosition(), 0.75f, 1.0e-6f);

        // Everything else must have survived untouched.
        expectWithinAbsoluteError (engine.getSourceParameters().osc2.level, 0.5f, 1.0e-6f);
        expectWithinAbsoluteError (engine.getSourceParameters().noiseLevel, 0.25f, 1.0e-6f);

        // Out-of-range selections are clamped, never silencing the instrument.
        engine.setWavetableIndex (-5);
        expectEquals (engine.getWavetableIndex(), 0);

        engine.setWavetableIndex (999);
        expectEquals (engine.getWavetableIndex(), 3);
    }

    void testStackedSourcesStayFinite()
    {
        beginTest ("Stacking every source stays finite, as documented");

        // Apollo does not claim full-scale headroom for this configuration —
        // see VoiceEngine::outputGain. What it does guarantee is that the result
        // is finite and bounded, so a patch built this way is loud rather than
        // corrupt.
        SourceParameters everything;
        everything.osc1.level = 1.0f;
        everything.osc1.unisonVoices = 16;
        everything.osc2.level = 1.0f;
        everything.osc2.unisonVoices = 16;
        everything.subLevel = 1.0f;
        everything.noiseLevel = 1.0f;

        // The same voicings the Phase 3 headroom test uses, because how voices
        // sum depends on their frequency relationships as much as their number:
        // one chord shape would prove nothing about the worst case.
        struct Voicing { const char* name; int firstNote; int step; };

        static constexpr Voicing voicings[] {
            { "chromatic cluster", 36, 1 },
            { "whole tones",       36, 2 },
            { "octaves",           24, 12 },
            { "fifths",            24, 7 },
            { "unison",            60, 0 }
        };

        float worstPeak = 0.0f;
        const char* worstVoicing = "";

        for (const auto& voicing : voicings)
        {
            VoiceEngine engine;
            prepareEngine (engine, VoiceEngine::maxPolyphony);
            engine.setSourceParameters (everything);

            for (int i = 0; i < VoiceEngine::maxPolyphony; ++i)
                engine.noteOn (juce::jlimit (0, 127, voicing.firstNote + i * voicing.step), 1.0f);

            // A quarter of a second, so the detuned unison voices have time to
            // drift through alignment rather than being sampled at one arbitrary
            // point in the beat.
            RenderBuffer buffer (2, static_cast<int> (testSampleRate * 0.25));
            render (engine, buffer);

            expect (buffer.isFinite(),
                    juce::String (voicing.name) + ": a fully stacked patch must stay finite");

            if (buffer.peak() > worstPeak)
            {
                worstPeak = buffer.peak();
                worstVoicing = voicing.name;
            }
        }

        logMessage ("fully stacked patch, worst of five voicings at maximum polyphony: peak "
                    + juce::String (worstPeak, 3) + " (" + juce::String (worstVoicing) + ")");

        // Measured worst case: 7.63, on the fifths voicing — about 18 dB over
        // full scale. Worth stating plainly, because the number is why Apollo
        // does not claim headroom for this configuration: pricing it in would
        // put a single default note near -40 dBFS.
        //
        // Which voicing wins is not incidental either. A single voicing measured
        // 1.8 here and would have made the overshoot look like 6 dB; fifths are
        // harmonically locked and reinforce, and only trying several finds them.
        //
        // The bound is loose enough not to trip on ordinary refactoring and
        // tight enough to catch a gain-staging regression, which would move this
        // by a factor rather than a few percent.
        expect (worstPeak < 16.0f,
                "a fully stacked patch peaked at " + juce::String (worstPeak, 2)
                    + ", well beyond the measured worst case of 7.63");
    }

    void testExtremeSourceParametersStayFinite()
    {
        beginTest ("Out-of-range source parameters are clamped, not propagated");

        constexpr auto nan = std::numeric_limits<float>::quiet_NaN();
        constexpr auto infinity = std::numeric_limits<float>::infinity();

        struct Case { const char* name; SourceParameters parameters; };

        std::vector<Case> cases;

        {
            SourceParameters p;
            p.osc1.level = nan;
            p.osc1.pan = nan;
            cases.push_back ({ "NaN level and pan", p });
        }

        {
            SourceParameters p;
            p.osc1.level = infinity;
            p.osc2.level = -infinity;
            cases.push_back ({ "infinite levels", p });
        }

        {
            SourceParameters p;
            p.osc1.pan = 12.0f;
            p.osc2.pan = -12.0f;
            p.osc2.level = 1.0f;
            cases.push_back ({ "balance beyond its range", p });
        }

        {
            SourceParameters p;
            p.osc1.unisonVoices = 10000;
            p.osc1.detune = 500.0f;
            p.osc1.spread = -7.0f;
            cases.push_back ({ "unison beyond its range", p });
        }

        {
            SourceParameters p;
            p.osc2.level = 1.0f;
            p.osc2.tuneSemitones = 1000.0f;
            p.subLevel = 1.0f;
            p.subOctave = -50;
            cases.push_back ({ "tuning beyond its range", p });
        }

        {
            SourceParameters p;
            p.osc1.wavetableIndex = -100;
            p.osc2.wavetableIndex = 100;
            p.osc2.level = 1.0f;
            cases.push_back ({ "table indices beyond the library", p });
        }

        for (const auto& testCase : cases)
        {
            VoiceEngine engine;
            prepareEngine (engine);
            engine.setSourceParameters (testCase.parameters);

            for (const int note : { 24, 60, 108 })
                engine.noteOn (note, 1.0f);

            RenderBuffer buffer (2, 4800);
            render (engine, buffer);

            expect (buffer.isFinite(),
                    juce::String (testCase.name) + " produced non-finite output");
            expect (buffer.peak() < 1000.0f,
                    juce::String (testCase.name) + " produced an absurd peak of "
                        + juce::String (buffer.peak(), 2));
        }
    }
};

SourceMixTests sourceMixTests;

} // namespace
