#include "Performance/Benchmarks.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>
#include <string>
#include <vector>

#include "Audio/ApolloAudioProcessor.h"
#include "DSP/Effects/EffectsRack.h"
#include "DSP/LFO/Lfo.h"
#include "DSP/Oscillators/UnisonOscillator.h"
#include "DSP/Oscillators/WavetableLibrary.h"
#include "DSP/Oversampling/Oversampler.h"
#include "Engine/VoiceEngine.h"
#include "Performance/Machine.h"
#include "Resources/FactoryPresets.h"
#include "State/PresetDocument.h"
#include "Telemetry/ScopeFrame.h"
#include "Telemetry/TelemetryHub.h"
#include "UI/TelemetryBridge.h"

namespace apollo::benchmarks
{

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;

/** How much audio each measurement renders.

    Four seconds is long enough that the timer's resolution and the first few
    blocks of cache warming stop mattering, and short enough that the whole
    report finishes in well under a minute.
*/
constexpr double secondsPerMeasurement = 4.0;

/** Somewhere a rendered document length can go that the optimiser cannot see
    through, so that timing the render does not time an empty loop.
*/
volatile int documentSink = 0;

/** How fast this machine is against the reference machine.

    Set once by `run()` from `assessMachine()`, which measures it carefully over
    several seconds. Above one means quicker than the reference machine, so a raw
    percentage measured here is optimistic and the normalised column divides it
    back.

    ONE SPEED FOR THE WHOLE REPORT, AND THAT WAS A CORRECTION. Taking a fresh
    reading either side of every row looks obviously better — a report is ninety
    seconds of solid work and a laptop really is slower at the end of it — and
    measurement says otherwise. Two runs compared row by row differed by 3.5 %
    when raw and **10.8 % when corrected that way**: a row is the median of five
    four-second passes and is already steady, while a tenth-of-a-second reference
    reading is not, so dividing one by the other injected more noise than the
    drift it removed. Between those two runs the machine's speed differed by
    1.5 %, so there was almost nothing to correct and everything to add.

    Normalisation is for comparing between runs and between machines, which is a
    property of a whole report rather than of a row. It is applied as one
    carefully measured factor, and the within-report thermal drift it cannot
    reach is left as a known limit rather than papered over with a noisier fix.
*/
double machineSpeed = 1.0;

/** The same, for rows whose cost is reaching memory rather than computing.

    ONE SPEED WAS NOT ENOUGH, AND THAT WAS ALSO A CORRECTION (Phase 10d-3,
    ADR-0072). Everything the comment above says about *when* to measure the
    reference still holds — once, carefully, for the whole report. What it got
    wrong was *what* to measure. A kernel walking a 32 KB table in L1 tracks the
    core clock; Apollo's voice engine reads a two-megabyte wavetable library out
    of L2 and L3, and memory latency does not throttle with the core clock.

    Dividing the second by the first therefore overshoots, and it was measured
    overshooting: across Phase 10d-2's runs the normalised heaviest-patch figure
    varied from 83 to 131 while the raw figure varied only from 84.7 to 104.5,
    so correcting made the row *less* reproducible than leaving it alone.

    Each row now declares which reference it resembles and is divided by that
    one. The two are measured the same way, at the same timescale, in the same
    opening assessment.
*/
double memoryMachineSpeed = 1.0;

/** @returns the divisor for a row of this kind. */
[[nodiscard]] double speedFor (Reference reference)
{
    return reference == Reference::memory ? memoryMachineSpeed : machineSpeed;
}

/** @returns the fraction of real time a render took.

    This is the number that matters for audio. 0.05 means the work took five
    per cent of the time it represents, so roughly twenty such loads would
    saturate one core. It is independent of block size and sample rate, unlike
    a raw duration, which is why it is reported instead of milliseconds.

    `normalisedFraction` is the same figure corrected to the reference machine's
    speed, and it is the one to compare against a figure from another run or
    another phase. `spread` is how far the passes disagreed, so a reader can see
    what the figure is worth without being told in prose (Machine.h).
*/
struct Measurement
{
    double realtimeFraction = 0.0;
    double normalisedFraction = 0.0;
    double secondsRendered = 0.0;
    double spread = 0.0;
};

/** How many passes every measurement takes.

    Five rather than three, because the reported figure is now a median and a
    spread rather than a minimum. Three passes can tell you the best of three;
    they cannot tell you whether the machine was steady.
*/
constexpr int passes = 5;

/** Renders @p seconds of audio through @p render and times it.

    The headline is the **median** pass, not the best one. The best of several
    passes was the right answer while the only question was "what does this code
    cost when nothing interrupts it"; it is the wrong one for a report that also
    has to say how much the machine moved, because the minimum of a drifting
    series hides the drift by construction.
*/
template <typename RenderBlock>
[[nodiscard]] Measurement measure (double seconds, RenderBlock&& render, Reference reference)
{
    const auto blocks = static_cast<int> (seconds * sampleRate / static_cast<double> (blockSize));

    std::vector<double> timings;
    timings.reserve (passes);

    for (int pass = 0; pass < passes; ++pass)
    {
        const auto start = std::chrono::steady_clock::now();

        for (int i = 0; i < blocks; ++i)
            render();

        const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;

        timings.push_back (elapsed.count());
    }

    const auto rendered = static_cast<double> (blocks * blockSize) / sampleRate;
    const auto statistics = statisticsOf (std::move (timings));

    const auto fraction = statistics.median / rendered;

    return { fraction, fraction / speedFor (reference), rendered, statistics.spread };
}

/** Measures two renderers *alternately* and returns the best of each.

    Use this wherever the headline is the difference between two measurements
    rather than either of them. Measuring one for twelve seconds and then the
    other for twelve more compares two different machines: a laptop warms up,
    and something else wants the core. That is not a theoretical concern here —
    measured one after the other, the visualisation overhead at eight voices came
    out *lower* than at one, and at thirty-two it came out negative, which is not
    a fact about the code.

    Alternating puts both under the same drift; taking the best of each pass
    still drops the passes something else interrupted.
*/
template <typename FirstBlock, typename SecondBlock>
[[nodiscard]] std::pair<Measurement, Measurement> measurePair (double seconds, FirstBlock&& first,
                                                               SecondBlock&& second,
                                                               Reference reference)
{
    const auto blocks = static_cast<int> (seconds * sampleRate / static_cast<double> (blockSize));
    const auto rendered = static_cast<double> (blocks * blockSize) / sampleRate;

    const auto time = [blocks] (auto&& render)
    {
        const auto start = std::chrono::steady_clock::now();

        for (int i = 0; i < blocks; ++i)
            render();

        const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
        return elapsed.count();
    };

    std::vector<double> firstTimings;
    std::vector<double> secondTimings;

    for (int pass = 0; pass < passes; ++pass)
    {
        firstTimings.push_back (time (first));
        secondTimings.push_back (time (second));
    }

    const auto describe = [rendered, reference] (std::vector<double> timings)
    {
        const auto statistics = statisticsOf (std::move (timings));
        const auto fraction = statistics.median / rendered;

        return Measurement { fraction, fraction / speedFor (reference), rendered, statistics.spread };
    };

    return { describe (std::move (firstTimings)), describe (std::move (secondTimings)) };
}

void printHeading (const char* title)
{
    const std::string heading (title);

    std::cout << "\n" << heading << "\n"
              << std::string (heading.size(), '-') << std::endl;
}

void printRow (const std::string& label, const Measurement& measurement, int voices)
{
    const auto percent = measurement.realtimeFraction * 100.0;
    const auto normalised = measurement.normalisedFraction * 100.0;

    std::cout << "  " << std::left << std::setw (34) << label
              << std::right << std::setw (8) << std::fixed << std::setprecision (3) << percent << " %"
              << std::setw (9) << normalised << " norm"
              << std::setw (7) << std::setprecision (1) << (measurement.spread * 100.0) << "% sp";

    if (voices > 0)
        std::cout << std::setw (10) << std::setprecision (4) << (percent / static_cast<double> (voices))
                  << " %/voice";

    std::cout << std::endl;
}

/** Configures the engine for the default patch: oscillator 1 alone. */
[[nodiscard]] engine::SourceParameters defaultPatch()
{
    return {};
}

/** Everything sounding, both oscillators at full unison. The heaviest patch a
    user can build without a modulation matrix.
*/
[[nodiscard]] engine::SourceParameters heaviestPatch()
{
    engine::SourceParameters parameters;

    parameters.osc1.unisonVoices = 16;
    parameters.osc1.level = 1.0f;
    parameters.osc1.detune = 0.4f;
    parameters.osc1.spread = 1.0f;

    parameters.osc2.unisonVoices = 16;
    parameters.osc2.level = 1.0f;
    parameters.osc2.detune = 0.4f;
    parameters.osc2.spread = 1.0f;

    parameters.subLevel = 1.0f;
    parameters.noiseLevel = 1.0f;

    return parameters;
}

/** Holds a note down on @p numVoices voices and measures the steady-state cost.

    Notes are spread over an octave rather than stacked on one pitch: voices at
    different pitches select different mipmap levels, which is the realistic
    case, and a single repeated note would measure an unrepresentatively
    cache-friendly one.
*/
[[nodiscard]] Measurement measureEngine (const engine::SourceParameters& parameters, int numVoices)
{
    static engine::VoiceEngine voiceEngine;

    voiceEngine.reset();
    voiceEngine.prepare (sampleRate);
    voiceEngine.setPolyphony (engine::VoiceEngine::maxPolyphony);
    voiceEngine.setSourceParameters (parameters);

    for (int i = 0; i < numVoices; ++i)
        voiceEngine.noteOn (48 + (i % 12) + 12 * (i / 12), 0.8f);

    std::vector<float> left (static_cast<std::size_t> (blockSize), 0.0f);
    std::vector<float> right (static_cast<std::size_t> (blockSize), 0.0f);

    float* channels[2] = { left.data(), right.data() };

    // Render a little first so every voice is past its attack and the
    // measurement covers steady state rather than the envelope ramp.
    for (int i = 0; i < 32; ++i)
        voiceEngine.render (channels, 2, 0, blockSize);

    // Memory: a voice engine render is 1088 oscillators reading a two-megabyte
    // wavetable library at scattered levels, frames and phases. This is the row
    // whose normalisation was wrong before Phase 10d-3 (ADR-0072).
    return measure (secondsPerMeasurement,
                    [&] { voiceEngine.render (channels, 2, 0, blockSize); },
                    Reference::memory);
}

void benchmarkPolyphony()
{
    printHeading ("Voice engine, default patch (oscillator 1 alone, no unison)");

    for (const auto voices : { 1, 2, 4, 8, 16, 32 })
        printRow (std::to_string (voices) + " voices", measureEngine (defaultPatch(), voices), voices);

    printHeading ("Voice engine, heaviest patch (2 x 16-voice unison, sub, noise)");

    for (const auto voices : { 1, 2, 4, 8, 16, 32 })
        printRow (std::to_string (voices) + " voices", measureEngine (heaviestPatch(), voices), voices);
}

/** A routing of the kind a real patch uses: a filter sweep, a vibrato, velocity
    on level, and some stereo movement.
*/
[[nodiscard]] dsp::ModulationRouting representativeRouting()
{
    dsp::ModulationRouting routing;

    routing.slots[0] = { dsp::ModSource::envelope2, dsp::ModDestination::filter1Cutoff, 0.7f };
    routing.slots[1] = { dsp::ModSource::lfo1, dsp::ModDestination::allPitch, 0.02f };
    routing.slots[2] = { dsp::ModSource::velocity, dsp::ModDestination::osc1Level, 0.3f };
    routing.slots[3] = { dsp::ModSource::lfo2, dsp::ModDestination::osc1Pan, 0.5f };

    return routing;
}

void benchmarkModulation()
{
    printHeading ("Voice engine, default patch with four modulation routings");

    // The question this answers is what the matrix costs when it is used, since
    // the unmodulated figures above already show it costs nothing when it is
    // not: a voice with no active slot skips evaluation entirely.
    for (const auto voices : { 1, 8, 32 })
    {
        static engine::VoiceEngine voiceEngine;

        voiceEngine.reset();
        voiceEngine.prepare (sampleRate);
        voiceEngine.setPolyphony (engine::VoiceEngine::maxPolyphony);
        voiceEngine.setSourceParameters (defaultPatch());

        engine::FilterParameters filters;
        filters.filter1.mode = dsp::StateVariableFilter::Mode::lowpass;
        filters.filter1.cutoffHz = 1200.0f;
        filters.filter1.q = 3.0f;
        voiceEngine.setFilterParameters (filters);

        dsp::LfoSettings lfo;
        lfo.rateHz = 5.0f;
        voiceEngine.setLfoSettings (0, lfo);
        voiceEngine.setLfoSettings (1, lfo);

        voiceEngine.setModulationRouting (representativeRouting());

        for (int i = 0; i < voices; ++i)
            voiceEngine.noteOn (48 + (i % 12) + 12 * (i / 12), 0.8f);

        std::vector<float> left (static_cast<std::size_t> (blockSize), 0.0f);
        std::vector<float> right (static_cast<std::size_t> (blockSize), 0.0f);

        float* channels[2] = { left.data(), right.data() };

        for (int i = 0; i < 32; ++i)
            voiceEngine.render (channels, 2, 0, blockSize);

        printRow (std::to_string (voices) + " voices, modulated",
                  measure (secondsPerMeasurement,
                           [&] { voiceEngine.render (channels, 2, 0, blockSize); },
                           Reference::memory),
                  voices);
    }
}

void benchmarkLfos()
{
    printHeading ("LFO, one instance at audio rate (per sample)");

    // The number that decides whether LFOs can run per sample per voice. Four
    // LFOs across 32 voices is 128 of these, so the per-instance cost is
    // multiplied by that before it is compared with the voice engine's own.
    struct Case { const char* name; dsp::LfoShape shape; float smoothing; };

    const Case cases[] = {
        { "sine", dsp::LfoShape::sine, 0.0f },
        { "triangle", dsp::LfoShape::triangle, 0.0f },
        { "square", dsp::LfoShape::square, 0.0f },
        { "sample and hold", dsp::LfoShape::sampleAndHold, 0.0f },
        { "sine, smoothed", dsp::LfoShape::sine, 0.5f },
    };

    for (const auto& testCase : cases)
    {
        static dsp::Lfo lfo;

        dsp::LfoSettings settings;
        settings.shape = testCase.shape;
        settings.rateHz = 5.0f;
        settings.smoothing = testCase.smoothing;

        lfo.prepare (sampleRate);
        lfo.setSettings (settings);
        lfo.noteOn (0.0);

        // Accumulated into a volatile so the whole loop cannot be optimised
        // away, which would make an LFO look free.
        static volatile float sink = 0.0f;

        const auto measurement = measure (secondsPerMeasurement, [&]
        {
            float total = 0.0f;

            for (int i = 0; i < blockSize; ++i)
                total += lfo.getNextValue();

            sink = total;
        }, Reference::compute);

        printRow (testCase.name, measurement, 0);

        std::cout << "      x128 (4 LFOs on 32 voices): "
                  << std::fixed << std::setprecision (2)
                  << (measurement.realtimeFraction * 100.0 * 128.0) << " %" << std::endl;
    }
}

void benchmarkOversampling()
{
    printHeading ("Oversampler, one channel (cost per stereo FX stage is twice this)");

    struct Case { const char* name; dsp::Oversampler::Factor factor; };

    const Case cases[] = {
        { "bypass (Factor::none)", dsp::Oversampler::Factor::none },
        { "2x", dsp::Oversampler::Factor::x2 },
        { "4x", dsp::Oversampler::Factor::x4 },
    };

    for (const auto& testCase : cases)
    {
        static dsp::Oversampler oversampler;
        oversampler.prepare (testCase.factor, blockSize);

        std::vector<float> input (static_cast<std::size_t> (blockSize), 0.0f);
        std::vector<float> output (static_cast<std::size_t> (blockSize), 0.0f);

        for (int i = 0; i < blockSize; ++i)
            input[static_cast<std::size_t> (i)] =
                0.5f * static_cast<float> (std::sin (0.05 * static_cast<double> (i)));

        const auto measurement = measure (secondsPerMeasurement, [&]
        {
            auto* wide = oversampler.upsample (input.data(), blockSize);

            if (wide != nullptr)
            {
                // A representative nonlinearity, so the figure reflects a real
                // oversampled stage rather than the conversion in isolation.
                const auto length = oversampler.getOversampledLength (blockSize);

                for (int i = 0; i < length; ++i)
                    wide[i] = std::tanh (wide[i] * 4.0f);

                oversampler.downsample (output.data(), blockSize);
            }
        }, Reference::compute);

        printRow (std::string (testCase.name) + ", with tanh drive", measurement, 0);

        std::cout << "      latency " << oversampler.getLatencySamples() << " samples ("
                  << std::fixed << std::setprecision (2)
                  << (static_cast<double> (oversampler.getLatencySamples()) / sampleRate * 1000.0)
                  << " ms at 48 kHz)" << std::endl;
    }
}

/** What the rack costs on the finished mix.

    Reported per stage rather than per voice, and that distinction is the whole
    argument for putting distortion here instead of in the voice: this figure is
    paid once no matter how many notes are held, where the same shaper inside a
    voice would be paid thirty-two times (ADR-0033).

    The empty rack is measured alongside, because "the rack costs nothing when
    it is empty" is a claim about the default patch that ought to be checked
    rather than asserted.
*/
void benchmarkEffects()
{
    printHeading ("Effects rack, stereo, per block");

    struct Case { const char* name; dsp::EffectType effect; dsp::Distortion::Mode mode; bool bypassed; };

    const Case cases[] = {
        { "empty rack", dsp::EffectType::none, dsp::Distortion::Mode::soft, false },
        { "distortion, bypassed", dsp::EffectType::distortion, dsp::Distortion::Mode::soft, true },
        { "distortion, soft (tanh at 4x)", dsp::EffectType::distortion, dsp::Distortion::Mode::soft, false },
        { "distortion, hard (clip at 4x)", dsp::EffectType::distortion, dsp::Distortion::Mode::hard, false },
        { "distortion, diode (exp at 4x)", dsp::EffectType::distortion, dsp::Distortion::Mode::diode, false },
        { "delay, bypassed", dsp::EffectType::delay, dsp::Distortion::Mode::soft, true },
        { "delay, stereo with feedback", dsp::EffectType::delay, dsp::Distortion::Mode::soft, false },
        { "reverb, bypassed", dsp::EffectType::reverb, dsp::Distortion::Mode::soft, true },
        { "reverb, 8-line FDN", dsp::EffectType::reverb, dsp::Distortion::Mode::soft, false },
        { "gate, stereo-linked peak", dsp::EffectType::gate, dsp::Distortion::Mode::soft, false },
        { "compressor, stereo-linked RMS", dsp::EffectType::compressor, dsp::Distortion::Mode::soft, false },
        { "equaliser, all bands transparent", dsp::EffectType::equaliser, dsp::Distortion::Mode::soft, false },
        { "equaliser, 7 bands x1", dsp::EffectType::equaliser, dsp::Distortion::Mode::soft, false },
        { "equaliser, 7 bands x4", dsp::EffectType::equaliser, dsp::Distortion::Mode::soft, false },
    };

    // The three equaliser rows are the same effect with different settings
    // rather than different effects, so they are told apart by position: the
    // first leaves every band at its transparent default, and the other two dial
    // all seven in at one instance and at four.
    //
    // The first is the row that matters most — a transparent band is skipped
    // rather than multiplied through, so an equaliser sitting in the chain doing
    // nothing should cost almost nothing, and this is where that is checked
    // rather than claimed.
    //
    // The other two are worth having together because the difference between
    // them is not the one you would guess. Four times the sections is nowhere
    // near four times the cost: measured at every order in between, seven bands
    // cost 0.35 % of a core at one instance and 0.48 % at four. A biquad's state
    // update depends on the previous sample's, so one section per band leaves
    // the processor waiting on that recurrence with most of its execution units
    // idle; the extra sections fill those slots rather than queueing behind
    // them. Raising the slope is close to free, and that is a measurement rather
    // than a claim about the arithmetic.
    auto equaliserCase = 0;

    for (const auto& testCase : cases)
    {
        static dsp::EffectsRack rack;
        rack.prepare (sampleRate, blockSize);

        dsp::EffectsRack::Chain chain;
        chain[0].effect = testCase.effect;
        chain[0].bypassed = testCase.bypassed;
        rack.setChain (chain);

        dsp::Distortion::Settings settings;
        settings.mode = testCase.mode;
        settings.driveDb = 18.0f;
        settings.mix = 1.0f;
        rack.distortion().setSettings (settings);

        dsp::Delay::Settings delay;
        delay.timeMs = 350.0f;
        delay.feedback = 0.5f;
        delay.dampingHz = 6000.0f;
        delay.lowCutHz = 120.0f;
        delay.mix = 0.5f;
        rack.delay().setSettings (delay);

        dsp::Reverb::Settings reverb;
        reverb.decaySeconds = 3.0f;
        reverb.dampingHz = 6000.0f;
        reverb.mix = 0.5f;
        rack.reverb().setSettings (reverb);

        dsp::NoiseGate::Settings gate;
        gate.thresholdDb = -30.0f;
        rack.gate().setSettings (gate);

        dsp::Compressor::Settings compressor;
        compressor.thresholdDb = -24.0f;
        compressor.ratio = 4.0f;
        rack.compressor().setSettings (compressor);

        if (testCase.effect == dsp::EffectType::equaliser)
        {
            auto equaliser = dsp::Equaliser::defaultSettings();

            if (equaliserCase > 0)
            {
                const auto order = equaliserCase == 1 ? 1 : dsp::EqualiserBand::maxOrder;

                for (std::size_t band = 0; band < equaliser.bands.size(); ++band)
                {
                    // Alternating boost and cut, so no band is left at exactly
                    // zero and skipped — the expensive case is the one being
                    // measured here.
                    equaliser.bands[band].gainDb = (band % 2 == 0) ? 6.0f : -6.0f;
                    equaliser.bands[band].order = order;
                }
            }

            rack.equaliser().setSettings (equaliser);
            ++equaliserCase;
        }

        std::vector<float> left (static_cast<std::size_t> (blockSize), 0.0f);
        std::vector<float> right (static_cast<std::size_t> (blockSize), 0.0f);
        float* channels[] = { left.data(), right.data() };

        const auto measurement = measure (secondsPerMeasurement, [&]
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto sample = 0.5f * static_cast<float> (std::sin (0.05 * static_cast<double> (i)));

                left[static_cast<std::size_t> (i)] = sample;
                right[static_cast<std::size_t> (i)] = sample;
            }

            rack.process (channels, 2, blockSize);
        }, Reference::memory);

