/*
    Validation of the assembled instrument.

    HOW THIS DIFFERS FROM EVERY OTHER TEST FILE. The DSP tests measure modules:
    the filter's cutoff is where it was asked for, the oversampler's halfband
    rejects its stopband, the wavetable's aliasing floor at each octave. All of
    that is necessary and none of it is this. **Phase 10 measures the
    instrument** — a MIDI note in, audio out, through the voice engine, the
    filters, the amplifier and whatever is in the rack — because a synthesiser
    can be built entirely from correct parts and still be out of tune, offset
    from zero, noisy at rest, or capable of emitting an infinity.

    THESE ARE MEASUREMENTS, NOT OPINIONS. Where a figure is a property of the
    design rather than a pass or a fail, it is logged as well as bounded, and
    the bound is set where a regression would matter rather than where today's
    build happens to sit. PROJECT-STATE §5c records what they came out at.

    THE INSTRUMENT IS PUT AT ITS DEFAULTS unless a test says otherwise, because
    that is the state every other claim in the project is made about, and it is
    what an empty project loads.
*/

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "Analysis/SignalAnalysis.h"
#include "Audio/ApolloAudioProcessor.h"
#include "Parameters/ParameterDefinitions.h"
#include "Parameters/ParameterLayout.h"

using namespace apollo;

namespace
{

constexpr int blockSize = 512;
constexpr int transformSize = 16384;

/** Sets a parameter by its plain value, the way a control or a host would. */
void setPlain (ApolloAudioProcessor& processor, const juce::String& id, float plain)
{
    auto* parameter = processor.getValueTreeState().getParameter (id);
    jassert (parameter != nullptr);

    if (parameter != nullptr)
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
}

/** Puts the instrument into a state that produces one clean tone.

    The sub oscillator alone: a pure sine, no unison, no filter, no effects. It
    is the only source in Apollo whose output is supposed to be a sine, and
    therefore the only one about which distortion and noise figures mean
    anything — a saw is all harmonics on purpose, and measuring its THD measures
    the waveform rather than the instrument.
*/
void makeSineInstrument (ApolloAudioProcessor& processor)
{
    setPlain (processor, "osc1_level", 0.0f);
    setPlain (processor, "osc2_level", 0.0f);
    setPlain (processor, "noise_level", 0.0f);
    setPlain (processor, "sub_level", 1.0f);
    setPlain (processor, "sub_octave", -1.0f);

    // Off rather than wide open: a filter at 20 kHz is still a filter, and its
    // phase response near the top would show up in a distortion figure.
    setPlain (processor, "filter1_type", 0.0f);
    setPlain (processor, "filter2_type", 0.0f);

    // A flat amplitude envelope, so what is measured is a steady tone rather
    // than a decay.
    setPlain (processor, "env1_attack", 1.0f);
    setPlain (processor, "env1_decay", 1.0f);
    setPlain (processor, "env1_sustain", 1.0f);
    setPlain (processor, "env1_release", 5.0f);
}

/** Renders @p blocks blocks, with an optional note held from the first sample. */
[[nodiscard]] std::vector<float> render (ApolloAudioProcessor& processor,
                                         int blocks,
                                         int midiNote = -1,
                                         float velocity = 0.8f,
                                         int discardBlocks = 0)
{
    juce::AudioBuffer<float> buffer (2, blockSize);

    std::vector<float> rendered;
    rendered.reserve (static_cast<std::size_t> (blocks * blockSize));

    for (int block = 0; block < blocks + discardBlocks; ++block)
    {
        buffer.clear();

        juce::MidiBuffer midi;

        if (block == 0 && midiNote >= 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, midiNote, velocity), 0);

        processor.processBlock (buffer, midi);

        if (block < discardBlocks)
            continue;

        const auto* left = buffer.getReadPointer (0);

        for (int i = 0; i < blockSize; ++i)
            rendered.push_back (left[i]);
    }

    return rendered;
}

/** @returns the frequency of MIDI note @p note in equal temperament. */
[[nodiscard]] double frequencyOfNote (int note) noexcept
{
    return 440.0 * std::pow (2.0, (static_cast<double> (note) - 69.0) / 12.0);
}

