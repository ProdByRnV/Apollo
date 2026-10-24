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
    # effect. Installation is an explicit, packaged step (Phase 11). A
    # developer who wants each build in a host turns on
    # APOLLO_COPY_VST3_TO_PLUGIN_FOLDER below, which is off by default.
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(Apollo PRIVATE
    PluginEntry.cpp
    StandaloneMidiAdoption.cpp
    StandaloneMidiAdoption.h
    StandaloneOutputDevice.cpp
    StandaloneOutputDevice.h)

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
    # Delegated to a script so that a copy which cannot happen — almost always
    # because the developer has the root copy open while rebuilding — reports
    # itself and lets the build succeed, rather than turning a good build into a
    # failed one. See CMake/CopyStandaloneToRoot.cmake.
    if(APPLE)
        set(APOLLO_STANDALONE_SOURCE "$<TARGET_BUNDLE_DIR:Apollo_Standalone>")
        set(APOLLO_STANDALONE_DESTINATION "${CMAKE_SOURCE_DIR}/Apollo.app")
        set(APOLLO_STANDALONE_IS_BUNDLE TRUE)
    else()
        set(APOLLO_STANDALONE_SOURCE "$<TARGET_FILE:Apollo_Standalone>")
        set(APOLLO_STANDALONE_DESTINATION
            "${CMAKE_SOURCE_DIR}/$<TARGET_FILE_NAME:Apollo_Standalone>")
        set(APOLLO_STANDALONE_IS_BUNDLE FALSE)
    endif()

    add_custom_command(TARGET Apollo_Standalone POST_BUILD
        COMMAND ${CMAKE_COMMAND}
                -D "APOLLO_STANDALONE_SOURCE=${APOLLO_STANDALONE_SOURCE}"
                -D "APOLLO_STANDALONE_DESTINATION=${APOLLO_STANDALONE_DESTINATION}"
                -D "APOLLO_STANDALONE_IS_BUNDLE=${APOLLO_STANDALONE_IS_BUNDLE}"
                -P "${CMAKE_SOURCE_DIR}/CMake/CopyStandaloneToRoot.cmake"
        COMMENT "Copying the standalone into the project root"
        VERBATIM)
endif()

# ---------------------------------------------------------------------------
# The VST3 in a plugin folder, for opening each build in a host.
#
# Requested by the developer, who loads Apollo in FL Studio. Off by default,
# because installing into a system folder is not something a build does to
# anyone who did not ask (see COPY_PLUGIN_AFTER_BUILD above), and a CI or
# packaging job has no host to open it in.
#
# The folder is the platform's standard VST3 location unless set. On Windows
# that location needs administrator rights, so the bundle folder is set up once
# by hand and the build only replaces what is inside it (Docs/BUILD.md).
option(APOLLO_COPY_VST3_TO_PLUGIN_FOLDER
       "After building, replace the VST3 in APOLLO_VST3_PLUGIN_FOLDER with the new one"
       OFF)

if(WIN32)
    set(_apollo_default_vst3_folder "$ENV{CommonProgramFiles}/VST3")
elseif(APPLE)
    set(_apollo_default_vst3_folder "$ENV{HOME}/Library/Audio/Plug-Ins/VST3")
else()
    set(_apollo_default_vst3_folder "$ENV{HOME}/.vst3")
endif()

file(TO_CMAKE_PATH "${_apollo_default_vst3_folder}" _apollo_default_vst3_folder)

set(APOLLO_VST3_PLUGIN_FOLDER "${_apollo_default_vst3_folder}" CACHE PATH
    "Where APOLLO_COPY_VST3_TO_PLUGIN_FOLDER puts the VST3")

if(APOLLO_COPY_VST3_TO_PLUGIN_FOLDER AND TARGET Apollo_VST3)
    # JUCE's own record of the bundle, as packaging uses: the VST3 is a folder
    # on every platform, but CMake only calls it a bundle on macOS.
    set(_apollo_vst3_bundle
        "$<TARGET_GENEX_EVAL:Apollo_VST3,$<TARGET_PROPERTY:Apollo_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>")

    add_custom_command(TARGET Apollo_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND}
                -D "APOLLO_VST3_SOURCE=${_apollo_vst3_bundle}"
                -D "APOLLO_VST3_FOLDER=${APOLLO_VST3_PLUGIN_FOLDER}"
                -P "${CMAKE_SOURCE_DIR}/CMake/CopyVst3ToPluginFolder.cmake"
        COMMENT "Replacing the VST3 in the plugin folder"
        VERBATIM)
endif()
