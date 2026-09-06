# WebView backend acquisition.
#
# Apollo's UI is delivered through JUCE's WebView facilities. JUCE picks the
# platform backend itself — WKWebView on macOS, WebKitGTK on Linux — and those
# come from the system. Windows is the exception: JUCE links WebView2, whose
# headers and import library ship in a NuGet package that is not present on a
# stock machine and that JUCE does not vendor.
#
# Rather than require every developer to install that package by hand and then
# fail the build with a cryptic FindWebView2 error when they have not, Apollo
# fetches it at a pinned version, exactly as it does for JUCE itself
# (CLAUDE.md §32: explicit, pinned, reproducible).
#
# The runtime is a separate matter: the WebView2 *runtime* ships with Microsoft
# Edge and is present on effectively every Windows 10/11 machine. Only the
# build-time SDK is fetched here.

include_guard(GLOBAL)

if(NOT APOLLO_ENABLE_WEBVIEW)
    set(APOLLO_WEBVIEW_DESCRIPTION "disabled")
    return()
endif()

if(NOT WIN32)
    # macOS and Linux use the system-provided backend; nothing to acquire.
    set(APOLLO_WEBVIEW_DESCRIPTION "system backend")
    return()
endif()

# Pinned by exact version. The package layout FindWebView2.cmake expects is
# build/native/include/WebView2.h plus build/native/<arch>/WebView2LoaderStatic.lib,
# which is what this package provides.
set(APOLLO_WEBVIEW2_VERSION "1.0.2903.40")

# JUCE's FindWebView2 does NOT take the package directory itself. It globs
#     ${JUCE_WEBVIEW2_PACKAGE_LOCATION}/*Microsoft.Web.WebView2*
# and treats the first match as the package root — that is, it expects a NuGet
# *packages folder* containing one or more versioned package directories.
#
# Extracting the archive straight into the location therefore fails in a way
# that looks like success: the glob matches the package's own
# Microsoft.Web.WebView2.nuspec *file*, find_path is given a file as a hint and
# returns NOTFOUND, and find_package_handle_standard_args still reports "Found"
# because the composed include path is a non-empty string. The build then fails
# much later on a missing WebView2.h.
#
# So the package is extracted into a versioned subdirectory and the *parent* is
# handed to JUCE, reproducing the layout it expects.
set(APOLLO_WEBVIEW2_ROOT "${CMAKE_BINARY_DIR}/_deps/webview2-packages")
set(APOLLO_WEBVIEW2_PACKAGE_DIR
    "${APOLLO_WEBVIEW2_ROOT}/Microsoft.Web.WebView2.${APOLLO_WEBVIEW2_VERSION}")

include(FetchContent)

FetchContent_Declare(webview2
    URL "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${APOLLO_WEBVIEW2_VERSION}"
    SOURCE_DIR "${APOLLO_WEBVIEW2_PACKAGE_DIR}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

FetchContent_MakeAvailable(webview2)

# Validate what JUCE will actually look for, not merely that the header exists
# somewhere. The two are not the same thing, as the layout note above explains.
file(GLOB apollo_webview2_candidates "${APOLLO_WEBVIEW2_ROOT}/*Microsoft.Web.WebView2*")
set(apollo_webview2_ok FALSE)

foreach(candidate IN LISTS apollo_webview2_candidates)
    if(IS_DIRECTORY "${candidate}" AND EXISTS "${candidate}/build/native/include/WebView2.h")
        set(apollo_webview2_ok TRUE)
    endif()
endforeach()

if(NOT apollo_webview2_ok)
    message(FATAL_ERROR
        "The WebView2 package was fetched but is not in the layout JUCE expects.\n"
        "  packages folder: ${APOLLO_WEBVIEW2_ROOT}\n"
        "Configure with -DAPOLLO_ENABLE_WEBVIEW=OFF to build without the WebView UI.")
endif()

# find_path caches its result, including a failure. A stale NOTFOUND from an
# earlier configure would survive a corrected layout, so it is cleared here —
# safe because this branch owns the package location outright.
unset(WebView2_root_dir CACHE)

# Consumed by JUCE's FindWebView2, which juce_add_plugin invokes for a target
# that sets NEEDS_WEBVIEW2.
set(JUCE_WEBVIEW2_PACKAGE_LOCATION "${APOLLO_WEBVIEW2_ROOT}" CACHE PATH
    "Location of the WebView2 packages folder used by JUCE" FORCE)

set(APOLLO_WEBVIEW_DESCRIPTION "WebView2 ${APOLLO_WEBVIEW2_VERSION} (fetched)")
