#!/bin/bash
set -e

# 配置变量
APP_NAME="CrossDesk"
APP_VERSION="$1"
ARCHITECTURE="amd64"
MAINTAINER="Junkun Di <junkun.di@hotmail.com>"
DESCRIPTION="A simple cross-platform remote desktop client."

# 目录结构
DEB_DIR="$APP_NAME-$APP_VERSION"
DEBIAN_DIR="$DEB_DIR/DEBIAN"
BIN_DIR="$DEB_DIR/usr/local/bin"
CERT_SRC_DIR="$DEB_DIR/opt/$APP_NAME/certs"  # 用于中转安装时分发
ICON_DIR="$DEB_DIR/usr/share/icons/hicolor/256x256/apps"
DESKTOP_DIR="$DEB_DIR/usr/share/applications"

# 清理已有的打包文件夹
rm -rf "$DEB_DIR"

# 创建目录结构
mkdir -p "$DEBIAN_DIR" "$BIN_DIR" "$CERT_SRC_DIR" "$ICON_DIR" "$DESKTOP_DIR"

# 复制二进制文件
cp build/linux/x86_64/release/crossdesk "$BIN_DIR"

# 复制证书文件（将来通过 postinst 拷贝到每个用户 XDG_CONFIG_HOME）
cp certs/crossdesk.cn_root.crt "$CERT_SRC_DIR/crossdesk.cn_root.crt"

# 复制图标文件
cp icons/crossdesk.png "$ICON_DIR/crossdesk.png"

# 设置可执行权限
chmod +x "$BIN_DIR/crossdesk"

# 创建 control 文件
cat > "$DEBIAN_DIR/control" << EOF
Package: $APP_NAME
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

# 创建 desktop 文件
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

# 创建卸载脚本 postrm
cat > "$DEBIAN_DIR/postrm" << EOF
#!/bin/bash
# post-removal script for $APP_NAME

set -e

if [ "\$1" = "remove" ] || [ "\$1" = "purge" ]; then
    rm -f /usr/local/bin/crossdesk
    rm -f /usr/share/icons/hicolor/256x256/apps/crossdesk.png
    rm -f /usr/share/applications/$APP_NAME.desktop
    rm -rf /opt/$APP_NAME
fi

exit 0
EOF

chmod +x "$DEBIAN_DIR/postrm"

# 创建安装后脚本 postinst（拷贝证书到每个用户 XDG_CONFIG_HOME）
cat > "$DEBIAN_DIR/postinst" << 'EOF'
#!/bin/bash
set -e

CERT_SRC="/opt/CrossDesk/certs"
CERT_FILE="crossdesk.cn_root.crt"

# 处理每个普通用户的配置目录
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

# 处理 root 用户（可选）
if [ -d "/root" ]; then
    config_dir="/root/.config/CrossDesk/certs"
    mkdir -p "$config_dir"
    cp "$CERT_SRC/$CERT_FILE" "$config_dir/$CERT_FILE"
    chown -R root:root /root/.config/CrossDesk
fi

exit 0
EOF

chmod +x "$DEBIAN_DIR/postinst"

# 构建 .deb 包
dpkg-deb --build "$DEB_DIR"

OUTPUT_FILE="crossdesk-linux-x86_64-$APP_VERSION.deb"
mv "$DEB_DIR.deb" "$OUTPUT_FILE"

# 清理构建目录
rm -rf "$DEB_DIR"

echo "✅ Deb package for $APP_NAME created successfully."
