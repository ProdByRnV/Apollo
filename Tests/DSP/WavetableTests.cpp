/*
    Wavetable oscillator tests, including the spectral validation Phase 4
    requires.

    The central claim of the wavetable engine is that it does not alias. That is
    not something code review can establish — aliasing is a property of the
    output signal, so it has to be measured. These tests render steady tones,
    take an FFT, classify each bin as harmonic or not, and report the worst
    non-harmonic component relative to the fundamental.
*/

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <vector>

#include "DSP/Oscillators/Wavetable.h"
#include "DSP/Oscillators/WavetableLibrary.h"
#include "DSP/Oscillators/WavetableOscillator.h"

using namespace apollo::dsp;

namespace
{

constexpr double testSampleRate = 48000.0;

/** FFT size for the spectral tests. 16384 bins at 48 kHz gives ~2.9 Hz
    resolution, fine enough to separate a fundamental from an alias sitting
    close to it.
*/
constexpr int fftOrder = 14;
constexpr int fftSize = 1 << fftOrder;

/** ACCEPTABLE ALIASING THRESHOLD.

    Non-harmonic content must stay at least this far below the fundamental.

    -60 dBc is roughly ten bits of clean dynamic range below the note being
    played, and comfortably below the noise floor of any real playback chain. It
    is a deliberate engineering budget rather than a measured artefact of the
    current implementation: the mipmap removes aliasing from the source, so what
    remains is interpolation error, and the threshold leaves room for that to
    grow slightly without silently degrading.
*/
constexpr float maxAliasDecibels = -60.0f;

/** Renders a steady tone and returns its magnitude spectrum in decibels
    relative to the loudest bin.

    The first samples are discarded so nothing but steady state is analysed, and
    a Hann window is applied so a fundamental that does not land exactly on a bin
    does not smear across the spectrum and masquerade as aliasing.
*/
[[nodiscard]] std::vector<float> renderSpectrum (const Wavetable& table,
                                                 double frequencyHz,
                                                 float position)
{
    WavetableOscillator oscillator;
    oscillator.setSampleRate (testSampleRate);
    oscillator.setTable (&table);
    oscillator.setPosition (position);
    oscillator.setFrequency (frequencyHz);
    oscillator.resetPhase (0.0);

    // Discard a little output so any start transient is excluded.
    for (int i = 0; i < 1024; ++i)
        (void) oscillator.getNextSample();

    std::vector<float> samples (static_cast<std::size_t> (fftSize) * 2, 0.0f);

    for (int i = 0; i < fftSize; ++i)
        samples[static_cast<std::size_t> (i)] = oscillator.getNextSample();

    // Blackman-Harris, not Hann. A tone whose frequency does not land exactly on
    // an FFT bin leaks into its neighbours, and Hann's first sidelobe is only
    // about -31 dB — far above the aliasing being measured, so leakage would be
    // reported as aliasing. (It was: a pure sine measured -47 dBc at 110 Hz,
    // which lands on bin 37.55, while the same sine measured -112 dBc at 3000 Hz,
    // which lands exactly on bin 1024.) Blackman-Harris sidelobes are near
    // -92 dB, well below the threshold under test.
    juce::dsp::WindowingFunction<float> window (static_cast<std::size_t> (fftSize),
                                                juce::dsp::WindowingFunction<float>::blackmanHarris);
    window.multiplyWithWindowingTable (samples.data(), static_cast<std::size_t> (fftSize));

    juce::dsp::FFT fft (fftOrder);
    fft.performFrequencyOnlyForwardTransform (samples.data());

    // Normalise to the loudest bin, which for these signals is the fundamental.
    float loudest = 0.0f;

    for (int bin = 1; bin < fftSize / 2; ++bin)
        loudest = std::max (loudest, samples[static_cast<std::size_t> (bin)]);

    std::vector<float> decibels (static_cast<std::size_t> (fftSize / 2), -200.0f);

    if (loudest <= 0.0f)
        return decibels;

    for (int bin = 0; bin < fftSize / 2; ++bin)
        decibels[static_cast<std::size_t> (bin)] =
            juce::Decibels::gainToDecibels (samples[static_cast<std::size_t> (bin)] / loudest, -200.0f);

    return decibels;
}

/** @returns the loudest non-harmonic bin, in dB relative to the fundamental.

    Bins within `binTolerance` of a true harmonic are excluded, as is the DC
    region, so what remains is aliasing and numerical noise.
*/
[[nodiscard]] float measureWorstAlias (const std::vector<float>& spectrumDb, double fundamentalHz)
{
    const auto binsPerHz = static_cast<double> (fftSize) / testSampleRate;

    // Wide enough to contain the window's main lobe. Blackman-Harris spreads a
    // tone across roughly eight bins, so anything narrower would read the
    // window's own shape as aliasing.
    constexpr int binTolerance = 10;

    float worst = -200.0f;

    for (int bin = 8; bin < static_cast<int> (spectrumDb.size()); ++bin)
    {
        const auto frequency = static_cast<double> (bin) / binsPerHz;

        // Distance, in bins, to the closest true harmonic of the fundamental.
        const auto harmonic = std::round (frequency / fundamentalHz);

        if (harmonic >= 1.0)
        {
            const auto harmonicBin = harmonic * fundamentalHz * binsPerHz;

            if (std::abs (static_cast<double> (bin) - harmonicBin) <= binTolerance)
                continue;
        }

        worst = std::max (worst, spectrumDb[static_cast<std::size_t> (bin)]);
    }

    return worst;
}

class WavetableTests final : public juce::UnitTest
{
public:
    WavetableTests()
        : juce::UnitTest ("Wavetable oscillator", "DSP")
    {
    }