        printRow (testCase.name, measurement, 0);
    }

    // And the row nobody could measure until every effect existed: what a rack
    // with all six of them in it actually costs. This is the figure that decides
    // whether a full chain is a thing a user can have on several instances at
    // once, and it is deliberately measured last, from the same settings the
    // rows above used.
    {
        static dsp::EffectsRack rack;
        rack.prepare (sampleRate, blockSize);

        dsp::EffectsRack::Chain chain;

        chain[0].effect = dsp::EffectType::gate;
        chain[1].effect = dsp::EffectType::equaliser;
        chain[2].effect = dsp::EffectType::distortion;
        chain[3].effect = dsp::EffectType::compressor;
        chain[4].effect = dsp::EffectType::delay;
        chain[5].effect = dsp::EffectType::reverb;

        rack.setChain (chain);

        dsp::Distortion::Settings distortion;
        distortion.mode = dsp::Distortion::Mode::soft;
        distortion.driveDb = 18.0f;
        distortion.mix = 1.0f;
        rack.distortion().setSettings (distortion);

        dsp::Delay::Settings delay;
        delay.timeMs = 350.0f;
        delay.feedback = 0.5f;
        delay.dampingHz = 6000.0f;
        delay.mix = 0.5f;
        rack.delay().setSettings (delay);

        dsp::Reverb::Settings reverb;
        reverb.decaySeconds = 3.0f;
        reverb.dampingHz = 6000.0f;
        reverb.mix = 0.5f;
        rack.reverb().setSettings (reverb);

        dsp::NoiseGate::Settings gate;
        gate.thresholdDb = -70.0f;
        rack.gate().setSettings (gate);

        dsp::Compressor::Settings compressor;
        compressor.thresholdDb = -24.0f;
        compressor.ratio = 4.0f;
        rack.compressor().setSettings (compressor);

        auto equaliser = dsp::Equaliser::defaultSettings();
        equaliser.bands[1].gainDb = 6.0f;
        equaliser.bands[4].gainDb = -6.0f;
        rack.equaliser().setSettings (equaliser);

        std::vector<float> left (static_cast<std::size_t> (blockSize), 0.0f);
        std::vector<float> right (static_cast<std::size_t> (blockSize), 0.0f);
        float* channels[] = { left.data(), right.data() };

        const auto measurement = measure (secondsPerMeasurement, [&]
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto sample = 0.5f * static_cast<float> (std::sin (0.05 * static_cast<double> (i)));

                left[static_cast<std::size_t> (i)] = sample;
                right[static_cast<std::size_t> (i)] = sample;
            }

            rack.process (channels, 2, blockSize);
        }, Reference::memory);

        printRow ("all six at once", measurement, 0);
    }

    std::cout << "      (includes filling the input buffer, which the empty-rack row isolates)"
              << std::endl;
}

} // namespace

