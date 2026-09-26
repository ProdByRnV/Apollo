/*
    Apollo host harness.

    Loads the built VST3 bundle through a VST3 host implementation and drives it
    the way a DAW does. A separate binary from ApolloTests, because that suite
    links Apollo's engine directly and this one must not: anything it knew about
    Apollo other than what the bundle tells it would be a way for the wrapper to
    be bypassed without anyone noticing (ADR-0075).

    Usage:
        ApolloHostTests                     run every host test
        ApolloHostTests --plugin <path>     test a different bundle, such as the
                                            copy a DAW has installed
        ApolloHostTests --category <name>   run one category only
        ApolloHostTests --list              list the tests, run nothing
        ApolloHostTests --parameter-ids     print the pinned parameter identities,
                                            for Tests/Host/ParameterIdentities.cpp
        ApolloHostTests --help
*/

#include "Host/HostedApollo.h"
#include "Host/ParameterIdentities.h"

#include <cstdlib>
#include <iostream>

#ifndef APOLLO_HOST_TEST_DEFAULT_BUNDLE
 #error "The build must say where the VST3 bundle it produced is (Tests/CMakeLists.txt)"
#endif

namespace
{

class HostTestRunner final : public juce::UnitTestRunner
{
public:
    HostTestRunner() { setAssertOnFailure (false); }

private:
    void logMessage (const juce::String& message) override
    {
        std::cout << message.toStdString() << std::endl;
    }
};

juce::String valueForFlag (const juce::StringArray& args, juce::StringRef flag)
{
    const auto index = args.indexOf (flag);

    if (index >= 0 && index + 1 < args.size())
        return args[index + 1];

    return {};
}

void printUsage()
{
    std::cout
        << "Apollo host harness\n\n"
        << "  --plugin <path>     the VST3 bundle to load (default: the one this build produced)\n"
        << "  --category <name>   run only the tests in one category\n"
        << "  --soak <n>          run the reliability tests n times longer than their\n"
        << "                      default length (default 1, which is what CI runs)\n"
        << "  --list              list the tests without running them\n"
        << "  --parameter-ids     print the pinned parameter identities, for\n"
        << "                      Tests/Host/ParameterIdentities.cpp\n"
        << "  --help              show this message\n"
        << std::endl;
}

int run (const juce::StringArray& args)
{
    const auto pluginArgument = valueForFlag (args, "--plugin");
    const auto bundle = pluginArgument.isNotEmpty()
                            ? juce::File::getCurrentWorkingDirectory().getChildFile (pluginArgument)
                            : juce::File (juce::CharPointer_UTF8 (APOLLO_HOST_TEST_DEFAULT_BUNDLE));

    apollo::host::setPluginBundle (bundle);

    if (args.contains ("--list"))
    {
        for (const auto& category : juce::UnitTest::getAllCategories())
        {
            std::cout << "[" << category.toStdString() << "]" << std::endl;

            for (auto* test : juce::UnitTest::getTestsInCategory (category))
                std::cout << "    " << test->getName().toStdString() << std::endl;
        }

        return EXIT_SUCCESS;
    }

    // Output, not a test. Redirected over Tests/Host/ParameterIdentities.cpp by
    // somebody who means to change what a host stores automation against —
    // which, for a released plugin, is nobody (Docs/PARAMETER-CONVENTIONS.md §1).
    if (args.contains ("--parameter-ids"))
    {
        juce::String error;
        const auto source = apollo::host::generateParameterIdentitySource (error);

        if (source.isEmpty())
        {
            std::cerr << error.toStdString() << std::endl;
            return EXIT_FAILURE;
        }

        std::cout << source.toStdString() << std::flush;
        return EXIT_SUCCESS;
    }

    std::cout << "Plugin under test: " << bundle.getFullPathName().toStdString() << "\n" << std::endl;

    // One clear failure rather than sixty identical ones: every test needs the
    // bundle, so a bundle that cannot be scanned is the only thing to report.
    {
        juce::String error;

        if (apollo::host::scan (error).isEmpty())
        {
            std::cerr << "The plugin could not be scanned: " << error.toStdString() << std::endl;
            return EXIT_FAILURE;
        }
    }

    // How much longer the reliability tests run than their default length.
    // The defaults are sized for CI; a real soak is a deliberate act, and this
    // is how it is asked for (ADR-0080).
    if (const auto soak = valueForFlag (args, "--soak"); soak.isNotEmpty())
    {
        apollo::host::setSoakScale (soak.getIntValue());
        std::cout << "Soak scale: x" << apollo::host::soakScale() << "\n" << std::endl;
    }

    const auto category = valueForFlag (args, "--category");
    HostTestRunner runner;

    if (category.isNotEmpty())
        runner.runTestsInCategory (category);
    else
        runner.runAllTests();

    int passes = 0;
    int failures = 0;
    juce::StringArray failed;

    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        if (const auto* result = runner.getResult (i))
        {
            passes += result->passes;
            failures += result->failures;

            if (result->failures > 0)
                failed.add (result->unitTestName + " / " + result->subcategoryName);
        }
    }

    std::cout << "\n----------------------------------------------------------\n"
              << "Apollo host tests: " << passes << " passed, " << failures << " failed" << std::endl;

    for (const auto& name : failed)
        std::cout << "  - " << name.toStdString() << std::endl;

    std::cout << "----------------------------------------------------------\n" << std::endl;

    if (runner.getNumResults() == 0)
    {
        std::cerr << "No tests were executed." << std::endl;
        return EXIT_FAILURE;
    }

    return failures > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
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

    // This thread is the message thread, as a host's main thread is. A VST3
    // controller expects to be created, queried and destroyed on it.
    juce::MessageManager::getInstance()->setCurrentThreadAsMessageThread();

    const auto result = run (args);

    juce::DeletedAtShutdown::deleteAll();
    juce::MessageManager::deleteInstance();

    return result;
}
