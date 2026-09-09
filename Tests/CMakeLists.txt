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
    Foundation/ParameterIdTests.cpp
    Audio/ProcessorLifecycleTests.cpp
    Engine/VoiceEngineTests.cpp
    Engine/SourceMixTests.cpp
    DSP/WavetableTests.cpp
    DSP/UnisonTests.cpp
    DSP/EnvelopeTests.cpp
    DSP/FilterTests.cpp
    DSP/LfoTests.cpp
    DSP/OversamplingTests.cpp
    Performance/Benchmarks.cpp
    DSP/NoiseTests.cpp
    Audio/MidiRenderingTests.cpp
    Parameters/ParameterRegistryTests.cpp
    State/StateSerializationTests.cpp
    UI/BridgeProtocolTests.cpp
    UI/ParameterBridgeTests.cpp)

target_compile_definitions(ApolloTests PRIVATE
    JUCE_STANDALONE_APPLICATION=1
    # Keep the test binary free of optional platform dependencies: the suite
    # needs no browser, and requiring one would make the Linux CI image heavier
    # than the tests justify.
    JUCE_WEB_BROWSER=0
    # Headless: the bridge is exercised through its protocol layer, which needs
    # no browser. The processor builds its generic-editor fallback here.
    APOLLO_WITH_WEBVIEW=0
    # JUCE's *own* internal unit tests are not part of Apollo's suite.
    JUCE_UNIT_TESTS=0)

# apollo::strict_warnings is deliberately not applied here: this target compiles
# JUCE module sources, which are held to JUCE's own warning standard rather than
# Apollo's. Apollo code under test lives in apollo::core, which is strict.
#
# apollo::engine is linked so that the processor is tested as the plugin
# actually builds it, from the same sources.
# juce_dsp is linked for its FFT, which the spectral tests use to measure
# aliasing. It is a JUCE module with no external dependency, and the DSP engine
# itself does not use it — the analysis lives only in the tests.
target_link_libraries(ApolloTests PRIVATE
    apollo::engine
    juce::juce_dsp
    apollo::project_options
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# The test tree is on the include path so test-only headers (the benchmarks)
# can be included by path, the same way Apollo headers are.
target_include_directories(ApolloTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

set_target_properties(ApolloTests PROPERTIES FOLDER "Apollo/Tests")

add_test(NAME apollo.unit COMMAND ApolloTests)

# The timeout is a deadlock guard, not a performance budget.
#
# 300 seconds was ample when the suite was small and every job ran it
# optimised. It stopped being ample in Phase 4b: the sanitizer job builds Debug
# with ASan and UBSan under Clang, and the source-section tests render a great
# deal of audio — 32 voices with 16-voice unison on both oscillators, spectral
# sweeps, and per-sample gain ramps. That job reached 92% of the suite and was
# killed at exactly 300.02 s, which reported as a failure while nothing had
# actually gone wrong.
#
# Measured on the development machine, for scale: 24 s optimised, 82 s in
# Debug. The sanitized Linux build is roughly four times the Debug figure again.
# 1800 s leaves room for the phases still to come without turning a slow
# configuration into a red build, and still catches a genuine hang far inside
# GitHub's six-hour job limit.
#
# This deliberately does not police how long the suite takes. Phase 4c measures
# CPU cost properly, and a real performance budget belongs there rather than
# hidden in a CI timeout.
set_tests_properties(apollo.unit PROPERTIES
    TIMEOUT 1800
    LABELS "unit")