/** What a visualisation tap costs.

    PRD §30.1 requires this to be measured rather than assumed, because the
    scopes are only affordable if they do not cost polyphony. The question the
    measurement has to answer is not "is capture fast" — one linear pass over a
    buffer obviously is — but "is it fast *next to the render it follows*", which
    is a ratio, and a ratio needs both halves measured the same way.
*/
void benchmarkTelemetry()
{
    printHeading ("Visualisation capture, per block, against the render it follows");

    const int voiceCounts[] = { 1, 8, 32 };

    for (const auto voices : voiceCounts)
    {
        static engine::VoiceEngine voiceEngine;
        voiceEngine.prepare (sampleRate);
        voiceEngine.setPolyphony (voices);

        for (int i = 0; i < voices; ++i)
            voiceEngine.noteOn (36 + i * 2, 0.9f);

        std::vector<float> left (static_cast<std::size_t> (blockSize), 0.0f);
        std::vector<float> right (static_cast<std::size_t> (blockSize), 0.0f);
        float* channels[] = { left.data(), right.data() };

        static telemetry::TelemetryHub hub;
        hub.reset();
        hub.setCapturing (false);
        voiceEngine.setTelemetry (&hub);

        // Alternated rather than measured one after the other, because the
        // headline here is the *difference* between them — see measurePair.
        //
        // With nobody watching this must be the render the engine has always
        // done: the whole point of the gate is that an instance with its editor
        // closed pays nothing, so the first row is also a check that the gate
        // works. The second is everything a watched instance pays: the five taps
        // inside the voice loop, which scale with polyphony, the modulation
        // traces sampled between chunks, and the output tap and meter after it,
        // which do not.
        const auto [renderOnly, withCapture] = measurePair (
            secondsPerMeasurement,
            [&]
            {
                hub.setCapturing (false);
                voiceEngine.render (channels, 2, 0, blockSize);
            },
            [&]
            {
                hub.setCapturing (true);
                voiceEngine.render (channels, 2, 0, blockSize);
                hub.scope (telemetry::ScopeSource::output)
                    .writeMixedToMono (channels, 2, 0, blockSize);
                hub.outputMeter().process (channels, 2, 0, blockSize);
            },
            // Memory: a voice render plus six capture buffers being written.
            Reference::memory);

        hub.setCapturing (false);

        printRow (std::to_string (voices) + " voices, render only (nothing watching)", renderOnly,
                  voices);
        printRow (std::to_string (voices) + " voices, render + six-source capture", withCapture,
                  voices);

        const auto overhead = withCapture.realtimeFraction - renderOnly.realtimeFraction;

        std::cout << "      capture adds " << std::fixed << std::setprecision (4)
                  << (overhead * 100.0) << " % of real time";

        if (renderOnly.realtimeFraction > 0.0)
            std::cout << " (" << std::setprecision (1)
                      << (overhead / renderOnly.realtimeFraction * 100.0) << " % of the render)";

        std::cout << std::endl;

        voiceEngine.setTelemetry (nullptr);
        voiceEngine.reset();
    }

    printHeading ("Building one frame for every source (message thread, 30 Hz)");

    {
        static telemetry::TelemetryHub hub;
        hub.reset();

        std::vector<float> noise (static_cast<std::size_t> (blockSize), 0.0f);

        for (int i = 0; i < blockSize; ++i)
            noise[static_cast<std::size_t> (i)] =
                static_cast<float> (std::sin (0.017 * static_cast<double> (i)));

        for (std::size_t source = 0; source < telemetry::scopeSourceCount; ++source)
            for (int i = 0; i < telemetry::scopeBufferSize / blockSize + 1; ++i)
                hub.scope (static_cast<telemetry::ScopeSource> (source))
                    .write (noise.data(), blockSize);

        std::array<telemetry::ScopeFrame, telemetry::scopeSourceCount> frames;

        // Measured against the block rate rather than the frame rate, so the
        // number is comparable with everything else in this report; the note
        // below converts it to what it actually costs.
        const auto measurement = measure (secondsPerMeasurement, [&]
        {
            for (std::size_t source = 0; source < telemetry::scopeSourceCount; ++source)
                (void) telemetry::buildScopeFrame (
                    hub.scope (static_cast<telemetry::ScopeSource> (source)), frames[source]);
        }, Reference::memory);

        printRow ("six frames, per block", measurement, 0);

        // A frame is built thirty times a second, not once per block, so the
        // real cost is this figure scaled by the ratio of the two rates.
        const auto blocksPerSecond = sampleRate / static_cast<double> (blockSize);
        const auto actual = measurement.realtimeFraction
                          * (static_cast<double> (ui::TelemetryBridge::frameRateHz)
                             / blocksPerSecond);

        std::cout << "      at " << ui::TelemetryBridge::frameRateHz << " frames a second that is "
                  << std::fixed << std::setprecision (4) << (actual * 100.0)
                  << " % of real time, on the message thread" << std::endl;
    }
}

