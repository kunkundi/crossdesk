#!/bin/bash
set -e

PKG_NAME="crossdesk"
APP_NAME="CrossDesk"

ARCHITECTURE="amd64"
MAINTAINER="Junkun Di <junkun.di@hotmail.com>"
DESCRIPTION="A simple cross-platform remote desktop client."
ALSA_RUNTIME_DEP="libasound2 | libasound2t64"
PORTAL_RUNTIME_RECOMMENDS="xdg-desktop-portal, xdg-desktop-portal-gtk | xdg-desktop-portal-kde | xdg-desktop-portal-wlr"

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

APP_VERSION="$(normalize_app_version "$1")"

# Remove 'v' prefix from version for Debian package (Debian version must start with digit)
DEB_VERSION="${APP_VERSION#v}"

DEB_DIR="${PKG_NAME}-${DEB_VERSION}"
DEBIAN_DIR="$DEB_DIR/DEBIAN"
BIN_DIR="$DEB_DIR/usr/bin"
ICON_BASE_DIR="$DEB_DIR/usr/share/icons/hicolor"
DESKTOP_DIR="$DEB_DIR/usr/share/applications"

rm -rf "$DEB_DIR"

mkdir -p "$DEBIAN_DIR" "$BIN_DIR" "$DESKTOP_DIR"

cp build/linux/x86_64/release/crossdesk "$BIN_DIR/$PKG_NAME"
chmod +x "$BIN_DIR/$PKG_NAME"

ln -s "$PKG_NAME" "$BIN_DIR/$APP_NAME"

for size in 16 24 32 48 64 96 128 256; do
    mkdir -p "$ICON_BASE_DIR/${size}x${size}/apps"
    cp "icons/linux/crossdesk_${size}x${size}.png" \
       "$ICON_BASE_DIR/${size}x${size}/apps/${PKG_NAME}.png"
done

cat > "$DEBIAN_DIR/control" << EOF
Package: $PKG_NAME
Version: $DEB_VERSION
Architecture: $ARCHITECTURE
Maintainer: $MAINTAINER
Description: $DESCRIPTION
Depends: libc6 (>= 2.29), libstdc++6 (>= 9), libx11-6, libxcb1,
 libxcb-randr0, libxcb-xtest0, libxcb-xinerama0, libxcb-shape0,
 libxcb-xkb1, libxcb-xfixes0, libxv1, libxtst6, $ALSA_RUNTIME_DEP,
 libsndio7.0, libxcb-shm0, libpulse0, libdrm2, libdbus-1-3
Recommends: $PORTAL_RUNTIME_RECOMMENDS, nvidia-cuda-toolkit
Priority: optional
Section: utils
EOF

cat > "$DESKTOP_DIR/$PKG_NAME.desktop" << EOF
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

cat > "$DEBIAN_DIR/postrm" << EOF
#!/bin/bash
set -e

if [ "\$1" = "remove" ] || [ "\$1" = "purge" ]; then
    rm -f /usr/bin/$PKG_NAME || true
    rm -f /usr/bin/$APP_NAME || true
    rm -f /usr/share/applications/$PKG_NAME.desktop || true
    for size in 16 24 32 48 64 96 128 256; do
        rm -f /usr/share/icons/hicolor/\${size}x\${size}/apps/$PKG_NAME.png || true
    done
fi

exit 0
EOF
chmod +x "$DEBIAN_DIR/postrm"

cat > "$DEBIAN_DIR/postinst" << 'EOF'
#!/bin/bash
set -e

exit 0
EOF

chmod +x "$DEBIAN_DIR/postinst"

dpkg-deb --build "$DEB_DIR"

OUTPUT_FILE="${PKG_NAME}-linux-${ARCHITECTURE}-${APP_VERSION}.deb"
mv "$DEB_DIR.deb" "$OUTPUT_FILE"

rm -rf "$DEB_DIR"

echo "✅ Deb package created: $OUTPUT_FILE"
