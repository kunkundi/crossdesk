#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "Usage: $0 <app-bundle> <expected-architecture>" >&2
    exit 2
fi

APP_BUNDLE="$1"
EXPECTED_ARCH="$2"
APP_EXECUTABLE="$APP_BUNDLE/Contents/MacOS/CrossDesk"
FRAMEWORKS_DIR="$APP_BUNDLE/Contents/Frameworks"
INFO_PLIST="$APP_BUNDLE/Contents/Info.plist"

if [[ ! -x "$APP_EXECUTABLE" ]]; then
    echo "Missing app executable: $APP_EXECUTABLE" >&2
    exit 1
fi

if ! lipo "$APP_EXECUTABLE" -verify_arch "$EXPECTED_ARCH" >/dev/null 2>&1; then
    echo "Unexpected app architecture; expected $EXPECTED_ARCH" >&2
    lipo -archs "$APP_EXECUTABLE" >&2 || true
    exit 1
fi

if ! otool -l "$APP_EXECUTABLE" | awk '
    $1 == "cmd" { in_rpath = ($2 == "LC_RPATH"); next }
    in_rpath && $1 == "path" && $2 == "@executable_path/../Frameworks" { found = 1 }
    END { exit(found ? 0 : 1) }'; then
    echo "Missing app rpath: @executable_path/../Frameworks" >&2
    exit 1
fi

verify_dependencies() {
    local macho="$1"
    local dependency=""
    local relative_path=""

    while IFS= read -r dependency; do
        case "$dependency" in
            /System/Library/*|/usr/lib/*)
                ;;
            @rpath/*)
                relative_path="${dependency#@rpath/}"
                if [[ ! -f "$FRAMEWORKS_DIR/$relative_path" ]]; then
                    echo "Unbundled dependency in $macho: $dependency" >&2
                    return 1
                fi
                ;;
            @executable_path/../Frameworks/*)
                relative_path="${dependency#@executable_path/../Frameworks/}"
                if [[ ! -f "$FRAMEWORKS_DIR/$relative_path" ]]; then
                    echo "Unbundled dependency in $macho: $dependency" >&2
                    return 1
                fi
                ;;
            @loader_path/*)
                relative_path="${dependency#@loader_path/}"
                if [[ ! -f "$(dirname "$macho")/$relative_path" ]]; then
                    echo "Unbundled dependency in $macho: $dependency" >&2
                    return 1
                fi
                ;;
            *)
                echo "External non-system dependency in $macho: $dependency" >&2
                return 1
                ;;
        esac
    done < <(otool -L "$macho" | tail -n +2 | awk '{ print $1 }')
}

verify_dependencies "$APP_EXECUTABLE"

SLINT_LIBRARY="$(find "$FRAMEWORKS_DIR" -maxdepth 1 -type f -name 'libslint_cpp*.dylib' -print -quit 2>/dev/null || true)"
if [[ -z "$SLINT_LIBRARY" ]]; then
    echo "The packaged app does not contain libslint_cpp.dylib" >&2
    exit 1
fi

if ! lipo "$SLINT_LIBRARY" -verify_arch "$EXPECTED_ARCH" >/dev/null 2>&1; then
    echo "Unexpected Slint runtime architecture; expected $EXPECTED_ARCH" >&2
    lipo -archs "$SLINT_LIBRARY" >&2 || true
    exit 1
fi
verify_dependencies "$SLINT_LIBRARY"

PLIST_MIN_VERSION="$(/usr/libexec/PlistBuddy -c 'Print :LSMinimumSystemVersion' "$INFO_PLIST")"
BINARY_MIN_VERSION="$(otool -l "$APP_EXECUTABLE" | awk '
    $1 == "cmd" { build_version = ($2 == "LC_BUILD_VERSION"); version_min = ($2 == "LC_VERSION_MIN_MACOSX"); next }
    build_version && $1 == "minos" { print $2; exit }
    version_min && $1 == "version" { print $2; exit }')"
if [[ -z "$BINARY_MIN_VERSION" || "$PLIST_MIN_VERSION" != "$BINARY_MIN_VERSION" ]]; then
    echo "Minimum macOS version mismatch: plist=$PLIST_MIN_VERSION binary=${BINARY_MIN_VERSION:-unknown}" >&2
    exit 1
fi

codesign --verify --deep --strict --verbose=2 "$APP_BUNDLE"

echo "Verified $APP_BUNDLE ($EXPECTED_ARCH, macOS $BINARY_MIN_VERSION+)"
