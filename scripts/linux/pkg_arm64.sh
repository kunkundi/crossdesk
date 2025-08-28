#!/bin/bash
set -e

APP_NAME="CrossDesk"
APP_VERSION="$1"
ARCHITECTURE="arm64"
MAINTAINER="Junkun Di <junkun.di@hotmail.com>"
DESCRIPTION="A simple cross-platform remote desktop client."

DEB_DIR="$APP_NAME-$APP_VERSION"
DEBIAN_DIR="$DEB_DIR/DEBIAN"
BIN_DIR="$DEB_DIR/usr/local/bin"
CERT_SRC_DIR="$DEB_DIR/opt/$APP_NAME/certs"
ICON_BASE_DIR="$DEB_DIR/usr/share/icons/hicolor"
DESKTOP_DIR="$DEB_DIR/usr/share/applications"

rm -rf "$DEB_DIR"

mkdir -p "$DEBIAN_DIR" "$BIN_DIR" "$CERT_SRC_DIR" "$DESKTOP_DIR"

cp build/linux/arm64/release/crossdesk "$BIN_DIR"
cp certs/crossdesk.cn_root.crt "$CERT_SRC_DIR/crossdesk.cn_root.crt"

for size in 16 24 48 128 256; do
    mkdir -p "$ICON_BASE_DIR/${size}x${size}/apps"
    cp "icons/linux/crossdesk_${size}x${size}.png" \
       "$ICON_BASE_DIR/${size}x${size}/apps/crossdesk.png"
done

chmod +x "$BIN_DIR/crossdesk"

cat > "$DEBIAN_DIR/control" << EOF
Package: $APP_NAME
Version: $APP_VERSION
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

cat > "$DESKTOP_DIR/$APP_NAME.desktop" << EOF
[Desktop Entry]
Version=$APP_VERSION
Name=$APP_NAME
Comment=$DESCRIPTION
Exec=/usr/local/bin/crossdesk
Icon=crossdesk
Terminal=false
Type=Application
Categories=Utility;
EOF

cat > "$DEBIAN_DIR/postrm" << EOF
#!/bin/bash
# post-removal script for $APP_NAME

set -e

if [ "\$1" = "remove" ] || [ "\$1" = "purge" ]; then
    rm -f /usr/local/bin/crossdesk
    rm -f /usr/share/applications/$APP_NAME.desktop
    rm -rf /opt/$APP_NAME
    for size in 16 24 48 128 256; do
        rm -f /usr/share/icons/hicolor/\${size}x\${size}/apps/crossdesk.png
    done
fi

exit 0
EOF
EOF

chmod +x "$DEBIAN_DIR/postrm"

cat > "$DEBIAN_DIR/postinst" << 'EOF'
#!/bin/bash
set -e

CERT_SRC="/opt/CrossDesk/certs"
CERT_FILE="crossdesk.cn_root.crt"

for user_home in /home/*; do
    [ -d "$user_home" ] || continue
    username=$(basename "$user_home")
    config_dir="$user_home/.config/CrossDesk/certs"
    target="$config_dir/$CERT_FILE"

    if [ ! -f "$target" ]; then
        mkdir -p "$config_dir"
        cp "$CERT_SRC/$CERT_FILE" "$target"
        chown -R "$username:$username" "$user_home/.config/CrossDesk"
        echo "✔ Installed cert for $username at $target"
    fi
done

if [ -d "/root" ]; then
    config_dir="/root/.config/CrossDesk/certs"
    mkdir -p "$config_dir"
    cp "$CERT_SRC/$CERT_FILE" "$config_dir/$CERT_FILE"
    chown -R root:root /root/.config/CrossDesk
fi

exit 0
EOF

chmod +x "$DEBIAN_DIR/postinst"

dpkg-deb --build "$DEB_DIR"

OUTPUT_FILE="crossdesk-linux-arm64-$APP_VERSION.deb"
mv "$DEB_DIR.deb" "$OUTPUT_FILE"

INSTALL_PATH="/tmp/$OUTPUT_FILE"
mv "$OUTPUT_FILE" "$INSTALL_PATH"

rm -rf "$DEB_DIR"

echo "✅ Deb package for $APP_NAME (ARM64) created successfully."