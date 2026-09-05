# Apollo third-party dependencies.
#
# Policy (CLAUDE.md §32): dependencies are explicit, pinned to an immutable
# revision, reproducible, and avoided entirely when JUCE or the standard library
# already provide what is needed.
#
# Current dependency set:
#   JUCE 8 — application framework, DSP utilities, plugin wrappers, WebView.
#
# Notably NOT depended on:
#   A third-party unit-test framework. Apollo uses juce::UnitTest, which ships
#   with juce_core and therefore adds no new dependency (see Docs/TESTING.md).

include_guard(GLOBAL)

# ---------------------------------------------------------------------------
# JUCE
# ---------------------------------------------------------------------------
#
# The project specification targets JUCE 8 (PRD §5, ARCHITECTURE.md §5,
# CLAUDE.md §6.1). 8.0.15 is the newest JUCE 8 release. JUCE 9 exists upstream
# but adopting it is a specification change, not a build-system decision, so it
# is deliberately not pinned here.

set(APOLLO_JUCE_VERSION "8.0.15")
set(APOLLO_JUCE_COMMIT  "91ad83ae34a81e0833b1a2b0866f54846370ae53")
set(APOLLO_JUCE_REPOSITORY "https://github.com/juce-framework/JUCE.git")

# Keep JUCE's module source groups in IDE project files.
set(JUCE_ENABLE_MODULE_SOURCE_GROUPS ON CACHE BOOL "" FORCE)

if(APOLLO_JUCE_SOURCE_DIR)
    # A developer-supplied checkout. Used for offline builds, CI caches and
    # JUCE debugging. The path is a cache variable, never a hard-coded path.
    if(NOT EXISTS "${APOLLO_JUCE_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "APOLLO_JUCE_SOURCE_DIR is set to '${APOLLO_JUCE_SOURCE_DIR}' but no "
            "CMakeLists.txt was found there.")
    endif()
    add_subdirectory("${APOLLO_JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_juce" EXCLUDE_FROM_ALL)
    set(APOLLO_JUCE_CHECKOUT_DIR "${APOLLO_JUCE_SOURCE_DIR}")
    set(APOLLO_JUCE_DESCRIPTION "local checkout at ${APOLLO_JUCE_SOURCE_DIR}")
else()
    include(FetchContent)

    # A shallow clone of the tag keeps configure times reasonable; the exact
    # commit is verified below so the pin is still enforced.
    FetchContent_Declare(JUCE
        GIT_REPOSITORY "${APOLLO_JUCE_REPOSITORY}"
        GIT_TAG        "${APOLLO_JUCE_VERSION}"
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE)

    FetchContent_MakeAvailable(JUCE)
    set(APOLLO_JUCE_CHECKOUT_DIR "${juce_SOURCE_DIR}")
    set(APOLLO_JUCE_DESCRIPTION "${APOLLO_JUCE_VERSION} (fetched)")
endif()

# ---------------------------------------------------------------------------
# Pin verification
# ---------------------------------------------------------------------------
#
# Tags are mutable on the server. Verifying the resolved commit turns the pin
# into an actual reproducibility guarantee instead of a comment.

find_package(Git QUIET)

if(GIT_EXECUTABLE AND EXISTS "${APOLLO_JUCE_CHECKOUT_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${APOLLO_JUCE_CHECKOUT_DIR}" rev-parse HEAD
        OUTPUT_VARIABLE APOLLO_JUCE_RESOLVED_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE apollo_juce_rev_parse_result)

    if(apollo_juce_rev_parse_result EQUAL 0
       AND NOT APOLLO_JUCE_RESOLVED_COMMIT STREQUAL APOLLO_JUCE_COMMIT)
        if(APOLLO_ALLOW_UNPINNED_JUCE)
            message(WARNING
                "JUCE checkout is at ${APOLLO_JUCE_RESOLVED_COMMIT}, expected the pinned "
                "${APOLLO_JUCE_COMMIT} (${APOLLO_JUCE_VERSION}). Continuing because "
                "APOLLO_ALLOW_UNPINNED_JUCE is ON.")
            set(APOLLO_JUCE_DESCRIPTION "${APOLLO_JUCE_DESCRIPTION} [UNPINNED]")
        else()
            message(FATAL_ERROR
                "JUCE checkout does not match Apollo's pinned revision.\n"
                "  expected: ${APOLLO_JUCE_COMMIT} (${APOLLO_JUCE_VERSION})\n"
                "  found:    ${APOLLO_JUCE_RESOLVED_COMMIT}\n"
                "Re-fetch JUCE, or configure with -DAPOLLO_ALLOW_UNPINNED_JUCE=ON to "
                "override deliberately.")
        endif()
    endif()
else()
    message(STATUS "Apollo: skipping JUCE pin verification (no git metadata available)")
endif()
