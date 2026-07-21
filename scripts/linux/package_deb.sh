#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 3 || $# -gt 4 ]]; then
    echo "Usage: $0 <debian-arch> <xmake-arch> <version> [extra-recommendation]" >&2
    exit 2
fi

DEBIAN_ARCH="$1"
XMAKE_ARCH="$2"
INPUT_VERSION="$3"
EXTRA_RECOMMENDATION="${4:-}"

PKG_NAME="crossdesk"
APP_NAME="CrossDesk"
MAINTAINER="Junkun Di <junkun.di@hotmail.com>"
DESCRIPTION="A simple cross-platform remote desktop client."
ALSA_RUNTIME_DEP="libasound2 | libasound2t64"
PORTAL_RUNTIME_RECOMMENDS="xdg-desktop-portal, xdg-desktop-portal-gtk | xdg-desktop-portal-kde | xdg-desktop-portal-wlr"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

normalize_app_version() {
    local input="$1"
    local prefix=""
    local body="$input"

    if [[ "$body" == v* ]]; then
        prefix="v"
        body="${body#v}"
    fi

    if [[ "$body" =~ ^([0-9]+(\.[0-9]+){1,3})-([0-9]{8})-([0-9]+)$ ]]; then
        echo "${prefix}${BASH_REMATCH[1]}-${BASH_REMATCH[4]}-${BASH_REMATCH[3]}"
    else
        echo "$input"
    fi
}

find_slint_runtime() {
    local needed="$1"
    local resolved=""

    resolved="$(ldd "$BUILD_BINARY" 2>/dev/null | awk -v needed="$needed" \
        '$1 == needed && $2 == "=>" && $3 ~ /^\// { print $3; exit }')"
    if [[ -n "$resolved" && -f "$resolved" ]]; then
        echo "$resolved"
        return 0
    fi

    local package_roots=()
    if [[ -n "${XMAKE_GLOBALDIR:-}" ]]; then
        package_roots+=("$XMAKE_GLOBALDIR/.xmake/packages")
    fi
    package_roots+=("$HOME/.xmake/packages" "/data/.xmake/packages")

    local package_root=""
    local candidate=""
    for package_root in "${package_roots[@]}"; do
        [[ -d "$package_root/s/slint" ]] || continue
        candidate="$(find "$package_root/s/slint" -path "*/lib/$needed" -print -quit 2>/dev/null || true)"
        if [[ -n "$candidate" ]]; then
            echo "$candidate"
            return 0
        fi
    done

    return 1
}

APP_VERSION="$(normalize_app_version "$INPUT_VERSION")"
DEB_VERSION="${APP_VERSION#v}"
if [[ ! "$DEB_VERSION" =~ ^[0-9][0-9A-Za-z.+:~_-]*$ ]]; then
    echo "Invalid Debian version: $DEB_VERSION" >&2
    exit 2
fi

BUILD_BINARY="$PROJECT_ROOT/build/linux/$XMAKE_ARCH/release/crossdesk"
if [[ ! -x "$BUILD_BINARY" ]]; then
    echo "Missing release binary: $BUILD_BINARY" >&2
    echo "Build it first with: xmake f -m release -y && xmake b -vy crossdesk" >&2
    exit 1
fi

for command_name in readelf ldd dpkg-deb; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Required packaging command is missing: $command_name" >&2
        exit 1
    fi
done

WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/crossdesk-deb.XXXXXX")"
trap 'rm -rf "$WORK_DIR"' EXIT

DEB_DIR="$WORK_DIR/${PKG_NAME}-${DEB_VERSION}"
DEBIAN_DIR="$DEB_DIR/DEBIAN"
BIN_DIR="$DEB_DIR/usr/bin"
PRIVATE_DIR="$DEB_DIR/usr/lib/$PKG_NAME"
ICON_BASE_DIR="$DEB_DIR/usr/share/icons/hicolor"
DESKTOP_DIR="$DEB_DIR/usr/share/applications"
OUTPUT_FILE="$PROJECT_ROOT/${PKG_NAME}-linux-${DEBIAN_ARCH}-${APP_VERSION}.deb"