/** @returns how far @p measured is from @p expected, in cents. */
[[nodiscard]] double centsBetween (double measured, double expected) noexcept
{
    if (measured <= 0.0 || expected <= 0.0)
        return 1200.0;

    return 1200.0 * std::log2 (measured / expected);
}

class InstrumentValidationTests final : public juce::UnitTest
{
public:
    InstrumentValidationTests()
        : juce::UnitTest ("Instrument validation", "Validation")
    {
    }

    void runTest() override
    {
        testPitchAccuracyAcrossTheRange();
        testPitchIsIndependentOfSampleRate();
        testDistortionAndNoiseOfACleanTone();
        testTheInstrumentIsSilentAtRest();
        testNoPathIntroducesAnOffset();
        testAliasingThroughAWholeVoice();
        testTheReleaseReachesExactSilence();
        testAnAbruptParameterChangeDoesNotClick();
        testAFullChainDecaysToExactSilence();
        testNoParameterValueCanProduceANonFiniteSample();
        testAHostilePatchStaysBoundedForAMinute();
    }

private:
    //==========================================================================
    // Tuning.

    void testPitchAccuracyAcrossTheRange()
    {
        beginTest ("Every note is in tune, across the instrument's range");

        ApolloAudioProcessor processor;
        makeSineInstrument (processor);
        processor.prepareToPlay (48000.0, blockSize);

        auto worstCents = 0.0;
        auto worstNote = 0;

        // Two octaves below middle C to three above, which is every note anyone
        // plays and a little either side.
        for (int note = 24; note <= 96; note += 3)
        {
            const auto rendered = render (processor, transformSize / blockSize + 1, note,
                                          0.8f, /* discard */ 4);

            const auto spectrum = analysis::analyse (rendered, transformSize, 48000.0);

            // The sub is an octave below the note played, which is what makes
            // this a test of the whole pitch path — the note, the transposition
            // and the oscillator — rather than of the oscillator alone.
            const auto expected = frequencyOfNote (note) * 0.5;
            const auto measured = spectrum.peakFrequency();
            const auto cents = centsBetween (measured, expected);

            if (std::abs (cents) > std::abs (worstCents))
            {
                worstCents = cents;
                worstNote = note;
            }

            expect (std::abs (cents) < 1.0,
                    "note " + juce::String (note) + " measured "
                        + juce::String (measured, 2) + " Hz against "
                        + juce::String (expected, 2) + " Hz, " + juce::String (cents, 2)
                        + " cents out");

            processor.reset();
        }

        logMessage ("  worst tuning error " + juce::String (worstCents, 3)
                    + " cents, at note " + juce::String (worstNote));
    }

    void testPitchIsIndependentOfSampleRate()
    {
        beginTest ("A note is the same pitch at every sample rate");

        for (const auto sampleRate : { 44100.0, 48000.0, 88200.0, 96000.0 })
        {
            ApolloAudioProcessor processor;
            makeSineInstrument (processor);
            processor.prepareToPlay (sampleRate, blockSize);

            const auto rendered = render (processor, transformSize / blockSize + 1, 69,
                                          0.8f, /* discard */ 4);

            const auto spectrum = analysis::analyse (rendered, transformSize, sampleRate);

            // A4 played, so the sub sounds A3.
            const auto cents = centsBetween (spectrum.peakFrequency(), 220.0);

            expect (std::abs (cents) < 1.0,
                    juce::String (sampleRate, 0) + " Hz: A3 came out "
                        + juce::String (cents, 2) + " cents out");
        }
    }

    //==========================================================================
    // Fidelity.

