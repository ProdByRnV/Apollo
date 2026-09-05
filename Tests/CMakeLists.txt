# Apollo test suite.
#
# Framework: juce::UnitTest (ships with juce_core). See Docs/TESTING.md for the
# rationale and for how to add a test.
#
# The runner is a console application so that it can run headless in CI and in
# a terminal, with no audio device, no plugin host and no WebView.

juce_add_console_app(ApolloTests
    PRODUCT_NAME "Apollo Tests")

target_sources(ApolloTests PRIVATE
    TestMain.cpp
    Foundation/BuildInfoTests.cpp
    Foundation/ParameterIdTests.cpp)

target_compile_definitions(ApolloTests PRIVATE
    JUCE_STANDALONE_APPLICATION=1
    # Keep the test binary free of optional platform dependencies: the suite
    # needs no networking and no browser, and requiring them would make the
    # Linux CI image heavier than the tests justify.
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    # JUCE's *own* internal unit tests are not part of Apollo's suite.
    JUCE_UNIT_TESTS=0)

# apollo::strict_warnings is deliberately not applied here: this target compiles
# JUCE module sources, which are held to JUCE's own warning standard rather than
# Apollo's. Apollo code under test lives in apollo::core, which is strict.
target_link_libraries(ApolloTests PRIVATE
    apollo::core
    juce::juce_core
    apollo::project_options
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

set_target_properties(ApolloTests PROPERTIES FOLDER "Apollo/Tests")

add_test(NAME apollo.unit COMMAND ApolloTests)

set_tests_properties(apollo.unit PROPERTIES
    TIMEOUT 300
    LABELS "unit")