install -d "$DEBIAN_DIR" "$BIN_DIR" "$PRIVATE_DIR" "$DESKTOP_DIR"
install -m 0755 "$BUILD_BINARY" "$PRIVATE_DIR/crossdesk-bin"

# Slint is intentionally built as a shared library. Keep that runtime private
# to CrossDesk instead of relying on a non-existent distribution package.
SLINT_NEEDED="$(readelf -d "$BUILD_BINARY" | sed -n \
    's/.*Shared library: \[\(libslint_cpp[^]]*\)\].*/\1/p' | head -n 1)"
if [[ -n "$SLINT_NEEDED" ]]; then
    SLINT_LIBRARY="$(find_slint_runtime "$SLINT_NEEDED" || true)"
    if [[ -z "$SLINT_LIBRARY" ]]; then
        echo "Unable to locate the required Slint runtime: $SLINT_NEEDED" >&2
        exit 1
    fi
    install -m 0755 "$SLINT_LIBRARY" "$PRIVATE_DIR/$SLINT_NEEDED"
fi

cat > "$BIN_DIR/$PKG_NAME" <<'EOF'
#!/bin/sh
private_lib_dir="/usr/lib/crossdesk"
if [ -n "${LD_LIBRARY_PATH:-}" ]; then
    export LD_LIBRARY_PATH="$private_lib_dir:$LD_LIBRARY_PATH"
else
    export LD_LIBRARY_PATH="$private_lib_dir"
fi
exec /usr/lib/crossdesk/crossdesk-bin "$@"
EOF
chmod 0755 "$BIN_DIR/$PKG_NAME"
ln -s "$PKG_NAME" "$BIN_DIR/$APP_NAME"

for size in 16 24 32 48 64 96 128 256; do
    install -d "$ICON_BASE_DIR/${size}x${size}/apps"
    install -m 0644 "$PROJECT_ROOT/icons/linux/crossdesk_${size}x${size}.png" \
        "$ICON_BASE_DIR/${size}x${size}/apps/${PKG_NAME}.png"
done

RECOMMENDS="$PORTAL_RUNTIME_RECOMMENDS"
if [[ -n "$EXTRA_RECOMMENDATION" ]]; then
    RECOMMENDS="$RECOMMENDS, $EXTRA_RECOMMENDATION"
fi

cat > "$DEBIAN_DIR/control" <<EOF
Package: $PKG_NAME
Version: $DEB_VERSION
Architecture: $DEBIAN_ARCH
Maintainer: $MAINTAINER
Description: $DESCRIPTION
Depends: libc6 (>= 2.29), libstdc++6 (>= 9), libx11-6, libxext6,
 libxrender1, libxft2, libxrandr2, libxfixes3, libxcb1, libxcb-randr0,
 libxcb-xtest0, libxcb-xinerama0, libxcb-shape0, libxcb-xkb1,
 libxcb-xfixes0, libxv1, libxtst6, $ALSA_RUNTIME_DEP, libsndio7.0,
 libxcb-shm0, libpulse0, libdrm2, libdbus-1-3
Recommends: $RECOMMENDS
Priority: optional
Section: utils
EOF

cat > "$DESKTOP_DIR/$PKG_NAME.desktop" <<EOF
[Desktop Entry]
Version=$DEB_VERSION
Name=$APP_NAME
Comment=$DESCRIPTION
Exec=/usr/bin/$PKG_NAME
Icon=$PKG_NAME
Terminal=false
Type=Application
Categories=Utility;
EOF

rm -f "$OUTPUT_FILE"
dpkg-deb --root-owner-group --build "$DEB_DIR" "$OUTPUT_FILE"
dpkg-deb --info "$OUTPUT_FILE" >/dev/null
dpkg-deb --contents "$OUTPUT_FILE" > "$WORK_DIR/package-contents.txt"

if [[ -n "$SLINT_NEEDED" ]] && \
   ! grep -q "usr/lib/$PKG_NAME/$SLINT_NEEDED" "$WORK_DIR/package-contents.txt"; then
    echo "Packaged artifact is missing $SLINT_NEEDED" >&2
    exit 1
fi

echo "Deb package created: $OUTPUT_FILE"