    void testDistortionAndNoiseOfACleanTone()
    {
        beginTest ("A tone that should be a sine is one");

        ApolloAudioProcessor processor;
        makeSineInstrument (processor);
        processor.prepareToPlay (48000.0, blockSize);

        const auto rendered = render (processor, transformSize / blockSize + 1, 69,
                                      0.8f, /* discard */ 8);

        const auto distortion = analysis::totalHarmonicDistortionPlusNoise (
            rendered, 220.0, 48000.0, transformSize);

        const auto percent = distortion * 100.0;

        logMessage ("  THD+N of the sub oscillator " + juce::String (percent, 4) + " %  ("
                    + juce::String (analysis::toDecibels (distortion), 1) + " dB)");

        // A tenth of a per cent. The sub is a single-harmonic wavetable read
        // with cubic interpolation, so what is left is interpolation error and
        // the amplitude envelope's smoothing — both of which should be far
        // below anything audible. A regression here means something in the
        // voice path has started adding harmonics it did not add before.
        expect (percent < 0.1,
                "the sub oscillator measured " + juce::String (percent, 4) + " % THD+N");
    }

    void testTheInstrumentIsSilentAtRest()
    {
        beginTest ("An instrument playing nothing produces exact silence");

        ApolloAudioProcessor processor;
        processor.prepareToPlay (48000.0, blockSize);

        const auto rendered = render (processor, 40);

        // Not "quiet": zero. A synthesiser with no note sounding has nothing to
        // produce, and a noise floor at rest would be a bug rather than a
        // property — there is no analogue circuit here to hiss.
        expectEquals (analysis::peakOf (rendered), 0.0f,
                      "silence should be exactly silent");

        // And with every source turned up, which is where a stuck oscillator or
        // an unclosed envelope would show.
        setPlain (processor, "osc1_level", 1.0f);
        setPlain (processor, "osc2_level", 1.0f);
        setPlain (processor, "sub_level", 1.0f);
        setPlain (processor, "noise_level", 1.0f);

        processor.reset();

        expectEquals (analysis::peakOf (render (processor, 40)), 0.0f,
                      "sources at full level must still be silent with no note");
    }

    void testNoPathIntroducesAnOffset()
    {
        beginTest ("A held note sits on zero rather than beside it");

        ApolloAudioProcessor processor;
        processor.prepareToPlay (48000.0, blockSize);

        // Every source at once, which is the case where an offset in any one of
        // them would show. The pulse wavetable is the interesting one: its
        // Fourier series carries a DC term proportional to its duty cycle, and
        // the table drops it deliberately (ADR-0066).
        setPlain (processor, "osc1_level", 0.8f);
        setPlain (processor, "osc1_wavetable", 1.0f);
        setPlain (processor, "osc1_position", 0.9f);
        setPlain (processor, "osc2_level", 0.6f);
        setPlain (processor, "osc2_wavetable", 3.0f);
        setPlain (processor, "sub_level", 0.7f);
        setPlain (processor, "noise_level", 0.2f);
        setPlain (processor, "env1_sustain", 1.0f);

        const auto rendered = render (processor, 60, 57, 0.9f, /* discard */ 8);

        const auto offset = analysis::dcOffsetOf (rendered);

        logMessage ("  DC offset with every source at once "
                    + juce::String (analysis::toDecibels (offset), 1) + " dBFS");

        // A thousandth of full scale. Anything larger costs headroom, moves the
        // zero crossing a downstream effect sees, and is inaudible right up
        // until something clips.
        expect (std::abs (offset) < 1.0e-3,
                "the output sits at " + juce::String (offset, 6) + " rather than at zero");
    }

