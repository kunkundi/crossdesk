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

for command_name in codesign install_name_tool lipo otool; do
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

find_slint_runtime() {
    local candidate=""
    local candidate_version=""
    local package_root=""
    local package_roots=()

    if [[ "$SLINT_REFERENCE" == /* && -f "$SLINT_REFERENCE" ]]; then
        if lipo "$SLINT_REFERENCE" -verify_arch "$EXPECTED_ARCH" >/dev/null 2>&1; then
            echo "$SLINT_REFERENCE"
            return 0
        fi
    fi

    if [[ -n "${XMAKE_GLOBALDIR:-}" ]]; then
        package_roots+=("$XMAKE_GLOBALDIR/.xmake/packages")
    fi
    package_roots+=("$HOME/.xmake/packages" "/data/.xmake/packages")

    for package_root in "${package_roots[@]}"; do
        [[ -d "$package_root/s/slint" ]] || continue
        while IFS= read -r candidate; do
            if ! lipo "$candidate" -verify_arch "$EXPECTED_ARCH" >/dev/null 2>&1; then
                continue
            fi

            candidate_version="$(slint_runtime_version "$candidate")"
            if [[ -n "$SLINT_CURRENT_VERSION" && "$candidate_version" != "$SLINT_CURRENT_VERSION" ]]; then
                continue
            fi

            echo "$candidate"
            return 0
        done < <(find "$package_root/s/slint" -type f -path "*/lib/$SLINT_NAME" -print 2>/dev/null)
    done

    return 1
}

SLINT_LIBRARY="$(find_slint_runtime || true)"
if [[ -z "$SLINT_LIBRARY" ]]; then
    echo "Unable to locate $SLINT_NAME for $EXPECTED_ARCH (current version $SLINT_CURRENT_VERSION)" >&2
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

# install_name_tool invalidates the linker's ad-hoc signature. Sign nested code
# first, then seal the complete app bundle so local packages remain launchable.
codesign --force --sign - "$FRAMEWORKS_DIR/$SLINT_NAME"
codesign --force --deep --sign - "$APP_BUNDLE"

echo "Bundled Slint runtime: $SLINT_LIBRARY"