//==============================================================================
// What a host actually feels: one callback at a time.
//
// Every figure above this point is an average over thousands of blocks, and an
// average is the wrong statistic for real-time audio. A synthesiser that
// averages thirty per cent of its deadline and spends one block at three
// hundred does not sound like a synthesiser using thirty per cent; it sounds
// like a click. The deadline is per callback and so is the failure.

/** Sets a parameter by its plain value, the way a control or a host would. */
void setPlain (ApolloAudioProcessor& processor, const char* id, float plain)
{
    if (auto* parameter = processor.getValueTreeState().getParameter (id))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
}

/** Everything sounding, through the plugin rather than through the engine. */
void applyHeaviestPatch (ApolloAudioProcessor& processor)
{
    setPlain (processor, "osc1_level", 1.0f);
    setPlain (processor, "osc1_unison", 16.0f);
    setPlain (processor, "osc1_detune", 0.4f);
    setPlain (processor, "osc1_spread", 1.0f);

    setPlain (processor, "osc2_level", 1.0f);
    setPlain (processor, "osc2_unison", 16.0f);
    setPlain (processor, "osc2_detune", 0.4f);
    setPlain (processor, "osc2_spread", 1.0f);

    setPlain (processor, "sub_level", 1.0f);
    setPlain (processor, "noise_level", 1.0f);

    setPlain (processor, "env1_sustain", 1.0f);
}

/** The four routings the modulation table above measures. */
void applyModulation (ApolloAudioProcessor& processor)
{
    setPlain (processor, "mod01_source", 2.0f);    // envelope 2
    setPlain (processor, "mod01_destination", 12.0f);  // filter 1 cutoff
    setPlain (processor, "mod01_depth", 0.8f);

    setPlain (processor, "mod02_source", 5.0f);    // LFO 1
    setPlain (processor, "mod02_destination", 1.0f);   // all pitch — a vibrato
    setPlain (processor, "mod02_depth", 0.15f);

    setPlain (processor, "mod03_source", 9.0f);    // velocity
    setPlain (processor, "mod03_destination", 16.0f);  // amplitude
    setPlain (processor, "mod03_depth", 0.5f);

    setPlain (processor, "mod04_source", 6.0f);    // LFO 2
    setPlain (processor, "mod04_destination", 4.0f);   // oscillator 1 position
    setPlain (processor, "mod04_depth", 0.6f);
}

/** All six effects in the rack, each audibly doing something. */
void applyFullRack (ApolloAudioProcessor& processor)
{
    setPlain (processor, "fx_slot1", 1.0f);        // distortion
    setPlain (processor, "fx_distortion_drive", 18.0f);
    setPlain (processor, "fx_distortion_mix", 0.6f);

    setPlain (processor, "fx_slot2", 2.0f);        // delay
    setPlain (processor, "fx_delay_mix", 0.4f);
    setPlain (processor, "fx_delay_feedback", 0.5f);

    setPlain (processor, "fx_slot3", 3.0f);        // reverb
    setPlain (processor, "fx_reverb_mix", 0.35f);

    setPlain (processor, "fx_slot4", 4.0f);        // gate
    setPlain (processor, "fx_gate_threshold", -60.0f);

    setPlain (processor, "fx_slot5", 5.0f);        // compressor
    setPlain (processor, "fx_compressor_threshold", -18.0f);
    setPlain (processor, "fx_compressor_makeup", 6.0f);

    setPlain (processor, "fx_slot6", 6.0f);        // equaliser
    setPlain (processor, "fx_eq_band3_gain", 6.0f);
    setPlain (processor, "fx_eq_band6_gain", -4.0f);
}

