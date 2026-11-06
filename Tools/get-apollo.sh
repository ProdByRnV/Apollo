#!/usr/bin/env bash
#
# Installs Apollo on macOS or Linux and opens it, in one command:
#
#   macOS   curl -fsSL https://raw.githubusercontent.com/ProdByRnV/Apollo/main/Tools/get-apollo.sh | bash
#   Linux   wget -qO- https://raw.githubusercontent.com/ProdByRnV/Apollo/main/Tools/get-apollo.sh | bash
#
# What it does, in order:
#
#   1. Asks GitHub which release is the latest, and which download is for this
#      computer: one for every Mac, one for each Linux processor.
#   2. If that version is already installed, opens it and stops.
#   3. Otherwise downloads it, and checks it against the checksums published
#      with the release. A download that does not match is not installed.
#   4. Installs the application and the plugin where they belong, for this user:
#        macOS   /Applications/Apollo.app (or ~/Applications without the rights)
#                ~/Library/Audio/Plug-Ins/VST3/Apollo.vst3
#        Linux   ~/.local/opt/apollo/Apollo, an "apollo" command, a menu entry
#                ~/.vst3/Apollo.vst3
#   5. Opens Apollo.
#
# WHY THIS WORKS WHERE DOUBLE-CLICKING DOES NOT, ON A MAC. macOS marks anything
# a web browser downloads, and refuses to open marked software that is not
# signed with an Apple Developer ID, which Apollo is not. A file fetched from
# Terminal is not marked, so there is nothing to refuse; and anything Apollo
# that a browser did mark is unmarked here too.
#
# If GitHub cannot be reached, it looks for an Apollo already on this computer —
# installed, or unzipped in Downloads or on the Desktop — and opens that.
#
# Nothing here needs administrator rights, and nothing outside the places above
# is touched. Run it again to update.
#
# For testing: APOLLO_ZIP=<file> installs that archive instead of downloading
# one, and APOLLO_NO_LAUNCH=1 installs without opening anything.
#
# Written for the bash every Mac ships, which is version 3.2.
#
# Not `pipefail`: several lookups below end in `head`, which closes its pipe
# early, and under pipefail the writer that pipe killed would fail a lookup that
# found what it wanted. Every step that can fail is checked by name instead.

set -eu

readonly repository="ProdByRnV/Apollo"
readonly latest="https://api.github.com/repos/${repository}/releases/latest"

say()  { printf '%s\n' "$*"; }
fail() { printf '\nApollo: %s\n' "$*" >&2; exit 1; }

#-------------------------------------------------------------------------------
# Which computer this is.

case "$(uname -s)" in
    Darwin) system=macos ;;
    Linux)  system=linux ;;
    *)      fail "this installer is for macOS and Linux. On Windows, paste this into PowerShell instead: irm https://raw.githubusercontent.com/${repository}/main/Tools/get-apollo.ps1 | iex" ;;
esac

if [ "$system" = macos ]; then
    package=macos-universal
else
    case "$(uname -m)" in
        x86_64|amd64)  package=linux-x86_64 ;;
        aarch64|arm64) package=linux-arm64 ;;
        *)             fail "there is no Apollo for this processor ($(uname -m)) yet." ;;
    esac
fi

#-------------------------------------------------------------------------------
# Tools.

# Downloads $1 to $2, with curl or wget, whichever the system has.
fetch() {
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL --retry 2 -o "$2" "$1"
    elif command -v wget >/dev/null 2>&1; then
        wget -q -O "$2" "$1"
    else
        return 1
    fi
}

# The GitHub API, which also takes a token when one is given, so a test that
# calls it often is not turned away.
fetch_api() {
    if [ -n "${APOLLO_GITHUB_TOKEN:-}" ] && command -v curl >/dev/null 2>&1; then
        curl -fsSL --retry 2 -H "Authorization: Bearer ${APOLLO_GITHUB_TOKEN}" -o "$2" "$1"
    else
        fetch "$1" "$2"
    fi
}

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d ' ' -f 1
    else
        shasum -a 256 "$1" | cut -d ' ' -f 1
    fi
}

# Unpacks archive $1 into directory $2, keeping what is executable executable.
unpack() {
    if [ "$system" = macos ]; then
        ditto -x -k "$1" "$2"
    elif command -v unzip >/dev/null 2>&1; then
        unzip -q "$1" -d "$2"
    elif command -v python3 >/dev/null 2>&1; then
        python3 -m zipfile -e "$1" "$2"
    elif command -v bsdtar >/dev/null 2>&1; then
        bsdtar -xf "$1" -C "$2"
    else
        fail "unpacking needs unzip. Install it (for example: sudo apt install unzip) and run this again."
    fi
}

work=$(mktemp -d "${TMPDIR:-/tmp}/apollo.XXXXXX")
trap 'rm -rf "$work"' EXIT

#-------------------------------------------------------------------------------
# Where things go.

if [ "$system" = macos ]; then
    if [ -w /Applications ]; then
        applications=/Applications
    else
        applications="$HOME/Applications"
    fi

    installed_app="$applications/Apollo.app"
    plugins="$HOME/Library/Audio/Plug-Ins/VST3"