    void runTest() override
    {
        testMipLevelSelection();
        testTablesAreBuiltAndBounded();
        testPitchAccuracy();
        testAntiAliasingAcrossTheRange();
        testAliasingWhileScanning();
        testFrameScanningIsSmooth();
        testTableSelectionChangesTimbre();
        testInterpolationIsAccurate();
        testDegenerateInputIsSafe();
    }

private:
    WavetableLibrary library;

    /** The mipmap is the anti-aliasing mechanism, so its selection rule is
        worth pinning directly rather than only through its audible effect.
    */
    void testMipLevelSelection()
    {
        beginTest ("Mip level selection keeps every harmonic below Nyquist");

        for (const double frequency : { 20.0, 55.0, 110.0, 440.0, 1000.0, 4186.0, 10000.0 })
        {
            const auto level = Wavetable::selectMipLevel (frequency, testSampleRate);

            expect (level >= 0 && level < Wavetable::numMipLevels,
                    "level out of range at " + juce::String (frequency) + " Hz");

            const auto harmonics = Wavetable::harmonicsAtLevel (level);
            const auto highest = frequency * static_cast<double> (harmonics);

            expect (highest <= testSampleRate * 0.5 + 1.0,
                    "at " + juce::String (frequency) + " Hz the chosen level keeps "
                        + juce::String (harmonics) + " harmonics, reaching "
                        + juce::String (highest, 0) + " Hz, above Nyquist");
        }

        // Higher notes must never select a more detailed level than lower ones.
        int previous = -1;

        for (int note = 12; note <= 120; note += 6)
        {
            const auto frequency = 440.0 * std::pow (2.0, (note - 69) / 12.0);
            const auto level = Wavetable::selectMipLevel (frequency, testSampleRate);

            expect (level >= previous, "mip level went down as pitch went up");
            previous = level;
        }
    }

    void testTablesAreBuiltAndBounded()
    {
        beginTest ("Every built-in table is populated and normalised");

        for (int index = 0; index < WavetableLibrary::numTables; ++index)
        {
            const auto& table = library.getTable (index);

            expect (! table.isEmpty(), "table " + juce::String (index) + " is empty");
            expectEquals (table.getNumFrames(), WavetableLibrary::framesPerTable);

            for (int frame = 0; frame < table.getNumFrames(); ++frame)
            {
                for (int level = 0; level < Wavetable::numMipLevels; ++level)
                {
                    const auto* data = table.getReadPointer (level, frame);
                    expect (data != nullptr);

                    if (data == nullptr)
                        continue;

                    float peak = 0.0f;
                    bool allFinite = true;

                    for (int i = 0; i < Wavetable::samplesAtLevel (level); ++i)
                    {
                        peak = std::max (peak, std::abs (data[i]));
                        allFinite = allFinite && std::isfinite (data[i]);
                    }

                    expect (allFinite, "table " + juce::String (index) + " level "
                                           + juce::String (level) + " contains non-finite data");

                    // Normalisation targets unity at the top level; lower levels
                    // hold fewer harmonics and so peak at or below it.
                    expect (peak <= 1.001f,
                            "table " + juce::String (index) + " level " + juce::String (level)
                                + " peaks at " + juce::String (peak, 4));

                    expect (peak > 0.0f,
                            "table " + juce::String (index) + " level " + juce::String (level)
                                + " frame " + juce::String (frame) + " is silent");
                }
            }
        }
    }

