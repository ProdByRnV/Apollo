# Apollo packaging.
#
# Phase 11a. Until now the build produced artefacts inside the build tree and
# nothing collected them: `COPY_PLUGIN_AFTER_BUILD` is deliberately off, because
# a build must never install itself into the user's system plugin folders
# (Source/Plugin/CMakeLists.txt). This is the explicit step that was deferred.
#
# What it produces:
#
#   cmake --install build --config Release --prefix <dir>
#       <dir>/VST3/Apollo.vst3          the plugin, as a bundle
#       <dir>/Standalone/Apollo(.exe)   the standalone application
#       <dir>/INSTALL.txt               where to put the plugin, per platform
#       <dir>/README.md
#
#   cpack --config build/CPackConfig.cmake -C Release -G ZIP
#       Apollo-<version>-<system>-<arch>.zip of exactly that tree
#
# Two rules shape all of it:
#
#   **No developer paths.** Everything installed is named by a target or by a
#   path relative to the project, never by an absolute path from this machine
#   (CLAUDE.md §31.2). The packaging test reads the staged tree back and fails
#   on anything that looks like a path from the machine that built it.
#
#   **The package is the thing that gets tested.** ApolloHostTests takes
#   `--plugin`, so the staged bundle can be loaded and driven exactly as the
#   build-tree one is. A package nobody loaded is a guess (ADR-0076).

include_guard(GLOBAL)

option(APOLLO_ENABLE_PACKAGING "Generate install and package rules" ${APOLLO_IS_TOP_LEVEL})

if(NOT APOLLO_ENABLE_PACKAGING)
    return()
endif()

# ---------------------------------------------------------------------------
# What the package is called
# ---------------------------------------------------------------------------
#
# Written down here rather than assembled at pack time, because the packaging
# test asserts the same shape: a name a user can read, that says which platform
# and which processor it is for, so two downloads cannot be confused.

if(WIN32)
    set(APOLLO_PACKAGE_SYSTEM "windows")
elseif(APPLE)
    set(APOLLO_PACKAGE_SYSTEM "macos")
elseif(UNIX)
    set(APOLLO_PACKAGE_SYSTEM "linux")
else()
    string(TOLOWER "${CMAKE_SYSTEM_NAME}" APOLLO_PACKAGE_SYSTEM)
endif()

# The architecture the binaries are actually for. CMAKE_SYSTEM_PROCESSOR
# describes the host, which is not the same thing when cross-compiling or when
# building a universal binary on macOS, so the explicit signals win.
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    list(LENGTH CMAKE_OSX_ARCHITECTURES apollo_osx_arch_count)

    if(apollo_osx_arch_count GREATER 1)
        set(APOLLO_PACKAGE_ARCH "universal")
    else()
        set(APOLLO_PACKAGE_ARCH "${CMAKE_OSX_ARCHITECTURES}")
    endif()
elseif(MSVC AND CMAKE_GENERATOR_PLATFORM)
    string(TOLOWER "${CMAKE_GENERATOR_PLATFORM}" APOLLO_PACKAGE_ARCH)

    if(APOLLO_PACKAGE_ARCH STREQUAL "x64")
        set(APOLLO_PACKAGE_ARCH "x86_64")
    endif()
else()
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" APOLLO_PACKAGE_ARCH)

    if(APOLLO_PACKAGE_ARCH STREQUAL "amd64" OR APOLLO_PACKAGE_ARCH STREQUAL "x86_64")
        set(APOLLO_PACKAGE_ARCH "x86_64")
    elseif(APOLLO_PACKAGE_ARCH STREQUAL "aarch64")
        set(APOLLO_PACKAGE_ARCH "arm64")
    endif()
endif()

set(APOLLO_PACKAGE_NAME "Apollo-${PROJECT_VERSION}-${APOLLO_PACKAGE_SYSTEM}-${APOLLO_PACKAGE_ARCH}")

# ---------------------------------------------------------------------------
# What goes in it
# ---------------------------------------------------------------------------

if(TARGET Apollo_VST3)
    # The VST3 artefact is a bundle on every platform Apollo targets — a
    # directory with a fixed internal layout, not a single file — so it is
    # installed as a directory. JUCE records where it put it; the nested
    # evaluation is because that property is itself a generator expression.
    install(DIRECTORY
                "$<TARGET_GENEX_EVAL:Apollo_VST3,$<TARGET_PROPERTY:Apollo_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>"
            DESTINATION "VST3"
            COMPONENT vst3
            USE_SOURCE_PERMISSIONS
            # Debug symbols are not part of a release package: they are large,
            # they are not needed to run anything, and on Windows they carry the
            # absolute path of the machine that built them.
            PATTERN "*.pdb" EXCLUDE
            PATTERN "*.ilk" EXCLUDE
            PATTERN "*.exp" EXCLUDE
            PATTERN "*.lib" EXCLUDE)
endif()

if(TARGET Apollo_Standalone)
    if(APPLE)
        install(DIRECTORY "$<TARGET_BUNDLE_DIR:Apollo_Standalone>"
                DESTINATION "Standalone"
                COMPONENT standalone
                USE_SOURCE_PERMISSIONS)
    else()
        install(PROGRAMS "$<TARGET_FILE:Apollo_Standalone>"
                DESTINATION "Standalone"
                COMPONENT standalone)
    endif()
endif()

# Where the plugin has to go for a host to find it. Per platform, because the
# answer differs, and as a file in the package rather than only in a web page
# somebody has to still be hosting.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/CMake/INSTALL.txt.in"
               "${CMAKE_CURRENT_BINARY_DIR}/INSTALL.txt"
               @ONLY)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/INSTALL.txt"
              "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
        DESTINATION "."
        COMPONENT documentation)

# ---------------------------------------------------------------------------
# The archive
# ---------------------------------------------------------------------------
#
# ZIP on every platform. An installer is a separate decision with its own
# consequences — administrator rights, an uninstaller, code signing — and a
# plugin that is one folder to copy needs none of them (§29.3 made the same
# argument for factory content).

set(CPACK_PACKAGE_NAME "Apollo")
set(CPACK_PACKAGE_VENDOR "ProdByRnV")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_FILE_NAME "${APOLLO_PACKAGE_NAME}")
set(CPACK_GENERATOR "ZIP")
set(CPACK_ARCHIVE_COMPONENT_INSTALL OFF)
set(CPACK_COMPONENTS_ALL vst3 standalone documentation)

# The archive contains the tree directly rather than one directory containing
# it: the user unzips into the folder they chose, and a wrapper directory only
# adds a step.
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)

include(CPack)

message(STATUS "  Package ................ ${APOLLO_PACKAGE_NAME}.zip")
