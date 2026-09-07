# Apollo native source.
#
# Two libraries, split by dependency rather than by feature:
#
#   apollo_core   STATIC, JUCE-free. Pure logic that can be unit tested with no
#                 framework, no audio device and no plugin host. Held to
#                 apollo::strict_warnings.
#
#   apollo_engine INTERFACE, JUCE-dependent. The synthesis engine and plugin
#                 glue, propagated as sources to each final target.
#
# Why apollo_engine is INTERFACE and not STATIC: JUCE modules add their sources
# to whichever target links them. A STATIC library that linked
# juce_audio_processors would compile the JUCE module sources into itself, and
# the plugin target would compile them a second time, producing duplicate
# symbols at link. An INTERFACE library propagates Apollo's own sources to each
# final target instead — the same mechanism JUCE uses for its own modules — so
# the plugin and the test runner each get one clean copy.

# ---------------------------------------------------------------------------
# apollo_core
# ---------------------------------------------------------------------------

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/ApolloVersion.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/ApolloVersion.h"
    @ONLY)

add_library(apollo_core STATIC
    DSP/Oscillators/Wavetable.cpp
    DSP/Oscillators/Wavetable.h
    DSP/Oscillators/WavetableLibrary.cpp
    DSP/Oscillators/WavetableLibrary.h
    DSP/Oscillators/WavetableOscillator.cpp
    DSP/Oscillators/WavetableOscillator.h
    Engine/Voice.cpp
    Engine/Voice.h
    Engine/VoiceEngine.cpp
    Engine/VoiceEngine.h
    Parameters/ParameterId.cpp
    Parameters/ParameterId.h
    Parameters/ParameterDefinitions.h
    "${CMAKE_CURRENT_BINARY_DIR}/generated/ApolloVersion.h")

add_library(apollo::core ALIAS apollo_core)

target_include_directories(apollo_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")

# project_options is PUBLIC: the language level and any sanitizer flags must
# reach consumers and their link steps. strict_warnings is PRIVATE: Apollo holds
# its own code to that standard without imposing it on anything that links here.
target_link_libraries(apollo_core
    PUBLIC  apollo::project_options
    PRIVATE apollo::strict_warnings)

set_target_properties(apollo_core PROPERTIES
    FOLDER "Apollo"
    POSITION_INDEPENDENT_CODE ON)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    Parameters/ParameterId.cpp
    Parameters/ParameterId.h)

# ---------------------------------------------------------------------------
# apollo_engine
# ---------------------------------------------------------------------------

add_library(apollo_engine INTERFACE)
add_library(apollo::engine ALIAS apollo_engine)

target_sources(apollo_engine INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/Audio/ApolloAudioProcessor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Audio/ApolloAudioProcessor.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Parameters/ParameterLayout.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Parameters/ParameterLayout.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/State/StateSerialization.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/State/StateSerialization.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/BridgeProtocol.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/BridgeProtocol.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/ParameterBridge.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/ParameterBridge.h")

# ---------------------------------------------------------------------------
# apollo_editor
# ---------------------------------------------------------------------------
#
# The WebView editor is a separate INTERFACE library rather than part of
# apollo_engine, for two reasons:
#
#   - Layering. The engine must not depend on the UI (ARCHITECTURE.md §5.2);
#     making that a build-level fact rather than a convention means a stray
#     include from DSP code fails to link.
#   - The test runner is headless. It exercises the bridge through its protocol
#     layer, which needs no browser, so pulling juce_gui_extra and the WebView2
#     SDK into the test binary would add platform dependencies (and CI packages)
#     that buy nothing.
#
# APOLLO_WITH_WEBVIEW is therefore a per-target definition, not an engine-wide
# one: the plugin builds the WebView editor, the test runner builds the generic
# fallback.

add_library(apollo_editor INTERFACE)
add_library(apollo::editor ALIAS apollo_editor)

if(APOLLO_ENABLE_WEBVIEW)
    target_sources(apollo_editor INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/UI/ApolloWebViewEditor.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/UI/ApolloWebViewEditor.h")

    target_link_libraries(apollo_editor INTERFACE
        apollo::engine
        juce::juce_gui_extra)

    # JUCE_WEB_BROWSER is deliberately NOT defined here. On Linux JUCE sets it
    # itself from the plugin's NEEDS_WEB_BROWSER property, in the same place it
    # links the pkg-config target carrying the GTK/WebKitGTK include paths.
    # Defining it here as well produced two conflicting command-line definitions
    # and a build that included <gtk/gtk.h> with no path to find it.
    # On Windows and macOS the module default is already 1.
    target_compile_definitions(apollo_editor INTERFACE
        APOLLO_WITH_WEBVIEW=1)

    if(WIN32)
        # Selects JUCE's WebView2 backend rather than the legacy IE control.
        target_compile_definitions(apollo_editor INTERFACE JUCE_USE_WIN_WEBVIEW2=1)
    endif()
else()
    # Without the WebView, the processor falls back to a generic parameter
    # editor rather than to no editor at all, so the plugin stays usable on a
    # configuration where the platform backend is unavailable (CLAUDE.md §33).
    target_link_libraries(apollo_editor INTERFACE apollo::engine)
    target_compile_definitions(apollo_editor INTERFACE
        APOLLO_WITH_WEBVIEW=0
        JUCE_WEB_BROWSER=0)
endif()

target_include_directories(apollo_engine INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(apollo_engine INTERFACE
    apollo::core
    juce::juce_audio_processors
    juce::juce_audio_utils)

# Applied to every target that compiles the engine, so the plugin and the test
# runner cannot disagree about how JUCE itself is configured.
target_compile_definitions(apollo_engine INTERFACE
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

add_subdirectory(Plugin)
