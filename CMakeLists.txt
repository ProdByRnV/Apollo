# Apollo — top-level build definition.
#
# CMake is the authoritative build system for Apollo (ARCHITECTURE.md §10, CLAUDE.md §31).
# Projucer is deliberately not used and no generated Projucer project is checked in.
#
# Build and test invocation is documented in Docs/BUILD.md.

cmake_minimum_required(VERSION 3.22)

# Apollo may be consumed as a sub-project later (installer/packaging repositories),
# so anything that mutates global state is guarded on being the top-level project.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(APOLLO_IS_TOP_LEVEL TRUE)
else()
    set(APOLLO_IS_TOP_LEVEL FALSE)
endif()

if(APOLLO_IS_TOP_LEVEL AND CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
        "Apollo does not support in-source builds. Configure into a separate directory, e.g.\n"
        "  cmake -S . -B build")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake")

# The project version is the single source of truth for the product version.
# Versioning conventions (product / plugin / state schema / bridge protocol) are
# described in Docs/VERSIONING.md.
project(Apollo
    VERSION 0.1.0
    DESCRIPTION "Apollo — cross-platform wavetable synthesizer"
    LANGUAGES C CXX)

include(ApolloBuildOptions)
include(ApolloCompilerWarnings)
include(ApolloDependencies)
include(ApolloWebView)
include(ApolloWebUI)

add_subdirectory(Source)

if(APOLLO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()

if(APOLLO_IS_TOP_LEVEL)
    message(STATUS "")
    message(STATUS "Apollo ${PROJECT_VERSION} configured")
    message(STATUS "  Generator .............. ${CMAKE_GENERATOR}")
    if(APOLLO_IS_MULTI_CONFIG)
        message(STATUS "  Configurations ......... ${CMAKE_CONFIGURATION_TYPES}")
    else()
        message(STATUS "  Build type ............. ${CMAKE_BUILD_TYPE}")
    endif()
    message(STATUS "  C++ standard ........... ${CMAKE_CXX_STANDARD}")
    message(STATUS "  JUCE ................... ${APOLLO_JUCE_DESCRIPTION}")
    message(STATUS "  WebView ................ ${APOLLO_WEBVIEW_DESCRIPTION}")
    message(STATUS "  Tests .................. ${APOLLO_BUILD_TESTS}")
    message(STATUS "  Warnings as errors ..... ${APOLLO_WARNINGS_AS_ERRORS}")
    message(STATUS "  AddressSanitizer ....... ${APOLLO_ENABLE_ASAN}")
    message(STATUS "  UndefinedBehaviorSan ... ${APOLLO_ENABLE_UBSAN}")
    message(STATUS "")
endif()
