#!/bin/bash
set -e

PKG_NAME="crossdesk"
APP_NAME="CrossDesk"

APP_VERSION="$1"
ARCHITECTURE="arm64"
MAINTAINER="Junkun Di <junkun.di@hotmail.com>"
DESCRIPTION="A simple cross-platform remote desktop client."

# Remove 'v' prefix from version for Debian package (Debian version must start with digit)
DEB_VERSION="${APP_VERSION#v}"

DEB_DIR="${PKG_NAME}-${DEB_VERSION}"
DEBIAN_DIR="$DEB_DIR/DEBIAN"
BIN_DIR="$DEB_DIR/usr/bin"
ICON_BASE_DIR="$DEB_DIR/usr/share/icons/hicolor"
DESKTOP_DIR="$DEB_DIR/usr/share/applications"

rm -rf "$DEB_DIR"

mkdir -p "$DEBIAN_DIR" "$BIN_DIR" "$DESKTOP_DIR"

cp build/linux/arm64/release/crossdesk "$BIN_DIR"
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
 libxcb-xkb1, libxcb-xfixes0, libxv1, libxtst6, libasound2,
 libsndio7.0, libxcb-shm0, libpulse0
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

OUTPUT_FILE="crossdesk-linux-arm64-$APP_VERSION.deb"
mv "$DEB_DIR.deb" "$OUTPUT_FILE"

rm -rf "$DEB_DIR"

echo "✅ Deb package created: $OUTPUT_FILE"