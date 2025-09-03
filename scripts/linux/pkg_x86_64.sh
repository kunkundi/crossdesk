#!/bin/bash
set -e

PKG_NAME="crossdesk"
APP_NAME="CrossDesk"

APP_VERSION="$1"
ARCHITECTURE="amd64"
MAINTAINER="Junkun Di <junkun.di@hotmail.com>"
DESCRIPTION="A simple cross-platform remote desktop client."

DEB_DIR="${PKG_NAME}-${APP_VERSION}"
DEBIAN_DIR="$DEB_DIR/DEBIAN"
BIN_DIR="$DEB_DIR/usr/bin"
CERT_SRC_DIR="$DEB_DIR/opt/$PKG_NAME/certs"
ICON_BASE_DIR="$DEB_DIR/usr/share/icons/hicolor"
DESKTOP_DIR="$DEB_DIR/usr/share/applications"

rm -rf "$DEB_DIR"

mkdir -p "$DEBIAN_DIR" "$BIN_DIR" "$CERT_SRC_DIR" "$DESKTOP_DIR"

cp build/linux/x86_64/release/crossdesk "$BIN_DIR/$PKG_NAME"
chmod +x "$BIN_DIR/$PKG_NAME"

ln -s "$PKG_NAME" "$BIN_DIR/$APP_NAME"

cp certs/crossdesk.cn_root.crt "$CERT_SRC_DIR/crossdesk.cn_root.crt"

for size in 16 24 32 48 64 96 128 256; do
    mkdir -p "$ICON_BASE_DIR/${size}x${size}/apps"
    cp "icons/linux/crossdesk_${size}x${size}.png" \
       "$ICON_BASE_DIR/${size}x${size}/apps/${PKG_NAME}.png"
done

cat > "$DEBIAN_DIR/control" << EOF
Package: $PKG_NAME
Version: $APP_VERSION
Architecture: $ARCHITECTURE
Maintainer: $MAINTAINER
Description: $DESCRIPTION
Depends: libc6 (>= 2.29), libstdc++6 (>= 9), libx11-6, libxcb1,
 libxcb-randr0, libxcb-xtest0, libxcb-xinerama0, libxcb-shape0,
 libxcb-xkb1, libxcb-xfixes0, libxv1, libxtst6, libasound2,
 libsndio7.0, libxcb-shm0, libpulse0, nvidia-cuda-toolkit
Priority: optional
Section: utils
EOF

cat > "$DESKTOP_DIR/$PKG_NAME.desktop" << EOF
[Desktop Entry]
Version=$APP_VERSION
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
    rm -rf /opt/$PKG_NAME/certs || true
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

CERT_SRC="/opt/crossdesk/certs"
CERT_FILE="crossdesk.cn_root.crt"

for user_home in /home/*; do
    [ -d "$user_home" ] || continue
    username=$(basename "$user_home")
    config_dir="$user_home/.config/crossdesk/certs"
    target="$config_dir/$CERT_FILE"

    if [ ! -f "$target" ]; then
        mkdir -p "$config_dir" || true
        cp "$CERT_SRC/$CERT_FILE" "$target" || true
        chown -R "$username:$username" "$user_home/.config/crossdesk" || true
        echo "✔ Installed cert for $username at $target"
    fi
done

if [ -d "/root" ]; then
    config_dir="/root/.config/crossdesk/certs"
    mkdir -p "$config_dir" || true
    cp "$CERT_SRC/$CERT_FILE" "$config_dir/$CERT_FILE" || true
    chown -R root:root /root/.config/crossdesk || true
fi

exit 0
EOF
chmod +x "$DEBIAN_DIR/postinst"

dpkg-deb --build "$DEB_DIR"

OUTPUT_FILE="${PKG_NAME}-${APP_VERSION}-${ARCHITECTURE}.deb"
mv "$DEB_DIR.deb" "$OUTPUT_FILE"

rm -rf "$DEB_DIR"

echo "✅ Deb package created: $OUTPUT_FILE"