#include "DSP/Oscillators/WavetableLibrary.h"

#include "DSP/Oscillators/WavetableBuilder.h"
#include "DSP/Utilities/Fft.h"

#include <algorithm>
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

/** @returns @p frame as a position in [0, 1] across @p numFrames. */
[[nodiscard]] double frameAmount (int frame, int numFrames) noexcept
{
    return numFrames > 1 ? static_cast<double> (frame) / static_cast<double> (numFrames - 1)
                         : 0.0;
}

//==============================================================================
// SWEEP — a saw that grows one harmonic at a time.
//
// The obvious way to write "sine to saw" is to crossfade a sine against a saw,
// and it is wrong: the middle frames are a loud fundamental with a faint saw
// underneath, which is a sine with a buzz rather than a brighter tone. What a
// player expects is the *bandwidth* opening — every frame a real saw, each one
// carrying more harmonics than the last.
//
// The knee moves exponentially because pitch is exponential: a knee travelling
// linearly would spend half the table between the top two octaves and cross the
// musically interesting bottom in its first two frames.

[[nodiscard]] FrameSpectrum makeSweepFrame (double amount)
{
    auto spectrum = makeSilentSpectrum (harmonics);

    // From the fundamental alone to the full 1024 harmonics.
    const auto knee = std::pow (static_cast<double> (harmonics), amount);

    for (int k = 1; k <= harmonics; ++k)
    {
        const auto harmonic = static_cast<double> (k);

        // A soft shoulder rather than a hard cut: a brick wall that stepped
        // from one harmonic to the next would make scanning the table click
        // once per harmonic at the bottom of its travel.
        const auto rolloff = 1.0 / (1.0 + std::pow (harmonic / knee, 6.0));

        spectrum[static_cast<std::size_t> (k - 1)].sine = rolloff / harmonic;
    }

    return spectrum;
}

//==============================================================================
// PULSE — a square wave whose width narrows across the table.
//
// The classic modulation, stored rather than modulated: frame 0 is a square and
// the last frame is a narrow spike. Pulse width is the one shape that cannot be
// reached by blending two others at all, because every frame is a different
// waveform rather than a mixture of two.
//
// Cosine phase, which is what centres the pulse on the frame. The DC term the
// series carries is dropped: a pulse's average is its duty cycle, and an
// oscillator that carried it would push the voice off centre by an amount that
// changed as the table was scanned.

[[nodiscard]] FrameSpectrum makePulseFrame (double amount)
{
    auto spectrum = makeSilentSpectrum (harmonics);

    // Half down to a twentieth. Narrower than that is mostly silence with a
    // click in it, and it loses level faster than it gains character.
    const auto duty = 0.5 - 0.45 * amount;

    for (int k = 1; k <= harmonics; ++k)
    {
        const auto harmonic = static_cast<double> (k);

        spectrum[static_cast<std::size_t> (k - 1)].cosine =
            2.0 * std::sin (harmonic * pi * duty) / (harmonic * pi);
    }

    return spectrum;
}

//==============================================================================
// FORMANT — a saw under a resonant peak that climbs the spectrum.
//
// Somewhere between a vowel and a filter sweep baked into the table. The peak
// is Gaussian on a log-frequency axis, because that is what a resonance is when
// you plot it the way a musician hears it; a peak that was Gaussian in linear
// frequency would be a wide smear at the top and a spike at the bottom.
//
// The saw underneath keeps the low harmonics present at every position, so the
// table scans as a colour changing rather than as a note appearing and
// disappearing.

[[nodiscard]] FrameSpectrum makeFormantFrame (double amount)
{
    auto spectrum = makeSilentSpectrum (harmonics);

    // The peak climbs from the second harmonic to the fortieth, which is about
    // 100 Hz to 2 kHz over a bass note and covers the range vowels live in.
    const auto centre = 2.0 * std::pow (20.0, amount);
    const auto width = 0.45;

    for (int k = 1; k <= harmonics; ++k)
    {
        const auto harmonic = static_cast<double> (k);

        const auto distance = std::log (harmonic / centre);
        const auto resonance = std::exp (-(distance * distance) / (2.0 * width * width));

        // The 0.25 is the saw that stays underneath; the peak adds to it rather
        // than replacing it.
        spectrum[static_cast<std::size_t> (k - 1)].sine =
            (0.25 + resonance) / harmonic;
    }

    return spectrum;
}

