/*
    Apollo test runner.

    Drives juce::UnitTestRunner over every test registered in this binary and
    reports a machine- and human-readable summary. Exits non-zero if any test
    failed, which is what CTest and CI consume.

    Usage:
        ApolloTests                     run every test
        ApolloTests --category <name>   run one category only
        ApolloTests --list              list categories and tests, run nothing
        ApolloTests --seed <n>          use a fixed random seed (default 0)
        ApolloTests --benchmark         run the CPU measurements, run no tests
        ApolloTests --help
*/

#include <juce_core/juce_core.h>

#include <cstdlib>
#include <iostream>

#include "Performance/Benchmarks.h"

namespace
{

/** Collects results as tests run so progress is visible on a CI log tail,
    rather than only appearing after the whole suite finishes.
*/
class ApolloTestRunner final : public juce::UnitTestRunner
{
public:
    ApolloTestRunner() { setAssertOnFailure (false); }

private:
    void logMessage (const juce::String& message) override
    {
        std::cout << message.toStdString() << std::endl;
    }
};

void printUsage()
{
    std::cout
        << "Apollo test runner\n\n"
        << "  --category <name>   run only the tests in one category\n"
        << "  --list              list categories and tests without running them\n"
        << "  --seed <n>          random seed for tests that use randomness (default 0)\n"
        << "  --benchmark         run the CPU measurements instead of the tests\n"
        << "  --help              show this message\n"
        << std::endl;
}

void listTests()
{
    const auto categories = juce::UnitTest::getAllCategories();

    std::cout << "Registered tests (" << juce::UnitTest::getAllTests().size() << "):\n" << std::endl;

    for (const auto& category : categories)
    {
        std::cout << "  [" << category.toStdString() << "]" << std::endl;

        for (auto* test : juce::UnitTest::getTestsInCategory (category))
            std::cout << "      " << test->getName().toStdString() << std::endl;
    }

    std::cout << std::endl;
}

/** @returns the value following @p flag, or an empty string if absent. */
juce::String valueForFlag (const juce::StringArray& args, juce::StringRef flag)
{
    const auto index = args.indexOf (flag);

    if (index >= 0 && index + 1 < args.size())
        return args[index + 1];

    return {};
}

} // namespace

int main (int argc, char* argv[])
{
    juce::StringArray args;

    for (int i = 1; i < argc; ++i)
        args.add (juce::String (juce::CharPointer_UTF8 (argv[i])));

    if (args.contains ("--help") || args.contains ("-h"))
    {
        printUsage();
        return EXIT_SUCCESS;
    }

    if (args.contains ("--list"))
    {
        listTests();
        return EXIT_SUCCESS;
    }

    // Measurements, not tests: they report numbers rather than passing or
    // failing, so they are never part of a ctest run. See
    // Tests/Performance/Benchmarks.h for why they live in this binary anyway.
    if (args.contains ("--benchmark"))
    {
        apollo::benchmarks::run();
        return EXIT_SUCCESS;
    }

    const auto category = valueForFlag (args, "--category");
    const auto seedText = valueForFlag (args, "--seed");
    const auto seed = seedText.isNotEmpty() ? seedText.getLargeIntValue() : 0;

    if (category.isNotEmpty() && ! juce::UnitTest::getAllCategories().contains (category))
    {
        std::cerr << "Unknown test category: " << category.toStdString() << std::endl;
        listTests();
        return EXIT_FAILURE;
    }

    ApolloTestRunner runner;

    if (category.isNotEmpty())
        runner.runTestsInCategory (category, seed);
    else
        runner.runAllTests (seed);

    int totalPasses = 0;
    int totalFailures = 0;
    juce::StringArray failedTests;

    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        if (const auto* result = runner.getResult (i))
        {
            totalPasses += result->passes;
            totalFailures += result->failures;

            if (result->failures > 0)
                failedTests.add (result->unitTestName + " / " + result->subcategoryName);
        }
    }

    std::cout << "\n----------------------------------------------------------\n"
              << "Apollo tests: " << totalPasses << " passed, "
              << totalFailures << " failed"
              << std::endl;

    if (totalFailures > 0)
    {
        std::cout << "\nFailing tests:" << std::endl;

        for (const auto& name : failedTests)
            std::cout << "  - " << name.toStdString() << std::endl;
    }

    std::cout << "----------------------------------------------------------\n" << std::endl;

    // A run that executed nothing is a configuration failure, not a pass: it
    // would otherwise let a broken test registration report success in CI.
    if (runner.getNumResults() == 0)
    {
        std::cerr << "No tests were executed." << std::endl;
        return EXIT_FAILURE;
    }

    return totalFailures > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
