# Replaces the plugin in a VST3 folder with the one just built, and never fails
# the build if it cannot.
#
# For the developer, who opens each build in a host and asked for the build to
# put it there. Off unless APOLLO_COPY_VST3_TO_PLUGIN_FOLDER is set.
#
# THE CONTENTS ARE REPLACED, NOT THE FOLDER. The system-wide VST3 folder needs
# administrator rights to create anything in, and a build has none. The bundle
# folder is made once, with rights given to the developer's account on that one
# folder (Docs/BUILD.md, "The plugin in a host"); after that a build can replace
# what is inside it without asking.
#
# COPIED OVER FIRST, TIDIED AFTER. The usual reason a copy is refused is a host
# with the plugin loaded, holding the binary open. Emptying the bundle first
# would get as far as that file and stop, leaving a bundle with its metadata
# deleted and the old binary in it, which a host may then fail to load. So the
# new files are written over the old ones, and only once every one of them has
# landed is anything a previous build left behind removed. A refused copy
# leaves a bundle that still loads.
#
# A refused copy is reported and the build carries on.
#
# Invoked with -D APOLLO_VST3_SOURCE (the built .vst3 bundle) and
# -D APOLLO_VST3_FOLDER (the plugin folder it goes in).

if(NOT DEFINED APOLLO_VST3_SOURCE OR NOT DEFINED APOLLO_VST3_FOLDER)
    message(FATAL_ERROR "CopyVst3ToPluginFolder.cmake requires a source and a folder")
endif()

get_filename_component(_apollo_bundle_name "${APOLLO_VST3_SOURCE}" NAME)
set(_apollo_destination "${APOLLO_VST3_FOLDER}/${_apollo_bundle_name}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${APOLLO_VST3_SOURCE}" "${_apollo_destination}"
    RESULT_VARIABLE _apollo_copy_result
    OUTPUT_QUIET ERROR_QUIET)

if(_apollo_copy_result)
    message(STATUS
        "Apollo: could not refresh ${_apollo_destination}. "
        "A host probably has the plugin loaded - close it and build again, "
        "or the folder has not been set up yet (Docs/BUILD.md). "
        "The build itself succeeded.")
    return()
endif()

# Everything the new bundle does not contain came from an older build.
file(GLOB_RECURSE _apollo_built LIST_DIRECTORIES false RELATIVE "${APOLLO_VST3_SOURCE}" "${APOLLO_VST3_SOURCE}/*")
file(GLOB_RECURSE _apollo_installed LIST_DIRECTORIES false RELATIVE "${_apollo_destination}" "${_apollo_destination}/*")

foreach(_apollo_file IN LISTS _apollo_installed)
    if(NOT _apollo_file IN_LIST _apollo_built)
        file(REMOVE "${_apollo_destination}/${_apollo_file}")
    endif()
endforeach()

message(STATUS "Apollo: refreshed ${_apollo_destination}")
