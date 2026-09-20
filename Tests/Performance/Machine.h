#pragma once

/*
    The measurement environment.

    WHY THIS EXISTS. Phase 10c's first requirement is not another number — it is
    a machine you can believe. PROJECT-STATE §5b records what happens without
    one: the same binary measured the reverb at 1.36 % and then at 0.37 %, two
    *idle* runs minutes apart disagreed by forty per cent, and in one of them
    hard clipping came out more expensive than `tanh`, which cannot be true. A
    benchmark that reports a number it cannot reproduce is worse than no
    benchmark, because somebody will optimise against the noise.

    WHAT DRIFTS, AND WHAT CAN BE DONE ABOUT EACH.

      - **The clock speed.** A cool laptop boosts; a warm one throttles, and the
        ratio between the two is the forty per cent. Nothing in user space can
        stop this. What it *can* do is measure it, which is what the reference
        kernel below is for: a fixed amount of arithmetic, timed beside every
        measurement, so a figure can be corrected back to a known speed.
      - **Which core the work lands on.** Modern processors do not have
        identical cores, and a thread that migrates between a performance core
        and an efficiency one changes speed by more than anything Apollo could
        do to itself. The measuring thread is pinned.
      - **Everything else on the machine.** A browser, an indexer, a virus
        scanner. This cannot be fixed either, but its signature is a *slow*
        outlier rather than a fast one, so the distribution is reported and the
        median used, instead of reporting a mean that one interruption ruins.

    NONE OF THIS MAKES A LAPTOP A MEASUREMENT INSTRUMENT. It makes one that
    tells you when it is not being one, which is the honest version. Every run
    reports how far the reference kernel wandered, and a run that wandered too
    far says so at the top instead of quietly producing numbers.
*/

#include <cstddef>
#include <vector>

namespace apollo::benchmarks
{

/** A measurement is a distribution, not a number.

    Reporting one figure hides the thing a reader most needs — whether 2.8 means
    2.8 ± 0.1 or 2.8 ± 1.2. §5b had to carry a paragraph of prose saying "read
    anything under ten per cent as unchanged" because the figures themselves
    could not say it.
*/
struct Statistics
{
    double median = 0.0;
    double best = 0.0;
    double worst = 0.0;

    /** (worst - best) / median: how much this measurement moved while it was
        being taken. Small is trustworthy.
    */
    double spread = 0.0;

    int samples = 0;
};

/** @returns the distribution of @p values. Takes a copy, because it sorts. */
[[nodiscard]] Statistics statisticsOf (std::vector<double> values);

//==============================================================================

/** A fixed amount of arithmetic, unchanging for the life of the project.

    The chain is deliberately **dependent** — each step needs the previous one's
    result — so it is latency-bound rather than throughput-bound, which is what a
    filter's difference equation is too. A kernel the compiler could vectorise
    into wide independent lanes would track the machine's peak throughput, and
    peak throughput is not what a synthesiser is limited by.

    It also walks a small table, so the figure moves with the cache as well as
    with the arithmetic units. That is imperfect: memory speed and core clock do
    not throttle together, so correcting a memory-bound workload by a partly
    compute-bound reference is an approximation. It is a far better one than
    assuming the machine did not move.

    @returns a value, so that no compiler can decide the work was pointless.
*/
[[nodiscard]] double referenceKernel();

/** A second reference, bound by **memory** rather than by arithmetic.

    WHY A SECOND ONE EXISTS (Phase 10d-3, ADR-0072). The kernel above walks a
    32 KB table that never leaves L1, so what it measures is very nearly the
    core clock. Apollo's voice engine does not look like that: one wavetable is
    about 480 KB across its eleven mip levels, four of them are about 2 MB
    together, and 1088 oscillators read them at scattered levels, frames and
    phases. That workload lives in L2 and L3, and **memory latency does not
    throttle with the core clock**.

    Correcting one by the other therefore overshoots, and it was measured
    overshooting. Across Phase 10d-2's runs the *normalised* heaviest-patch
    figure varied from 83 to 131 while the *raw* figure varied only from 84.7 to
    104.5 — normalisation turned a 23 % spread into 58 %, which is worse than
    not correcting at all. The header above had predicted exactly this
    ("correcting a memory-bound workload by a partly compute-bound reference is
    an approximation"); 10d-2 found out how bad the approximation is.

    So this kernel is deliberately shaped like the read it has to stand in for:
    a **dependent chase** through a 2 MB table — the size of Apollo's built-in
    wavetable library — reading four neighbouring entries at each stop, which is
    the width of the interpolator's taps. The next location depends on what was
    just read, so the loads cannot be issued ahead of one another and the figure
    is latency at that size rather than peak bandwidth.

    It is a frozen synthetic analogue and never the real read, so optimising the
    oscillator cannot move the reference it is normalised against.

    @returns a value, so that no compiler can decide the work was pointless.
*/
[[nodiscard]] double memoryKernel();

/** Which reference a row should be corrected by.

    Declared per row rather than guessed once, because the report contains both
    kinds of work and a single answer is wrong for half of it.
*/
enum class Reference
{
    /** Arithmetic in registers and small tables: LFO shapes, waveshapers,
        oversampling filters, biquads.
    */
    compute,

