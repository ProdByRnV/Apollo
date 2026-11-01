#include "DSP/Oscillators/ExtraTables.h"

#include "DSP/Utilities/Fft.h"

#include <array>
#include <cmath>
#include <vector>

namespace apollo::dsp
{

namespace
{

constexpr double pi = 3.14159265358979323846;
constexpr double twoPi = 2.0 * pi;

constexpr int harmonics = Wavetable::topLevelHarmonics;

/** How much longer than a frame a time-domain cycle is drawn before it is
    analysed. A discontinuity's harmonics fall as 1/k, so what folds back from
    above the drawn cycle's own Nyquist limit is sixteen times quieter than the
    highest harmonic a frame keeps. */
constexpr std::size_t oversampledCycle = static_cast<std::size_t> (Wavetable::topLevelSamples) * 16;

[[nodiscard]] double frameAmount (int frame) noexcept
{
    return static_cast<double> (frame) / static_cast<double> (extraTableFrames - 1);
}

/** A cycle drawn by @p shape, a function of phase in [0, 1), analysed. */
template <typename Shape>
[[nodiscard]] FrameSpectrum analyseShape (Shape&& shape)
{
    std::vector<double> cycle (oversampledCycle, 0.0);

    for (std::size_t i = 0; i < cycle.size(); ++i)
        cycle[i] = shape (static_cast<double> (i) / static_cast<double> (cycle.size()));

    const auto analysed = analyseCycle (cycle, harmonics);

    auto spectrum = makeSilentSpectrum (harmonics);

    for (std::size_t k = 0; k < spectrum.size() && k < analysed.size(); ++k)
        spectrum[k] = { analysed[k].real(), analysed[k].imag() };

    return spectrum;
}

//==============================================================================
// ANALOG — sine, triangle, square, saw: the four shapes every analogue
// oscillator has, in one scan.
//
// A third of the table between each pair, blended as spectra. Unlike SWEEP's
// opening bandwidth, a blend is what is wanted here: halfway from a square to
// a saw is a square with even harmonics coming in, which is exactly the sound
// of turning an analogue shape knob.
//
// All four in sine phase with a positive fundamental, so a blend reinforces
// rather than cancels and every frame starts on the same rising zero crossing.

[[nodiscard]] double analogShape (int which, int k)
{
    const auto harmonic = static_cast<double> (k);
    const auto odd = (k % 2) == 1;

    switch (which)
    {
        case 0: return k == 1 ? 1.0 : 0.0;                                                 // sine
        case 1: return odd ? (((k - 1) / 2) % 2 == 0 ? 1.0 : -1.0) / (harmonic * harmonic) : 0.0; // triangle
        case 2: return odd ? 1.0 / harmonic : 0.0;                                          // square
        default: return (k % 2 == 1 ? 1.0 : -1.0) / harmonic;                               // saw
    }
}

[[nodiscard]] FrameSpectrum makeAnalogFrame (double amount)
{
    auto spectrum = makeSilentSpectrum (harmonics);

    const auto scaled = amount * 3.0;
    const auto from = std::min (2, static_cast<int> (std::floor (scaled)));
    const auto blend = scaled - static_cast<double> (from);

    for (int k = 1; k <= harmonics; ++k)
        spectrum[static_cast<std::size_t> (k - 1)].sine =
            (1.0 - blend) * analogShape (from, k) + blend * analogShape (from + 1, k);

    return spectrum;
}

//==============================================================================
// SYNC — a saw hard-synced to the note, its own pitch climbing almost three
// octaves.
//
// The sound of a synced oscillator's pitch being swept, stored rather than
// modulated: the slave restarts at the start of every cycle, so the note stays
// put while the timbre tears upwards. The ratio climbs exponentially, as pitch
// does, so the table spends as long in each octave of the sweep.

[[nodiscard]] FrameSpectrum makeSyncFrame (double amount)
{
    const auto ratio = std::pow (7.5, amount);

    return analyseShape ([ratio] (double phase)
    {
        const auto slave = phase * ratio;
        return 2.0 * (slave - std::floor (slave)) - 1.0;
    });
}

//==============================================================================
// ORGAN — drawbars pulled out one registration at a time.
//
// The drawbars that sound at whole multiples of the note: 8', 4', 2 2/3', 2',
// 1 3/5', 1 1/3' and 1', harmonics 1, 2, 3, 4, 5, 6 and 8. The 16' and 5 1/3'
// sound below and between them, and a table cannot hold either without moving
// its own pitch. Each step of a drawbar is 3 dB, as on the instrument, and out
// is silent.
//
// From a lone flute stop to every drawbar out, then the fundamental eased back
// for the thin, bright end of a registration.

constexpr std::array<int, 7> drawbarHarmonics { 1, 2, 3, 4, 5, 6, 8 };

constexpr std::array<std::array<int, 7>, extraTableFrames> registrations { {
    { 8, 0, 0, 0, 0, 0, 0 },
    { 8, 4, 0, 0, 0, 0, 0 },
    { 8, 6, 0, 2, 0, 0, 0 },
    { 8, 8, 0, 4, 0, 0, 0 },
    { 8, 8, 4, 4, 0, 0, 0 },
    { 8, 8, 6, 6, 0, 0, 2 },
    { 8, 8, 8, 6, 0, 0, 4 },
    { 8, 8, 8, 8, 0, 0, 6 },
    { 8, 8, 8, 8, 2, 2, 8 },
    { 8, 8, 8, 8, 4, 4, 8 },
    { 8, 8, 8, 8, 6, 6, 8 },
    { 8, 8, 8, 8, 8, 8, 8 },
    { 6, 8, 8, 8, 8, 8, 8 },
    { 4, 6, 8, 8, 8, 8, 8 },
    { 3, 4, 6, 8, 8, 8, 8 },
    { 2, 2, 4, 6, 8, 8, 8 },
} };

[[nodiscard]] FrameSpectrum makeOrganFrame (int frame)
{
    auto spectrum = makeSilentSpectrum (harmonics);
    const auto& registration = registrations[static_cast<std::size_t> (frame)];

    for (std::size_t bar = 0; bar < drawbarHarmonics.size(); ++bar)
    {
        const auto setting = registration[bar];

        if (setting <= 0)
            continue;

        spectrum[static_cast<std::size_t> (drawbarHarmonics[bar] - 1)].sine =
            std::pow (10.0, -3.0 * static_cast<double> (8 - setting) / 20.0);
    }

    return spectrum;
}

//==============================================================================
// CHIME — partials at the squares of the harmonic numbers, brightening.
//
// Harmonics 1, 4, 9, 16, 25 … are spread further apart the higher they go,
// which is the spacing a struck bar or a bell has and a string does not. A
// table can only hold whole harmonics, so this is the nearest a periodic wave
// comes to metal: glassy rather than clanging. The partials fall away steeply
// at the start of the table and hardly at all at its end.
//
// Alternating cosine and sine phase, so the partials do not all peak at once
// and the frame is not a single spike with silence around it.

[[nodiscard]] FrameSpectrum makeChimeFrame (double amount)
{
    auto spectrum = makeSilentSpectrum (harmonics);

    const auto slope = 3.0 - 2.5 * amount;

    for (int n = 1; n * n <= harmonics; ++n)
    {
        const auto level = std::pow (static_cast<double> (n), -slope);
        auto& harmonic = spectrum[static_cast<std::size_t> (n * n - 1)];

        if (n % 2 == 1)
            harmonic.sine = level;
        else
            harmonic.cosine = level;
    }

    return spectrum;
}

//==============================================================================
// DIGITAL — a sine with fewer and fewer levels to be drawn with.
//
// Sixty-four steps at the start, which is a sine with a faint grain; two at the
// end, which is a square. The number of levels falls exponentially, so the
// table spends as long between sixty-four and eight as between eight and one
// bit. The staircase is the sound of an early sampler or a game console, and
// the band-limiting is what keeps it from also being the sound of aliasing.

[[nodiscard]] FrameSpectrum makeDigitalFrame (double amount)
{
    const auto levels = std::max (2.0, std::round (64.0 * std::pow (2.0 / 64.0, amount)));
    const auto half = levels * 0.5;

    return analyseShape ([half] (double phase)
    {
        // A mid-rise quantiser, so it has no level at zero and two levels is
        // a square rather than silence with spikes in it.
        const auto value = std::sin (twoPi * phase);
        const auto step = std::min (half - 1.0, std::floor (value * half));
        return (step + 0.5) / half;
    });
}

} // namespace

const char* extraTableName (int index) noexcept
{
    switch (index)
    {
        case 0: return "Analog";
        case 1: return "Sync";
        case 2: return "Organ";
        case 3: return "Chime";
        case 4: return "Digital";
        default: return "";
    }
}

TableSpectrum makeExtraTable (int index)
{
    if (index < 0 || index >= numExtraTables)
        return {};

    TableSpectrum table;
    table.reserve (static_cast<std::size_t> (extraTableFrames));

    for (int frame = 0; frame < extraTableFrames; ++frame)
    {
        const auto amount = frameAmount (frame);

        switch (index)
        {
            case 0: table.push_back (makeAnalogFrame (amount)); break;
            case 1: table.push_back (makeSyncFrame (amount)); break;
            case 2: table.push_back (makeOrganFrame (frame)); break;
            case 3: table.push_back (makeChimeFrame (amount)); break;
            default: table.push_back (makeDigitalFrame (amount)); break;
        }
    }

    return table;
}

} // namespace apollo::dsp
