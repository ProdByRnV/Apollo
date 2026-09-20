#include "Performance/Machine.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#if JUCE_WINDOWS
 // Without this, <windows.h> defines `max` and `min` as macros and every
 // `std::max` below stops compiling. Lean, because all this needs from Windows
 // is one function that reports the process's memory.
 #define NOMINMAX
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>
 #include <psapi.h>
#elif JUCE_MAC || JUCE_IOS
 #include <mach/mach.h>
#elif JUCE_LINUX || JUCE_BSD
 #include <cstdio>
 #include <unistd.h>
#endif

namespace apollo::benchmarks
{

namespace
{

/** The reference kernel's table: 4096 doubles, 32 KB, comfortably inside L1.

    Small on purpose. The reference is meant to measure the *machine*, and a
    table that spilled to main memory would make it measure whatever else is
    competing for the last level of cache — which is the noise it exists to
    correct for.
*/
constexpr std::size_t tableSize = 4096;

/** Iterations per reference unit.

    Chosen so one unit lands near three milliseconds: far enough above the
    clock's resolution that the timing is exact, short enough that the settling
    pass and every measurement can afford to run one.
*/
constexpr int referenceIterations = 1'000'000;

[[nodiscard]] const std::array<double, tableSize>& referenceTable()
{
    static const auto table = []
    {
        std::array<double, tableSize> values {};

        // Deterministic and irregular: a fixed generator rather than anything
        // seeded from the clock, so the kernel is the same work every time it
        // runs, on every machine, for ever.
        std::uint32_t state = 0x9E3779B9u;

        for (auto& value : values)
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;

            value = static_cast<double> (static_cast<std::int32_t> (state)) / 2147483648.0;
        }

        return values;
    }();

    return table;
}

/** The memory reference's table: 512K 32-bit entries, 2 MB.

    Sized to match Apollo's built-in wavetable library — four tables of about
    480 KB each across their eleven mip levels — because the point of this
    reference is to sit in the same part of the memory hierarchy as the reads it
    has to correct for.
*/
constexpr std::size_t memoryTableSize = 512 * 1024;

/** Iterations per memory reference unit.

    Chosen the same way as `referenceIterations`: so that one unit lands near
    three and a half milliseconds on the development machine, and then frozen.
    A dependent chase at this size costs on the order of ten nanoseconds a step.
*/
constexpr int memoryIterations = 350'000;

/** A single long cycle through the table.

    Entry `i` holds the index to visit after `i`, and the links form **one**
    cycle covering every entry rather than several short ones — a chase that
    fell into a small loop would sit in cache and measure nothing.

    Built by shuffling the indices with a fixed generator and then linking them
    in shuffled order, which produces a single cycle by construction.
*/
[[nodiscard]] const std::vector<std::uint32_t>& memoryTable()
{
    static const auto table = []
    {
        std::vector<std::uint32_t> order (memoryTableSize);

        for (std::size_t i = 0; i < memoryTableSize; ++i)
            order[i] = static_cast<std::uint32_t> (i);

        // The same xorshift as the arithmetic kernel's table, for the same
        // reason: this must be identical work on every machine, for ever.
        std::uint32_t state = 0x9E3779B9u;

        const auto next = [&state]
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;

            return state;
        };

        // Fisher-Yates over everything but the first entry, so `order` is a
        // random sequence of all the indices.
        for (std::size_t i = memoryTableSize - 1; i > 0; --i)
            std::swap (order[i], order[next() % static_cast<std::uint32_t> (i + 1)]);

        std::vector<std::uint32_t> links (memoryTableSize);

        // Link each visited index to the next one visited, and close the loop.
        for (std::size_t i = 0; i + 1 < memoryTableSize; ++i)
            links[order[i]] = order[i + 1];

        links[order[memoryTableSize - 1]] = order[0];

        return links;
    }();

    return table;
}

/** Somewhere for results to go that the optimiser cannot see through. */
volatile double sink = 0.0;

[[nodiscard]] double secondsFor (auto&& work)
{
    const auto start = std::chrono::steady_clock::now();
    work();
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;

    return elapsed.count();
}

} // namespace

Statistics statisticsOf (std::vector<double> values)
{
    Statistics statistics;

    if (values.empty())
        return statistics;

    std::sort (values.begin(), values.end());

    statistics.samples = static_cast<int> (values.size());
    statistics.best = values.front();
    statistics.worst = values.back();

    const auto middle = values.size() / 2;

    statistics.median = values.size() % 2 == 1
                      ? values[middle]
                      : 0.5 * (values[middle - 1] + values[middle]);

    if (statistics.median > 0.0)
        statistics.spread = (statistics.worst - statistics.best) / statistics.median;

    return statistics;
}

