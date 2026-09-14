# Copies the built standalone into the project root, and never fails the build
# if it cannot.
#
# WHY THIS IS A SCRIPT RATHER THAN TWO LINES IN add_custom_command. The copy's
# most likely failure is the one that matters: the developer has the root copy
# *open* while rebuilding, so the file is locked and the copy is refused. That
# is a completely normal thing to be doing — the whole point of the root copy is
# that the application is easy to launch — and it must not turn a successful
# build into a failed one.
#
# So the copy is attempted, and a failure is reported as a status line saying
# what probably caused it. The build carries on, the binaries in the build tree
# are correct, and the stale copy in the root is the only casualty.
#
# Invoked with -D APOLLO_STANDALONE_SOURCE, -D APOLLO_STANDALONE_DESTINATION and
# -D APOLLO_STANDALONE_IS_BUNDLE.

if(NOT DEFINED APOLLO_STANDALONE_SOURCE OR NOT DEFINED APOLLO_STANDALONE_DESTINATION)
    message(FATAL_ERROR "CopyStandaloneToRoot.cmake requires a source and a destination")
endif()

set(_apollo_copy_failed FALSE)

if(APOLLO_STANDALONE_IS_BUNDLE)
    # A macOS .app is a directory. The previous one is removed first because
    # copy_directory merges into an existing tree rather than replacing it,
    # which would leave a previous build's files inside the new bundle.
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${APOLLO_STANDALONE_DESTINATION}"
        RESULT_VARIABLE _apollo_remove_result
        OUTPUT_QUIET ERROR_QUIET)

    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${APOLLO_STANDALONE_SOURCE}" "${APOLLO_STANDALONE_DESTINATION}"
        RESULT_VARIABLE _apollo_copy_result
        OUTPUT_QUIET ERROR_QUIET)

    if(_apollo_copy_result)
        set(_apollo_copy_failed TRUE)
    endif()
else()
    file(COPY_FILE
         "${APOLLO_STANDALONE_SOURCE}"
         "${APOLLO_STANDALONE_DESTINATION}"
         RESULT _apollo_copy_result
         ONLY_IF_DIFFERENT)

    if(_apollo_copy_result)
        set(_apollo_copy_failed TRUE)
    endif()
endif()

if(_apollo_copy_failed)
    message(STATUS
        "Apollo: could not refresh ${APOLLO_STANDALONE_DESTINATION}. "
        "It is most likely running - close it and build again to update it. "
        "The build itself succeeded.")
endif()