//==============================================================================
// FOLD — a sine driven into a wavefolder, harder with every frame.
//
// This one is defined in the time domain because that is the only place a
// wavefolder means anything: it is a shape, not a spectrum, and its harmonics
// are whatever falls out. So the frame is drawn as samples and then analysed
// (Fft.h), which is the same route a wavetable loaded from a file takes into
// the engine.
//
// Folding rather than clipping, because a clipped sine is a square with extra
// steps. Folding turns each fold into a new pair of partials, so the spectrum
// grows sideways as well as upwards and the table has somewhere to go that the
// other three do not.

[[nodiscard]] FrameSpectrum makeFoldFrame (double amount)
{
    constexpr auto cycleLength = static_cast<std::size_t> (Wavetable::topLevelSamples);

    std::vector<double> cycle (cycleLength, 0.0);

    // One at the start, so frame 0 is an unfolded sine and the table begins
    // somewhere recognisable.
    const auto drive = 1.0 + 6.0 * amount;

    for (std::size_t i = 0; i < cycleLength; ++i)
    {
        const auto phase = twoPi * static_cast<double> (i) / static_cast<double> (cycleLength);

        // A triangle fold: everything outside [-1, 1] is reflected back in, as
        // many times as it takes. `std::asin (std::sin (x))` is that reflection
        // written in one line, and it is exact rather than iterative.
        cycle[i] = std::asin (std::sin (pi * 0.5 * drive * std::sin (phase))) / (pi * 0.5);
    }

    const auto analysed = analyseCycle (cycle, harmonics);

    auto spectrum = makeSilentSpectrum (harmonics);

    for (std::size_t k = 0; k < spectrum.size() && k < analysed.size(); ++k)
        spectrum[k] = { analysed[k].real(), analysed[k].imag() };

    return spectrum;
}

//==============================================================================

/** Every table Apollo is built with, shared by every instance of the plugin. */
struct BuiltInTables
{
    std::array<std::shared_ptr<const Wavetable>,
               static_cast<std::size_t> (WavetableLibrary::numTables)> tables;

    std::shared_ptr<const Wavetable> sub;
};

/** Builds a table by asking @p makeFrame for each frame's spectrum. */
template <typename MakeFrame>
void buildTable (Wavetable& table, int numFrames, MakeFrame&& makeFrame)
{
    TableSpectrum spectrum;
    spectrum.reserve (static_cast<std::size_t> (numFrames));

    for (int frame = 0; frame < numFrames; ++frame)
        spectrum.push_back (makeFrame (frameAmount (frame, numFrames)));

    buildWavetable (table, spectrum);
}

/** The built-in tables, built once for the whole process.

    BUILT ONCE, NOT ONCE PER INSTANCE, and the difference is measured rather
    than assumed: rendering the four spectral tables takes a couple of hundred
    milliseconds, and a host that instantiates a plugin forty times while
    scanning its menu would otherwise pay that forty times over — in the
    constructor, where it is time the user watches.

    Sharing is safe because a built-in table is immutable from the moment it
    exists: it is described by a formula, built here, and never written to
    again. Any number of instances read the same bytes with no synchronisation,
    which is the same property that lets every voice share one (Wavetable.h).

    A function-local static, so the work happens the first time an instrument is
    made rather than during static initialisation, and so C++ guarantees exactly
    one thread does it.
*/
[[nodiscard]] const BuiltInTables& builtInTables()
{
    static const BuiltInTables tables = []
    {
        const std::array<FrameSpectrum (*) (double), static_cast<std::size_t> (
            WavetableLibrary::numTables)> makers {
            makeSweepFrame, makePulseFrame, makeFormantFrame, makeFoldFrame
        };

        BuiltInTables built;

        for (std::size_t i = 0; i < built.tables.size(); ++i)
        {
            auto table = std::make_shared<Wavetable>();

            buildTable (*table, WavetableLibrary::framesPerTable, makers[i]);

            built.tables[i] = std::move (table);
        }

        // The sub oscillator. One frame, one harmonic: there is nothing to scan
        // and nothing that can alias at any pitch or octave transposition.
        auto sub = std::make_shared<Wavetable>();

        buildTable (*sub, WavetableLibrary::subTableFrames, [] (double)
        {
            auto spectrum = makeSilentSpectrum (harmonics);
            spectrum[0].sine = 1.0;

            return spectrum;
        });

        built.sub = std::move (sub);

        return built;
    }();

    return tables;
}

} // namespace