    void testPitchAccuracy()
    {
        beginTest ("The oscillator reproduces the requested frequency");

        for (const double frequency : { 55.0, 220.0, 440.0, 1760.0 })
        {
            const auto spectrum = renderSpectrum (library.getTable (0), frequency, 1.0f);

            // The loudest bin should sit at the fundamental.
            int loudestBin = 0;
            float loudest = -200.0f;

            for (int bin = 1; bin < static_cast<int> (spectrum.size()); ++bin)
            {
                if (spectrum[static_cast<std::size_t> (bin)] > loudest)
                {
                    loudest = spectrum[static_cast<std::size_t> (bin)];
                    loudestBin = bin;
                }
            }

            const auto binWidth = testSampleRate / static_cast<double> (fftSize);
            const auto measured = static_cast<double> (loudestBin) * binWidth;

            // Tolerance is bounded by the FFT's own resolution, not by a
            // percentage: at 55 Hz one bin is 2.9 Hz, so a 1% tolerance would be
            // demanding precision the measurement cannot deliver. Pitch accuracy
            // to a fraction of a semitone is asserted separately, and far more
            // sharply, by the zero-crossing test in the engine suite.
            expect (std::abs (measured - frequency) <= binWidth * 1.5,
                    "requested " + juce::String (frequency, 1) + " Hz, measured "
                        + juce::String (measured, 1) + " Hz (bin width "
                        + juce::String (binWidth, 2) + " Hz)");
        }
    }

    /** The headline Phase 4 requirement. A naive wavetable read would alias
        badly at high pitches, so the range is swept from the bottom of the
        keyboard to well above the top.
    */
    void testAntiAliasingAcrossTheRange()
    {
        beginTest ("Aliasing stays below the threshold across the pitch range");

        // The saw end of table 0 — the brightest content available, and so the
        // hardest case for aliasing.
        const auto& table = library.getTable (0);

        struct Note { const char* name; double frequency; };

        static constexpr Note notes[] {
            { "A0 (lowest)",   27.5 },
            { "A1",            55.0 },
            { "A2",           110.0 },
            { "A3",           220.0 },
            { "A4",           440.0 },
            { "A5",           880.0 },
            { "A6",          1760.0 },
            { "A7",          3520.0 },
            { "C8",          4186.0 },
            { "above range", 8000.0 }
        };

        for (const auto& note : notes)
        {
            const auto spectrum = renderSpectrum (table, note.frequency, 1.0f);
            const auto worstAlias = measureWorstAlias (spectrum, note.frequency);

            logMessage ("    " + juce::String (note.name).paddedRight (' ', 14)
                        + juce::String (note.frequency, 1).paddedLeft (' ', 8) + " Hz   worst alias "
                        + juce::String (worstAlias, 1) + " dBc");

            expect (worstAlias < maxAliasDecibels,
                    juce::String (note.name) + ": worst alias " + juce::String (worstAlias, 1)
                        + " dBc exceeds the " + juce::String (maxAliasDecibels, 0) + " dBc budget");
        }
    }

    /** Scanning changes the harmonic content, so aliasing has to hold across
        positions, not just at the extremes.
    */
    void testAliasingWhileScanning()
    {
        beginTest ("Aliasing stays below the threshold at every scan position");

        for (int tableIndex = 0; tableIndex < WavetableLibrary::numTables; ++tableIndex)
        {
            const auto& table = library.getTable (tableIndex);

            for (const float position : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                const auto spectrum = renderSpectrum (table, 1000.0, position);
                const auto worstAlias = measureWorstAlias (spectrum, 1000.0);

                expect (worstAlias < maxAliasDecibels,
                        "table " + juce::String (tableIndex) + " position "
                            + juce::String (position, 2) + ": worst alias "
                            + juce::String (worstAlias, 1) + " dBc");
            }
        }
    }

