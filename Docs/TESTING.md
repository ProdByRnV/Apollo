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
├── Analysis/             # measurement equipment, not tests (spectra, THD, DC)
├── Regression/           # golden renders and the harness that compares them (§7)
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
ApolloTests --benchmark        run the CPU measurements, run no tests
ApolloTests --goldens          re-render the regression references (see §7)
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

---

## 7. Regression renders

`Tests/Regression/` asks a question no other test asks: **is it still the same
sound?** It renders fifteen cases — ten of them the factory presets — and
compares each against a reference checked in beside it (ADR-0068).

A reference is not a wave file. Identical source does not produce identical
floats across MSVC, Apple Clang and GCC, so a bit-exact golden would report three
of the four CI platforms as broken; and a changed WAV in a diff tells a reviewer
nothing. What is stored is a description of the audio: peak, RMS, mid and side
level through sixteen slices of time, and energy in thirty-two logarithmic bands.
Fine enough to catch a real change, loose enough to survive a compiler.

### When a regression test fails

A failure means a render moved. It does not by itself mean something is broken.

1. **If the change was not intended**, this is the only test that was watching
   for it. Find out what moved the sound. The failure message names each
   measurement, its reference value and its new one, so a change in the bands is
   a timbre, a change in the later segments is an envelope or a tail, and a
   change in side alone is the stereo image.
2. **If the change was intended** — a new wavetable, a reshaped envelope, a
   different default — regenerate the references in the same commit:

   ```sh
   ApolloTests --goldens > Tests/Regression/GoldenRenders.cpp
   ```

   The diff then records what the change did to the sound, which is the point of
   keeping the references under version control at all.

Nothing regenerates them automatically, and nothing should: a harness that
rewrote its own references when they stopped matching would be an elaborate way
of asserting nothing.

### Adding a case

Add a `RenderCase` to `Tests/Regression/RenderCases.cpp` — a starting patch, the
parameters it moves, a note sequence, a sample rate, a block size and a length —
then regenerate. A case that names a parameter which is not registered, or a
value the parameter would not accept, fails as itself rather than as a mismatched
fingerprint.

Keep cases short. The suite renders every one of them twice over, and the
sanitized CI job pays several times what this machine does for each sample.