double referenceKernel()
{
    const auto& table = referenceTable();

    auto a = 1.0;
    auto b = 0.5;

    for (int i = 0; i < referenceIterations; ++i)
    {
        // The stride is coprime with the table size, so the walk visits every
        // entry rather than cycling through a handful of cache lines.
        const auto value = table[static_cast<std::size_t> (i * 37) & (tableSize - 1)];

        a = a * 0.9999 + value * b;
        b = b * 0.9998 + a * 1.0e-6;
    }

    return a + b;
}

double memoryKernel()
{
    const auto& links = memoryTable();

    constexpr std::size_t mask = memoryTableSize - 1;

    std::size_t index = 0;
    std::uint64_t accumulator = 0;

    for (int i = 0; i < memoryIterations; ++i)
    {
        // Three neighbours beside the one being chased, which is the shape of
        // the interpolator's four taps: usually one or two cache lines, the
        // same as a wavetable read.
        accumulator += links[(index + 1) & mask];
        accumulator += links[(index + 2) & mask];
        accumulator += links[(index + 3) & mask];

        // The dependent step. The address of the next load is the value of the
        // last one, so the processor cannot run ahead: this is memory latency
        // at two megabytes, which is where the wavetables live.
        index = links[index];
    }

    return static_cast<double> (accumulator) + static_cast<double> (index);
}

WarmUp prepareMachine()
{
    // One core, consistently, and deliberately **not** core 0: on Windows that
    // is where device interrupts are serviced, so it is the one core guaranteed
    // to have someone else's work on it. Core 2 where the machine has one, core
    // 0 where it does not, which is the only case that cannot be avoided.
    //
    // Which core matters far less than the fact that it does not change: a
    // thread that migrates between a performance core and an efficiency core
    // changes speed by more than anything Apollo could do to itself.
    const auto cores = juce::SystemStats::getNumCpus();
    const auto chosen = cores > 2 ? 2 : 0;

    juce::Thread::setCurrentThreadAffinityMask (1u << chosen);

    // Allowed to fail. An operating system may refuse, and a benchmark that
    // would not run without elevated priority is one that does not run on
    // somebody's machine.
    juce::Process::setPriority (juce::Process::HighPriority);

    // Fault in both kernels' tables. The memory one has two megabytes to touch
    // and a permutation to build, and neither should be paid for inside a
    // measurement.
    for (int pass = 0; pass < 8; ++pass)
        sink = referenceKernel();

    for (int pass = 0; pass < 4; ++pass)
        sink = memoryKernel();

    // WARM TO THE THERMAL FLOOR, NOT MERELY TO A WARM CACHE (Phase 10d-3,
    // ADR-0072).
    //
    // This is the correction that matters most in this file, and it came from
    // measuring the thing nothing had measured: how much the machine slows
    // *during* a report. On the development laptop — an i7-1255U, a 15 W part
    // with a 1.7 GHz base clock and a turbo near 4.7 — the answer was up to
    // **68 %**. A run would open at turbo, be assessed at turbo, and reach its
    // last section near base clock.
    //
    // Every normalised figure in the report is divided by a speed measured in
    // the first few seconds, so on such a machine the divisor describes a
    // processor that no longer exists by the time the later rows are taken.
    // That is the real reason normalised figures did not transport between runs
    // (Phase 10d-1's anomaly, and 10d-2's finding that normalising *amplified*
    // a row's spread from 23 % to 58 %). It is not primarily a compute-versus-
    // memory problem, though that is real too and second-order.
    //
    // The fix is to stop measuring a clock the machine cannot hold. Run
    // sustained load until the reference unit stops getting slower, and only
    // then assess and report. Everything that follows is then taken at a speed
    // the machine can sustain, which is also the speed a user's session
    // actually runs at — a synthesiser is not a burst workload.
    //
    // Bounded, because a desktop that never throttles would otherwise warm for
    // ever, and it exits early on exactly that machine.
    constexpr double burstSeconds = 0.3;
    constexpr double maxWarmSeconds = 90.0;
    constexpr double settledWithin = 0.015;

    // A FLOOR ON THE WARM-UP, and it was needed. The first version of this loop
    // had only the settling test below, and it let two runs in three exit while
    // the machine was still boosting: they opened at 1.07x and 1.08x the
    // reference speed and then drifted 16 % and 56 % over the report, while the
    // one run that did warm properly opened at 0.90x and drifted -1 %.
    //
    // The reason is that the settling test is a comparison of noisy medians, and
    // on a ramping machine it is satisfied by chance long before the ramp ends.
    // Forty-five seconds, and the number is not a guess. At twenty-five the
    // warm-up exited at 25.6 s on every run and the report still drifted by up
    // to 56 % afterwards. Intel mobile parts hold their turbo power budget for
    // a Tau of around twenty-eight seconds and only then fall back to the
    // sustained limit, so a warm-up that stops at twenty-five ends immediately
    // before the drop it exists to get past. Forty-five is comfortably beyond
    // it.
    //
    // The two were also compared directly, three passes each, interleaved so
    // both saw the same machine. The heaviest-patch row reproduced between runs
    // to 9 % at forty-five seconds and to 25 % at twenty-five, and its
    // within-run pass spread was 11 % against 16 %. That comparison ran on a
    // badly heat-soaked laptop, so its absolute figures are worthless and only
    // the pairing means anything — but it agrees with the physical argument,
    // which is the most that can be asked of a tie-break.
    constexpr double minWarmSeconds = 45.0;

    const auto oneUnit = secondsFor ([] { sink = referenceKernel(); });
    const auto unitsPerBurst = std::max (1, static_cast<int> (burstSeconds / std::max (oneUnit, 1.0e-9)));

    const auto burst = [unitsPerBurst]
    {
        for (int unit = 0; unit < unitsPerBurst; ++unit)
            sink = referenceKernel();
    };

    std::vector<double> recent;
    auto warmed = 0.0;
    auto confirmations = 0;

    while (warmed < maxWarmSeconds)
    {
        const auto elapsed = secondsFor (burst);

        warmed += elapsed;
        recent.push_back (elapsed / static_cast<double> (unitsPerBurst));

        if (warmed < minWarmSeconds || recent.size() < 8)
            continue;

        // Four bursts against the four before them, rather than three against
        // three: a wider window is harder to satisfy by chance.
        const auto last = recent.end();

        const auto newer = statisticsOf (std::vector<double> (last - 4, last)).median;
        const auto older = statisticsOf (std::vector<double> (last - 8, last - 4)).median;

        // Only *slowing* counts as unsettled. A burst that came out faster than
        // the one before it is the machine recovering or an interruption
        // ending, neither of which is a reason to keep going.
        //
        // Required twice in succession, because once is the coincidence this
        // loop was previously falling for.
        if (older > 0.0 && (newer - older) / older < settledWithin)
        {
            if (++confirmations >= 2)
                break;
        }
        else
        {
            confirmations = 0;
        }
    }

    WarmUp warmUp;
    warmUp.seconds = warmed;
    warmUp.reachedFloor = confirmations >= 2;

    if (! recent.empty() && recent.front() > 0.0)
        warmUp.slowdown = (recent.back() - recent.front()) / recent.front();

    return warmUp;
}

