# Apollo native source.
#
# Two libraries, split by dependency rather than by feature:
#
#   apollo_core   STATIC, JUCE-free. Pure logic that can be unit tested with no
#                 framework, no audio device and no plugin host. Held to
#                 apollo::strict_warnings.
#
#   apollo_engine INTERFACE, JUCE-dependent. The synthesis engine and plugin
#                 glue, propagated as sources to each final target.
#
# Why apollo_engine is INTERFACE and not STATIC: JUCE modules add their sources
# to whichever target links them. A STATIC library that linked
# juce_audio_processors would compile the JUCE module sources into itself, and
# the plugin target would compile them a second time, producing duplicate
# symbols at link. An INTERFACE library propagates Apollo's own sources to each
# final target instead — the same mechanism JUCE uses for its own modules — so
# the plugin and the test runner each get one clean copy.

# ---------------------------------------------------------------------------
# apollo_core
# ---------------------------------------------------------------------------

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/ApolloVersion.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/ApolloVersion.h"
    @ONLY)

add_library(apollo_core STATIC
    Parameters/ParameterId.cpp
    Parameters/ParameterId.h
    "${CMAKE_CURRENT_BINARY_DIR}/generated/ApolloVersion.h")

add_library(apollo::core ALIAS apollo_core)

target_include_directories(apollo_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")

# project_options is PUBLIC: the language level and any sanitizer flags must
# reach consumers and their link steps. strict_warnings is PRIVATE: Apollo holds
# its own code to that standard without imposing it on anything that links here.
target_link_libraries(apollo_core
    PUBLIC  apollo::project_options
    PRIVATE apollo::strict_warnings)

set_target_properties(apollo_core PROPERTIES
    FOLDER "Apollo"
    POSITION_INDEPENDENT_CODE ON)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    Parameters/ParameterId.cpp
    Parameters/ParameterId.h)

# ---------------------------------------------------------------------------
# apollo_engine
# ---------------------------------------------------------------------------

add_library(apollo_engine INTERFACE)
add_library(apollo::engine ALIAS apollo_engine)

target_sources(apollo_engine INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/Audio/ApolloAudioProcessor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Audio/ApolloAudioProcessor.h")

target_include_directories(apollo_engine INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(apollo_engine INTERFACE
    apollo::core
    juce::juce_audio_processors
    juce::juce_audio_utils)

# Applied to every target that compiles the engine, so the plugin and the test
# runner cannot disagree about how JUCE itself is configured.
target_compile_definitions(apollo_engine INTERFACE
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

add_subdirectory(Plugin)
