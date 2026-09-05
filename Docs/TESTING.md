# Apollo Testing

Testing is mandatory for production DSP (CLAUDE.md §34). This document covers the
framework and the mechanics of adding a test. What *must* be tested per subsystem
is specified in CLAUDE.md §34, ARCHITECTURE.md §15 and the ROADMAP phase tasks.

---

## 1. Framework

Apollo uses **`juce::UnitTest`**, which ships inside `juce_core`.

### Why not Catch2 / GoogleTest

CLAUDE.md §32 requires dependencies to be avoided when JUCE or the standard
library already suffice, and warns against adding one purely for convenience.
JUCE is already a hard dependency, so `juce::UnitTest` costs nothing extra, while
Catch2 or GoogleTest would add a second fetched dependency to pin, license-audit
and keep building on every supported platform and architecture.

The practical trade-off accepted in exchange: `juce::UnitTest` has a smaller
assertion vocabulary and no built-in parameterised tests. Neither has blocked
anything so far. Revisit only if a concrete need appears — and record the change
in [DECISIONS.md](DECISIONS.md) rather than mixing frameworks.

---

## 2. Layout

```text
Tests/
├── CMakeLists.txt
├── TestMain.cpp          # runner: argument handling, reporting, exit status
└── Foundation/           # build system, conventions, project invariants
```

Further directories arrive with the subsystems they cover, matching CLAUDE.md
§44: `Tests/DSP/`, `Tests/State/`, `Tests/MIDI/`, `Tests/Integration/`. They are
deliberately not created empty ahead of the code they test.

The category string passed to the `juce::UnitTest` constructor should match the
directory, so `--category` selection and the directory layout stay aligned.

---

## 3. Running

```sh
ctest --test-dir build -C RelWithDebInfo --output-on-failure
```

Direct invocation, which is faster while iterating:

```text
ApolloTests                    run everything
ApolloTests --category <name>  run one category
ApolloTests --list             list categories and tests, run nothing
ApolloTests --seed <n>         fix the random seed (default 0)
ApolloTests --help
```

The runner exits non-zero when any test fails, when an unknown category is
requested, and when no test ran at all — the last case guards against a broken
registration silently reporting success.

---

## 4. Adding a test

1. Create `Tests/<Category>/<Thing>Tests.cpp`.
2. Derive from `juce::UnitTest`, passing a name and the category.
3. Register it with a namespace-scope instance (JUCE's idiom — the constructor
   adds it to the global registry).
4. Add the file to `target_sources` in `Tests/CMakeLists.txt`. **CMake is the
   authoritative build system; a source file that is not listed does not exist**
   (CLAUDE.md §31).

```cpp
#include <juce_core/juce_core.h>

namespace
{

class ExampleTests final : public juce::UnitTest
{
public:
    ExampleTests() : juce::UnitTest ("Example behaviour", "DSP") {}

    void runTest() override
    {
        beginTest ("does the thing");
        expect (thing(), "explain what should have happened");
    }
};

ExampleTests exampleTests;

} // namespace
```

Prefer `static_assert` for anything decidable at compile time — it costs no
run time and fails the build rather than the test run.

---

## 5. Warning policy inside tests

`Tests/` compiles JUCE module sources into the runner, so it uses JUCE's
recommended warning flags rather than `apollo::strict_warnings`. Apollo's own
code lives in `apollo_core`, which *is* held to the strict set. Keeping
production code out of the test target is what makes that separation possible —
see [CODING-STANDARDS.md](CODING-STANDARDS.md).

---

## 6. Real-time safety in tests

DSP tests must exercise the same code path the audio thread uses, including
`prepare`/`reset` lifecycles, variable block sizes and sample-rate changes
(CLAUDE.md §34.3). A test that only calls a processing function once, at one
block size, has not tested the real-time contract.

Allocation checking, denormal and NaN/Inf propagation tests, and spectral
validation arrive with the DSP they cover (roadmap Phases 3–5 and 10).