/** The duration of every individual callback, in order. */
struct CallbackTrace
{
    std::vector<double> seconds;
    int worstBlock = -1;
};

/** Renders @p blocks callbacks through a real processor, timing each one.

    Notes arrive during the render rather than before it. That is the point: the
    expensive blocks in a synthesiser are the ones where something *happens* — a
    voice is allocated, a voice is stolen, an envelope changes stage, an
    oscillator crosses into a different mipmap level — and a trace taken with
    every note already held would miss all of them.
*/
[[nodiscard]] CallbackTrace traceCallbacks (ApolloAudioProcessor& processor,
                                            int blocks,
                                            int notes,
                                            bool provokeStealing = false)
{
    juce::AudioBuffer<float> buffer (2, blockSize);

    CallbackTrace trace;
    trace.seconds.reserve (static_cast<std::size_t> (blocks));

    auto worst = 0.0;

    for (int block = 0; block < blocks; ++block)
    {
        juce::MidiBuffer midi;

        // One note per block until @p notes are down, each a **different**
        // pitch. Distinct matters: a note-on for a pitch that is already
        // sounding retriggers that voice rather than allocating another, so
        // cycling a short set of pitches would quietly measure a fraction of
        // the polyphony the row claims.
        if (block < notes)
        {
            midi.addEvent (juce::MidiMessage::noteOn (1, 36 + block, 0.9f), 0);
        }
        else if (provokeStealing && block % 16 == 0)
        {
            // Only where the row asks for it, and only from **outside** the
            // held set, so the engine must take a sounding voice away rather
            // than retrigger one.
            //
            // THIS IS OPTIONAL FOR A REASON. The first version did it on every
            // row, cycling twenty-four extra pitches — which meant the row
            // labelled "8 notes" had thirty-two voices sounding within a few
            // hundred blocks, and measured exactly what the "32 notes" row
            // did. The two came out at 4.24 % and 4.22 % and looked like a
            // discovery about polyphony being free.
            midi.addEvent (juce::MidiMessage::noteOn (
                               1, 36 + engine::VoiceEngine::maxPolyphony + ((block / 16) % 8), 0.9f),
                           0);
        }

        buffer.clear();

        const auto start = std::chrono::steady_clock::now();
        processor.processBlock (buffer, midi);
        const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;

        trace.seconds.push_back (elapsed.count());

        if (elapsed.count() > worst)
        {
            worst = elapsed.count();
            trace.worstBlock = block;
        }
    }

    return trace;
}

/** @returns the value @p fraction of the way through a sorted copy of @p values. */
[[nodiscard]] double percentileOf (std::vector<double> values, double fraction)
{
    if (values.empty())
        return 0.0;

    std::sort (values.begin(), values.end());

    const auto index = static_cast<std::size_t> (fraction * static_cast<double> (values.size() - 1));

    return values[std::min (index, values.size() - 1)];
}

void printCallbackRow (const std::string& label, const CallbackTrace& trace)
{
    // The deadline: a 512-sample block at 48 kHz has 10.67 ms to be filled.
    const auto deadline = static_cast<double> (blockSize) / sampleRate;

    const auto asPercent = [deadline] (double seconds) { return seconds / deadline * 100.0; };

    std::cout << "  " << std::left << std::setw (30) << label << std::right << std::fixed
              << std::setw (9) << std::setprecision (2) << asPercent (percentileOf (trace.seconds, 0.5)) << " %"
              << std::setw (9) << asPercent (percentileOf (trace.seconds, 0.99)) << " %"
              << std::setw (9) << asPercent (percentileOf (trace.seconds, 0.999)) << " %"
              << std::setw (9) << asPercent (*std::max_element (trace.seconds.begin(), trace.seconds.end())) << " %"
              << std::setw (8) << trace.worstBlock
              << std::endl;
}

//==============================================================================
// Would SIMD across unison voices be worth it? (Phase 10d-4)
//
// 10d-1 and 10d-2 made the voice path about 2.8x cheaper and left the worst
// patch at roughly a third of its callback deadline, so vectorising the unison
// stack is headroom rather than a fix. That changes what it has to justify: a
// hand-vectorised inner loop is a substantial increase in the amount of code
// that has to be right, on the innermost function in the instrument, with a
// scalar fallback to maintain for every target that does not get the vector
// path — and ARM64, a stated target (CLAUDE.md §3.2), has no gather at all.
//
// So this measures the gain BEFORE paying for it. The prototype below is not
// production code and is not wired into the engine; it exists to answer one
// question with a number, so that the decision in PROJECT-STATE §8 is bought
// with a measurement rather than an expectation.
//
// WHAT THE PROTOTYPE CHANGES, and each is a cost as well as a saving:
//
//   * It splits the per-sample work into a scalar *gather* pass and an
//     arithmetic pass over contiguous arrays. Only the second can vectorise;
//     the first is loads at addresses that depend on each voice's phase, which
//     is the part no vector unit helps with. Whatever speedup this shows is
//     therefore bounded by how much of the cost is arithmetic.
//   * It interpolates in **float** rather than double. That is most of where a
//     vector win would come from — twice the lanes — and it is not free, so the
//     accuracy cost is measured here too rather than assumed negligible.
//
// It deliberately does NOT hand-write intrinsics. A portable implementation
// would have to work on SSE, NEON and whatever a future target brings, and the
// realistic form is exactly this: separate the gather, then write arithmetic
// simple enough for the compiler to vectorise. Measuring hand-tuned AVX2 would
// answer a question Apollo cannot ship.

// Templated on the arithmetic type so the win can be DECOMPOSED. The prototype
// changes two things at once and only one of them needs a vector unit:
//
//   * the arithmetic type (float halves the width, which is where SIMD lanes
//     come from, and costs accuracy);
//   * the data layout (taps and gains in flat parallel arrays, no per-sample
//     applyLayout call, no reads through sixteen separate Reader objects).
//
// Measuring only the float version would leave it impossible to say which of
// those bought the speedup -- and if it is mostly the layout, the same win is
// available in double with no accuracy cost and no vector code at all. So both
// are instantiated and both are reported.
template <typename Sample>
struct UnisonPrototypeOf
{
    static constexpr int maxVoices = dsp::UnisonLayout::maxVoices;

    void prepare (const dsp::Wavetable& table, const dsp::UnisonLayout& layout,
                  double baseFrequency, double framePosition)
    {
        count = layout.getCount();

        const auto level = dsp::Wavetable::selectMipLevel (baseFrequency, sampleRate);
        const auto frameSize = dsp::Wavetable::samplesAtLevel (level);

        const auto lowerIndex = static_cast<int> (framePosition);

        blend = static_cast<Sample> (framePosition - static_cast<double> (lowerIndex));

        const auto* base = table.getReadPointer (level, lowerIndex);

        for (int v = 0; v < count; ++v)
        {
            // Every unison voice reads the same table at the same frame and the
            // same mip level; only its phase differs. That is exactly why the
            // stack is the candidate for vectorising — sixteen reads whose only
            // difference is an index.
            lower[v] = base;
            upper[v] = base + frameSize;
            mask[v] = frameSize - 1;
            size[v] = static_cast<float> (frameSize);

            increment[v] = baseFrequency * layout.getFrequencyRatio (v) / sampleRate;
            phase[v] = layout.getStartPhase (v);
            gainLeft[v] = layout.getGainLeft (v);
            gainRight[v] = layout.getGainRight (v);
        }
    }

