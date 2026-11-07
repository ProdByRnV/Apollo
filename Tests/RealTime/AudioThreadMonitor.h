#pragma once

/*
    Watches what the audio thread does while the tests play the instrument
    (Phase 18i, ADR-0127).

    CLAUDE.md §7 forbids the audio callback to allocate, free or lock. Every
    part of Apollo's engine was written to that rule, and until this file
    nothing checked it: a std::string built from a parameter's name, a MidiBuffer
    that grows by one event, a container resized on the block where a note is
    stolen — none of them changes a sample, so no test that listens to the output
    can hear one. They are heard instead in a DAW at a small buffer size, as a
    click nobody can reproduce.

    A test marks the stretch of code that runs as the audio callback with an
    AudioThreadSection. What happens inside it is then checked in one of two ways,
    depending on how the test binary was built:

    - **Under RealtimeSanitizer** (Clang's -fsanitize=realtime, a CI job of its
      own), the section is a real-time context in RTSan's sense. RTSan intercepts
      malloc, free, every pthread lock, file and socket I/O, sleeps and the rest
      of what may not happen on an audio thread, anywhere in the call tree —
      Apollo's code, JUCE's and the C library's alike — and stops the run at the
      first one with the stack that led to it. That is the authoritative check.

    - **In every other build** — the developer's, and each platform in CI — the
      test binary replaces the global operator new and operator delete, and
      counts each call made on a thread that is inside a section. That sees every
      allocation C++ makes through new: containers, strings, std::function, smart
      pointers. It does not see malloc called directly, which is how JUCE's
      HeapBlock allocates, and it sees no locks. It is the everyday check that
      runs everywhere, and coverage() says exactly what it is.

    Under AddressSanitizer or ThreadSanitizer the monitor watches nothing:
    those runtimes replace operator new themselves, and a second replacement
    would fight them. The sanitizer job has its own work to do.
*/

#include <juce_core/juce_core.h>

#include <cstdint>

namespace apollo::realtime
{

/** What this build of the tests can see inside a section. */
struct Coverage
{
    /** Allocations and frees made through operator new and delete. */
    bool cppAllocation = false;

    /** Allocations made by calling malloc and its relatives directly. */
    bool mallocFamily = false;

    /** Mutexes, condition variables and the other blocking primitives. */
    bool locks = false;

    /** True when RealtimeSanitizer does the checking. A violation then ends
        the run with RTSan's report, rather than being counted. */
    bool sanitizer = false;
};

/** @returns what this build of the tests can see. */
[[nodiscard]] Coverage coverage() noexcept;

/** @returns one line saying what coverage() means, for the test log. */
[[nodiscard]] juce::String describeCoverage();

/** What happened inside sections since the last takeFindings(). */
struct Findings
{
    std::uint64_t allocations = 0;
    std::uint64_t deallocations = 0;

    /** Where the first of them happened, if this platform can say. */
    juce::String firstStack;

    [[nodiscard]] bool clean() const noexcept { return allocations == 0 && deallocations == 0; }
};

/** @returns everything seen since the last call, and starts again from nothing. */
[[nodiscard]] Findings takeFindings();

/** How much a section checks. */
enum class Checks
{
    /** Everything this build can see. Use for Apollo's own callbacks. */
    everything,

    /** Allocation only, and never under RTSan. For Apollo code that a host
        reaches through a path of its own which takes a lock of its own — JUCE's
        VST3 wrapper delivers automation under one — where the question is
        whether *Apollo's* part allocates, and the host's lock is not Apollo's
        to remove. */
    allocationOnly
};

/** Marks the current thread as the audio thread for its lifetime.

    Sections nest; the thread stays marked until the outermost one ends.
*/
class AudioThreadSection
{
public:
    explicit AudioThreadSection (Checks checks = Checks::everything) noexcept;
    ~AudioThreadSection();

    AudioThreadSection (const AudioThreadSection&) = delete;
    AudioThreadSection& operator= (const AudioThreadSection&) = delete;

private:
    /** Read only in a RealtimeSanitizer build. */
    [[maybe_unused]] bool enteredSanitizer = false;
};

/** Allocates inside a section, on purpose.

    The suite has to be able to fail: a monitor that never reported anything
    would pass for ever and look exactly like one that worked (CLAUDE.md
    §34.6). The tests call this to prove the in-binary monitor sees an
    allocation; under RTSan, `ApolloTests --realtime-canary` calls it and CI
    requires the run to be stopped.
*/
void allocateInsideASection();

} // namespace apollo::realtime