    void testAliasingThroughAWholeVoice()
    {
        beginTest ("A bright note high in the range does not alias through the voice");

        ApolloAudioProcessor processor;
        processor.prepareToPlay (48000.0, blockSize);

        // The brightest thing the instrument can do, played high: a fully open
        // swept saw with unison, which is where aliasing is loudest. The
        // oscillator's own floors are measured in the wavetable tests; this is
        // the same claim made about the assembled voice, with unison detuning,
        // the filter and the amplifier in the path.
        setPlain (processor, "osc1_level", 1.0f);
        setPlain (processor, "osc1_wavetable", 0.0f);
        setPlain (processor, "osc1_position", 1.0f);
        setPlain (processor, "osc1_unison", 7.0f);
        setPlain (processor, "osc1_detune", 0.15f);
        setPlain (processor, "osc2_level", 0.0f);
        setPlain (processor, "filter1_type", 0.0f);
        setPlain (processor, "env1_sustain", 1.0f);
        setPlain (processor, "env1_attack", 1.0f);

        // A4, two octaves up: 1760 Hz, whose thirteenth harmonic is already
        // past Nyquist, so everything above that has to have been removed
        // rather than folded.
        const auto note = 93;
        const auto fundamental = frequencyOfNote (note);

        const auto rendered = render (processor, transformSize / blockSize + 1, note,
                                      0.9f, /* discard */ 8);

        const auto spectrum = analysis::analyse (rendered, transformSize, 48000.0);

        // Wide enough to cover seven detuned unison voices spread around each
        // harmonic, which are not aliases.
        const auto worst = analysis::worstInharmonicLevel (spectrum, fundamental, 0.08);

        logMessage ("  worst inharmonic content at " + juce::String (fundamental, 0)
                    + " Hz: " + juce::String (worst, 1) + " dBc");

        expect (worst < -60.0f,
                "aliasing reached " + juce::String (worst, 1) + " dBc");
    }

    void testTheReleaseReachesExactSilence()
    {
        beginTest ("A released note stops rather than fading for ever");

        ApolloAudioProcessor processor;
        processor.prepareToPlay (48000.0, blockSize);

        setPlain (processor, "env1_release", 50.0f);

        juce::AudioBuffer<float> buffer (2, blockSize);

        {
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);

            buffer.clear();
            processor.processBlock (buffer, midi);
        }

        {
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);