    void addNextStereoSample (float& leftOut, float& rightOut) noexcept
    {
        // PASS ONE: the gather. Scalar by necessity — each voice's four taps sit
        // at an address derived from its own phase.
        for (int v = 0; v < count; ++v)
        {
            const auto wrapped = phase[v] - std::floor (phase[v]);
            const auto position = wrapped * static_cast<double> (size[v]);

            auto index = static_cast<int> (position);

            fraction[v] = static_cast<Sample> (position - static_cast<double> (index));

            const auto m = mask[v];

            if (index > m)
                index = m;

            const auto i0 = (index + m) & m;
            const auto i1 = index;
            const auto i2 = (index + 1) & m;
            const auto i3 = (index + 2) & m;

            const auto* lo = lower[v];
            const auto* hi = upper[v];

            // Blended here rather than in the arithmetic pass, because the
            // blend is what doubles the loads and it belongs with them.
            const auto mix = [this] (float a, float b) noexcept
            {
                const auto low = static_cast<Sample> (a);
                return low + (static_cast<Sample> (b) - low) * blend;
            };

            tap0[v] = mix (lo[i0], hi[i0]);
            tap1[v] = mix (lo[i1], hi[i1]);
            tap2[v] = mix (lo[i2], hi[i2]);
            tap3[v] = mix (lo[i3], hi[i3]);

            phase[v] += increment[v];

            if (phase[v] >= 1.0 || phase[v] < 0.0)
                phase[v] -= std::floor (phase[v]);
        }

        // PASS TWO: the arithmetic, over contiguous arrays with no
        // data-dependent addressing. This is the part a vector unit earns its
        // keep on, and the part written so a compiler can see it.
        Sample sumLeft = Sample (0);
        Sample sumRight = Sample (0);

        for (int v = 0; v < count; ++v)
        {
            const auto y0 = tap0[v];
            const auto y1 = tap1[v];
            const auto y2 = tap2[v];
            const auto y3 = tap3[v];
            const auto t = fraction[v];

            const auto c1 = Sample (0.5) * (y2 - y0);
            const auto c2 = y0 - Sample (2.5) * y1 + Sample (2) * y2 - Sample (0.5) * y3;
            const auto c3 = Sample (0.5) * (y3 - y0) + Sample (1.5) * (y1 - y2);

            const auto sample = ((c3 * t + c2) * t + c1) * t + y1;

            sumLeft += sample * static_cast<Sample> (gainLeft[v]);
            sumRight += sample * static_cast<Sample> (gainRight[v]);
        }

        leftOut += static_cast<float> (sumLeft);
        rightOut += static_cast<float> (sumRight);
    }

    const float* lower[maxVoices] {};
    const float* upper[maxVoices] {};
    int mask[maxVoices] {};
    float size[maxVoices] {};

    double phase[maxVoices] {};
    double increment[maxVoices] {};
    float gainLeft[maxVoices] {};
    float gainRight[maxVoices] {};

    Sample tap0[maxVoices] {};
    Sample tap1[maxVoices] {};
    Sample tap2[maxVoices] {};
    Sample tap3[maxVoices] {};
    Sample fraction[maxVoices] {};

    Sample blend = Sample (0);
    int count = 0;
};

using UnisonPrototype = UnisonPrototypeOf<float>;
using UnisonPrototypeDouble = UnisonPrototypeOf<double>;

void benchmarkUnisonSimd()
{
    printHeading ("Unison stack: what a restructured inner loop is worth (Phase 10d-4)");

    const dsp::WavetableLibrary library;
    const auto& table = library.getTable (0);

    dsp::UnisonLayout layout;
    layout.update (dsp::UnisonLayout::maxVoices, 0.4f, 0.6f);

    // A frame position deliberately between two frames, so both paths pay for
    // the two-frame blend. At a whole-numbered position both skip it, and the
    // comparison would flatter the prototype by measuring the easier case.
    constexpr double framePosition = 7.35;
    constexpr double frequency = 110.0;

    const auto normalisedPosition =
        static_cast<float> (framePosition / static_cast<double> (table.getNumFrames() - 1));

    dsp::UnisonOscillator scalar;
    scalar.setSampleRate (sampleRate);
    scalar.setTable (&table);
    scalar.setLayout (&layout);
    scalar.setFrequency (frequency);
    scalar.setPosition (normalisedPosition);
    scalar.resetPhase (0.0);

    UnisonPrototype prototype;
    prototype.prepare (table, layout, frequency, framePosition);

    UnisonPrototypeDouble prototypeDouble;
    prototypeDouble.prepare (table, layout, frequency, framePosition);

    //==========================================================================
    // AGREEMENT FIRST. A faster path that computes something else is not a
    // faster path, and the float interpolation means "identical" is not the
    // bar — so the bar is a measured bound, stated rather than assumed.
    {
        double worst = 0.0;
        double sumSquaredError = 0.0;
        double sumSquaredSignal = 0.0;

        for (int i = 0; i < 4096; ++i)
        {
            float scalarLeft = 0.0f, scalarRight = 0.0f;
            float protoLeft = 0.0f, protoRight = 0.0f;

            scalar.addNextStereoSample (scalarLeft, scalarRight);
            prototype.addNextStereoSample (protoLeft, protoRight);

            const auto errorLeft = static_cast<double> (protoLeft - scalarLeft);
            const auto errorRight = static_cast<double> (protoRight - scalarRight);

            worst = std::max (worst, std::max (std::abs (errorLeft), std::abs (errorRight)));

            sumSquaredError += errorLeft * errorLeft + errorRight * errorRight;
            sumSquaredSignal += static_cast<double> (scalarLeft) * scalarLeft
                              + static_cast<double> (scalarRight) * scalarRight;
        }

        const auto errorDecibels = sumSquaredSignal > 0.0
                                 ? 10.0 * std::log10 (sumSquaredError / sumSquaredSignal)
                                 : -200.0;

        std::cout << "  float prototype vs scalar:  worst sample " << std::scientific
                  << std::setprecision (3) << worst << ", error energy "
                  << std::fixed << std::setprecision (1) << errorDecibels << " dB"
                  << std::endl;
    }

    //==========================================================================
    // THEN THE COST, measured alternately. Two long runs one after the other
    // would compare two different machines on this laptop (ADR-0072).
    scalar.resetPhase (0.0);
    prototype.prepare (table, layout, frequency, framePosition);

    static volatile float sink = 0.0f;

    const auto [scalarCost, protoCost] = measurePair (
        secondsPerMeasurement,
        [&]
        {
            float left = 0.0f, right = 0.0f;

            for (int i = 0; i < blockSize; ++i)
                scalar.addNextStereoSample (left, right);

            sink = left + right;
        },
        [&]
        {
            float left = 0.0f, right = 0.0f;

            for (int i = 0; i < blockSize; ++i)
                prototype.addNextStereoSample (left, right);

            sink = left + right;
        },
        Reference::memory);

    // The double variant, alternated against the same scalar path so the two
    // ratios are comparable. This is the row that decides whether float and a
    // vector unit are needed at all, or whether the data layout was the whole
    // story.
    scalar.resetPhase (0.0);
    prototypeDouble.prepare (table, layout, frequency, framePosition);

    const auto [scalarAgain, doubleCost] = measurePair (
        secondsPerMeasurement,
        [&]
        {
            float left = 0.0f, right = 0.0f;

            for (int i = 0; i < blockSize; ++i)
                scalar.addNextStereoSample (left, right);

            sink = left + right;
        },
        [&]
        {
            float left = 0.0f, right = 0.0f;

            for (int i = 0; i < blockSize; ++i)
                prototypeDouble.addNextStereoSample (left, right);

            sink = left + right;
        },
        Reference::memory);

    printRow ("scalar, 16 voices", scalarCost, 0);
    printRow ("prototype, float arithmetic", protoCost, 0);
    printRow ("prototype, double arithmetic", doubleCost, 0);

    if (protoCost.realtimeFraction > 0.0 && doubleCost.realtimeFraction > 0.0
        && scalarAgain.realtimeFraction > 0.0)
    {
        std::cout << "      float:  " << std::fixed << std::setprecision (2)
                  << (scalarCost.realtimeFraction / protoCost.realtimeFraction)
                  << "x the scalar path\n"
                  << "      double: "
                  << (scalarAgain.realtimeFraction / doubleCost.realtimeFraction)
                  << "x the scalar path, at no accuracy cost at all"
                  << std::endl;
    }

    std::cout << "\n  One oscillator's stack, not a whole voice. A voice runs two of these\n"
                 "  plus a sub, a noise generator, filters and an envelope, so the effect on\n"
                 "  the instrument is smaller than the ratios above (§5b).\n"
                 "\n"
                 "  THE TWO ROWS ARE THE POINT. The double row changes only the data layout --\n"
                 "  flat parallel arrays, gains hoisted out of the layout object, no per-sample\n"
                 "  generation check, and per-voice state packed instead of strided across\n"
                 "  sixteen oscillator objects. It costs nothing in accuracy and needs no\n"
                 "  vector code, no runtime dispatch and no scalar fallback. The float row adds\n"
                 "  half-width arithmetic on top, which is where SIMD lanes would come from.\n"
                 "\n"
                 "  Most of the available win is in the layout, not the vectorising. That is\n"
                 "  what ADR-0073 decided on, and it is why Apollo is not getting hand-written\n"
                 "  intrinsics it would then have to maintain twice over.\n"
              << std::endl;
}
void benchmarkCallbacks()
{
    printHeading ("Worst-case callback, through the whole processor (% of one block's deadline)");

    std::cout << "  " << std::left << std::setw (30) << "patch" << std::right
              << std::setw (11) << "median" << std::setw (11) << "p99"
              << std::setw (11) << "p99.9" << std::setw (11) << "worst"
              << std::setw (8) << "block" << std::endl;

    // Ten seconds of audio per row, which is 938 callbacks — enough that a
    // 99.9th percentile means something rather than being the second-worst
    // sample of a handful.
    constexpr int blocks = 938;

    // THE WITNESS ROW, and it runs first for that reason.
    //
    // The default patch at eight voices is the cheapest thing Apollo does: a few
    // per cent of a callback, steady, with its worst case within a fifth of its
    // median on any machine that is behaving. A large worst case *here* cannot
    // be Apollo — there is not enough work in this row to produce one — so it is
    // a usable witness for whether anything else had the machine during the
    // traces, which is a question the opening assessment cannot answer because
    // it finished minutes ago (ADR-0072).
    //
    // Phase 10d-2 applied exactly this rule by hand, discarding four runs of
    // fourteen, after a run that reported 2.7 % contention went on to produce a
    // 435 % worst-case block. Doing it by hand is fine once; having the report
    // say it is better.
    double witnessWorst = 0.0;
    double witnessP99 = 0.0;

    {
        ApolloAudioProcessor processor;
        processor.prepareToPlay (sampleRate, blockSize);

        const auto trace = traceCallbacks (processor, blocks, 8);

        if (! trace.seconds.empty())
        {
            const auto deadline = static_cast<double> (blockSize) / sampleRate;
            const auto slowest = *std::max_element (trace.seconds.begin(), trace.seconds.end());

            witnessWorst = slowest / deadline * 100.0;
            witnessP99 = percentileOf (trace.seconds, 0.99) / deadline * 100.0;
        }

        printCallbackRow ("default patch, 8 held", trace);
        processor.releaseResources();
    }

    {
        ApolloAudioProcessor processor;
        processor.prepareToPlay (sampleRate, blockSize);
        printCallbackRow ("default patch, 32 held", traceCallbacks (processor, blocks, 32));
        processor.releaseResources();
    }

    {
        ApolloAudioProcessor processor;
        applyFullRack (processor);
        processor.prepareToPlay (sampleRate, blockSize);
        printCallbackRow ("default + full rack, 32 held", traceCallbacks (processor, blocks, 32));
        processor.releaseResources();
    }

    {
        ApolloAudioProcessor processor;
        applyHeaviestPatch (processor);
        processor.prepareToPlay (sampleRate, blockSize);
        printCallbackRow ("heaviest patch, 8 held", traceCallbacks (processor, blocks, 8));
        processor.releaseResources();
    }

    // The real worst case, and the one nothing before Phase 10c measured: every
    // source at full unison, the modulation matrix working, all six effects in
    // the rack, at full polyphony. Each of those has been measured on its own;
    // a user builds them together.
    {
        ApolloAudioProcessor processor;
        applyHeaviestPatch (processor);
        applyModulation (processor);
        applyFullRack (processor);
        processor.prepareToPlay (sampleRate, blockSize);
        printCallbackRow ("everything at once, 32 held", traceCallbacks (processor, blocks, 32, true));
        processor.releaseResources();
    }

    std::cout << "\n  The last row is the worst patch Apollo's own controls can build. Anything\n"
                 "  over 100 % of the deadline will not keep up on this machine (issue 13).\n"
              << std::endl;

    // Five per cent of one block's deadline. The default patch at eight voices
    // measures two to four per cent in the median and lands within a fifth of
    // that at its worst on a quiet machine, so five per cent as a *worst case*
    // is already generous — it is set to catch interference, not to be a
    // performance assertion about Apollo.
    constexpr double witnessLimit = 5.0;

    if (witnessWorst > witnessLimit)
    {
        std::cout << "  *** THESE TRACES ARE NOT TRUSTWORTHY. ***\n"
                     "  The cheapest row above, the default patch at eight voices, had a worst\n"
                     "  callback of " << std::fixed << std::setprecision (1) << witnessWorst
                  << " % of the deadline. There is not enough work in that\n"
                     "  patch to produce such a block, so something else was using this machine\n"
                     "  while these traces were being taken.\n";

        // Which columns are ruined depends on whether the interference was
        // sustained or a single event, and saying which is strictly more useful
        // than one verdict — a run spoiled by one interrupt still has usable
        // medians, and a run spoiled throughout has nothing.
        //
        // The gate itself stays on the worst case. Loosening it to the 99th
        // percentile was considered and the evidence refused it: across the
        // fourteen Phase 10d-2 runs the worst case separated cleanly (clean runs
        // reached 4.0 %, disturbed ones started at 12.9 %), and one disturbed run
        // had a 99th percentile of 2.67 %, inside the clean range, while its
        // worst block was 23 %. A gate on the 99th percentile would have passed
        // it.
        if (witnessP99 > witnessLimit)
            std::cout << "  Its 99th percentile was " << witnessP99
                      << " % too, so the interference was sustained\n"
                         "  rather than a single event: every column of every row above is\n"
                         "  suspect, including the ones that look reasonable. Discard this run.\n";
        else
            std::cout << "  Its 99th percentile was only " << witnessP99
                      << " %, so this was an isolated event\n"
                         "  rather than sustained contention. The median and p99 columns above\n"
                         "  are probably sound; the p99.9 and worst columns are not, and those\n"
                         "  are the ones issue 13 is about.\n";

        std::cout << std::endl;
    }
}