    /** Scanning must not produce steps. A discontinuity as frames cross would
        be audible as a click on an automated or modulated position.
    */
    void testFrameScanningIsSmooth()
    {
        beginTest ("Sweeping the scan position produces no discontinuity");

        const auto& table = library.getTable (0);

        WavetableOscillator oscillator;
        oscillator.setSampleRate (testSampleRate);
        oscillator.setTable (&table);
        oscillator.setFrequency (220.0);
        oscillator.resetPhase (0.0);

        constexpr int numSamples = 48000;
        std::vector<float> rendered (static_cast<std::size_t> (numSamples), 0.0f);

        for (int i = 0; i < numSamples; ++i)
        {
            // A full sweep across the table during the render.
            oscillator.setPosition (static_cast<float> (i) / static_cast<float> (numSamples - 1));
            rendered[static_cast<std::size_t> (i)] = oscillator.getNextSample();
        }

        const auto largestStepOf = [] (const std::vector<float>& data)
        {
            float largest = 0.0f;

            for (std::size_t i = 1; i < data.size(); ++i)
                largest = std::max (largest, std::abs (data[i] - data[i - 1]));

            return largest;
        };

        // Measured against the waveform's own slope, not an absolute number. A
        // band-limited saw is *supposed* to move fast — its transition spans a
        // few samples — so an absolute threshold would only measure how bright
        // the table is. What matters is whether sweeping adds a discontinuity
        // beyond what holding a fixed position already produces.
        float largestStaticStep = 0.0f;

        for (const float position : { 0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f })
        {
            WavetableOscillator fixedOscillator;
            fixedOscillator.setSampleRate (testSampleRate);
            fixedOscillator.setTable (&table);
            fixedOscillator.setFrequency (220.0);
            fixedOscillator.setPosition (position);
            fixedOscillator.resetPhase (0.0);

            std::vector<float> staticRender (static_cast<std::size_t> (numSamples), 0.0f);

            for (int i = 0; i < numSamples; ++i)
                staticRender[static_cast<std::size_t> (i)] = fixedOscillator.getNextSample();

            largestStaticStep = std::max (largestStaticStep, largestStepOf (staticRender));
        }

        const auto sweptStep = largestStepOf (rendered);

        expect (sweptStep <= largestStaticStep * 1.10f,
                "sweeping the scan position added a discontinuity: swept step "
                    + juce::String (sweptStep, 4) + " against a static maximum of "
                    + juce::String (largestStaticStep, 4));

        for (const auto sample : rendered)
            expect (std::isfinite (sample), "scanning produced non-finite output");
    }

    void testTableSelectionChangesTimbre()
    {
        beginTest ("Different tables produce different spectra");

        const auto spectrumOf = [this] (int index)
        {
            return renderSpectrum (library.getTable (index), 440.0, 1.0f);
        };

        const auto sawEnd = spectrumOf (0);   // sine -> saw
        const auto squareEnd = spectrumOf (1); // sine -> square

        // A square has no even harmonics; a saw has them all. Compare the second
        // harmonic, which is the clearest discriminator.
        const auto binOfHarmonic = [] (int harmonic)
        {
            return static_cast<int> (std::round (440.0 * harmonic * static_cast<double> (fftSize)
                                                 / testSampleRate));
        };

        const auto sawSecond = sawEnd[static_cast<std::size_t> (binOfHarmonic (2))];
        const auto squareSecond = squareEnd[static_cast<std::size_t> (binOfHarmonic (2))];

        expect (sawSecond > squareSecond + 20.0f,
                "a saw should have far more second harmonic than a square: saw "
                    + juce::String (sawSecond, 1) + " dBc, square "
                    + juce::String (squareSecond, 1) + " dBc");
    }

    /** Interpolation error shows up as a noise floor under a pure tone, so the
        sine end of a table is the cleanest way to measure it.
    */
    void testInterpolationIsAccurate()
    {
        beginTest ("Interpolation error stays below the aliasing budget");

        // Position 0 of table 0 is a pure sine, so anything else in the spectrum
        // is interpolation error.
        for (const double frequency : { 110.0, 440.0, 1000.0, 3000.0 })
        {
            const auto spectrum = renderSpectrum (library.getTable (0), frequency, 0.0f);
            const auto worst = measureWorstAlias (spectrum, frequency);

            logMessage ("    sine at " + juce::String (frequency, 0).paddedLeft (' ', 6)
                        + " Hz   interpolation floor " + juce::String (worst, 1) + " dBc");

            expect (worst < maxAliasDecibels,
                    "interpolation floor at " + juce::String (frequency, 0) + " Hz was "
                        + juce::String (worst, 1) + " dBc");
        }
    }

    void testDegenerateInputIsSafe()
    {
        beginTest ("Degenerate input produces silence rather than misbehaviour");

        WavetableOscillator oscillator;
        oscillator.setSampleRate (testSampleRate);

        // No table assigned.
        oscillator.setFrequency (440.0);
        expectEquals (oscillator.getNextSample(), 0.0f, "an oscillator with no table must be silent");

        oscillator.setTable (&library.getTable (0));

        // Zero, negative and absurd frequencies must all stay finite.
        for (const double frequency : { 0.0, -440.0, 1.0e9, testSampleRate })
        {
            oscillator.setFrequency (frequency);

            for (int i = 0; i < 256; ++i)
                expect (std::isfinite (oscillator.getNextSample()),
                        "non-finite output at " + juce::String (frequency) + " Hz");
        }

        // Out-of-range positions clamp rather than read out of bounds.
        oscillator.setFrequency (440.0);

        for (const float position : { -5.0f, -0.001f, 1.001f, 5.0f })
        {
            oscillator.setPosition (position);

            for (int i = 0; i < 256; ++i)
                expect (std::isfinite (oscillator.getNextSample()),
                        "non-finite output at position " + juce::String (position));
        }
    }
};

WavetableTests wavetableTests;

} // namespace
