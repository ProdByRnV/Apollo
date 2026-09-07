#pragma once

/*
    Apollo's CPU measurements.

    Deliberately not unit tests, and deliberately in the same binary.

    Not tests, because a CPU number is not a pass or a fail. It depends on the
    machine, the compiler, the build configuration and whatever else the
    operating system is doing, so asserting a threshold on it would produce a
    suite that fails for reasons unrelated to Apollo. Phase 4's exit criterion
    asks for the cost to be *measured*, and the result belongs in
    PROJECT-STATE.md next to the machine it was measured on.

    In the same binary, because a benchmark in a target of its own is a target
    nobody builds, and it rots. This code compiles on every platform on every
    CI run and is simply never executed there: `ctest` runs the suite with no
    arguments, and these run only behind `--benchmark`.
*/

namespace apollo::benchmarks
{

/** Runs every benchmark and prints a report to stdout. */
void run();

} // namespace apollo::benchmarks
