#include "Performance/Machine.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>

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

void prepareMachine()
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

    // Warm the caches and give the clock a reason to ramp before anything is
    // timed. The first reference pass on a cold machine can take half again
    // what the settled ones take.
    for (int pass = 0; pass < 8; ++pass)
        sink = referenceKernel();
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

    // Five per cent of steady contention. Above that, something else is using
    // this machine consistently enough that the rows will not reproduce.
    stability.settled = stability.typicalSlowdown <= 0.05;

    return stability;
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
