# Apollo compiler options and warning policy.
#
# Two INTERFACE targets are provided so that Apollo's own code can be held to a
# stricter standard than third-party sources compiled into the same binary:
#
#   apollo::project_options
#       Language level, encoding, build-throughput and sanitizer flags. Safe to
#       apply to every Apollo target, including targets that compile JUCE module
#       sources (the plugin/standalone/test shells).
#
#   apollo::strict_warnings
#       The aggressive diagnostic set. Applied PRIVATE to Apollo-owned libraries
#       only. Applying it to targets that compile JUCE sources would bury Apollo
#       diagnostics under third-party noise, so those targets use JUCE's own
#       recommended warning flags instead.
#
# Rationale and the full coding standard live in Docs/CODING-STANDARDS.md.

include_guard(GLOBAL)

# ---------------------------------------------------------------------------
# apollo::project_options
# ---------------------------------------------------------------------------

add_library(apollo_project_options INTERFACE)
add_library(apollo::project_options ALIAS apollo_project_options)

target_compile_features(apollo_project_options INTERFACE cxx_std_20)
target_link_libraries(apollo_project_options INTERFACE apollo::sanitizers)

if(MSVC)
    target_compile_options(apollo_project_options INTERFACE
        /utf-8   # source and execution character sets are UTF-8 on every platform
        /MP      # parallel compilation; the VS generator does not do this by default
        /FS)     # serialise PDB writes
endif()

# /FS is the documented companion to /MP: parallel compilation means several
# cl.exe processes writing one PDB, and without forced serialisation that races
# into "C1041: cannot open program database". Recent MSVC often implies /FS, but
# relying on that is relying on a default that has changed before.

# ---------------------------------------------------------------------------
# apollo::strict_warnings
# ---------------------------------------------------------------------------

add_library(apollo_strict_warnings INTERFACE)
add_library(apollo::strict_warnings ALIAS apollo_strict_warnings)

if(MSVC)
    target_compile_options(apollo_strict_warnings INTERFACE
        /W4
        /permissive-        # conforming mode
        /Zc:__cplusplus     # report the real __cplusplus value
        /Zc:preprocessor    # conforming preprocessor
        /Zc:inline)
    if(APOLLO_WARNINGS_AS_ERRORS)
        target_compile_options(apollo_strict_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(apollo_strict_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Woverloaded-virtual
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Wconversion         # DSP code silently narrowing is an audio-quality bug
        -Wsign-conversion
        -Wdouble-promotion   # accidental float->double in an audio loop is a real cost
        -Wformat=2
        -Wnull-dereference)
    if(APOLLO_WARNINGS_AS_ERRORS)
        target_compile_options(apollo_strict_warnings INTERFACE -Werror)
    endif()
endif()
