#include "Performance/Benchmarks.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "DSP/LFO/Lfo.h"
#include "DSP/Oversampling/Oversampler.h"
#include "Engine/VoiceEngine.h"

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

/** @returns the fraction of real time a render took.

    This is the number that matters for audio. 0.05 means the work took five
    per cent of the time it represents, so roughly twenty such loads would
    saturate one core. It is independent of block size and sample rate, unlike
    a raw duration, which is why it is reported instead of milliseconds.
*/
struct Measurement
{
    double realtimeFraction = 0.0;
    double secondsRendered = 0.0;
};

/** Renders @p seconds of audio through @p render and times it.

    The result is the best of several passes rather than the mean. A slower pass
    means the operating system took the core away, which says nothing about
    Apollo; the fastest pass is the closest available estimate of the work the
    code actually does.
*/
template <typename RenderBlock>
[[nodiscard]] Measurement measure (double seconds, RenderBlock&& render)
{
    const auto blocks = static_cast<int> (seconds * sampleRate / static_cast<double> (blockSize));

    auto best = std::numeric_limits<double>::max();

    for (int pass = 0; pass < 3; ++pass)
    {
        const auto start = std::chrono::steady_clock::now();

        for (int i = 0; i < blocks; ++i)
            render();

        const auto finish = std::chrono::steady_clock::now();
        const std::chrono::duration<double> elapsed = finish - start;

        best = std::min (best, elapsed.count());
    }

    const auto rendered = static_cast<double> (blocks * blockSize) / sampleRate;

    return { best / rendered, rendered };
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

    std::cout << "  " << std::left << std::setw (34) << label
              << std::right << std::setw (8) << std::fixed << std::setprecision (3) << percent << " %";

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

    return measure (secondsPerMeasurement,
                    [&] { voiceEngine.render (channels, 2, 0, blockSize); });
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
                           [&] { voiceEngine.render (channels, 2, 0, blockSize); }),
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
        });

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
        });

        printRow (std::string (testCase.name) + ", with tanh drive", measurement, 0);

        std::cout << "      latency " << oversampler.getLatencySamples() << " samples ("
                  << std::fixed << std::setprecision (2)
                  << (static_cast<double> (oversampler.getLatencySamples()) / sampleRate * 1000.0)
                  << " ms at 48 kHz)" << std::endl;
    }
}

} // namespace

void run()
{
    std::cout << "\n==========================================================\n"
              << "Apollo CPU measurements\n"
              << "  sample rate " << sampleRate << " Hz, block size " << blockSize << "\n"
              << "  figures are per cent of real time; lower is better\n"
              << "==========================================================" << std::endl;

    benchmarkPolyphony();
    benchmarkModulation();
    benchmarkLfos();
    benchmarkOversampling();

    std::cout << "\nMeasured on this machine, in this configuration. These numbers are\n"
                 "not portable and are not asserted on: see Tests/Performance/Benchmarks.h.\n"
              << std::endl;
}

} // namespace apollo::benchmarks