//==============================================================================
// Loading, which happens on somebody's message thread while they wait.

//==============================================================================
// First use: what somebody waits for, and what a session pays to hold.
//
// THIS SECTION MUST RUN BEFORE ANY OTHER, and that is not a stylistic
// preference. Apollo's four built-in wavetables are built once per process and
// shared by every instance after the first (ADR-0066), so "how long does a
// processor take to construct" has two different answers and which one you get
// depends entirely on whether anything constructed one earlier in the same run.
//
// The first version of this section ran last. It reported construction at
// 2.90 ms and each later instance costing *more* memory than the first, and
// then printed a note explaining that the first instance carries the shared
// library — which the numbers plainly contradicted, because by then six other
// benchmarks had already built it. The measurement was fine; the story attached
// to it was about a process that no longer existed by the time it ran.

void benchmarkFirstUse()
{
    printHeading ("First use: construction, loading and memory");

    const auto milliseconds = [] (auto&& work)
    {
        const auto start = std::chrono::steady_clock::now();
        work();
        const std::chrono::duration<double, std::milli> elapsed
            = std::chrono::steady_clock::now() - start;

        return elapsed.count();
    };

    const auto printMilliseconds = [] (const std::string& label, double value)
    {
        std::cout << "  " << std::left << std::setw (46) << label
                  << std::right << std::setw (9) << std::fixed << std::setprecision (2)
                  << value << " ms" << std::endl;
    };

    const auto printMegabytes = [] (const std::string& label, double value)
    {
        std::cout << "  " << std::left << std::setw (46) << label
                  << std::right << std::setw (9) << std::fixed << std::setprecision (2)
                  << value << " MB" << std::endl;
    };

    const auto megabytes = [] (std::size_t bytes)
    {
        return static_cast<double> (bytes) / (1024.0 * 1024.0);
    };

    const auto emptyProcess = footprintBytes();

    // THE FIRST INSTRUMENT PAYS FOR THE WAVETABLE LIBRARY, and this section is
    // the only place that can see it — which is why it runs first and why that
    // ordering is load-bearing rather than cosmetic.
    //
    // It did not always see it. Phase 10c reported this row at 4.17 ms and
    // flagged the result as contradicting ADR-0066, which says building the four
    // tables takes a couple of hundred milliseconds. ADR-0066 was right. Two
    // juce::UnitTest subclasses held a WavetableLibrary as a *member*, and a
    // unit test object is constructed at static-initialisation time, so 165 ms
    // of table building happened before main() and every benchmark in the
    // process measured a library that already existed. Both are lazy now, and
    // the row reads what ADR-0066 always said it should.

    //==========================================================================
    // The very first instrument in this process. Nothing above has touched the
    // engine, so this one pays for the wavetable library and the report can say
    // so truthfully.

    double firstConstruction = 0.0;
    double laterConstruction = 0.0;

    std::size_t afterFirst = 0;
    std::size_t afterSecond = 0;
    std::size_t afterMany = 0;

    constexpr int many = 16;

    {
        std::unique_ptr<ApolloAudioProcessor> first;
        firstConstruction = milliseconds ([&] { first = std::make_unique<ApolloAudioProcessor>(); });

        first->prepareToPlay (sampleRate, blockSize);
        afterFirst = footprintBytes();

        std::unique_ptr<ApolloAudioProcessor> second;
        laterConstruction = milliseconds ([&] { second = std::make_unique<ApolloAudioProcessor>(); });

        second->prepareToPlay (sampleRate, blockSize);
        afterSecond = footprintBytes();

        printMilliseconds ("Constructing the first instrument in the process", firstConstruction);
        printMilliseconds ("Constructing a second instrument", laterConstruction);

        printMilliseconds ("prepareToPlay at 48 kHz, 512 samples",
                           milliseconds ([&] { second->prepareToPlay (sampleRate, blockSize); }));

        //======================================================================
        // Presets, on the thread whose interface is waiting.

        auto& apvts = second->getValueTreeState();
        const auto& library = resources::factoryPresets();

        printMilliseconds ("Rendering all ten factory presets to documents",
                           milliseconds ([&]
                           {
                               for (const auto& preset : library)
                                   documentSink = resources::render (preset).length();
                           }));

        printMilliseconds ("Loading all ten factory presets into an instrument",
                           milliseconds ([&]
                           {
                               for (const auto& preset : library)
                                   (void) presets::read (apvts, resources::render (preset));
                           }));

        //======================================================================
        // What a session holding a rack of instruments pays.

        if (emptyProcess > 0)
        {
            std::vector<std::unique_ptr<ApolloAudioProcessor>> rest;
            rest.reserve (many);

            for (int i = 0; i < many; ++i)
            {
                rest.push_back (std::make_unique<ApolloAudioProcessor>());
                rest.back()->prepareToPlay (sampleRate, blockSize);
            }

            afterMany = footprintBytes();
        }
    }

    std::cout << std::endl;

    if (emptyProcess == 0)
    {
        std::cout << "  Memory is not reported on this platform." << std::endl;
        return;
    }

    printMegabytes ("The process before any instrument exists", megabytes (emptyProcess));

    if (afterFirst > emptyProcess)
        printMegabytes ("The first prepared instance adds", megabytes (afterFirst - emptyProcess));

    if (afterSecond > afterFirst)
        printMegabytes ("The second adds", megabytes (afterSecond - afterFirst));

    if (afterMany > afterSecond)
        printMegabytes ("Each of the next sixteen adds",
                        megabytes (afterMany - afterSecond) / static_cast<double> (many));

    std::cout << "\n  The first instance carries the built-in wavetable library and every later\n"
                 "  one shares it (ADR-0066), so the gap between the first two rows is what\n"
                 "  that library costs and the last row is what an instrument costs.\n"
              << std::endl;
}

