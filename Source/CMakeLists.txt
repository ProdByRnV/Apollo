# apollo_core — the shared, host-independent Apollo library.
#
# This is the target that the VST3 wrapper, the standalone application and the
# test suite will all link against (roadmap Phase 1). Keeping the engine in a
# library rather than in the plugin target is what makes ARCHITECTURE.md §5.1's
# "DSP engine independent of the plugin wrapper" enforceable by the build system
# rather than by convention.
#
# At Phase 0 it contains only the conventions established in this phase. DSP,
# parameter-registry, MIDI and state code arrive in their own phases.

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
