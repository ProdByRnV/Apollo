# Builds Apollo's React/TypeScript frontend and hands CMake the three files the
# plugin embeds.
#
# THE FRONTEND IS A BUILD INPUT LIKE ANY OTHER. Before Phase 7d the page was
# three hand-written files listed directly in `juce_add_binary_data`, which meant
# touching the CSS relinked the plugin and nothing else had to be understood.
# TypeScript and JSX cannot be embedded as they are written, so something has to
# compile them, and the choice is between running that compiler from CMake or
# committing its output to the repository.
#
# Running it from CMake is the honest one. A committed bundle is a generated file
# that can silently disagree with its source — someone edits a component, forgets
# to rebuild, and the plugin ships the previous interface with no diff to show
# for it. The cost is that building the plugin now needs Node; `apollo_core` and
# `ApolloTests` do not, which is deliberate, because the test suite has never had
# a browser dependency and must not acquire one.
#
# `APOLLO_ENABLE_WEBVIEW=OFF` skips all of this, which is the escape hatch for a
# machine with no Node at all.

find_program(APOLLO_NPM_EXECUTABLE NAMES npm.cmd npm)

# Returns, in `out_files`, the absolute paths of the built bundle, and defines
# the `apollo_webui` target that produces them.
function(apollo_build_webui out_files)
    set(webui_source "${CMAKE_SOURCE_DIR}/WebUI")
    set(webui_output "${CMAKE_BINARY_DIR}/webui")

    if(NOT APOLLO_NPM_EXECUTABLE)
        message(FATAL_ERROR
            "Apollo's interface is built from WebUI/ with npm, and npm was not found.\n"
            "Install Node.js (which provides it), or configure with "
            "-DAPOLLO_ENABLE_WEBVIEW=OFF to build the engine and tests without the "
            "editor.")
    endif()

    set(built
        "${webui_output}/index.html"
        "${webui_output}/apollo.css"
        "${webui_output}/apollo.js")

    # Every source the bundle is built from. Listing them is what makes an edit
    # to a component rebuild the plugin, which is the same property the three
    # hand-written files had and the one most easily lost in a migration.
    file(GLOB_RECURSE webui_sources CONFIGURE_DEPENDS
        "${webui_source}/src/*.ts"
        "${webui_source}/src/*.tsx"
        "${webui_source}/src/*.css")

    list(APPEND webui_sources
        "${webui_source}/index.html"
        "${webui_source}/build.mjs"
        "${webui_source}/tsconfig.json")

    # Dependencies are installed once and stamped, rather than on every build:
    # `npm ci` deletes and repopulates node_modules, which takes seconds and
    # reaches the network. Keyed on the lock file, so it runs again exactly when
    # the pinned tree changes.
    set(install_stamp "${CMAKE_BINARY_DIR}/webui-install.stamp")

    add_custom_command(
        OUTPUT "${install_stamp}"
        COMMAND "${APOLLO_NPM_EXECUTABLE}" ci --no-audit --no-fund
        COMMAND "${CMAKE_COMMAND}" -E touch "${install_stamp}"
        DEPENDS "${webui_source}/package-lock.json" "${webui_source}/package.json"
        WORKING_DIRECTORY "${webui_source}"
        COMMENT "Installing Apollo's frontend dependencies"
        VERBATIM)

    # Type-checked before it is bundled. esbuild strips types without looking at
    # them, so without this step a type error would reach the plugin as a
    # perfectly valid bundle that is wrong at run time.
    add_custom_command(
        OUTPUT ${built}
        COMMAND "${CMAKE_COMMAND}" -E env "APOLLO_WEBUI_OUT=${webui_output}"
                "${APOLLO_NPM_EXECUTABLE}" run check
        DEPENDS ${webui_sources} "${install_stamp}"
        WORKING_DIRECTORY "${webui_source}"
        COMMENT "Building Apollo's interface"
        VERBATIM)

    add_custom_target(apollo_webui DEPENDS ${built})
    set_target_properties(apollo_webui PROPERTIES FOLDER "Apollo")

    set(${out_files} ${built} PARENT_SCOPE)
endfunction()
