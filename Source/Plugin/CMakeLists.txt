# Apollo plugin and standalone targets.
#
# A single juce_add_plugin call produces both the VST3 and the Standalone
# artefacts from one shared target. That is what satisfies the roadmap's
# "shared source modules between plugin and standalone builds": there is exactly
# one copy of the engine, and no opportunity for the two builds to diverge.
#
# Additional formats (CLAP, AU, AAX) are added to FORMATS when licensing and
# JUCE support allow. No engine change is required, which is the point of
# keeping the processor free of format-specific code (ARCHITECTURE.md §8).

set(APOLLO_PLUGIN_WEBVIEW2 FALSE)
if(APOLLO_ENABLE_WEBVIEW AND WIN32)
    # Links the WebView2 loader statically; the SDK is fetched by
    # CMake/ApolloWebView.cmake so this does not require a preinstalled package.
    set(APOLLO_PLUGIN_WEBVIEW2 TRUE)
endif()

juce_add_plugin(Apollo
    # Must be JUCE's own option, not a hand-written JUCE_WEB_BROWSER define.
    # On Linux, _juce_link_optional_libraries reads this property to decide both
    # the compile definition *and* whether to link
    # juce::pkgconfig_JUCE_BROWSER_LINUX_DEPS, which is the only thing that
    # supplies the GTK and WebKitGTK include paths. Defining the macro by hand
    # turns on #include <gtk/gtk.h> without ever providing the path to it.
    NEEDS_WEB_BROWSER           ${APOLLO_ENABLE_WEBVIEW}
    NEEDS_WEBVIEW2              ${APOLLO_PLUGIN_WEBVIEW2}
    VERSION                     "${PROJECT_VERSION}"
    COMPANY_NAME                "ProdByRnV"
    BUNDLE_ID                   "com.prodbyrnv.apollo"
    PLUGIN_MANUFACTURER_CODE    Prnv
    PLUGIN_CODE                 Apol
    FORMATS                     VST3 Standalone
    PRODUCT_NAME                "Apollo"
    DESCRIPTION                 "${PROJECT_DESCRIPTION}"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES             "Instrument" "Synth"
    # Never install into the user's system plugin folders as a build side
    # effect. Installation is an explicit, packaged step (Phase 11).
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(Apollo PRIVATE
    PluginEntry.cpp)

# apollo::strict_warnings is deliberately absent: this target compiles JUCE
# module sources, which are held to JUCE's warning standard rather than
# Apollo's. See Docs/CODING-STANDARDS.md §2.
target_link_libraries(Apollo PRIVATE
    apollo::editor
    apollo::engine
    apollo::project_options
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

set_target_properties(Apollo PROPERTIES FOLDER "Apollo/Plugin")

# ---------------------------------------------------------------------------
# A runnable copy of the standalone in the project root.
#
# Requested by the developer, who launches the built application constantly and
# should not have to walk down to
# build*/Source/Plugin/Apollo_artefacts/<config>/Standalone/ to find it. The
# copy lands beside the top-level markdown files and is ignored by git.
#
# ONE NAME, LAST BUILD WINS. Building Debug and then Release leaves the Release
# build in the root, because "the one I just built" is what the shortcut is for.
# A per-configuration name would put three binaries beside the documents and
# make the developer choose between them, which is the errand this removes.
#
# An option rather than unconditional, defaulting on: a packaging or CI job has
# no use for it, and turning it off there is better than teaching those jobs to
# clean up after a convenience they never asked for.
option(APOLLO_COPY_STANDALONE_TO_ROOT
       "After building, copy the standalone application into the project root"
       ON)

if(APOLLO_COPY_STANDALONE_TO_ROOT AND TARGET Apollo_Standalone)
    if(APPLE)
        # The macOS standalone is Apollo.app, which is a directory. It is
        # removed first because copy_directory merges into an existing tree
        # rather than replacing it, which would leave a previous build's files
        # inside the new bundle.
        add_custom_command(TARGET Apollo_Standalone POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -rf
                    "${CMAKE_SOURCE_DIR}/Apollo.app"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "$<TARGET_BUNDLE_DIR:Apollo_Standalone>"
                    "${CMAKE_SOURCE_DIR}/Apollo.app"
            COMMENT "Copying the standalone into the project root"
            VERBATIM)
    else()
        add_custom_command(TARGET Apollo_Standalone POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:Apollo_Standalone>"
                    "${CMAKE_SOURCE_DIR}/$<TARGET_FILE_NAME:Apollo_Standalone>"
            COMMENT "Copying the standalone into the project root"
            VERBATIM)
    endif()
endif()