            buffer.clear();
            processor.processBlock (buffer, midi);
        }

        // Two seconds after a 50 ms release. An envelope that approached zero
        // asymptotically would still be producing denormal-range values here,
        // which is both a performance cliff and a voice that never frees
        // itself (CLAUDE.md §37).
        auto tail = std::vector<float> {};

        for (int block = 0; block < 180; ++block)
        {
            buffer.clear();

            juce::MidiBuffer empty;
            processor.processBlock (buffer, empty);

            if (block < 20)
                continue;

            const auto* left = buffer.getReadPointer (0);

            for (int i = 0; i < blockSize; ++i)
                tail.push_back (left[i]);
        }

        expectEquals (analysis::peakOf (tail), 0.0f,
                      "the tail should be exactly zero, not merely small");
    }

    //==========================================================================
    // Transient behaviour.

    /** The step response, in the form it takes in a synthesiser.

        A synthesiser has no audio input, so "feed it a step" means the thing a
        user actually does: move a control while a note is sounding. The
        question is the same one a step response answers — does the system reach
        its new state without producing a discontinuity on the way.

        Smoothing is what makes this pass (CLAUDE.md §36), and this is the test
        that would notice if a smoothed parameter stopped being smoothed.
    */
    void testAnAbruptParameterChangeDoesNotClick()
    {
        beginTest ("A control moved as far and as fast as possible does not click");

        ApolloAudioProcessor processor;
        processor.prepareToPlay (48000.0, blockSize);

        setPlain (processor, "osc1_level", 0.9f);
        setPlain (processor, "env1_attack", 1.0f);
        setPlain (processor, "env1_sustain", 1.0f);
        setPlain (processor, "filter1_type", 1.0f);
        setPlain (processor, "filter1_cutoff", 20000.0f);

        juce::AudioBuffer<float> buffer (2, blockSize);

        const auto renderOne = [&processor, &buffer] (bool noteOn)
        {
            buffer.clear();

            juce::MidiBuffer midi;

            if (noteOn)
                midi.addEvent (juce::MidiMessage::noteOn (1, 57, 0.9f), 0);

            processor.processBlock (buffer, midi);

            std::vector<float> block (static_cast<std::size_t> (blockSize));

            const auto* left = buffer.getReadPointer (0);

            for (int i = 0; i < blockSize; ++i)
                block[static_cast<std::size_t> (i)] = left[i];

            return block;
        };

        // The largest jump between one sample and the next.
        const auto largestStep = [] (const std::vector<float>& block)
        {
            auto largest = 0.0f;

            for (std::size_t i = 1; i < block.size(); ++i)
                largest = std::max (largest, std::abs (block[i] - block[i - 1]));

            return largest;
        };

        (void) renderOne (true);

        // Twenty blocks of steady tone to establish what a normal
        // sample-to-sample step looks like for this waveform at this pitch.
        auto steady = 0.0f;

        for (int block = 0; block < 20; ++block)
            steady = std::max (steady, largestStep (renderOne (false)));

        // The whole range of the control, in one go, between two blocks — which
        // is faster than any hand and faster than any automation curve.
        setPlain (processor, "filter1_cutoff", 20.0f);

        auto worstAfter = 0.0f;

        for (int block = 0; block < 20; ++block)
            worstAfter = std::max (worstAfter, largestStep (renderOne (false)));

        logMessage ("  largest sample step: " + juce::String (steady, 5) + " steady, "
                    + juce::String (worstAfter, 5) + " across the sweep");

        // Closing a filter can only ever *reduce* the rate of change of the
        // signal, so a jump larger than the steady-state one means the
        // coefficient change itself was audible rather than the sound it made.
        expect (worstAfter <= steady * 1.5f,
                "moving the cutoff produced a step of " + juce::String (worstAfter, 5)
                    + " against a steady-state " + juce::String (steady, 5));
    }

    /** Denormals, measured by their consequence rather than by their bit pattern.

        A feedback path that approaches zero asymptotically ends up multiplying
        denormal values for ever: on most processors that is a silent
        performance cliff, and in a plugin it is a voice that never frees itself
        (CLAUDE.md §37). Each effect asserts this for its own tail; this is the
        claim about the assembled chain, where a delay feeding a reverb feeding
        the output is exactly the arrangement that keeps a tiny value alive.

        Exact zero rather than "small": small is what a denormal is.
    */
    void testAFullChainDecaysToExactSilence()
    {
        beginTest ("A note through a full chain ends, rather than decaying for ever");

        ApolloAudioProcessor processor;
        processor.prepareToPlay (48000.0, blockSize);

        setPlain (processor, "osc1_level", 0.9f);
        setPlain (processor, "env1_release", 100.0f);

        // The two effects with feedback in them, at ordinary settings — the
        // point is the arrangement, not the extremes, which the hostile-patch
        // test covers.
        //
        // THE TIMES ARE CHOSEN BY ARITHMETIC, NOT BY TASTE, so that the window
        // below is justified rather than picked until it passed. Both effects
        // flush anything below 1e-18 to zero, and how long that takes is a
        // property of their settings:
        //
        //   the delay repeats every 120 ms losing 40 % each time, so it needs
        //   log(1e-18) / log(0.6) ≈ 81 repeats — about 10 seconds;
        //
        //   the reverb falls 60 dB in 800 ms, and 1e-18 is 360 dB down, so it
        //   needs about twelve of those — again about 10 seconds.
        //
        // Thirty seconds is therefore three times what either needs. The first
        // draft of this test used the default half-second delay, which wants
        // eighty-one *half-seconds* — forty seconds — and reported a defect
        // that was only ever an arithmetic mistake in the test.
        setPlain (processor, "fx_slot1", 2.0f);
        setPlain (processor, "fx_delay_time", 120.0f);
        setPlain (processor, "fx_delay_feedback", 0.6f);
        setPlain (processor, "fx_delay_mix", 0.5f);
        setPlain (processor, "fx_slot2", 3.0f);
        setPlain (processor, "fx_reverb_decay", 800.0f);
        setPlain (processor, "fx_reverb_mix", 0.5f);

        juce::AudioBuffer<float> buffer (2, blockSize);

        {
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 48, 1.0f), 0);

            buffer.clear();
            processor.processBlock (buffer, midi);
        }

        for (int block = 0; block < 40; ++block)
        {
            buffer.clear();

            juce::MidiBuffer empty;
            processor.processBlock (buffer, empty);
        }

        {
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOff (1, 48), 0);

            buffer.clear();
            processor.processBlock (buffer, midi);
        }

        // Thirty seconds, which is many times the decay of anything in the
        // chain. If it is still producing values at the end, they are not
        // music.
        constexpr int blocks = static_cast<int> (30.0 * 48000.0 / blockSize);

        auto lastHeard = -1;

        for (int block = 0; block < blocks; ++block)
        {
            buffer.clear();

            juce::MidiBuffer empty;
            processor.processBlock (buffer, empty);

            for (int channel = 0; channel < 2; ++channel)
            {
                const auto* data = buffer.getReadPointer (channel);

                for (int i = 0; i < blockSize; ++i)
                    if (data[i] != 0.0f)
                        lastHeard = block;
            }
        }

        const auto seconds = static_cast<double> (lastHeard + 1) * static_cast<double> (blockSize)
                           / 48000.0;

        logMessage ("  the chain fell to exact silence "
                    + juce::String (seconds, 2) + " s after the note was released");

        expect (lastHeard < blocks - 1,
                "the chain was still producing samples thirty seconds after release");
    }

    //==========================================================================
    // Robustness.

    void testNoParameterValueCanProduceANonFiniteSample()
    {
        beginTest ("No value any parameter accepts can make the instrument misbehave");

        ApolloAudioProcessor processor;
        processor.prepareToPlay (48000.0, blockSize);

        // Every parameter driven to each end of its range and to its middle, one
        // at a time, with a note held. The bridge refuses a value outside [0, 1]
        // and the registry clamps to the parameter's own range, so these are all
        // values a host or a preset can legitimately set.
        for (const auto& definition : params::parameterDefinitions)
        {
            const auto id = params::toJuceString (definition.id);

            auto* parameter = processor.getValueTreeState().getParameter (id);

            if (parameter == nullptr)
                continue;

            const auto original = parameter->getValue();

            for (const auto normalised : { 0.0f, 0.5f, 1.0f })
            {
                parameter->setValueNotifyingHost (normalised);
                processor.reset();

                const auto rendered = render (processor, 8, 60, 0.9f);

                expect (analysis::allFinite (rendered),
                        id + " at " + juce::String (normalised, 1)
                            + " produced a sample that is not a number");

                // Bounded as well as finite. A parameter that makes the
                // instrument a hundred times too loud is not a crash, but it is
                // not something a user can recover from either.
                const auto peak = analysis::peakOf (rendered);

                expect (peak < 16.0f,
                        id + " at " + juce::String (normalised, 1) + " peaked at "
                            + juce::String (peak, 2));
            }

            parameter->setValueNotifyingHost (original);
        }

        processor.reset();
    }

    void testAHostilePatchStaysBoundedForAMinute()
    {
        beginTest ("The worst patch the instrument allows stays finite and bounded");

        ApolloAudioProcessor processor;
        processor.prepareToPlay (48000.0, blockSize);

        // Everything at once, everything at its limit: every source at full,
        // maximum unison and detune, both filters resonant, and the whole rack
        // in circuit with the longest tails and the most feedback it has.
        setPlain (processor, "osc1_level", 1.0f);
        setPlain (processor, "osc1_unison", 16.0f);
        setPlain (processor, "osc1_detune", 1.0f);
        setPlain (processor, "osc1_position", 1.0f);
        setPlain (processor, "osc2_level", 1.0f);
        setPlain (processor, "osc2_unison", 16.0f);
        setPlain (processor, "osc2_detune", 1.0f);
        setPlain (processor, "sub_level", 1.0f);
        setPlain (processor, "noise_level", 1.0f);

        setPlain (processor, "filter1_type", 1.0f);
        setPlain (processor, "filter1_resonance", 10.0f);
        setPlain (processor, "filter1_drive", 1.0f);
        setPlain (processor, "filter2_type", 3.0f);
        setPlain (processor, "filter2_resonance", 10.0f);
        setPlain (processor, "filter2_drive", 1.0f);

        setPlain (processor, "fx_slot1", 1.0f);
        setPlain (processor, "fx_distortion_drive", 36.0f);
        setPlain (processor, "fx_distortion_mix", 1.0f);
        setPlain (processor, "fx_slot2", 2.0f);
        setPlain (processor, "fx_delay_feedback", 1.0f);
        setPlain (processor, "fx_delay_mix", 1.0f);
        setPlain (processor, "fx_slot3", 3.0f);
        setPlain (processor, "fx_reverb_decay", 20000.0f);
        setPlain (processor, "fx_reverb_mix", 1.0f);
        setPlain (processor, "fx_slot4", 5.0f);
        setPlain (processor, "fx_compressor_makeup", 24.0f);
        setPlain (processor, "fx_slot5", 6.0f);

        for (int band = 1; band <= 7; ++band)
            setPlain (processor, "fx_eq_band" + juce::String (band) + "_gain", 18.0f);

        setPlain (processor, "fx_eq_level", 18.0f);
        setPlain (processor, "master_gain", 6.0f);

        juce::AudioBuffer<float> buffer (2, blockSize);

        auto everyBlockFinite = true;

        // A minute of held chord: eight notes at once, so the voice pool is
        // full and the effects are being fed everything the instrument has.
        constexpr auto blocksPerSecond = 48000.0 / static_cast<double> (blockSize);
        constexpr int blocks = static_cast<int> (60.0 * blocksPerSecond);

        // Peak per ten-second window. **This is the measurement that matters**,
        // and the absolute level is not.
        //
        // This patch asks for roughly +88 dB of deliberate gain — seven EQ
        // bells at +18, the EQ trim at +18, 24 dB of makeup and the master at
        // its +6 — on top of four sources at full into a 36 dB distortion. A
        // number in the hundreds is the instrument doing what it was told, and
        // an assertion about how large it may be would be an assertion about
        // the range of the gain controls.
        //
        // What must be true is that it does not *compound*: maximum delay
        // feedback, a twenty-second reverb and two resonant filters in a chain
        // are the ingredients of something that grows without bound, and a
        // patch that is louder in its last ten seconds than in its first has
        // one of them running away.
        std::vector<float> windowPeaks;
        auto windowPeak = 0.0f;

        for (int block = 0; block < blocks; ++block)
        {
            buffer.clear();

            juce::MidiBuffer midi;

            if (block == 0)
                for (int note = 48; note < 56; ++note)
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), 0);

            processor.processBlock (buffer, midi);

            for (int channel = 0; channel < 2; ++channel)
            {
                const auto* data = buffer.getReadPointer (channel);

                for (int i = 0; i < blockSize; ++i)
                {
                    if (! std::isfinite (data[i]))
                        everyBlockFinite = false;

                    windowPeak = std::max (windowPeak, std::abs (data[i]));
                }
            }

            if (! everyBlockFinite)
                break;

            if ((block + 1) % static_cast<int> (10.0 * blocksPerSecond) == 0)
            {
                windowPeaks.push_back (windowPeak);
                windowPeak = 0.0f;
            }
        }

        expect (everyBlockFinite, "a minute at the limit produced a sample that is not a number");

        expect (windowPeaks.size() >= 6, "the render should have produced six windows");

        if (windowPeaks.size() < 6)
            return;

        juce::String trace;

        for (const auto peak : windowPeaks)
            trace += juce::String (analysis::toDecibels (peak), 1) + " ";

        logMessage ("  hostile patch, peak per ten seconds (dBFS): " + trace.trim());

        // Compared from the third window onwards: a twenty-second reverb is
        // still filling before that, so the first two are a sound arriving
        // rather than a sound running away.
        const auto settled = windowPeaks[2];
        const auto last = windowPeaks.back();

        expect (last <= settled * 1.2f,
                "the patch grew from " + juce::String (analysis::toDecibels (settled), 1)
                    + " dBFS to " + juce::String (analysis::toDecibels (last), 1)
                    + " dBFS over the last thirty seconds, which is something compounding");

        // And a ceiling loose enough to be about a runaway rather than about
        // the gain controls: four orders of magnitude past full scale is not a
        // patch, it is feedback.
        expect (analysis::peakOf (windowPeaks) < 10000.0f,
                "the hostile patch reached " + juce::String (analysis::peakOf (windowPeaks), 1));
    }
};

InstrumentValidationTests instrumentValidationTests;

} // namespace
