# Apollo build options, supported build configurations and sanitizer wiring.
#
# Supported configurations (Docs/BUILD.md):
#   Debug           - unoptimised, assertions on, used for DSP debugging
#   RelWithDebInfo  - optimised with debug info; the default for local development
#                     because real-time audio work needs realistic performance
#   Release         - shipping configuration
#   MinSizeRel      - kept for completeness / packaging experiments
#
# Sanitizer and profiling variants are expressed as options on top of a
# configuration rather than as extra configurations, so that they compose with
# any generator.

include_guard(GLOBAL)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------

option(APOLLO_BUILD_TESTS       "Build the Apollo test suite"                       ${APOLLO_IS_TOP_LEVEL})
option(APOLLO_WARNINGS_AS_ERRORS "Treat compiler warnings in Apollo code as errors" OFF)
option(APOLLO_ENABLE_ASAN       "Enable AddressSanitizer"                           OFF)
option(APOLLO_ENABLE_UBSAN      "Enable UndefinedBehaviorSanitizer"                 OFF)
option(APOLLO_ENABLE_IPO        "Enable interprocedural optimisation where supported" OFF)
option(APOLLO_ENABLE_WEBVIEW    "Build the WebView-based editor UI"                  ON)
option(APOLLO_ALLOW_UNPINNED_JUCE "Allow a JUCE checkout that does not match the pinned commit" OFF)

set(APOLLO_JUCE_SOURCE_DIR "" CACHE PATH
    "Path to an existing JUCE checkout. When empty, JUCE is fetched at the pinned revision.")

# ---------------------------------------------------------------------------
# Language standard
# ---------------------------------------------------------------------------

# C++20 is the project baseline. It is supported by every toolchain Apollo
# targets and by JUCE 8. Compiler extensions are disabled so that the same
# source compiles identically across MSVC, Clang and GCC.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Emitted for clangd / IDE tooling. Ignored by the Visual Studio generator.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# NOTE: symbol visibility is deliberately left at the toolchain default for now.
# Hidden visibility is the right end state for plugin bundles, but it interacts
# with the plugin format's exported entry points and must be validated against a
# real VST3/standalone build. That belongs to Phase 1, not here.

get_property(APOLLO_IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

# ---------------------------------------------------------------------------
# Build configurations
# ---------------------------------------------------------------------------

if(APOLLO_IS_TOP_LEVEL)
    if(APOLLO_IS_MULTI_CONFIG)
        set(CMAKE_CONFIGURATION_TYPES "Debug;Release;RelWithDebInfo;MinSizeRel"
            CACHE STRING "Apollo build configurations" FORCE)
    elseif(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING
            "Apollo build configuration" FORCE)
        set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
            Debug Release RelWithDebInfo MinSizeRel)
    endif()
endif()

# ---------------------------------------------------------------------------
# Floating-point policy
# ---------------------------------------------------------------------------
#
# Apollo deliberately does NOT enable fast-math style optimisations. The DSP
# engine relies on IEEE-754 semantics for the NaN/Inf guards and denormal
# handling required by CLAUDE.md §34.2 and §37; fast-math permits the compiler
# to assume those values never occur, which would silently delete those guards.
# Revisit only with measured evidence (Phase 10), never as a default.

# ---------------------------------------------------------------------------
# Interprocedural optimisation
# ---------------------------------------------------------------------------

if(APOLLO_ENABLE_IPO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT apollo_ipo_supported OUTPUT apollo_ipo_error)
    if(apollo_ipo_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
    else()
        message(WARNING "APOLLO_ENABLE_IPO requested but unsupported: ${apollo_ipo_error}")
    endif()
endif()

# ---------------------------------------------------------------------------
# Sanitizers
# ---------------------------------------------------------------------------
#
# apollo::sanitizers is an INTERFACE target carried by apollo::project_options,
# so sanitizer flags reach every Apollo target (including link steps) without
# being duplicated in each CMakeLists.

add_library(apollo_sanitizers INTERFACE)
add_library(apollo::sanitizers ALIAS apollo_sanitizers)

if(APOLLO_ENABLE_ASAN)
    if(MSVC)
        target_compile_options(apollo_sanitizers INTERFACE /fsanitize=address)
        # MSVC's ASan is incompatible with the run-time checks and incremental
        # linking that the Debug configuration enables by default.
        foreach(lang C CXX)
            foreach(config DEBUG RELWITHDEBINFO)
                string(REGEX REPLACE "/RTC(su|[1su])" ""
                       CMAKE_${lang}_FLAGS_${config} "${CMAKE_${lang}_FLAGS_${config}}")
            endforeach()
        endforeach()
        target_link_options(apollo_sanitizers INTERFACE /INCREMENTAL:NO)
    else()
        target_compile_options(apollo_sanitizers INTERFACE
            -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(apollo_sanitizers INTERFACE -fsanitize=address)
    endif()
endif()

if(APOLLO_ENABLE_UBSAN)
    if(MSVC)
        message(WARNING
            "APOLLO_ENABLE_UBSAN is not supported by MSVC and will be ignored. "
            "Run UBSan builds with Clang or GCC.")
    else()
        target_compile_options(apollo_sanitizers INTERFACE
            -fsanitize=undefined -fno-omit-frame-pointer)
        target_link_options(apollo_sanitizers INTERFACE -fsanitize=undefined)
    endif()
endif()