WavetableLibrary::WavetableLibrary()
{
    const auto& shared = builtInTables();

    for (std::size_t i = 0; i < builtIn.size(); ++i)
    {
        builtIn[i] = shared.tables[i];

        current[i].store (builtIn[i].get(), std::memory_order_release);
    }

    subTable = shared.sub;
}

WavetableLibrary::~WavetableLibrary() = default;

int WavetableLibrary::clampIndex (int index) noexcept
{
    return index < 0 ? 0 : (index >= numTables ? numTables - 1 : index);
}

const Wavetable& WavetableLibrary::getSubTable() const noexcept
{
    return *subTable;
}

const Wavetable& WavetableLibrary::getTable (int index) const noexcept
{
    // Acquire, to pair with the release in `publish`: the pointer arriving is
    // not enough, the table it points at has to be visible too.
    return *current[static_cast<std::size_t> (clampIndex (index))].load (std::memory_order_acquire);
}

void WavetableLibrary::retire (std::unique_ptr<Wavetable> previous)
{
    // A built-in is never retired: it stays owned separately so that every slot
    // can fall back to one at any moment. Only a loaded table is ever here.
    if (previous == nullptr)
        return;

    retired.push_back ({ std::move (previous),
                         blockCounter.load (std::memory_order_relaxed) });

}

bool WavetableLibrary::publish (int index, std::unique_ptr<Wavetable> table)
{
    if (index < 0 || index >= numTables || table == nullptr || table->isEmpty())
        return false;

    const auto slot = static_cast<std::size_t> (index);

    // The table the slot was playing, if it was a loaded one. Taken *before*
    // the swap so that ownership moves in one direction and the slot is never
    // pointing at something nobody owns.
    auto previous = std::move (loaded[slot]);

    current[slot].store (table.get(), std::memory_order_release);
    loaded[slot] = std::move (table);

    generation.fetch_add (1, std::memory_order_release);

    retire (std::move (previous));

    return true;
}

void WavetableLibrary::restoreBuiltIn (int index)
{
    if (index < 0 || index >= numTables)
        return;

    const auto slot = static_cast<std::size_t> (index);

    auto previous = std::move (loaded[slot]);

    current[slot].store (builtIn[slot].get(), std::memory_order_release);

    generation.fetch_add (1, std::memory_order_release);

    retire (std::move (previous));
}

bool WavetableLibrary::isReplaced (int index) const noexcept
{
    const auto slot = static_cast<std::size_t> (clampIndex (index));

    return current[slot].load (std::memory_order_acquire) != builtIn[slot].get();
}

int WavetableLibrary::collectRetired()
{
    const auto now = blockCounter.load (std::memory_order_relaxed);

    auto freed = 0;

    for (auto entry = retired.begin(); entry != retired.end();)
    {
        // A table only reaches this list once no slot names it any more, so the
        // single question left is whether the block that was running at the
        // time has finished. Two blocks answers it.
        if (now < entry->retiredAtBlock + retirementBlocks)
        {
            ++entry;
            continue;
        }

        entry = retired.erase (entry);
        ++freed;
    }

    return freed;
}

int WavetableLibrary::getRetiredCount() const
{
    return static_cast<int> (retired.size());
}

} // namespace apollo::dsp
