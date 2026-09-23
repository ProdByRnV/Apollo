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
# The Visual C++ runtime (Phase 11a)
# ---------------------------------------------------------------------------
#
# Linked statically, so that nothing Apollo ships needs the Visual C++
# Redistributable installed on the machine that receives it.
#
# This is not a preference. Measured on the built plugin, the default dynamic
# runtime made Apollo import MSVCP140.dll, VCRUNTIME140.dll and
# VCRUNTIME140_1.dll (ADR-0076). A user without the redistributable does not get
# a message about a missing runtime: the plugin fails to load, and the host
# reports that it could not find it — indistinguishable from never having
# installed it. Plugins are copied around rather than installed by a setup
# program that could carry a prerequisite, so the redistributable is a
# dependency nothing in the delivery path would satisfy.
#
# The cost is size — each binary carries the part of the runtime it uses — and
# that no runtime bug can be fixed under Apollo by updating the system
# redistributable. For a plugin that is the right trade: a larger download is a
# nuisance, and a plugin that will not load is not a plugin.
#
# It must be set here, before any target exists, because every object in a
# binary has to agree about which runtime it uses — JUCE's included.
option(APOLLO_MSVC_STATIC_RUNTIME "Link the Visual C++ runtime statically (Windows)" ON)

if(MSVC AND APOLLO_MSVC_STATIC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---------------------------------------------------------------------------
# macOS: how old a Mac, and which processors (Phase 11b)
# ---------------------------------------------------------------------------
#
# Both must be stated. Left alone, a macOS build targets whatever the machine
# that built it happens to be running and contains only that machine's
# processor — so a package built on a current Apple Silicon runner would refuse
# to launch on an older Mac and would not run on an Intel one at all. Neither
# failure is visible to the person who built it (ADR-0077).
#
# 11.0 (Big Sur, 2020) is the floor. It is what the arm64 slice of a universal
# binary requires in any case, so a lower number would buy Intel machines a few
# more years while making the two halves of the same file disagree about what
# they support.
set(APOLLO_MACOS_DEPLOYMENT_TARGET "11.0" CACHE STRING
    "Oldest macOS version Apollo is built to run on")

# Off by default because it doubles compile time, and a developer building to
# run the tests wants their own processor. A release package turns it on, and
# the package test asserts that both slices are actually there rather than
# trusting the flag.
option(APOLLO_MACOS_UNIVERSAL "Build for Apple Silicon and Intel in one binary" OFF)

if(APPLE)
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "${APOLLO_MACOS_DEPLOYMENT_TARGET}" CACHE STRING
            "Oldest macOS version Apollo is built to run on" FORCE)
    endif()

    if(APOLLO_MACOS_UNIVERSAL AND NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING
            "Architectures to build for" FORCE)
    endif()
endif()

# What the build asked the toolchain to produce, comma-separated, for the
# package test to check the artefact against. Empty when nothing was asked for,
# in which case whatever the toolchain produced is what ships.
set(APOLLO_EXPECTED_ARCHITECTURES "")

if(CMAKE_OSX_ARCHITECTURES)
    string(REPLACE ";" "," APOLLO_EXPECTED_ARCHITECTURES "${CMAKE_OSX_ARCHITECTURES}")
endif()

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
