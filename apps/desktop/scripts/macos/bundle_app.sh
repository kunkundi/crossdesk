#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "Usage: $0 <app-bundle> <expected-architecture> <slint-runtime>" >&2
    exit 2
fi

APP_BUNDLE="$1"
EXPECTED_ARCH="$2"
SLINT_LIBRARY="$3"
APP_EXECUTABLE="$APP_BUNDLE/Contents/MacOS/CrossDesk"
FRAMEWORKS_DIR="$APP_BUNDLE/Contents/Frameworks"

for command_name in codesign install_name_tool lipo otool strip; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Required bundling command is missing: $command_name" >&2
        exit 1
    fi
done

if [[ ! -x "$APP_EXECUTABLE" ]]; then
    echo "Missing app executable: $APP_EXECUTABLE" >&2
    exit 1
fi

if ! lipo "$APP_EXECUTABLE" -verify_arch "$EXPECTED_ARCH" >/dev/null 2>&1; then
    echo "App executable does not contain the expected architecture: $EXPECTED_ARCH" >&2
    lipo -archs "$APP_EXECUTABLE" >&2 || true
    exit 1
fi

SLINT_REFERENCE="$(otool -L "$APP_EXECUTABLE" | awk \
    '$1 ~ /(^|\/)libslint_cpp[^\/[:space:]]*\.dylib$/ { print $1; exit }')"
if [[ -z "$SLINT_REFERENCE" ]]; then
    echo "The app executable does not reference the Slint runtime" >&2
    exit 1
fi

SLINT_NAME="${SLINT_REFERENCE##*/}"
SLINT_CURRENT_VERSION="$(otool -L "$APP_EXECUTABLE" | awk -v dependency="$SLINT_REFERENCE" '
    $1 == dependency {
        for (i = 1; i <= NF; ++i) {
            if ($i == "current") {
                version = $(i + 2)
                gsub(/[),]/, "", version)
                print version
                exit
            }
        }
    }')"

slint_runtime_version() {
    local library="$1"
    otool -L "$library" | awk -v name="$SLINT_NAME" '
        index($1, "/" name) || $1 == "@rpath/" name {
            for (i = 1; i <= NF; ++i) {
                if ($i == "current") {
                    version = $(i + 2)
                    gsub(/[),]/, "", version)
                    print version
                    exit
                }
            }
        }'
}

if [[ ! -f "$SLINT_LIBRARY" ]] || ! lipo "$SLINT_LIBRARY" -verify_arch "$EXPECTED_ARCH" >/dev/null 2>&1; then
    echo "Missing $EXPECTED_ARCH Slint runtime from the application build: $SLINT_LIBRARY" >&2
    exit 1
fi
if [[ -n "$SLINT_CURRENT_VERSION" && "$(slint_runtime_version "$SLINT_LIBRARY")" != "$SLINT_CURRENT_VERSION" ]]; then
    echo "Expected Slint runtime version $SLINT_CURRENT_VERSION in $SLINT_LIBRARY" >&2
    exit 1
fi

install -d "$FRAMEWORKS_DIR"
install -m 0755 "$SLINT_LIBRARY" "$FRAMEWORKS_DIR/$SLINT_NAME"

install_name_tool -id "@rpath/$SLINT_NAME" "$FRAMEWORKS_DIR/$SLINT_NAME"
if [[ "$SLINT_REFERENCE" != "@rpath/$SLINT_NAME" ]]; then
    install_name_tool -change "$SLINT_REFERENCE" "@rpath/$SLINT_NAME" "$APP_EXECUTABLE"
fi

if ! otool -l "$APP_EXECUTABLE" | awk '
    $1 == "cmd" { in_rpath = ($2 == "LC_RPATH"); next }
    in_rpath && $1 == "path" && $2 == "@executable_path/../Frameworks" { found = 1 }
    END { exit(found ? 0 : 1) }'; then
    install_name_tool -add_rpath "@executable_path/../Frameworks" "$APP_EXECUTABLE"
fi

# Strip only the staged runtime, retaining exported symbols for Slint's C ABI.
# The package cache remains intact for debugging and other consumers.
strip -S -x "$FRAMEWORKS_DIR/$SLINT_NAME"

# install_name_tool and strip invalidate existing signatures. Sign only after
# all bundle changes, using Developer ID when configured or ad-hoc for local use.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
bash "$SCRIPT_DIR/sign_app.sh" "$APP_BUNDLE"

echo "Bundled Slint runtime: $SLINT_LIBRARY"
