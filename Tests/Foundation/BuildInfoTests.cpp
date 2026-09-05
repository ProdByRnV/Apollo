/*
    Build-infrastructure tests.

    These verify that the Phase 0 foundation itself is wired correctly: that the
    generated version header reaches consuming targets, that the version is
    internally consistent, and that the test runner really executes registered
    tests. A failure here indicates a broken build configuration rather than a
    DSP defect, which is why the suite reports it separately.
*/

#include <juce_core/juce_core.h>

#include <string>
#include <string_view>

#include "ApolloVersion.h"

namespace
{

class BuildInfoTests final : public juce::UnitTest
{
public:
    BuildInfoTests()
        : juce::UnitTest ("Build information", "Foundation")
    {
    }

    void runTest() override
    {
        testProductIdentity();
        testVersionConsistency();
    }

private:
    void testProductIdentity()
    {
        beginTest ("Product identity is available to consuming targets");

        expect (! apollo::productName.empty(), "product name must not be empty");
        expectEquals (juce::String (std::string (apollo::productName)), juce::String ("Apollo"));
    }

    /** The macro form and the constexpr form are both consumed (the macros by
        JUCE's plugin metadata, the constants by C++ code), so they must agree.
    */
    void testVersionConsistency()
    {
        beginTest ("Version string matches its numeric components");

        expect (apollo::versionMajor >= 0, "major version must be non-negative");
        expect (apollo::versionMinor >= 0, "minor version must be non-negative");
        expect (apollo::versionPatch >= 0, "patch version must be non-negative");

        const auto expected = juce::String (apollo::versionMajor)
                            + "." + juce::String (apollo::versionMinor)
                            + "." + juce::String (apollo::versionPatch);

        expectEquals (juce::String (std::string (apollo::versionString)), expected);

        expectEquals (juce::String (APOLLO_VERSION_STRING), expected);
        expectEquals (APOLLO_VERSION_MAJOR, apollo::versionMajor);
        expectEquals (APOLLO_VERSION_MINOR, apollo::versionMinor);
        expectEquals (APOLLO_VERSION_PATCH, apollo::versionPatch);
    }
};

BuildInfoTests buildInfoTests;

} // namespace
