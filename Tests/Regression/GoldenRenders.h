#pragma once

/*
    The references the regression suite compares against.

    HOW THESE ARE PRODUCED AND WHY THAT IS A COMMAND RATHER THAN A FILE PATH.

        ApolloTests --goldens > Tests/Regression/GoldenRenders.cpp

    The runner renders every case and prints the C++ below. Nothing about this
    depends on where the test binary was built, where the repository is
    checked out, or what the working directory was when it ran — a golden that
    lived in a data file next to the source would need the binary to be told
    where the source is, and the only ways to do that are a path baked in at
    configure time or an environment variable somebody forgets to set
    (CLAUDE.md §31.2). Compiling the references in is the same decision Apollo
    already made for its factory presets and for the same reasons (ADR-0065):
    always present, never half-installed, and impossible to get out of step with
    the binary that reads them.

    IT ALSO MAKES A CHANGE REVIEWABLE. A commit that moves a golden shows which
    sound moved, in which part of the spectrum, by how many decibels, in the
    diff. That is the entire point of keeping a reference under version control,
    and it is exactly what a checked-in WAV cannot do.

    REGENERATING IS A DELIBERATE ACT. Nothing regenerates these automatically,
    and nothing should: the suite exists to notice that a render changed, so a
    harness that quietly rewrote its own references when they stopped matching
    would be an elaborate way of asserting nothing. When a change to Apollo is
    *meant* to change how it sounds, the goldens are regenerated in the same
    commit and the diff is the record of what the change did.
*/

#include <juce_core/juce_core.h>

#include <span>
#include <string_view>

#include "Regression/Fingerprint.h"

namespace apollo::regression
{

/** One stored reference, named by the case it belongs to.

    The fingerprint's checksum is left at zero here. It is an exact hash of the
    samples and it is not comparable between builds, so storing one would be
    storing a number that must never be used.
*/
struct Golden
{
    std::string_view name;
    Fingerprint print;
};

/** Every stored reference, in the order the cases are declared. */
[[nodiscard]] std::span<const Golden> goldens();

/** Renders every case and returns the text of `GoldenRenders.cpp`.

    Printed by `ApolloTests --goldens`. Slow — it renders the whole set — and
    deliberately not part of any test run.
*/
[[nodiscard]] juce::String generateGoldenSource();

} // namespace apollo::regression