Stability assessMachine()
{
    // THE ASSESSMENT MUST RUN AT THE SAME TIMESCALE AS THE MEASUREMENTS, and
    // getting this wrong is not hypothetical — the first version of this
    // function timed single three-millisecond units and reported a steady 3.9 %
    // while rows in the same report were swinging 26 %. Of course it did: three
    // milliseconds is short enough to slip between two interruptions, and a
    // four-second render is not. A gate that cannot see the interference the
    // rows are subject to is a gate that says yes to everything.
    //
    // So the reference is timed in **bursts** of roughly a third of a second,
    // which is the order of a real measurement pass.
    //
    // The whole report's normalised column is divided by the figure this
    // produces, so it is worth several seconds to get right. Settling covers
    // about a second and a half, which is long enough for the clock to finish
    // ramping, and the measurement twelve bursts after that.
    constexpr double burstSeconds = 0.3;
    constexpr int settlingBursts = 5;
    constexpr int measuredBursts = 12;

    // How many units make a burst, worked out from one timed unit rather than
    // guessed, so the burst is the same *duration* on a fast machine and a slow
    // one instead of the same amount of work.
    const auto oneUnit = secondsFor ([] { sink = referenceKernel(); });
    const auto unitsPerBurst = std::max (1, static_cast<int> (burstSeconds / std::max (oneUnit, 1.0e-9)));

    const auto burst = [unitsPerBurst]
    {
        for (int unit = 0; unit < unitsPerBurst; ++unit)
            sink = referenceKernel();
    };

    // Discarded: these are the ones paying for the clock to come up.
    for (int pass = 0; pass < settlingBursts; ++pass)
        burst();

    std::vector<double> timings;
    timings.reserve (measuredBursts);

    for (int pass = 0; pass < measuredBursts; ++pass)
        timings.push_back (secondsFor (burst) / static_cast<double> (unitsPerBurst));

    const auto statistics = statisticsOf (std::move (timings));

    Stability stability;
    stability.spread = statistics.spread;

    stability.referenceSeconds = statistics.median;

    if (statistics.best > 0.0)
        stability.typicalSlowdown = (statistics.median - statistics.best) / statistics.best;

    if (statistics.median > 0.0)
        stability.speed = nominalReferenceSeconds / statistics.median;

    // The memory reference, timed the same way and at the same timescale. Fewer
    // bursts than the arithmetic one because it is only ever a divisor for the
    // rows that need it, and because the whole assessment already costs several
    // seconds before any Apollo code has run.
    {
        const auto oneMemoryUnit = secondsFor ([] { sink = memoryKernel(); });
        const auto memoryUnitsPerBurst =
            std::max (1, static_cast<int> (burstSeconds / std::max (oneMemoryUnit, 1.0e-9)));

        const auto memoryBurst = [memoryUnitsPerBurst]
        {
            for (int unit = 0; unit < memoryUnitsPerBurst; ++unit)
                sink = memoryKernel();
        };

        memoryBurst();
        memoryBurst();

        std::vector<double> memoryTimings;
        memoryTimings.reserve (8);

        for (int pass = 0; pass < 8; ++pass)
            memoryTimings.push_back (secondsFor (memoryBurst)
                                     / static_cast<double> (memoryUnitsPerBurst));

        const auto memoryStatistics = statisticsOf (std::move (memoryTimings));

        stability.memoryReferenceSeconds = memoryStatistics.median;

        if (memoryStatistics.median > 0.0)
            stability.memorySpeed = nominalMemoryReferenceSeconds / memoryStatistics.median;
    }

    // Five per cent of steady contention. Above that, something else is using
    // this machine consistently enough that the rows will not reproduce.
    stability.settled = stability.typicalSlowdown <= 0.05;

    return stability;
}