    /** Work whose cost is dominated by reaching memory: wavetable reads, delay
        lines, visualisation buffers.
    */
    memory
};

/** The reference machine: one whose reference unit takes three and a half
    milliseconds.

    **This is a unit, chosen arbitrarily and then fixed, not a claim about
    hardware.** It is deliberately a round number rather than a figure measured
    from some particular laptop on some particular afternoon, because nothing
    about the scale matters — only that it never moves. Normalised figures are
    "per cent of one core, on a machine running the reference at this speed", and
    that is comparable between runs, between phases and between machines in the
    way a raw percentage is not.

    The development laptop sits near this: its reference unit was measured
    between 3.3 and 4.8 ms depending on how warm it was, which is the drift this
    whole mechanism exists to divide out.

    Changing this number reprices every normalised figure ever recorded, so it
    does not change.
*/
inline constexpr double nominalReferenceSeconds = 0.0035;

/** The same convention for the memory reference: a machine whose memory unit
    takes three and a half milliseconds.

    A separate constant rather than the same one, because the two kernels are
    different amounts of work and there is no reason for them to coincide. Like
    the one above it is a fixed unit rather than a measurement, and changing it
    reprices every memory-normalised figure ever recorded.
*/
inline constexpr double nominalMemoryReferenceSeconds = 0.0035;

//==============================================================================

/** What this run's figures are worth. */
struct Stability
{
    /** Full range of the reference across the settling bursts, as a fraction of
        the median. Includes the single worst interruption, so it is the wrong
        thing to gate on and a useful thing to see.
    */
    double spread = 0.0;

    /** How much slower the typical burst was than the fastest one.

        **This is what the gate uses**, and the distinction is not pedantry. The
        range says how bad the worst moment was; this says whether the machine
        was being interfered with *most of the time*. A report's rows are medians
        of five long passes, so they shrug off one bad moment and they cannot
        shrug off steady contention — so the statistic that predicts whether the
        rows will reproduce is this one, not the range.

        Gating on the range instead was measurably wrong: it condemned every run
        on the development laptop at 13-32 %, while the rows in those same runs
        reproduced to within 3.5 % between reports.
    */
    double typicalSlowdown = 0.0;

    /** What one reference unit cost here, in seconds. */
    double referenceSeconds = 0.0;

    /** How fast this machine is against the reference machine. Greater than one
        means faster, so a raw percentage measured here is optimistic.

        This is the **compute** speed, and it is the right divisor only for rows
        whose cost is arithmetic. See `memorySpeed`.
    */
    double speed = 1.0;

    /** What one memory reference unit cost here, in seconds. */
    double memoryReferenceSeconds = 0.0;

    /** How fast this machine's *memory path* is against the reference machine.

        Corrects the rows that spend their time reaching memory rather than
        computing. The two speeds do not move together — that is the entire
        reason both are measured — and how far apart they are on a given run is
        reported, because it is the uncertainty every normalised figure carries.
    */
    double memorySpeed = 1.0;

    /** How much slower the machine became between the start of the report and
        the end of it, as a fraction.

        Filled in by `reassessDrift()` after the last section. The original
        assessment happens before any measuring and the report takes a minute
        and a half of solid work, so a laptop really is slower by the end. This
        does not *correct* anything — per-row correction was tried and made
        matters worse (see `machineSpeed` in Benchmarks.cpp) — it says how much
        of the report's spread is the machine cooling off rather than the code.

        Positive means the machine slowed down over the run.
    */
    double driftAcrossRun = 0.0;

    /** False when the machine moved too much for its absolute figures to mean
        anything. Ratios measured alternately are still usable.
    */
    bool settled = false;
};

/** Pins the measuring thread, raises its priority and warms the caches.

    Called once before any measurement. Every part of it is allowed to fail
    quietly — an operating system is entitled to refuse both requests, and a
    benchmark that would not run without them would be a benchmark that does not
    run on somebody's machine.
*/
/** What the warm-up had to do to reach a speed this machine can hold. */
struct WarmUp
{
    /** How long it took. A machine that does not throttle exits quickly; a
        15 W laptop takes the better part of half a minute.
    */
    double seconds = 0.0;

    /** How far the clock fell while warming, as a fraction of where it started.

        On a part that sustains its clock this is near zero. On the development
        laptop it is large, and that is the figure explaining why every earlier
        phase's normalised numbers would not transport between runs.
    */
    double slowdown = 0.0;

    /** False when the warm-up hit its time limit without the machine settling,
        which means the figures that follow were still taken on a moving clock.
    */
    bool reachedFloor = false;
};

WarmUp prepareMachine();

/** Times the reference kernel repeatedly and reports how steady the machine is.

    Called after `prepareMachine()` and before the report. The first passes are
    discarded: they are the ones that pay for the clock ramping up.
*/
[[nodiscard]] Stability assessMachine();

/** Re-times both references after the report and fills in `driftAcrossRun`.

    Called once, at the end. The steadiness figures in `assessMachine` are taken
    before any measuring, and Phase 10d-2 found a run that reported 2.7 %
    contention at the start and then produced a 435 % worst-case callback block
    minutes later, with every other row degraded too — a report that says the
    machine was quiet when it was not is worse than one that says nothing.

    Cheaper than the opening assessment on purpose: this is a second opinion at
    the other end of the run, not a second full measurement, and the figure it
    produces is reported rather than used as a divisor.
*/
void reassessDrift (Stability& stability);

//==============================================================================

/** @returns the process's resident footprint in bytes, or 0 where the platform
    does not offer one.

    Resident rather than reserved: what is actually in memory is the figure a
    user's machine feels, and a session holding twenty instances of a plugin is
    the case that matters. Zero means "not available here" rather than "nothing",
    and the report says so rather than printing a zero as though it were a
    measurement.
*/
[[nodiscard]] std::size_t footprintBytes();

} // namespace apollo::benchmarks