//==============================================================================

void run()
{
    std::cout << "\n==========================================================\n"
              << "Apollo CPU measurements\n"
              << "  sample rate " << sampleRate << " Hz, block size " << blockSize << "\n"
              << "==========================================================" << std::endl;

    // Before anything is timed: pin the thread, raise the priority, warm the
    // caches, and then find out whether this machine is in a fit state to be
    // measured on at all (Machine.h).
    const auto warmUp = prepareMachine();

    auto stability = assessMachine();

    machineSpeed = stability.speed;
    memoryMachineSpeed = stability.memorySpeed;

    std::cout << "\nMachine\n-------\n"
              << "  warmed to a sustainable clock   " << std::fixed << std::setprecision (1)
              << warmUp.seconds << " s, giving up " << (warmUp.slowdown * 100.0) << " % of clock\n"
              << (warmUp.reachedFloor
                    ? ""
                    : "      (it never settled -- everything below is on a moving clock)\n")
              << "  arithmetic reference unit       "
              << std::fixed << std::setprecision (3) << (stability.referenceSeconds * 1000.0)
              << " ms  (nominal " << (nominalReferenceSeconds * 1000.0) << " ms)\n"
              << "  memory reference unit           "
              << std::setprecision (3) << (stability.memoryReferenceSeconds * 1000.0)
              << " ms  (nominal " << (nominalMemoryReferenceSeconds * 1000.0) << " ms)\n"
              << "  speed, arithmetic               " << std::setprecision (3) << stability.speed << " x\n"
              << "  speed, memory                   " << std::setprecision (3) << stability.memorySpeed << " x\n"
              << "  typical contention              " << std::setprecision (1)
              << (stability.typicalSlowdown * 100.0) << " %"
              << "   (worst burst " << (stability.spread * 100.0) << " % off the fastest)"
              << std::endl;

    // How far apart the two references are is the uncertainty every normalised
    // figure carries, so it is stated rather than left for a reader to divide
    // out of the two lines above. They are not expected to agree: the point of
    // measuring both is that the core clock and the memory path do not throttle
    // together (ADR-0072).
    if (stability.speed > 0.0 && stability.memorySpeed > 0.0)
    {
        const auto disagreement = std::abs (stability.memorySpeed - stability.speed)
                                / std::max (stability.memorySpeed, stability.speed);

        std::cout << "  the two references disagree by  " << std::setprecision (1)
                  << (disagreement * 100.0) << " %";

        if (disagreement > 0.15)
            std::cout << "   <- large; normalised figures carry this";

        std::cout << std::endl;
    }

    // THE STANDING CAVEAT ON THE NORMALISED COLUMN, printed every run because it
    // is still true (ADR-0072).
    //
    // That the two references decouple is measured: their ratio moved by 24 %
    // across six runs, so they are demonstrably not reporting the same thing,
    // and the per-row choice between them therefore has something to choose.
    // That the *memory* one is the better divisor for the memory-bound rows is
    // reasoned rather than demonstrated: confirming it means showing that it
    // reduces a row's variation between runs, and that needs several runs this
    // report is willing to vouch for. On the machine this was developed on
    // there have been none.
    std::cout << "\n  The normalised column's per-row choice of reference is reasoned and not\n"
                 "  yet validated: see ADR-0072. Raw figures and same-sitting ratios do not\n"
                 "  depend on it."
              << std::endl;

    if (! stability.settled)
        std::cout << "\n  *** THIS MACHINE IS NOT STEADY. ***\n"
                     "  Something else was using this machine for more than five per cent of the\n"
                     "  time the reference was being timed, so the absolute figures below are a\n"
                     "  story about the machine as much as about Apollo. Ratios between rows\n"
                     "  measured alternately still hold.\n"
                     "  Close what else is running, let the machine cool, and measure again.\n"
                  << std::endl;

    std::cout << "\n  Columns: % of real time as measured here; 'norm' the same corrected to the\n"
                 "  reference machine, which is the one to compare between runs; 'sp' how far\n"
                 "  the passes of that row disagreed.\n"
              << std::endl;

    // First, and the ordering is load-bearing: this is the only section whose
    // figures are about the first instrument in a process, and any benchmark
    // running before it would have built the shared wavetable library already.
    benchmarkFirstUse();

    benchmarkPolyphony();
    benchmarkModulation();
    benchmarkLfos();
    benchmarkOversampling();
    benchmarkEffects();
    benchmarkTelemetry();
    benchmarkUnisonSimd();
    benchmarkCallbacks();

    // The other end of the run. The assessment above happened before any Apollo
    // code had executed and the report is a minute and a half of solid work, so
    // a report that only ever asks at the start cannot notice a machine that
    // went bad halfway through — which is exactly what happened during 10d-2
    // (ADR-0072).
    reassessDrift (stability);

    std::cout << "\nMachine, afterwards\n-------------------\n"
              << "  drift across the run            " << std::fixed << std::setprecision (1)
              << (stability.driftAcrossRun * 100.0) << " %"
              << (stability.driftAcrossRun > 0.0 ? "   (slower at the end)" : "   (no slower at the end)")
              << std::endl;

    if (stability.driftAcrossRun > 0.10)
        std::cout << "\n  *** THIS MACHINE SLOWED DOWN WHILE IT WAS BEING MEASURED. ***\n"
                     "  The sections near the top of this report and the sections near the\n"
                     "  bottom were not measured on the same machine. Rows cannot be compared\n"
                     "  against each other across sections, and none of them should be compared\n"
                     "  against another run.\n"
                  << std::endl;

    std::cout << "\nMeasured on this machine, in this configuration. These numbers are\n"
                 "not portable and are not asserted on: see Tests/Performance/Benchmarks.h.\n"
              << std::endl;
}

} // namespace apollo::benchmarks