void reassessDrift (Stability& stability)
{
    // MEASURED THE SAME WAY THE OPENING ASSESSMENT WAS, and that is the whole
    // design of this function rather than a detail.
    //
    // The first version timed one run of two dozen units — about a tenth of a
    // second — and compared it against an opening figure that was the median of
    // twelve third-of-a-second bursts. That is the asymmetry this file warns
    // about for the per-row case: a single short reading is noisy, and dividing
    // it by a carefully measured one reports the noise as drift. It produced
    // drift figures between 3 % and 55 % on a machine whose actual behaviour
    // could not have varied that much between consecutive runs.
    //
    // So this takes bursts of the same length and reports the median against
    // the median. Fewer of them, because this is a second opinion rather than
    // the divisor for the whole report, but the same shape.
    constexpr double burstSeconds = 0.3;
    constexpr int bursts = 6;

    const auto oneUnit = secondsFor ([] { sink = referenceKernel(); });
    const auto unitsPerBurst = std::max (1, static_cast<int> (burstSeconds / std::max (oneUnit, 1.0e-9)));

    const auto burst = [unitsPerBurst]
    {
        for (int unit = 0; unit < unitsPerBurst; ++unit)
            sink = referenceKernel();
    };

    std::vector<double> timings;
    timings.reserve (bursts);

    for (int pass = 0; pass < bursts; ++pass)
        timings.push_back (secondsFor (burst) / static_cast<double> (unitsPerBurst));

    const auto now = statisticsOf (std::move (timings)).median;

    if (stability.referenceSeconds > 0.0)
        stability.driftAcrossRun = (now - stability.referenceSeconds) / stability.referenceSeconds;

    // A machine that slowed by more than a tenth while the report was being
    // taken did not produce one report; it produced the beginning of one and
    // the end of a different one. That is worth un-settling the run for, and it
    // is a failure mode the opening assessment cannot see by construction.
    if (stability.driftAcrossRun > 0.10)
        stability.settled = false;
}

std::size_t footprintBytes()
{
   #if JUCE_WINDOWS
    PROCESS_MEMORY_COUNTERS counters {};

    // The K32 form is exported from kernel32, so this needs no extra library.
    if (K32GetProcessMemoryInfo (GetCurrentProcess(), &counters, sizeof (counters)))
        return static_cast<std::size_t> (counters.WorkingSetSize);

    return 0;
   #elif JUCE_MAC || JUCE_IOS
    mach_task_basic_info info {};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;

    if (task_info (mach_task_self(), MACH_TASK_BASIC_INFO,
                   reinterpret_cast<task_info_t> (&info), &count) == KERN_SUCCESS)
        return static_cast<std::size_t> (info.resident_size);

    return 0;
   #elif JUCE_LINUX || JUCE_BSD
    // The second field of /proc/self/statm is the resident set, in pages.
    if (auto* file = std::fopen ("/proc/self/statm", "r"))
    {
        long long total = 0;
        long long resident = 0;

        const auto read = std::fscanf (file, "%lld %lld", &total, &resident);
        std::fclose (file);

        if (read == 2 && resident > 0)
            return static_cast<std::size_t> (resident) * static_cast<std::size_t> (getpagesize());
    }

    return 0;
   #else
    return 0;
   #endif
}

} // namespace apollo::benchmarks
