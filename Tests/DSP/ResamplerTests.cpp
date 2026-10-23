/*
    The offline resampler a loaded noise sample goes through (Phase 13g).

    What it has to do is narrow and checkable: a tone that both rates can hold
    comes out as that tone, sample for sample to within the window's ripple; a
    tone the new rate cannot hold comes out gone rather than folded down; and
    nothing at all happens when the rate is already right.
*/

#include <juce_core/juce_core.h>

#include <cmath>
#include <vector>

#include "DSP/Utilities/Resampler.h"

using namespace apollo::dsp;

namespace
{

constexpr double twoPi = 6.283185307179586476925286766559;

[[nodiscard]] std::vector<double> sine (double frequency, double rate, std::size_t length)
{
    std::vector<double> out (length);

    for (std::size_t n = 0; n < length; ++n)
        out[n] = std::sin (twoPi * frequency * static_cast<double> (n) / rate);

    return out;
}

/** The largest difference from a sine of @p frequency at @p rate, away from
    the ends, where the kernel runs off the signal. */
[[nodiscard]] double worstError (const std::vector<double>& signal, double frequency, double rate)
{
    const auto margin = signal.size() / 8;
    auto worst = 0.0;

    for (auto n = margin; n + margin < signal.size(); ++n)
    {
        const auto ideal = std::sin (twoPi * frequency * static_cast<double> (n) / rate);
        worst = std::max (worst, std::abs (signal[n] - ideal));
    }

    return worst;
}

[[nodiscard]] double middleRms (const std::vector<double>& signal)
{
    const auto margin = signal.size() / 8;
    auto sum = 0.0;
    std::size_t count = 0;

    for (auto n = margin; n + margin < signal.size(); ++n, ++count)
        sum += signal[n] * signal[n];

    return count > 0 ? std::sqrt (sum / static_cast<double> (count)) : 0.0;
}

class ResamplerTests final : public juce::UnitTest
{
public:
    ResamplerTests()
        : juce::UnitTest ("Resampler", "DSP")
    {
    }

    void runTest() override
    {
        beginTest ("The right rate already is left exactly as it was");
        {
            const auto input = sine (1000.0, 48000.0, 1000);
            expect (resample (input, 48000.0, 48000.0) == input);
        }

        beginTest ("The output is as long as the input's duration at the new rate");
        {
            const std::vector<double> input (44100, 0.25);
            expectEquals (static_cast<int> (resample (input, 44100.0, 48000.0).size()), 48000);
            expectEquals (static_cast<int> (resample (input, 44100.0, 22050.0).size()), 22050);
        }

        beginTest ("A tone both rates can hold comes out as that tone");
        {
            struct Case { double from; double to; double frequency; };

            for (const auto& c : { Case { 44100.0, 48000.0, 1000.0 },
                                   Case { 48000.0, 44100.0, 1000.0 },
                                   Case { 96000.0, 48000.0, 5000.0 },
                                   Case { 22050.0, 48000.0, 3000.0 },
                                   Case { 192000.0, 48000.0, 15000.0 } })
            {
                const auto output = resample (sine (c.frequency, c.from, static_cast<std::size_t> (c.from / 4)),
                                              c.from, c.to);
                const auto error = worstError (output, c.frequency, c.to);

                logMessage ("  " + juce::String (c.from) + " to " + juce::String (c.to) + " Hz at "
                            + juce::String (c.frequency) + " Hz: worst error " + juce::String (error, 7));

                expect (error < 1.0e-3, "the tone changed shape: " + juce::String (error, 7));
            }
        }

        beginTest ("A tone the new rate cannot hold is removed rather than folded down");
        {
            // 30 kHz at 96 kHz would alias to 18 kHz at 48 kHz.
            const auto output = resample (sine (30000.0, 96000.0, 48000), 96000.0, 48000.0);
            const auto level = 20.0 * std::log10 (middleRms (output) / std::sqrt (0.5) + 1.0e-30);

            logMessage ("  30 kHz from 96 kHz to 48 kHz: " + juce::String (level, 1) + " dB");
            expect (level < -80.0, "it folded back at " + juce::String (level, 1) + " dB");
        }

        beginTest ("A constant stays at its level");
        {
            const auto output = resample (std::vector<double> (20000, 0.5), 44100.0, 48000.0);
            const auto margin = output.size() / 8;

            for (auto n = margin; n + margin < output.size(); ++n)
                expectWithinAbsoluteError (output[n], 0.5, 1.0e-4);
        }

        beginTest ("A rate that is not a rate gives nothing back");
        {
            const std::vector<double> input (100, 1.0);
            expect (resample (input, 0.0, 48000.0).empty());
            expect (resample (input, 44100.0, -1.0).empty());
            expect (resample (input, std::nan (""), 48000.0).empty());
            expect (resample ({}, 44100.0, 48000.0).empty());
        }
    }
};

ResamplerTests resamplerTests;

} // namespace