else
    program_dir="$HOME/.local/opt/apollo"
    installed_app="$program_dir/Apollo"
    plugins="$HOME/.vst3"
fi

installed_version() {
    if [ "$system" = macos ]; then
        [ -d "$installed_app" ] || return 0
        /usr/libexec/PlistBuddy -c 'Print CFBundleShortVersionString' "$installed_app/Contents/Info.plist" 2>/dev/null || true
    else
        if [ -f "$program_dir/VERSION" ]; then cat "$program_dir/VERSION"; fi
    fi
}

#-------------------------------------------------------------------------------
# Opening it.

open_apollo() {
    local app="$1"

    if [ "${APOLLO_NO_LAUNCH:-}" = 1 ]; then
        say "Not opening it (APOLLO_NO_LAUNCH is set)."
        return 0
    fi

    say "Opening Apollo…"

    if [ "$system" = macos ]; then
        open "$app"
    elif [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then
        nohup "$app" >/dev/null 2>&1 &
    else
        say "There is no screen in this session to open it on. From your desktop, run: apollo"
    fi
}

# What a browser marked is unmarked, and a signature that does not check out
# is replaced with an ad-hoc one, which an Apple Silicon Mac needs to run code
# at all. Both only ever touch Apollo.
make_runnable_on_this_mac() {
    local bundle
    for bundle in "$@"; do
        [ -e "$bundle" ] || continue
        xattr -dr com.apple.quarantine "$bundle" 2>/dev/null || true
        codesign --verify "$bundle" >/dev/null 2>&1 || codesign --force --deep --sign - "$bundle" >/dev/null 2>&1 || true
    done
}

# The libraries Apollo's interface opens while it runs, which a minimal Linux
# may not have (INSTALL.txt). Reported, with the command that installs them;
# the sound works without them.
check_linux_libraries() {
    local listing missing=""
    listing=$( (ldconfig -p 2>/dev/null || /sbin/ldconfig -p 2>/dev/null) || true)

    [ -n "$listing" ] || return 0

    local soname
    for soname in libX11.so.6 libXext.so.6 libXcursor.so.1 libXinerama.so.1 libXrender.so.1 libXrandr.so.2 \
                  libfreetype.so.6 libfontconfig.so.1 libasound.so.2 libgtk-3.so.0; do
        printf '%s' "$listing" | grep -Fq "$soname" || missing="$missing $soname"
    done

    if ! printf '%s' "$listing" | grep -Eq 'libwebkit2gtk-4\.(0|1)\.so'; then
        missing="$missing WebKitGTK"
    fi

    [ -n "$missing" ] || return 0

    say ""
    say "Apollo's window needs some libraries this system does not have:$missing"
    say "Install them with:"

    if command -v apt-get >/dev/null 2>&1; then
        say "  sudo apt install libx11-6 libxext6 libxcursor1 libxinerama1 libxrender1 libxrandr2 libfreetype6 libfontconfig1 libasound2 libgtk-3-0 libwebkit2gtk-4.1-0"
    elif command -v dnf >/dev/null 2>&1; then
        say "  sudo dnf install libX11 libXext libXcursor libXinerama libXrender libXrandr freetype fontconfig alsa-lib gtk3 webkit2gtk4.1"
    elif command -v pacman >/dev/null 2>&1; then
        say "  sudo pacman -S libx11 libxext libxcursor libxinerama libxrender libxrandr freetype2 fontconfig alsa-lib gtk3 webkit2gtk-4.1"
    else
        say "  your package manager's X11, GTK 3 and WebKitGTK packages (see INSTALL.txt in the download)"
    fi

    say "then open Apollo from your applications menu, or run: apollo"
    say ""
}

#-------------------------------------------------------------------------------
# No connection: open whichever Apollo is already here.

open_one_already_here() {
    local found=""

    if [ "$system" = macos ]; then
        for candidate in "$installed_app" /Applications/Apollo.app "$HOME/Applications/Apollo.app"; do
            if [ -d "$candidate" ]; then found="$candidate"; break; fi
        done

        if [ -z "$found" ]; then
            found=$(find "$HOME/Downloads" "$HOME/Desktop" -maxdepth 4 -type d -name Apollo.app 2>/dev/null | head -n 1 || true)
        fi

        if [ -z "$found" ] && command -v mdfind >/dev/null 2>&1; then
            found=$(mdfind 'kMDItemFSName == "Apollo.app"' 2>/dev/null | head -n 1 || true)
        fi

        [ -n "$found" ] || return 1
        make_runnable_on_this_mac "$found"
    else
        if [ -x "$installed_app" ]; then
            found="$installed_app"
        else
            found=$(find "$HOME/Downloads" "$HOME/Desktop" -maxdepth 4 -type f -name Apollo -not -path '*.vst3/*' 2>/dev/null | head -n 1 || true)
        fi

        [ -n "$found" ] || return 1
        chmod +x "$found"
    fi

    say "Found Apollo at $found"
    open_apollo "$found"
}

#-------------------------------------------------------------------------------
# The download.

version=""
archive=""

if [ -n "${APOLLO_ZIP:-}" ]; then
    [ -f "$APOLLO_ZIP" ] || fail "no archive at $APOLLO_ZIP"
    archive="$APOLLO_ZIP"
    version="(local archive)"
    say "Installing Apollo from $archive"
else
    say "Looking for the latest Apollo…"

    if ! fetch_api "$latest" "$work/release.json"; then
        say "GitHub could not be reached. Looking for an Apollo already on this computer…"
        open_one_already_here || fail "no connection, and no Apollo on this computer to open. Connect to the internet and run this again."
        exit 0
    fi

    release=$(tr -d '\r' < "$work/release.json")

    tag=$(printf '%s' "$release" | grep -o '"tag_name": *"[^"]*"' | head -n 1 | sed 's/.*"\([^"]*\)"$/\1/')
    zip_url=$(printf '%s' "$release" | grep -o '"browser_download_url": *"[^"]*-'"$package"'\.zip"' | head -n 1 | sed 's/.*"\(https[^"]*\)"$/\1/')
    sums_url=$(printf '%s' "$release" | grep -o '"browser_download_url": *"[^"]*SHA256SUMS\.txt"' | head -n 1 | sed 's/.*"\(https[^"]*\)"$/\1/')

    if [ -z "$tag" ] || [ -z "$zip_url" ] || [ -z "$sums_url" ]; then
        fail "the latest release has no download for this computer ($package)."
    fi

    version="${tag#v}"

    if [ "$(installed_version)" = "$version" ]; then
        say "Apollo $version is already installed."
        if [ "$system" = macos ]; then make_runnable_on_this_mac "$installed_app" "$plugins/Apollo.vst3"; fi
        open_apollo "$installed_app"
        exit 0
    fi

    archive="$work/$(basename "$zip_url")"

    say "Downloading Apollo $version for this computer…"
    fetch "$zip_url" "$archive" || fail "the download did not finish. Run this again."
    fetch "$sums_url" "$work/SHA256SUMS.txt" || fail "the checksums could not be downloaded, so the download cannot be checked."

    expected=$(grep -F "$(basename "$zip_url")" "$work/SHA256SUMS.txt" | head -n 1 | cut -d ' ' -f 1)
    actual=$(sha256_of "$archive")

    if [ -z "$expected" ] || [ "$expected" != "$actual" ]; then
        fail "the download does not match its published checksum, so it was not installed. Run this again."
    fi

    say "Checked: the download matches its published checksum."
fi

#-------------------------------------------------------------------------------
# Unpacking, and finding what is in it. Searched for rather than assumed, so an
# archive that keeps the application in a folder of its own, as releases before
# 1.3.0 did, installs too.

mkdir -p "$work/unpacked"
unpack "$archive" "$work/unpacked"

plugin=$(find "$work/unpacked" -maxdepth 3 -type d -name Apollo.vst3 | head -n 1 || true)

if [ "$system" = macos ]; then
    app=$(find "$work/unpacked" -maxdepth 3 -type d -name Apollo.app | head -n 1 || true)
else
    app=$(find "$work/unpacked" -maxdepth 3 -type f -name Apollo -not -path '*.vst3/*' | head -n 1 || true)
fi

[ -n "$app" ] || fail "the download has no Apollo application in it."

#-------------------------------------------------------------------------------
# Installing.

if [ "$system" = macos ]; then
    mkdir -p "$applications"
    rm -rf "$installed_app"
    ditto "$app" "$installed_app"

    if [ -n "$plugin" ]; then
        mkdir -p "$plugins"
        rm -rf "$plugins/Apollo.vst3"
        ditto "$plugin" "$plugins/Apollo.vst3"
    fi

    make_runnable_on_this_mac "$installed_app" "$plugins/Apollo.vst3"
else
    mkdir -p "$program_dir"
    rm -f "$installed_app"
    cp "$app" "$installed_app"
    chmod +x "$installed_app"
    printf '%s\n' "$version" > "$program_dir/VERSION"

    mkdir -p "$HOME/.local/bin"
    ln -sf "$installed_app" "$HOME/.local/bin/apollo"

    applications_menu="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
    mkdir -p "$applications_menu"
    cat > "$applications_menu/apollo.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Apollo
Comment=Wavetable synthesiser
Exec="$installed_app"
Icon=audio-x-generic
Terminal=false
Categories=AudioVideo;Audio;Music;
EOF

    if [ -n "$plugin" ]; then
        mkdir -p "$plugins"
        rm -rf "$plugins/Apollo.vst3"
        cp -R "$plugin" "$plugins/Apollo.vst3"
    fi
fi

say ""
say "Apollo $version is installed."
say "  Application   $installed_app"
if [ -n "$plugin" ]; then
    say "  Plugin        $plugins/Apollo.vst3 (rescan your plugins in your DAW to find it)"
fi

if [ "$system" = macos ]; then
    say "  Next time, open it from Launchpad or your Applications folder."
else
    say "  Next time, open it from your applications menu, or run: apollo"
    check_linux_libraries
fi

say ""
open_apollo "$installed_app"
